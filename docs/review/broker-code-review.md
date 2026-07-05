# Event Broker 代码审查报告

**日期**: 2026-07-05
**审查者**: claude-code-review (view03)

---

## 修复历史

### 2026-07-05 — 第四轮审查：Proxy 模型并发深度分析

- **审查范围**: `src/ns_broker.c` — proxy 模型重构后的并发安全性、OS 原语内存序假设、错误路径完整性、文档与实践一致性。
- **审查方法**: 10 角度 × 8 候选并发分析 → 1 轮验证 → sweep → 合并去重 → 15 条 findings（排除已有关闭-已拒绝条目）。
- **新开问题**: `BROKER-017` ~ `BROKER-030`（详见"现在打开的问题"和"现在关闭的问题"）。
- **后续修复**:
  - BROKER-025: 文件头注释 `持 watcher_mutex` → `无锁调用`（dispatch 重构后已不取锁），已修复。
  - BROKER-028: 删除冗余前向声明 `ns_broker_drain_op_queue_shutdown`，已修复。
  - BROKER-020: `ns_broker_wait_op_completion` 改为 500ms 超时，超时返回 `NS_E_TIMEOUT`。超时后调用方先 `ns_broker_try_dequeue_op`（O(1) link 自环检查）尝试从 op_queue 移除 req——移除成功则安全返回；移除失败说明 broker 已出队正在处理（窗口极窄），user 线程再等一次结果。第二次等待返回值也被检查——若再次超时，同样返回 `NS_E_TIMEOUT`。`nanosig_status.h` 新增 `NS_E_TIMEOUT = -14`。已修复。
  - BROKER-030: BROKER-020 修复中第二次 `ns_broker_wait_op_completion` 返回值被 `(void)` 丢弃——超时后读未被 broker 设置的 `req->rc`（仍为 NS_OK）会误报成功。改为检查返回值，超时返回 `NS_E_TIMEOUT`。已修复。

### 2026-07-04 — 第三轮修复（review 仲裁 + 代码修复 + 合同补全）

- **BROKER-011**: `ns_watcher_init_common` 入口加 `if(signal.mutex != NULL) return NS_E_EXISTS`，运行时阻止二次 init。
- **BROKER-013**: `ns_watcher_set_consume_handle` 文档重写为**持久句柄语义**——句柄可在一早设置、跨多次 dispatch 有效、生命周期由调用方保证。
- **BROKER-015**: 删除 `ns_broker_add` 和 `ns_broker_remove` 末尾的 `ns_broker_notify`（proxy 已 wake 循环，重复通知仅造成徒劳 wakeup）。
- **BROKER-018**: `ns_broker_dispatch_watcher` 中 `has_pending_event` 注释与代码行为同步（提前清零是设计意图）。
- **BROKER-009/010/014 合同补全**: `nanosig_broker.h` 中 `ns_broker_add`/`ns_broker_remove` 加入 `@warning 不得与 ns_shutdown() 并发调用`。`nanosig.h` 中 `ns_init`/`ns_shutdown` 已有 `@pre` 约束。

### 2026-07-04 — Proxy 模型重构

- **核心改动**: `ns_broker_add` / `ns_broker_remove` 改为 proxy 模式，通过 `ns_broker_op_queue` 把所有 add/remove 操作委托给 broker loop 线程执行
- **新机制**:
  - 每个 op_request 自带 `ns_platform_wakeup_t *wakeup`（用户线程创建/销毁，loop 仅 signal）
  - broker 线程先 drain `op_queue` 再 dispatch pending events，确保 add/remove 在事件派发之前完成
  - `is_pending_remove` 标记：remove 处理后置位，dispatch 阶段跳过此 watcher 的 pending_event
  - `ns_event_broker.shutdown_started` 标志：shutdown 路径置位后新 add/remove 直接返回 `NS_E_SHUTDOWN`
  - `ns_broker_drain_op_queue_shutdown` 在 loop join 后 drain 剩余 op，全部 signal `NS_E_SHUTDOWN`
- **影响文件**: `src/ns_broker.c`（重写）, `include/nanosig/nanosig_broker.h`（watcher struct 增 3 个 broker 内部字段）

