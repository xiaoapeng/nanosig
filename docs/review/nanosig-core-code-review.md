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

#### 加注（2026-10-17，watcher 回调化）
本条引用的 `test/unit/test_broker_consume_fn.c` 已随 watcher 回调化删除，其职责迁移到
`test/unit/test_broker_handler.c`（`ns_watcher_consume_fn` → `ns_watcher_handler_fn`）。
引用行号与文件名属历史快照，不再对应现行树；原结论（测试时序问题、非库缺陷）不变。不重开。

### CORE-015: slot 收到的 payload 指针只保证 8 字节对齐，`_Alignof > 8` 的 payload 类型触发未对齐访问

- **状态**: 打开
- **严重度**: 🟠 高
- **类型**: bug

#### 问题描述
`ns_loop_dispatch_pending`（src/nanosig.c:194-199）把交付给 slot 的 payload 指针设为 `record + sizeof(ns_slot_call_t)`，即记录起始 + 24 字节。record ring 的头部/记录粒度是 `NS_MPSC_RECORD_RING_ALIGNMENT`（= `sizeof(size_t)` = 8，include/nanosig/nanosig_mpsc_record_ring.h:28），记录头 8 字节，因此 payload 起始地址只保证 **8 字节对齐**。

公开 API 不对 payload 类型提出任何对齐要求：`ns_signal_init(signal, payload_type)`（include/nanosig/nanosig_signal.h:258）与 `ns_signal_init_raw` 接受任意类型，`ns_slot_fn`（include/nanosig/nanosig_signal.h:57）以 `const void *` 交付。当 payload 类型带 `_Alignof >= 16`（SIMD 向量类型、含 `_Atomic` 成员或 `aligned(16)` 的结构体）时，slot 内对该指针的正常解引用即为未对齐访问——C11 未定义行为，在 aarch64 的向量载入、TSAN/UBSAN 或带对齐检查的代码生成下会实际出问题。文档中没有任何"payload 类型对齐上限"的说明可供调用方规避。

#### review 建议
二选一：
1. 把 ring 的对齐粒度与 payload 交付对齐提升到 `_Alignof(max_align_t)`（会改变 record ring 头部与记录布局，须与 mpsc-record-ring 模块同步评估）；
2. 在 `nanosig_signal.h` 文档化"payload 类型的对齐不得超过 `sizeof(size_t)`"，并给 `ns_signal_init` 宏/`ns_signal_init_raw` 加编译期检查（如基于 `_Alignof` 的 `NS_STATIC_ASSERT`），把问题从运行期 UB 提前到编译期。

#### 作者建议
（待作者补充）

#### 可重现的失败场景
```c
typedef struct __attribute__((aligned(16))) vec4 { unsigned char b[16]; } vec4_t;
static unsigned long g_sum;
static void slot_vec(void *ud, const vec4_t *p){ (void)ud; g_sum += (unsigned long)p->b[0]; }

ns_loop_init(&loop, NULL);
ns_signal_init(&sig, vec4_t);
ns_signal_connect_typed(sig, slot_vec, vec4_t, loop, NULL, &conn);
for(i = 0; i < 3; i++) (void)ns_signal_emit_raw(&sig, &pl, sizeof(pl));
ns_loop_quit(loop);
ns_loop_run(loop);
```
以 `-fsanitize=alignment,undefined -fno-sanitize-recover=all` 构建库与用例，观测：
```
runtime error: member access within misaligned address 0x00010290e3f8 for type 'const vec4_t'
(aka 'const struct vec4'), which requires 16 byte alignment
SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior
```
对照观察（同一 loop 内连续 emit 3 次、`_Alignof(payload) == 1` 的普通结构体，`sizeof` 分别为 8 / 24）：
```
plain 8        sizeof=  8 align= 1  payload mod16/32 -> 0/0 8/8 0/16
plain 24       sizeof= 24 align= 1  payload mod16/32 -> 0/0 8/24 0/16
```
第 2 条记录的 payload 地址 `mod 16 == 8`，证明 16 字节对齐不在保证范围内。

#### 定位
src/nanosig.c:194
src/nanosig.c:196
include/nanosig/nanosig_signal.h:57
include/nanosig/nanosig_signal.h:258

---

### CORE-016: 超出 ring 半容量的 payload 被 emit 报成 `NS_E_INVAL`，该上限未在任何公开文档中说明

- **状态**: 打开
- **严重度**: 🟡 中
- **类型**: bug

