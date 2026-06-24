# 内存序审计（P9.4）

> nanosig v1 / 2026-06-20 审计快照

## 摘要

本次审计覆盖 nanosig v1 中所有跨线程可见性 / 同步相关的原子与 fence 操作。审计对象：

| # | 模块 | 关键原子对象 |
|---|------|--------------|
| 1 | MPSC record ring | `reserve_pos` / `write_pos` / `read_pos` / record `meta` |
| 2 | loop wakeup | 由 `ns_platform_wakeup_signal` / `_wait` 提供跨线程 happens-before（eventfd / kqueue / Win32 Event） |
| 3 | signal mutex | `ns_signal_t::mutex` 包裹的 slot 链表（无原子；纯 mutex） |
| 4 | broker wakeup | `broker->wakeup` + `broker->quit_requested` + `broker->waitset` |
| 5 | quit_requested | `ns_loop_t::quit_requested` / `ns_event_broker_t::quit_requested` |
| 6 | timer mgr rbtree | `g_timer_mgr.mutex` 包裹，无内部原子 |
| 7 | runtime initialized | `g_ns_initialized` |

**严重度分布**：

- Critical：0
- Major：1
- Info：3

**原子操作计数**（`rg -n "ns_atomic_"` 命中行去重）：

- `init` × 5
- `load_explicit` × 13
- `store_explicit` × 14
- `compare_exchange_weak_explicit` × 1
- `compare_exchange_strong` × 1

总计 34 处显式原子 API 调用 + 4 个 `atomic_int` / 3 个 `atomic_size_t` 字段 + 5 个 barrier 宏（未在源码中调用）。

## 关键原子操作清单

> 表中只列带 memory order 的有同步意义的操作；`atomic_init` 与 `compare_exchange_strong` (默认 seq_cst) 单列。