### BROKER-002 / BROKER-008: 关闭顺序导致 notify 回调悬空

- **修复 commit**: pre-existing
- **修复方式**: 在 `ns_broker_global_shutdown` 中将 `ns_timer_mgr_global_shutdown()` 移到 `g_broker = NULL` 之前。

### BROKER-007: `fire_expired` 后不立即重检 timer

- **修复 commit**: pre-existing
- **修复方式**: 在 `ns_timer_mgr_fire_expired()` 之后立即重检 `ns_timer_mgr_next_timeout`，避免等待下次 `waitset_wait`。

---

## 现在打开的问题

### BROKER-018: `ns_broker_drain_op_queue_shutdown` 丢弃 `wakeup_signal` 返回值 `bug`

- **状态**: 打开
- **严重度**: 🔴 关键

#### 问题描述
第 443 行 `(void)ns_platform_wakeup_signal(req->wakeup)` 丢弃返回值。若 signal 失败（Linux eventfd EAGAIN 计数器溢出、Windows SetEvent 失败、macOS kqueue NOTE_TRIGGER 被丢弃），用户线程将永久阻塞在 `ns_broker_wait_op_completion`（第 241 行 `NS_PLATFORM_WAIT_INFINITE_US`）。此时 shutdown 已推进到第 691 行销毁 waitset/wakeup 等资源，用户线程的 `req` 和 `wakeup` 指向已释放或孤立的句柄——没有外部救援路径。

这是第 684 行 shutdown 后 drain 的最后一道屏障——若此处失败，等待的用户没有恢复手段。

#### review 建议
在信号失败时给一个回退路径：使用一个共享的 shutdown condvar 或一个 `op_shutdown_done` 原子计数，让所有等待的用户线程能自检退出（而非依赖每个 `req.wakeup` 的个别信号）。最低成本方案：让 `ns_broker_wait_op_completion` 检查 `req->wakeup` 是否已被唤醒（用非阻塞 poll），同时定期（如 100ms）自检 `req->rc != NS_OK` 或一个阶段计数器。

#### 作者建议
（待作者补充）

#### 可重现的失败场景
1000 个并发 `ns_broker_add` 调用堆叠在 op_queue 上。`ns_shutdown()` 触发 drain → 第 443 行 eventfd_write 因内核计数器饱和返回 EAGAIN（Linux eventfd 最大计数值为 `UINT64_MAX-1`）。1000 个用户线程全部永久阻塞。

#### 定位
`src/ns_broker.c:443`

---

### BROKER-019: `ns_broker_queue_op` 丢弃 `ns_platform_wakeup_signal(broker->wakeup)` 返回值 `bug`

- **状态**: 打开
- **严重度**: 🔴 关键

#### 问题描述
第 229 行 `(void)ns_platform_wakeup_signal(broker->wakeup)` 丢弃返回值。若 signal 失败（eventfd EAGAIN、macOS EVFILT_USER 触发被丢弃、Windows SetEvent 失败），broker loop 线程不会被唤醒。op 已入队但无人处理，用户线程随后无限阻塞在 `ns_broker_wait_op_completion`。

与 BROKER-018 不同：此处的 signal 是用于唤醒 **broker** 的原始 wakeup（`broker->wakeup`），而非 per-op 的 `req->wakeup`。它是整个 proxy 模型的功能入口：没有它，broker 不会处理任何新 op。

#### review 建议
至少不应丢弃错误。在 `ns_broker_queue_op` 路径上增加：
1. 若 `wakeup_signal` 失败 → 从 op_queue 反取出刚入队的 req → 设 `req->rc = NS_E_SHUTDOWN` → `ns_platform_wakeup_signal(req->wakeup)`（告知用户线程失败）。
2. 或使用更可靠的通知机制：在 `op_lock` 锁区间内先读 `broker->quit_requested`/`shutdown_started` 来决定是否入队。

#### 作者建议
（待作者补充）

#### 可重现的失败场景
Linux eventfd 计数达到 `UINT64_MAX - 1`（极端信号风暴）→ `ns_broker_queue_op` 的 `eventfd_write` 返回 `EAGAIN`。Op 已入 op_queue。Broker 线程在 `waitset_wait` 中休眠。用户线程永远等不到 `req->wakeup`。无任何错误日志。