#### 问题描述
`ns_signal_emit_raw`（src/nanosig.c:415-453）只校验 `payload_size == signal->payload_size`（src/nanosig.c:423）与 payload 非空，随后把记录交给 `ns_mpsc_record_ring_try_pushv`。record ring 的单条记录上限是 `capacity / 2`，超限时 `try_pushv` 返回 `NS_E_INVAL`，emit 原样透传（src/nanosig.c:442-446）。对调用方而言这是"合法入参被判为非法"：

- 默认配置 `NS_LOOP_CONFIG_DEFAULT()` 的 ring 为 1024 字节，单条记录上限 512，减去 `sizeof(ns_slot_call_t)`（24）后 **payload 实际上限只有 480 字节**。payload 从 484 字节起，每次 emit 恒返回 `NS_E_INVAL`——与队列是否为空无关，也不是文档化的 `NS_E_QUEUE_FULL`。
- 同一根因还有一个更极端的形态：`ns_loop_config_validate`（src/nanosig.c:65-72）只要求容量为 2 的幂，`queue_byte_capacity = 32` 时 `ns_loop_init` 返回 `NS_OK`，但该 loop 上任何 emit 都无法成功（连"无 payload"记录的 8 + 24 字节都超过上限），loop 永远执行不了一次 slot 调用。

公开头文件都未说明这个约束：include/nanosig/nanosig_loop.h:42 只说 `queue_byte_capacity` 决定"跨线程 emit 可排队的总字节数"，include/nanosig/nanosig_signal.h:350-363 只说 payload_size 必须与 signal 一致、emit 路径不允许分配内存。调用方只能读 MPSC 实现或跑起来试错才能得到该限制。

#### review 建议
1. 把"单条记录超过 `capacity/2`"（配置/契约问题，重试无用）与"队列暂时放不下"（瞬时容量问题，可重试）区分表达，不要复用 `NS_E_INVAL`；或
2. 在 `nanosig_loop.h` / `nanosig_signal.h` 明确写出"单条 emit 的有效载荷上限 = `queue_byte_capacity / 2 - 记录头 - sizeof(ns_slot_call_t)`，超出返回 `NS_E_INVAL`"；并
3. 给 `ns_loop_config_validate` 加容量下限（至少能容纳一条最小 slot 调用记录），使 `queue_byte_capacity = 32` 这类永远无法工作的配置在 `ns_loop_init` 阶段就返回 `NS_E_INVAL`。

#### 作者建议
（待作者补充）

#### 可重现的失败场景
```c
typedef struct p500 { unsigned char b[500]; } p500_t;   /* payload_size = 500 */
ns_loop_init(&loop, NULL);                              /* 默认 1024 字节 ring */
ns_signal_init(&sig, p500_t);
ns_signal_connect_typed(sig, slot, p500_t, loop, NULL, &conn);
rc = ns_signal_emit_raw(&sig, &pl, sizeof(pl));         /* 队列为空 */
```
实测：`rc = -3 (NS_E_INVAL)`。同环境逐点扫描（队列始终为空）：
```
payload= 480 -> rc= 0 NS_OK
payload= 484 -> rc=-3 NS_E_INVAL
```
容量下限的实测：
```
capacity=32     loop_init OK, emit(no payload) rc=-3   <-- 该 loop 永远无法执行任何 slot
capacity=64     loop_init OK, emit(no payload) rc=0
capacity=1/16   loop_init rc=-3
```

#### 定位
src/nanosig.c:65
src/nanosig.c:423
src/nanosig.c:442
src/nanosig.c:444
include/nanosig/nanosig_loop.h:42
include/nanosig/nanosig_signal.h:363

---

### CORE-017: `ns_loop_init(out_loop == NULL)` 被文档允许，但会静默泄漏 loop 与 wakeup 句柄

- **状态**: 打开
- **严重度**: 🟡 中
- **类型**: bug

#### 问题描述
`ns_loop_init`（src/nanosig.c:122-165）在 `out_loop == NULL` 时于第 130 行只把入参置空便继续执行，完成 `ns_platform_alloc`（src/nanosig.c:140）与 `ns_platform_wakeup_init`（src/nanosig.c:156）后，在第 159 行跳过回传，返回 `NS_OK`。调用方拿不到任何句柄，**没有任何路径可以释放**这块内存与 wakeup 句柄（`ns_loop_deinit` 需要 loop 指针）。每次这样的调用泄漏一份 loop 结构体 + 内嵌 MPSC ring 存储 + 一个 OS 句柄（Linux eventfd / macOS kqueue / Windows Event）。