| file:line | 操作 | order | 所在场景 | 为什么用这个序 |
|-----------|------|-------|----------|----------------|
| `src/ds/ns_mpsc_record_ring.c:141` | `store meta=0` | release | `mark_uncommitted` 槽位初始化 | 槽位被覆盖前必须让所有消费者看到旧 valid=1 的释放；属于"复位"动作，release 与 acquire load 配对 |
| `src/ds/ns_mpsc_record_ring.c:252-254` | `init reserve_pos/write_pos/read_pos` | n/a | 环初始化 | 启动时单线程，atomic_init 即可 |
| `src/ds/ns_mpsc_record_ring.c:293` | `load reserve_pos` | relaxed | `free_capacity` 查询 | 乐观估计，不需要严格同步 |
| `src/ds/ns_mpsc_record_ring.c:294` | `load read_pos` | relaxed | 同上 | 同上 |
| `src/ds/ns_mpsc_record_ring.c:363` | `load write_pos` | **acquire** | `try_pushv` 重试窗口 | 与 producer 的 release store 配对：让本线程随后读 payload 缓存时能看到前一个 producer 的 memcpy 写入 |
| `src/ds/ns_mpsc_record_ring.c:364` | `load read_pos` | acquire | 同上 | 与 consumer release `read_pos` 配对（防止读到陈旧的 used 估计） |
| `src/ds/ns_mpsc_record_ring.c:374-379` | `CAS reserve_pos += publish_size` | **acq_rel** / relaxed failure | 抢占空位 | success: 取得自己 slot 的所有权，向 consumer 发布"已分配但未提交"的状态；failure: 不需要同步 |
| `src/ds/ns_mpsc_record_ring.c:392` | `store fake_header->meta` | release | 写入 fake record | 防止 fake 段被 consumer 错认为 valid 之前已被看到（同时给后续 record meta 提供合成的 release 链） |
| `src/ds/ns_mpsc_record_ring.c:398` | `store write_pos` | **release** | 推进环头 | 关键 release 点：让 consumer 在 `acquire load write_pos` 之后看到 payload memcpy 写入和 `record_header->meta` 的初始 valid=0 |
| `src/ds/ns_mpsc_record_ring.c:405` | `store record_header->meta` (valid=1) | **release** | 提交记录 | 关键 release 点：让 consumer 在 `acquire load meta` 之后看到完整 payload 数据 |
| `src/ds/ns_mpsc_record_ring.c:431` | `load read_pos` | relaxed | `try_acquire` 本地循环顶部 | read_pos 是消费者独占，relaxed 即可 |
| `src/ds/ns_mpsc_record_ring.c:432` | `load write_pos` | acquire | `try_acquire` 可见性门 | 与 producer `release store write_pos` 配对 |
| `src/ds/ns_mpsc_record_ring.c:438` | `load header->meta` | acquire | `try_acquire` 提交门 | 与 producer `release store meta` 配对；valid=1 之后的所有 payload 写可见 |
| `src/ds/ns_mpsc_record_ring.c:444` | `store header->meta=0` (fake) | relaxed | 跳过 fake 段 | 已被自己独占的旧槽位复位，relaxed 即可 |
| `src/ds/ns_mpsc_record_ring.c:445` | `store read_pos` | release | 步进 read_pos（fake） | 关键 release 点：让 producer 在 `acquire load read_pos` 之后知道本 fake 段已释放 |
| `src/ds/ns_mpsc_record_ring.c:472` | `load read_pos` | relaxed | `release` 本地 | 消费者独占 |
| `src/ds/ns_mpsc_record_ring.c:473` | `load write_pos` | acquire | `release` 边界检查 | 见 432 |
| `src/ds/ns_mpsc_record_ring.c:489` | `load header->meta` | acquire | `release` 校验 | 见 438 |
| `src/ds/ns_mpsc_record_ring.c:497` | `store header->meta=0` (real) | relaxed | 释放真实记录 | 消费者独占回写 |
| `src/ds/ns_mpsc_record_ring.c:498` | `store read_pos` | release | 步进 read_pos（real） | 见 445 |
| `src/nanosig.c:71` | `load g_ns_initialized` | acquire | `ns_is_initialized` 探测 | 让 ns_init 中 broker/platform 的初始化在读侧可见 |
| `src/nanosig.c:86` | `store g_ns_initialized=1` | release | `ns_init` 完成 | 发布"全局子系统已就绪" |
| `src/nanosig.c:99` | `store g_ns_initialized=0` | release | `ns_shutdown` 启动 | 关闭序列的发布 |
| `src/nanosig.c:166` | `load loop->running` | acquire | `ns_loop_deinit` | 让 destroy 看到上一次 run 结束（store release running=0） |
| `src/nanosig.c:206` | `CAS loop->running 0→1` | seq_cst (默认) | `ns_loop_run_impl` 入口 | 强约束：保证"只有一个 run"的语义对其他 observer（destroy）立即可见 |
| `src/nanosig.c:211` | `load quit_requested` | acquire | 主循环顶部 | 与 `ns_loop_quit` 的 release store 配对 |
| `src/nanosig.c:219` | `store quit_requested=0` | release | run 退出前复位 | 让 destroy 看到"已经退出" |
| `src/nanosig.c:223` | `store running=0` | release | run 退出收尾 | 让 destroy 的 acquire load 看到 |
| `src/nanosig.c:241` | `store quit_requested=1` | release | `ns_loop_quit` | 发布退出意图，必须在 `wakeup_signal` 之前 |
| `src/ns_broker.c:174` | `load broker->quit_requested` | acquire | broker 主循环 | 与 `ns_broker_global_shutdown` 的 release store 配对 |
| `src/ns_broker.c:300` | `init broker->quit_requested` | n/a | 启动 | 单线程 |
| `src/ns_broker.c:350` | `store broker->quit_requested=1` | release | broker 关闭 | 发布退出意图，必须在 `wakeup_signal` 之前 |

## MPSC 同步链图

### producer 路径（`ns_mpsc_record_ring_try_pushv`）

```
1. (C11 副作用) memcpy parts -> ring->storage (payload 写入)
2. ns_atomic_store_explicit(&ring->write_pos, ..., release)   [line 398]
3. ns_atomic_store_explicit(&header->meta, valid=1, release) [line 405]
4. (emit 路径) ns_platform_wakeup_signal(target_loop->wakeup) [nanosig.c:412]
```

注意："写 payload → 步进 write_pos → 写 meta=valid=1 → signal wakeup" 是一个 release chain。
最后一个 release fence 由 `write(2)` / `kevent(... EVFILT_USER, NOTE_TRIGGER, ...)` / `SetEvent` 提供，对端 consumer 看到 `poll/epoll/kevent/WaitForSingleObject` 返回时的 acquire 语义。