#### 定位
`src/ns_broker.c:229`

---

### BROKER-021: `ns_broker_remove_all_watchers` 在 watcher_mutex 锁失败时静默返回，watcher 残留链表节点 `bug`

- **状态**: 打开
- **严重度**: 🟠 高

#### 问题描述
第 705 行 `ns_platform_mutex_lock(broker->watcher_mutex)` 在锁失败时（返回非 NS_OK，如 EDEADLK、ERROR_ABANDONED_WAIT_0），函数直接返回，不执行任何清理。随后第 698 行直接 `ns_platform_free(broker)`——所有仍链接在 `watcher_head` 上的 watcher 的 `broker_node` 指针变成悬空指针。用户线程后续调用 `ns_watcher_deinit` 时第 171 行的 `ns_broker_node_is_linked` 会读到 stale 链表节点。

同时，`ns_broker_drain_op_queue`（第 386 行）和 `ns_broker_drain_op_queue_shutdown`（第 430 行）在 `op_lock` 锁失败时也有相同的静默返回问题。

#### review 建议
统一修复三处锁失败：至少 `assert`（debug build）或 `abort`（release build），因为这些 locks 都是在内部路径使用，失败表示严重故障（锁对象被破坏、死锁、多线程重复等）。不推荐静默返回，因为状态不一致比 abort 更危险（内存损坏 → 遗漏侧信道）。最低成本方案：拉出 `watcher_head` 链表后释放锁再截断链表，即使锁失败也采取紧急清理（走 force-unlink 路径，不做 waitset_remove 但至少断链）。

#### 作者建议
（待作者补充）

#### 可重现的失败场景
Lock 实现被信号中断后返回 `EINTR`（某些较旧的 POSIX 平台）→ `ns_platform_mutex_lock` 返回非 NS_OK → `ns_broker_remove_all_watchers` 直接 return。Broker 被 freed。Watcher 的 `broker_node` 成为悬空指针。用户线程 `ns_watcher_deinit` → `ns_broker_node_is_linked` 读到无效节点 → UAF 或无限循环。

#### 定位
`src/ns_broker.c:705`（同类问题：`src/ns_broker.c:386`、`src/ns_broker.c:430`）

---

### BROKER-023: macOS kqueue EVFILT_USER NOTE_TRIGGER 不能保证用户数据的内存序 `bug`

- **状态**: 打开
- **严重度**: 🟠 高

#### 问题描述
第 412-413 行注释声称"happens-before 由 OS 原语提供"。在 Linux eventfd 和 Windows SetEvent/WaitForSingleObject 上这成立，但 macOS 上 wakeup 使用 `EVFILT_USER` + `NOTE_TRIGGER`，Apple 内核文档**没有明确承诺** signal 侧的写（`req->rc = op_rc`）在 kevent 返回等待线程后对用户线程可见。`NOTE_TRIGGER` 是纯事件通知，不是 release/acquire 屏障。

用户线程在第 551/582 行读 `req->rc` 时，可能观察到的是一个未更新的值（仍为 init 时的 NS_OK），导致 `ns_broker_add/remove` 在 op 尚未被处理的情况下提前返回成功。这会怎样？用户以为 watcher 已注册实际未注册，事件丢失。

#### review 建议
将 `req->rc` 改为 atomic，loop 线程侧写 `req->rc = op_rc` 用 `atomic_store_explicit(req->rc, op_rc, memory_order_release)`，用户线程侧用 `atomic_load_explicit(&req->rc, memory_order_acquire)`。这以最小的改动修复 mac 端的内存序问题，不影响 Linux/Windows 上已有的 OS 屏障。

#### 作者建议
（待作者补充）

#### 可重现的失败场景
macOS/ARM64（Apple Silicon）上，通知密集型场景：user 线程调用 `ns_broker_add(W)`，broker 线程处理 op 后写 `req->rc = NS_OK`，然后 `kevent(NOTE_TRIGGER)` 唤醒等待线程。用户线程被 kevent 唤醒，但 CPU store buffer 使 `req->rc` 尚未可见。用户读到 stale NS_OK（初始化预设值），返回成功，认为 W 已注册。Watcher 实际未加入 waitset → 对应 fd 的事件永远不会被 dispatch。

