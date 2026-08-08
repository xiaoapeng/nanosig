# Nanosig Core 代码审查报告

**日期**: 2026-07-04
**审查者**: claude-code-review

---

## 修复历史

(无)

---

## 现在打开的问题

### CORE-010: `ns_loop_deinit` 在 `ns_platform_wakeup_destroy` 失败时泄漏 loop 内存

- **状态**: 关闭-已修复
- **严重度**: 🟡 中
- **类型**: bug

#### 问题描述
`ns_loop_deinit` 在 `ns_platform_wakeup_destroy` 返回错误时提前返回（第 162 行），未调用 `ns_platform_free(loop)`。这导致 loop 结构体及其内嵌的 MPSC ring 存储区域泄漏。调用方无法重试——wakeup 句柄可能处于未定义状态，而 loop 指针指向的内存已不可恢复。

#### review 建议
无论 `ns_platform_wakeup_destroy` 是否成功，都应释放 loop 内存：
```c
rc = ns_platform_wakeup_destroy(loop->wakeup);
ns_platform_free(loop);
return rc;
```

#### 作者建议
当前代码（`src/nanosig.c:171-177`）已实现 review 建议——无论 wakeup_destroy 是否失败，都打印日志后 fall through 到 `ns_platform_free(loop)`。问题已修复，review 文档未同步更新。→ 关闭-已修复

#### 关闭原因
代码已修复：`src/nanosig.c:172-177` 在 wakeup_destroy 失败时先记日志，然后始终执行 `ns_platform_free(loop)`。

- 关闭日期: 2026-07-18
- 状态: 关闭-已修复

#### 可重现的失败场景
1. 调用 `ns_loop_init(&loop, NULL)` 成功创建 loop。
2. 平台层 `ns_platform_wakeup_destroy` 因某种原因返回错误。
3. `ns_loop_deinit(loop)` 返回错误码，但 `loop` 指向的内存未被释放。
4. 调用方丢弃 loop 指针，内存永久泄漏。

#### 定位
src/nanosig.c:161-165

---

### CORE-011: `ns_signal_connect` 缺少"禁止重复连接"前置条件文档

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低
- **类型**: doc

#### 问题描述
`ns_signal_connect` 未文档化"connection 必须处于未连接状态"这一前置条件。如果调用方对一个已经连接到某个 signal 的 `ns_connection_t` 再次调用 `ns_signal_connect`（未先 disconnect），`ns_list_init` 将 `signal_node` 重置为自引用，但不更新原链表中前后节点的指针，导致原 signal 的 slot_list 断链。

#### review 建议
在 `nanosig_signal.h` 的 `ns_signal_connect` docstring 中添加 `@pre` 前置条件。可在实现中添加防御性检查：如果 `connection->signal_node` 不是自引用状态，返回 `NS_E_INVAL`。

#### 作者建议
已采纳 review 建议，在 docstring 中添加 `@pre connection 必须处于未连接状态` 和 `@warning 重复连接会导致原 signal 的 slot_list 损坏`。
→ 关闭-已修复

#### 关闭原因
`include/nanosig/nanosig_signal.h:290-313` 已添加 `@pre` 和 `@warning` 文档。

- 关闭日期: 2026-08-02
- 状态: 关闭-已修复

#### 可重现的失败场景
1. `ns_signal_connect(&sig_a, ...)` 将 conn 连接到 sig_a。
2. `ns_signal_connect(&sig_b, ...)` 将 conn 连接到 sig_b（未先 disconnect）。
3. sig_a 的 slot_list 断链：遍历 sig_a 时无法到达原后继节点。

#### 定位
src/nanosig.c:337-342（实现）
include/nanosig/nanosig_signal.h:290-313（docstring）

---

### CORE-012: `ns_loop_quit` 在 loop 被 `ns_loop_deinit` 释放后写入 → heap-use-after-free `bug`

- **状态**: 关闭-已修复（测试问题，非库缺陷）
- **严重度**: 🟠 高
- **类型**: bug