> ⚠️ **关键 invariant**：步骤 2 和 3 的顺序不能调换（见 `ns_mpsc_record_ring.c:386-406` 注释 "下面的顺序是高性能铁律，不能动"）。如果先 store meta=1 再 store write_pos，consumer 在 acquire load write_pos 之后会立即看到 meta=1 然后读 payload —— 看上去也行？**实际上不行**，因为：
> - 多 producer 并发场景下，consumer 可能在 producer A 还没写 payload 时就观察到 write_pos 已经步进（来自 producer B 的 store）。
> - 消费者检查 `meta.valid=1` 后读 payload 时的 acquire 会把所有 producer 之前的 store（payload、write_pos）一起同步过来。
> - 因此 store write_pos（2）必须先于 memcpy + store meta（3）；本实现正是按此顺序。
> - store write_pos 时 meta.valid=0，消费者会跳过该槽位继续重试，从而不会被未提交的 payload 误导。

### consumer 路径（`ns_mpsc_record_ring_try_acquire` / `release`）

```
1. ns_platform_wakeup_wait(wakeup, INF, &result)              [nanosig.c:213]
   ← acquire 语义：让 producer 在 signal 之前的所有 release store 可见
2. load read_pos (relaxed)                                     [line 431]
3. load write_pos (acquire)                                    [line 432]
4. load header->meta (acquire)                                 [line 438]
5. 若 valid=1：读 payload 内存                                  [line 451, *out_record = header+1]
6. (consumer 逻辑) call->fn(...)
7. ns_mpsc_record_ring_release:
      load read_pos (relaxed) [472]
      load write_pos (acquire) [473]
      load header->meta (acquire) [489]
      store header->meta = 0 (relaxed) [497]
      store read_pos += stride (release) [498]
```

### 跨 store 的可见性论证

| 写入点（producer 侧） | 读取点（consumer 侧） | 配对的 release/acquire 链 |
|----------------------|----------------------|---------------------------|
| `write_pos` release [398] | `write_pos` acquire [432] | 跨 producer 写 payload / fake-meta → consumer 看到正确的 used |
| `meta` release [405] | `meta` acquire [438] | 跨 producer payload memcpy → consumer 读出真实数据 |
| `read_pos` release [445,498] | `read_pos` acquire [364] | consumer 释放空间 → 下一个 producer 抢占时不会误判 full |
| `meta=0` release [141] | `meta` acquire [438] | 初始化阶段把所有槽位先标 valid=0，再让消费者 acquire 时严格读到 0 |
| `wakeup_signal` (OS acquire fence) | `wakeup_wait` (OS release fence) | 跨平台事件机制（eventfd write/read、kevent trigger+wait、SetEvent/WaitForSingleObject） |

> 💡 **副观察**：步骤 `mark_uncommitted (release, [141])` 是在 init 阶段单线程执行的；它把每个槽位的 meta 先 release-store 成 0，再走 init 流程。这样从消费者首次 `acquire load meta` 起，就一定能观察到 valid=0，而不会出现"读到全 1 残留"的脏读。这是个良性的 startup release。

## 跨线程同步点