#### 定位
`src/ns_broker.c:412-415` 伴随 `src/ns_broker.c:551` 和 `src/ns_broker.c:582`（用户读 rc）

---

### BROKER-029: `ns_broker_drain_op_queue` 中 op_lock 锁失败时静默返回导致待处理 op 未被 signal `bug`

- **状态**: 打开
- **严重度**: 🟠 高

#### 问题描述
第 386 行 `if(ns_platform_mutex_lock(broker->op_lock) != NS_OK) return;`——在 drain 循环中途，若 `op_lock` 加锁失败（EDEADLK、错误的中断等），函数直接返回，不解锁、不继续、不 error log。此时至少有一个 `req` 已经被从 op_queue 中取出（第 391-396 行）但尚未 signal `req->wakeup`——用户线程永久阻塞。

此问题与 BROKER-021（remove_all_watchers 锁失败）是同类型的锁失败静默返回问题。

#### review 建议
将锁失败变成硬失败（`assert` debug build，`abort` release build），或至少记录 fatal error 日志。因为锁失败表示严重的内部状态问题（死锁、销毁后使用），静默恢复是不可能的——任何不完整的 drain 都会导致至少一个用户线程永久挂起。

#### 作者建议
（待作者补充）

#### 可重现的失败场景
Broker 线程在 drain 循环中，第 386 行 op_lock 加锁因 EDEADLK 失败。req 已被取出（第 391-396 行），但 `req->rc` 未设置，`req->wakeup` 未 signal。用户线程将永远等待。Broker 继续循环的下一次 `waitset_wait` 后静默工作（缺少上述 req 的处理不会使 broker 崩溃），但那个用户线程的栈和资源再也得不到释放。

#### 定位
`src/ns_broker.c:386`

---

## 现在关闭的问题

### BROKER-025: 文件注释声称 `consume_fn` "在持 watcher_mutex 下调用"，但 `dispatch_pending_events` 明确不取锁 `doc`

- **状态**: 关闭-已修复
- **严重度**: 🟡 中
- **关闭原因**: 文件头注释同步为"consume_fn 由 broker loop 线程在 dispatch 阶段无锁调用"。
- **关闭日期**: 2026-07-05

### BROKER-028: `ns_broker_drain_op_queue_shutdown` 死前向声明 `cleanup`

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低
- **关闭原因**: 删除第 516 行冗余前向声明。
- **关闭日期**: 2026-07-05

---

### BROKER-017: `op_lock` 在 `g_broker = NULL` 之前被销毁，争用线程可能锁定已销毁互斥锁 `bug`

- **状态**: 关闭-已拒绝
- **严重度**: 🔴 关键
- **关闭原因**: 合同免责。`ns_broker_add`/`ns_broker_remove` 的 `@warning 不得与 ns_shutdown() 并发调用` 前提覆盖此场景。`ns_broker_global_shutdown` 是关闭阶段操作，用户线程不得在 shutdown 进行中调用 `ns_broker_add/remove`。`shutdown_started` 置位后新调用直接返回 `NS_E_SHUTDOWN`，不接触 `op_lock`。竞态仅在用户违反合同（在 shutdown 后仍调用 API）时触发。
- **关闭日期**: 2026-07-05

---

### BROKER-030: BROKER-020 修复中第二次 `ns_broker_wait_op_completion` 返回值被丢弃 `bug`

- **状态**: 关闭-已修复
- **严重度**: 🟠 高
- **关闭原因**: 检查第二次 `ns_broker_wait_op_completion` 返回值——若再次超时，销毁 wakeup 后返回 `NS_E_TIMEOUT`，不再读取未被 broker 设置的 `req->rc`。
- **关闭日期**: 2026-07-05

---

### BROKER-020: `ns_broker_wait_op_completion` 无限等待 + 无恢复路径 `bug`

- **状态**: 关闭-已修复
- **严重度**: 🔴 关键
- **关闭原因**: `ns_broker_wait_op_completion` 改为 500ms 超时。超时后先 `ns_broker_try_dequeue_op`（O(1) link 自环检查）尝试从 op_queue 摘除 req：移除成功说明 broker 未碰过该 req，user 线程安全返回 `NS_E_TIMEOUT`；移除失败说明 broker 已出队处理中（窗口极窄，broker 即将完成），user 线程再等一次 wakeup 结果。`nanosig_status.h` 新增 `NS_E_TIMEOUT = -14`。
- **关闭日期**: 2026-07-05