include/nanosig/nanosig_loop.h:74 明确文档化"输出创建得到的 loop 句柄；可为 `NULL`，此时不向外传值"，因此这是被公开文档允许的调用，不是调用方违约（不属于合同前提保护）。

#### review 建议
在入口把 `out_loop == NULL` 当作参数错误拒绝（返回 `NS_E_INVAL`，与 `ns_loop_run` / `ns_loop_deinit` 等直接返回 `NS_E_INVAL` 的校验风格一致），或删除该参数"可为 NULL"的契约描述。不应保留一条会分配资源却无法回收的路径。

#### 作者建议
（待作者补充）

#### 可重现的失败场景
```c
ns_init();
for(i = 0; i < 20; i++) rc = ns_loop_init(NULL, NULL);   /* 每次 rc == NS_OK */
```
实测（macOS，统计 /dev/fd 条目数）：
```
fds before=7 after=27 delta=20 (20 calls with out_loop=NULL)
```
即 20 次调用泄漏 20 个 OS 句柄，另有 20 块已分配内存无从释放。

#### 定位
src/nanosig.c:130
src/nanosig.c:140
src/nanosig.c:156
src/nanosig.c:159
include/nanosig/nanosig_loop.h:74

---

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

### CORE-013: `ns_loop_stop` 在 join 失败时仍清除 `async_started`，旧 worker 与 loop 生命周期脱钩

- **状态**: 关闭-已修复
- **严重度**: 🟠 高
- **类型**: bug

#### 问题描述
`ns_loop_stop`（src/nanosig.c:296-308）先 `ns_loop_quit`，再 `ns_platform_thread_join`，然后**无条件**把 `loop->async_started` 清零（src/nanosig.c:305）。平台层 `ns_platform_thread_join` 在 `pthread_join` 返回非 0 时一律返回 `NS_E_INVAL`（platform/macos/port.c:370、platform/linux/port.c:325），最典型的触发场景就是**在 loop 自身线程（slot 回调内）调用 stop**——`pthread_join` 自 join 返回 `EDEADLK`。

此时 `ns_loop_stop` 返回错误，但 `async_started` 已经被清掉，两个守卫同时失效：
- `ns_loop_start`（src/nanosig.c:281）的 `NS_E_BUSY` 守卫失效 → 旧 worker 线程仍存活时可以再次 start，同一 loop 上出现两个线程并发执行 `ns_loop_run_impl`（src/nanosig.c:206）的消费侧逻辑，共享同一个 MPSC ring 的单一消费者位置。
- `ns_loop_deinit`（src/nanosig.c:172）的 `NS_E_BUSY` 守卫同样失效 → 只要 `running` 在旧 worker 处于 slot 内部时读到 0，deinit 就会 `ns_platform_free(loop)` 并关闭 wakeup 句柄（src/nanosig.c:175-180），而旧 worker 线程仍在运行并会继续访问 `loop->queue` / `loop->wakeup`。

`nanosig_loop.h` 只声明了 start/stop 必须串行化（include/nanosig/nanosig_loop.h:8、include/nanosig/nanosig_loop.h:138），未声明"禁止在 loop 线程或 slot 回调内调用 `ns_loop_stop`"；slot 内自停是自然写法，属未文档化约束下的可达路径。

#### 作者建议
按 review 建议修复：只在 join 成功时清 `async_started`；并在 `ns_loop_stop` 的 docstring 补 `@pre`（不得在 loop 自身线程调用；失败时须由其他线程再次 stop），把该约束从"未文档化"变为文档化契约。
→ 关闭-已修复

#### 关闭原因
`src/nanosig.c:307` 改为 `if(rc == NS_OK) loop->async_started = 0;`；`include/nanosig/nanosig_loop.h:140` 补 `@pre`。
新增回归用例 `test/unit/test_loop.c:397`（`test_async_stop_in_slot`，`#if !defined(_WIN32)` 守卫：Windows 的 join 是 `WaitForSingleObject` 自身句柄，自 join 会永久阻塞而非返回错误）：slot 内自停返回错误后，断言 `ns_loop_start` / `ns_loop_deinit` 均返回 `NS_E_BUSY`，再由主线程 stop 得到 `NS_OK`。去掉守卫后该用例在 `ns_loop_start(loop) == NS_E_BUSY` 处失败。

复现口径修正：实测（macOS release）确认的是 start 守卫失效（自停 rc=-3，随后 `ns_loop_start` 返回 0）；场景 2 的 deinit 在"worker 仍处于 slot 内"时被 `running` 守卫拦下（rc=-3），该路径未被本次复现观测到。两条路径同根因，修复后均不再成立。