| 同步点 | producer 端 | consumer 端 | 序配对 | 备注 |
|--------|-------------|-------------|--------|------|
| MPSC record 数据 | `store meta (release)` [405] | `load meta (acquire)` [438] | ✅ | 正确的 MPSC 发布语义 |
| MPSC 空间回收 | `store read_pos (release)` [445,498] | `load read_pos (acquire)` [364] | ✅ | 让 producer 重试时不会看到陈旧 read_pos |
| MPSC 抢占 | `CAS reserve_pos (acq_rel)` [374] | `load reserve_pos (relaxed)` [293] | ✅ | consumer 侧 free_capacity 只是乐观估计，relaxed 足够 |
| loop wakeup | `wakeup_signal(target_loop->wakeup)` [nanosig.c:412] | `wakeup_wait(target_loop->wakeup)` [nanosig.c:213] | ✅ | 三个平台后端都通过原子化的 syscall (write/eventfd / kevent / SetEvent) 提供 release/acquire |
| loop quit | `store quit_requested=1 (release)` [nanosig.c:241] | `load quit_requested (acquire)` [nanosig.c:211] | ✅ | 先 store 再 wakeup_signal，保证 main loop 醒来时一定能看到 quit=1 |
| loop running | `CAS running 0→1 (seq_cst)` [nanosig.c:206] | `load running (acquire)` [nanosig.c:166] | ✅ | CAS 默认 seq_cst，足以让 destroy 看到唯一一次 run 结束 |
| broker quit | `store quit_requested=1 (release)` [ns_broker.c:350] | `load quit_requested (acquire)` [ns_broker.c:174] | ✅ | 同 loop quit 模式 |
| broker wakeup (timer notify) | `ns_broker_notify → wakeup_signal` [ns_broker.c:151] | broker thread 在 `waitset_wait` 返回后处理 wakeup waitable [ns_broker.c:198] | ✅ | waitset 返回时自带 OS 级 acquire，broker 接着调 `ns_timer_mgr_fire_expired`（在 timer mutex 内部读取 timer 状态） |
| signal mutex 临界区 | `ns_signal_lock/unlock` | 同上 | 平台 mutex（pthread_mutex / SRWLock / dispatch） | mutex 自带 acquire/release 语义，覆盖 slot_list 遍历与连接/断连 |
| broker watcher list | `ns_broker_add/remove` 取 `watcher_mutex` | broker 自身不直接遍历 | ✅ | broker 主循环不读 watcher_head，只在 `waitset_wait` 中通过 OS 拿 ready 事件；遍历仅发生在 `ns_broker_remove_all_watchers` (shutdown) |
| timer mgr rbtree | `ns_timer_mgr_lock` | `ns_timer_mgr_unlock` | 平台 mutex | 所有 rbtree 操作都在 mutex 内；rbtree 内部不引入任何原子（已 `rg` 验证 `src/ds/ns_rbtree.c` 0 命中） |
| runtime init | `store g_ns_initialized=1 (release)` [nanosig.c:86] | `load g_ns_initialized (acquire)` [nanosig.c:71] | ✅ | 让 init→use 的依赖关系跨线程可见 |

## 严重度等级

- **Critical** —— 数据竞争 / 可见性漏洞（行为未定义）。
- **Major** —— 用了过强 / 过弱的序，可能影响性能或正确性边界。
- **Info** —— 命名 / 风格。

## 关键发现

### Major #1 — `ns_atomic_init` 不是原子操作，跨线程 init 存在隐患（**实际不构成 bug，但有微小文档/可读性风险**）

`nanosig.c:137-138`：

```c
ns_atomic_init(&loop->quit_requested, 0);
ns_atomic_init(&loop->running, 0);
```

`atomic_init` 在 C11 中是**非原子**的（即不提供 happens-before）；它的契约是"对象在并发访问开始前由单一线程完成初始化"。前提是其它线程必须先 `ns_loop_run` 或先 `ns_loop_deinit`，而这两个函数都在内部做 acquire load `running` / `quit_requested`。`ns_loop_run` 的入口 CAS（默认 seq_cst）会发布 `init` 的所有副作用到消费者一侧。

`src/ds/ns_mpsc_record_ring.c:252-254` 同理：环由 `ns_loop_init` 在 broker 启动前完成 init；之后只有 `try_pushv` / `try_acquire` 访问，发布路径在 `ns_mpsc_record_ring_init` 结束后通过 `ns_loop_quit` 的 release store / wakeup_signal 提供可见性。

**结论**：不构成数据竞争，但**建议在审计备注中说明 `atomic_init` 的契约前置条件**，以免后续维护者在已并发的对象上错误使用 `ns_atomic_init`。

> Severity: Info → Major 边界，标注为 Major 仅为提醒。

### Info #1 — `compare_exchange_strong` 默认 seq_cst

`src/nanosig.c:206` 使用 `ns_atomic_compare_exchange_strong`（无 `_explicit` 后缀），映射到 C11 `atomic_compare_exchange_strong`，默认 `memory_order_seq_cst`。对于 "loop 入口抢唯一运行权" 的场景，seq_cst 是正确选择（不能让其他线程看到"陈旧的 running=0"导致误启动第二个 loop）。**保留即可**。

### Info #2 — `mark_uncommitted` 在 init 阶段使用 release 是过强

`src/ds/ns_mpsc_record_ring.c:141` `ns_mpsc_record_ring_mark_uncommitted` 使用 `release`，但 init 阶段单线程执行，且没有任何 acquire 配对需求（producer 端自己写 meta 之后会再 release-store）。**建议**：在 init 内部用 `atomic_store_explicit(... relaxed)` 即可，等价的语义（消费者通过自己的 acquire meta 看到 valid=1 之前的 payload 数据）由后续的 `try_pushv` release 链提供；此处的 release 是冗余的。但保留 release 不会产生正确性问题。