---

### BROKER-022: Shutdown 后 `ns_broker_remove_all_watchers` 的 `watcher_head` 遍历与用户线程 `ns_watcher_deinit` 竞态 `bug`

- **状态**: 关闭-已拒绝
- **严重度**: 🟠 高
- **关闭原因**: 合同免责。`ns_broker_global_shutdown` 是关闭阶段操作，此时用户线程不得持有仍在 broker 中注册的 watcher——调用方必须在 shutdown 前 remove 并 deinit 所有 watcher。`ns_watcher_deinit` 与 `ns_broker_remove_all_watchers` 并发是用户违反合同，不在库防御范围内。
- **关闭日期**: 2026-07-05

---

### BROKER-024: Proxy 模型为每次 add/remove 分配/销毁 OS wakeup，热路径回归 `cleanup`

- **状态**: 关闭-已拒绝
- **严重度**: 🟡 中
- **关闭原因**: 设计权衡。Proxy 模型用独立 wakeup 隔离 user 线程的等待语义，避免在 broker 的 waitset 上做条件广播，是当前架构下的合理选择。`add/remove` 不是热路径——watcher 生命周期操作的频率远低于事件 dispatch，对 fd 消耗（一次性 eventfd / CreateEvent，用完即销毁）和 syscall 开销可接受。过早优化（预分配池）会引入复杂的状态管理，收益有限。
- **关闭日期**: 2026-07-05

---

### BROKER-026: `ns_broker_drain_op_queue` 和 `ns_broker_drain_op_queue_shutdown` 重复代码维护风险 `cleanup`

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低
- **关闭原因**: 两个 drain 函数共享队列弹出逻辑，但语义不同：`drain_op_queue` 需要传 `completions` 参数给 `do_remove`，`drain_op_queue_shutdown` 直接设 `rc = NS_E_SHUTDOWN` 不执行实际操作。强行合并需额外抽象层（回调或 flag），增加理解成本，对仅 20+40 行的两个函数得不偿失。保持独立实现更清晰。
- **关闭日期**: 2026-07-05

---

### BROKER-027: 测试 Hook `g_ns_test_waitset_wait_result` 使用 `volatile int` 而非 `atomic_int` `test`

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低
- **关闭原因**: 设计权衡。当前测试模式在 `ns_init()` 前写入 `g_ns_test_waitset_wait_result`，`pthread_create` 提供 happens-before 保证，使用安全。改为 `_Atomic` 会引入测试代码对 `<stdatomic.h>` 的依赖，且容易误导未来测试在运行中修改此变量（`atomic` 暗示线程安全可随时写）。保留 `volatile` 但维护现状：只在 init 前一次性写入。
- **关闭日期**: 2026-07-05

---

### BROKER-009: `shutdown_started` 是 plain int，与 shutdown 写者竞争导致 UAF (HIGH) `bug`

- **状态**: 关闭-已拒绝
- **严重度**: 🟠 高

#### 关闭原因
合同前提保护：`ns_broker_add`/`ns_broker_remove` 在 `nanosig_broker.h:131,143` 已文档化
"不得与 `ns_shutdown()` 并发调用"。finding 所述竞争仅在调用方违反该前提时触发，
`ns_shutdown()` 本身已保证 shutdown 序列内只由主线程调用。属用户错误而非库缺陷。

#### 关闭日期
2026-07-04

---

### BROKER-010: `g_broker` 是 plain pointer，与 shutdown 的 NULL+free 竞争 (HIGH) `bug`

- **状态**: 关闭-已拒绝
- **严重度**: 🟠 高

#### 关闭原因
合同前提保护：与 BROKER-009 同类——`ns_broker_add`/`ns_broker_remove` 的
"不得与 `ns_shutdown()` 并发调用"前提覆盖同一场景。

#### 关闭日期
2026-07-04

---

### BROKER-014: `ns_broker_global_init` 在并发 init 之间存在 TOCTOU (MEDIUM) `bug`