#### 问题描述
ASAN 下运行 `nanosig_test_broker_consume_fn` 触发 heap-use-after-free：主线程 `ns_loop_quit` 写入 `loop->quit_requested`（src/nanosig.c:256）时，loop 已被 broker worker 线程的 `ns_loop_deinit`（src/nanosig.c:176 → `ns_platform_free`）释放。

完整调用栈（ASAN）：
```
WRITE of size 4 at 0x51a000000168 thread T0
  #0 ns_loop_quit  src/nanosig.c:256
  #1 test_consume_fn_impl  test/unit/test_broker_consume_fn.c:213
freed by thread T2 here:
  #0 free
  #1 ns_platform_free  platform/linux/port.c:73
  #2 ns_loop_deinit  src/nanosig.c:176
  #3 broker_worker_entry  test/unit/test_broker_consume_fn.c:67
previously allocated by thread T2 here:
  #1 ns_platform_alloc  platform/linux/port.c:68
  #2 ns_loop_init  src/nanosig.c:135
  #3 broker_worker_entry  test/unit/test_broker_consume_fn.c:57
```

#### review 建议
需调查根因方向：
1. 测试时序问题：`test_broker_consume_fn.c` 主线程在 broker worker 已 deinit loop 后仍调用 `ns_loop_quit`，违反"loop 生命周期由调用方保证"合同（与 CORE-001 同类）。
2. 库防御缺口：`ns_loop_quit` 未检查 loop 是否仍存活/运行中，deinit 后调用即为 UAF。若合同允许 deinit 后不调用 quit，则测试需修正时序；若库需防御，应在 quit 入口检查 `running` 或增加生命周期同步。
3. 建议先用 TSAN 或加日志确认是测试时序竞态还是库缺陷，再决定修复归属。

#### 作者建议
确认是测试问题而非库缺陷。根因：`test_broker_consume_fn.c` 的 slot 回调（如 `consume_slot` line 103）内部已调用 `ns_loop_quit(ctx->loop)`，loop 被 slot quit 后 worker 线程继续执行 `ns_loop_deinit` 释放 loop；主线程成功路径又在 join 前**重复调用** `ns_loop_quit`（如 line 213），此时 loop 可能已被 worker 释放 → UAF。
修复：主线程成功路径移除多余的二次 quit，只 join（slot 已负责 quit）。但**保留**"slot 未调用"测试（`slot_called == 0`，如 prevents_refire / negative return）的 quit——这些测试的 loop 仍在 run，需主线程 quit 才能退出。共 6 处成功路径：4 处移除重复 quit，2 处保留。
→ 关闭-已修复

#### 关闭原因
`test/unit/test_broker_consume_fn.c` 修复成功路径的重复 `ns_loop_quit`：slot 被调用（`slot_called != 0`）的测试只 join；slot 未调用（`slot_called == 0`）的测试保留 quit。ASAN 下重跑无 UAF，release 完整测试 48/48 通过。

- 关闭日期: 2026-08-08
- 状态: 关闭-已修复

#### 可重现的失败场景
```sh
cmake --preset linux-debug-asan
cmake --build build --target nanosig_test_broker_consume_fn
./build/nanosig_test_broker_consume_fn
# ASAN: heap-use-after-free in ns_loop_quit
```

#### 定位
src/nanosig.c:256
src/nanosig.c:176
test/unit/test_broker_consume_fn.c:213
test/unit/test_broker_consume_fn.c:67

## 现在关闭的问题

### CORE-001: `ns_loop_deinit` 与同步 `ns_loop_run` 并发可造成 UAF

- **状态**: 关闭-已拒绝
- **严重度**: 🟠 高

#### 关闭原因
文档化合同违规。`nanosig_loop.h` 明确要求 deinit 前必须确保 loop 未运行；`ns_loop_deinit` 已通过 `running` 原子标志检查拦截。提案的引用计数方案属于过度设计——文档化的"run/deinit 不能并发调用"约束已足够。

#### 关闭日期
2026-07-04

---

### CORE-002: `ns_signal_disconnect` 在 signal 销毁后可解引用悬空 mutex