### Info #3 — barrier 宏未使用

`include/nanosig/nanosig_atomic.h:36-56` 暴露了 5 个 fence 宏（`ns_compiler_barrier` / `ns_memory_order_acquire_barrier` 等），全代码库 0 命中调用。原因：所有需要 barrier 的地方都已经用了 `_explicit` 版的 atomic 操作内置 fence。**无问题**，但如果计划公开成 API，可以让它们在文档中标记为 "internal reserved"。

## 结论

nanosig v1 的内存序契约**总体成立**，理由：

1. **MPSC 同步链完整**：每个 producer 的 `store write_pos/release` + `store meta/release` 都有 consumer 端的 `acquire load` 严格配对；fake record 的处理也保持 release/acquire 同步。
2. **loop / broker quit 路径标准**：release store + wakeup_signal 在前，acquire load 在 wakeup 之后，严格遵循 Linux/macOS/Windows 三套事件系统的 release/acquire 语义。
3. **timer mgr / watcher list 的 rbtree / 链表完全受 mutex 保护**，mutex 自带 acquire/release；没有"靠原子 + mutex 混合"导致的难缠可见性。
4. **没有使用 fence hack**（如 `atomic_thread_fence` 模拟 mutex），全部走 `_explicit` 原子操作；TSAN 友好的同时也不易踩"看到 fence 不知道配对" 的坑。
5. **唯一的 Major 项（`atomic_init` 跨线程契约）**经分析不构成实际 bug，但建议在 nanosig.h 顶部加一句 "对象必须在被并发访问前由单一线程完成 atomic_init" 的人类可读契约。

**未在该平台验证**：本次审计主机为 macOS（Darwin 25.5.0），没有尝试 `cmake --preset linux-debug-tsan`。按 audit 任务约束，标注为 "未在该平台验证"。`CMakePresets.json` 中 `linux-debug-tsan` 已存在并启用 `NANOSIG_ENABLE_TSAN=ON` / `thread_sanitizer`，逻辑上应当通过（所有 cross-thread 同步点均使用 explicit memory order）。

**建议（可选）**：

1. 在 `nanosig.h` / `nanosig_atomic.h` 顶部加一段注释，明确 "atomic_init 必须在并发访问前由单线程完成"；附上 `ns_loop_init` / `ns_mpsc_record_ring_init` 作为示例。
2. `mark_uncommitted` 的 release 改为 relaxed（仅在 init 阶段使用）以避免误读。
3. 长期：可考虑在 `ns_signal_t::mutex` 之上做 TSAN 注解（`__attribute__((annotate("threading")))` 或 LLVM `no_thread_safety_analysis`），强化对 lock/connect/disconnect 的静态检查。

## 附录：所有 ns_atomic_* 调用汇总

| 出现位置 | 操作 | order | 备注 |
|----------|------|-------|------|
| `ns_mpsc_record_ring.c:141` | store meta=0 | release | init 阶段 |
| `ns_mpsc_record_ring.c:252-254` | init ×3 | n/a | init 阶段 |
| `ns_mpsc_record_ring.c:293,294` | load reserve/read | relaxed | 容量查询 |
| `ns_mpsc_record_ring.c:363,364` | load write/read | acquire | try_pushv |
| `ns_mpsc_record_ring.c:374-379` | CAS reserve | acq_rel/relaxed | 抢占 |
| `ns_mpsc_record_ring.c:392,398,405` | store fake-meta / write_pos / meta | release | push commit |
| `ns_mpsc_record_ring.c:431,432,438` | load read/write/meta | relaxed/acquire/acquire | try_acquire |
| `ns_mpsc_record_ring.c:444,445` | store meta=0 / read_pos | relaxed/release | fake 跳过 |
| `ns_mpsc_record_ring.c:472,473,489` | load read/write/meta | relaxed/acquire/acquire | release |
| `ns_mpsc_record_ring.c:497,498` | store meta=0 / read_pos | relaxed/release | release 收尾 |
| `nanosig.c:71,86,99` | init/load/store g_ns_initialized | acquire/release | runtime init |
| `nanosig.c:137,138,166,206,211,219,223,241` | init/load/CAS/load/store/store/store running & quit | 各异 | loop lifecycle |
| `ns_broker.c:174,300,350` | init/load/store quit | acquire/release | broker lifecycle |