- **状态**: 关闭-已拒绝
- **严重度**: 🟡 中

#### 关闭原因
合同前提保护：`ns_init()` 在 `nanosig.h:27` 已文档化
"本函数不得与 `ns_shutdown()` 或自身并发调用"。初始化阶段天然串行，
并发 init 违反前提。`ns_init`/`ns_shutdown` 合同已覆盖。

#### 关闭日期
2026-07-04

---

### BROKER-016: shutdown 延迟无上界：long consume_fn 卡住 loop 退出 (LOW) `cleanup`

- **状态**: 关闭-不适用
- **严重度**: 🟢 较低

#### 关闭原因
用户责任。consume_fn 的"必须非阻塞"约束已在 `nanosig_broker.h:52` 文档化。
阻塞 consume_fn 是用户错误，库不应对此进行防御。加超时/ yield 点增加复杂度而无新增能力。

#### 关闭日期
2026-07-04

---

### BROKER-001: `ns_broker_add/remove` 中 `g_broker` 无同步读取，使用-后-释放

- **状态**: 关闭-已修复
- **严重度**: 🔴 关键
- **关闭原因**: proxy 模型重构中实现 `shutdown_started` 标志（见 2026-07-04 修复历史）。新增的 `ns_broker_add` / `ns_broker_remove` 在函数入口检查 `shutdown_started`，若已置位直接返回 `NS_E_SHUTDOWN`，不再解引用 broker。这样 `g_broker` 即使在 shutdown 期间被另一线程置 NULL，新调用也安全返回，不需要原子指针或引用计数。`ns_shutdown` 内部序列化：先设置 `shutdown_started`、join loop 线程、最后才将 `g_broker = NULL`，确保顺序正确。
- **关闭日期**: 2026-07-04

### BROKER-003: `ns_broker_emit_completion` 无互斥保护读 `user_data`，与 `remove` 竞态

- **状态**: 关闭-已修复
- **严重度**: 🟠 高
- **关闭原因**: proxy 模型重构彻底解决了用户-线程与 broker-线程之间的 `user_data` / `waitable` 指针竞争：
  - `ns_broker_add` / `ns_broker_remove` 不再由 user 线程直接操作 broker 链表或 waitset，所有修改都在 broker loop 线程串行执行。
  - user 线程入队 op + 等待 op 完成 wakeup，broker 线程处理后再 signal；user 线程在 broker 处理完之前不会返回，更不会释放 watcher 内存。
  - 文档强约束：watcher 在 pending proxy 期间不能 deinit，否则 UAF。
  - `shutdown_started` 标志确保 shutdown 后新 add/remove 直接返回错误而非永久阻塞。
- **关闭日期**: 2026-07-04

### BROKER-004: `consume_fn` 同步约束与 level-triggered 忙循环

- **状态**: 关闭-已修复
- **严重度**: 🟠 高
- **关闭原因**:
  - **同步约束**: proxy 模型 + consume_fn 不得 add/remove 文档约束保证 broker 线程上 consume_fn 的 watcher 不会被并发释放。
  - **Level-triggered 忙循环**: 重构后 `ns_broker_dispatch_pending_events` 处理前先检查 `watcher->has_pending_event`，dispatch 后立即清零。如果 consume_fn 返回 0 跳过了 emit，下次 waitset_wait 重新触发该 watcher 时 `ns_broker_queue_event` 会重新设置 `has_pending_event=1`，不会丢失事件（pending_event.triggered_events 用 `|=` 累积）。
- **关闭日期**: 2026-07-04

### BROKER-012: `consume_fn` → `ns_broker_add/remove` 死锁 — 无运行时防护

- **状态**: 关闭-已拒绝
- **严重度**: 🟠 高
- **关闭原因**: 文档仲裁：consume_fn 不得调用 ns_broker_add/remove 的约束已在 `nanosig_broker.h:53-55` 明确文档化——"不得调用任何 nanosig API"——属已知设计约束。proxy 模型重构后此约束通过 user-线程不入 broker 状态维护路径自然保证（详见 BROKER-004 关闭原因）。运行时检测为作者未来可选工作，非当前阻塞项。
- **关闭日期**: 2026-07-04