- **状态**: 关闭-已拒绝
- **严重度**: 🟠 高

#### 关闭原因
文档化合同违规。`nanosig_signal.h` 明确要求 connection 生命周期 ≤ signal 生命周期（先 disconnect 再 deinit）。`ns_signal_lock` 已检查 `signal->mutex == NULL` 并返回 `NS_E_INVAL` 作为软失败模式，提供了安全防护。

#### 关闭日期
2026-07-04

---

### CORE-003: `ns_signal_emit_raw` 在持锁时执行阻塞性 `wakeup_signal`

- **状态**: 关闭-已拒绝
- **严重度**: 🟠 高

#### 关闭原因
设计约束。eventfd/Windows event 的 level-triggered wakeup_signal 是 O(1) 非阻塞 syscall（一次 write 系统调用），不构成 critical section 延长。提案的批处理 + 延迟 wakeup 需要临时内存分配，违反 emit 路径无分配约束（`nanosig_signal.h` 明确声明）。批处理 wakeup 是调用方的优化关注点。

#### 关闭日期
2026-07-04

---

### CORE-004: Partial-broadcast 没有原子语义保证

- **状态**: 关闭-已拒绝
- **严重度**: 🟡 中

#### 关闭原因
文档化约束。`nanosig.c` 函数注释明确记录此约束：第 N 个 push 失败时前 N-1 个已成功入队无法回滚。提案的全或无语义需要两阶段提交（commit token）或 dry-run reserve，与非阻塞 API 设计冲突。调用方应在 emit 前检查队列容量。

#### 关闭日期
2026-07-04

---

### CORE-005: `quit_requested` 重置时的 wakeup_signal 可能在边沿触发下丢失

- **状态**: 关闭-已拒绝
- **严重度**: 🟡 中

#### 关闭原因
文档化设计约束。`nanosig.c:213-221` 注释明确标记此为"已知设计约束"，并指出"单线程驱动 loop 的使用模式不受影响"。提案（reset 移到 run 入口）无法根本解决非原子检查-修改序列的固有竞态窗口。文档要求 wakeup 必须保持 level-triggered。

#### 关闭日期
2026-07-04

---

### CORE-006: `ns_loop_stop` 后未 drain in-flight slot 调用

- **状态**: 关闭-已拒绝
- **严重度**: 🟡 中

#### 关闭原因
文档化行为。`nanosig_loop.h` 明确 `ns_loop_stop` 不保证 slot_fn 完成（只 join 循环主线程）。loop 线程是唯一执行 slot_fn 的线程，thread_join 后无 in-flight 调用。若调用方需确保 slot_fn 完成，需自行同步（如 dispatch 后再 join）。提案的 `ns_loop_drain_and_stop` 增加了 API 表面而无新能力。

#### 关闭日期
2026-07-04

---

### CORE-007: `ns_signal_disconnect_all` 唤醒释放的 connections

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低

#### 关闭原因
文档化行为。`nanosig_signal.h:330-344` 明确描述 `ns_signal_disconnect_all` 为 teardown / 兜底接口，调用方负责后续 connection 的处理。header 已记录 disconnect_all 后 connection 的 signal 引用应视为无效。

#### 关闭日期
2026-07-04

---

### CORE-008: `ns_signal_emit_raw` 跨多个目标循环的部分广播无法 rollback

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低

#### 关闭原因
重复问题（与 CORE-004 同源）。同样的无回滚设计约束。提案的"取消标记"机制违反 emit 路径无分配约束。文档已记录此行为，调用方需自行处理部分广播后果。

#### 关闭日期
2026-07-04

---

### CORE-009: `ns_loop_quit` 在非运行状态下仍可调用

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低

#### 关闭原因
设计意图。`ns_loop_quit` 的预设置行为对于"优雅退出"场景有用（loop 启动前预先 set quit_requested）。审查本身也承认"可以不修复"。函数幂等且无副作用，重复调用安全。属于文档问题，非代码缺陷。

#### 关闭日期
2026-07-04