- 关闭日期: 2026-10-19
- 状态: 关闭-已修复

#### 可重现的失败场景
slot 内调用 `ns_loop_stop`（返回错误），随后主线程 `ns_loop_start` 返回 `NS_OK` 而旧 worker 仍存活。实测输出（macOS release）：
```
1) ns_loop_stop() called from the loop's own thread -> rc=-3 (NS_E_INVAL=-3)
2) worker thread is still alive & inside the slot: 1
3) ns_loop_start() while the old worker is still alive -> rc=0 (NS_OK=0)
```

#### 定位
src/nanosig.c:296
src/nanosig.c:307
src/nanosig.c:281
include/nanosig/nanosig_loop.h:140
test/unit/test_loop.c:397

### CORE-014: slot 内重入 emit 使 loop 永远观察不到 `quit_requested`，`ns_loop_run` / `ns_loop_stop` 永久阻塞

- **状态**: 关闭-已拒绝
- **严重度**: 🟠 高
- **类型**: bug

#### 问题描述
`ns_loop_run_impl`（src/nanosig.c:217-226）的循环体是"先把队列**排空**，再检查 `quit_requested`"，而 `ns_loop_dispatch_pending`（src/nanosig.c:184-204）是 `for(;;)`，只有 `ns_mpsc_record_ring_try_acquire` 返回非 `NS_OK` 才 break。只要一次 dispatch 过程中队列始终非空，退出检查（src/nanosig.c:220）就永远轮不到。

slot 回调内 `ns_signal_emit` 到同一 loop 会持续往队列补记录，因此在该边界上队列从不空：`ns_loop_quit`（含 `ns_loop_stop` 内部调用，src/nanosig.c:303）设置标志并 signal wakeup 被完全忽略。

这与公开文档承诺冲突：include/nanosig/nanosig_loop.h:97 写明"持续等待并分发队列中的 slot 调用，**直到收到 `ns_loop_quit()` 请求**"，include/nanosig/nanosig_loop.h:133-136 承诺 `ns_loop_stop` 会 join 返回。实测两者都不成立——`ns_loop_run` 永不返回，`ns_loop_stop` 卡死在 join（无超时、无恢复路径）。计划文档记录的"dispatch_pending 后只检查一次 quit_requested"是该实现的来源，但对外头文件的承诺并未随之收窄。

#### review 建议
在批量分发之间周期性检查 `quit_requested`（例如每处理 N 条记录或每轮 acquire 成功后检查一次原子标志），使"持续事件源存在时 quit 仍然生效"。
若有意接受当前语义，则必须收窄文档：明确写"quit 只在队列出现空档时生效；slot 内无限重入 emit 会导致 loop 无法退出"，由作者确认取舍。

#### 作者建议
（待作者补充）

#### 可重现的失败场景
```c
static ns_signal_t g_sig;
static void slot_chain(void *ud, const ns_no_payload_t *p){
    (void)ud; (void)p;
    (void)ns_signal_emit(g_sig, NS_NO_PAYLOAD);   /* 自喂，队列永不空 */
}
ns_signal_emit(g_sig, NS_NO_PAYLOAD);             /* 播种一条记录 */
pthread_create(&t, NULL, quitter, NULL);          /* 300ms 后调用 ns_loop_quit(g_loop) */
rc = ns_loop_run(g_loop);                         /* 永不返回 */
```
实测输出（5 秒后进程仍存活，手工 kill）：
```
main: entering synchronous ns_loop_run
quitter: ns_loop_quit -> 0
>>> ns_loop_run NEVER returned despite ns_loop_quit from another thread
```
异步模式同样复现：`ns_loop_start` + slot 自喂后，主线程调用 `ns_loop_stop(loop)`，4 秒后仍未返回。

#### 定位
src/nanosig.c:184
src/nanosig.c:190
src/nanosig.c:218
src/nanosig.c:220
include/nanosig/nanosig_loop.h:97
include/nanosig/nanosig_loop.h:133

#### 关闭原因

作者仲裁：判定不构成需要修复的库缺陷——只有当调用方让事件源永不中断时才会触发，
而"队列空档处生效"的 quit 语义是已知且可接受的。
保留口径记录：include/nanosig/nanosig_loop.h:97 的表述（"直到收到 ns_loop_quit 请求"）
与现状存在语义差距，如需可另开 doc 条目收窄。

- 关闭日期: 2026-10-18
- 状态: 关闭-已拒绝

---

