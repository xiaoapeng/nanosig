# broker review 归档 2026-07

<a id="BROKER-019"></a>
### BROKER-019: `ns_broker_queue_op` 丢弃 `ns_platform_wakeup_signal(broker->wakeup)` 返回值 `bug`

- **状态**: 关闭-已修复
- **严重度**: 🔴 关键

#### 问题描述
第 229 行 `(void)ns_platform_wakeup_signal(broker->wakeup)` 丢弃返回值。若 signal 失败（eventfd EAGAIN、macOS EVFILT_USER 触发被丢弃、Windows SetEvent 失败），broker loop 线程不会被唤醒。op 已入队但无人处理，用户线程随后无限阻塞在 `ns_broker_wait_op_completion`。

与 BROKER-018 不同：此处的 signal 是用于唤醒 **broker** 的原始 wakeup（`broker->wakeup`），而非 per-op 的 `req->wakeup`。它是整个 proxy 模型的功能入口：没有它，broker 不会处理任何新 op。

#### review 建议
至少不应丢弃错误。在 `ns_broker_queue_op` 路径上增加：
1. 若 `wakeup_signal` 失败 → 从 op_queue 反取出刚入队的 req → 设 `req->rc = NS_E_SHUTDOWN` → `ns_platform_wakeup_signal(req->wakeup)`（告知用户线程失败）。
2. 或使用更可靠的通知机制：在 `op_lock` 锁区间内先读 `broker->quit_requested`/`shutdown_started` 来决定是否入队。

#### 作者建议
`ns_broker_queue_op` 改为返回 int，持 `op_lock` 期间调用 `wakeup_signal`——成功继续，失败则从 op_queue 原子回滚并返回错误码。`ns_broker_add`/`ns_broker_remove` 两个调用方检查返回值，失败时走 `out` 清理路径。→ 关闭-已修复

#### 关闭原因
`src/ns_broker.c:224-235`：函数改为 `static int` 返回类型；持 `op_lock` 期间 signal 失败时 `ns_list_remove_init` 回滚；调用方均检查返回值。

- **关闭日期**: 2026-07-18
- **状态**: 关闭-已修复

#### 可重现的失败场景
Linux eventfd 计数达到 `UINT64_MAX - 1`（极端信号风暴）→ `ns_broker_queue_op` 的 `eventfd_write` 返回 `EAGAIN`。Op 已入 op_queue。Broker 线程在 `waitset_wait` 中休眠。用户线程永远等不到 `req->wakeup`。无任何错误日志。

#### 定位
src/ns_broker.c:224-235

---

<a id="BROKER-023"></a>

---

### BROKER-023: macOS kqueue EVFILT_USER NOTE_TRIGGER 不能保证用户数据的内存序 `bug`

- **状态**: 关闭-已修复
- **严重度**: 🟠 高

#### 问题描述
第 412-413 行注释声称"happens-before 由 OS 原语提供"。在 Linux eventfd 和 Windows SetEvent/WaitForSingleObject 上这成立，但 macOS 上 wakeup 使用 `EVFILT_USER` + `NOTE_TRIGGER`，Apple 内核文档**没有明确承诺** signal 侧的写（`req->rc = op_rc`）在 kevent 返回等待线程后对用户线程可见。`NOTE_TRIGGER` 是纯事件通知，不是 release/acquire 屏障。

用户线程在第 551/582 行读 `req->rc` 时，可能观察到的是一个未更新的值（仍为 init 时的 NS_OK），导致 `ns_broker_add/remove` 在 op 尚未被处理的情况下提前返回成功。这会怎样？用户以为 watcher 已注册实际未注册，事件丢失。

#### review 建议
将 `req->rc` 改为 atomic，loop 线程侧写 `req->rc = op_rc` 用 `atomic_store_explicit(req->rc, op_rc, memory_order_release)`，用户线程侧用 `atomic_load_explicit(&req->rc, memory_order_acquire)`。这以最小的改动修复 mac 端的内存序问题，不影响 Linux/Windows 上已有的 OS 屏障。

#### 作者建议
不在调用方（broker.c）逐点改 atomic，而是从 port 层保证：在 `ns_macos_user_event_signal` 的 `kevent` 调用前加 `atomic_thread_fence(memory_order_release)`，在 `ns_platform_wakeup_wait` 的 `kevent` 返回成功后加 `atomic_thread_fence(memory_order_acquire)`。这样 MAC 端 `wakeup_signal`/`wakeup_wait` 对的语义与 Linux eventfd write/poll、Windows SetEvent/WaitForSingleObject 一致，不需要改任何调用方。→ 关闭-已修复

#### 关闭原因
`platform/macos/port.c:94` 新增 `atomic_thread_fence(memory_order_release)`（signal 侧），`platform/macos/port.c:202` 新增 `atomic_thread_fence(memory_order_acquire)`（wait 侧）。注释同步更新。

- **关闭日期**: 2026-07-18
- **状态**: 关闭-已修复

#### 可重现的失败场景
macOS/ARM64（Apple Silicon）上，通知密集型场景：user 线程调用 `ns_broker_add(W)`，broker 线程处理 op 后写 `req->rc = NS_OK`，然后 `kevent(NOTE_TRIGGER)` 唤醒等待线程。用户线程被 kevent 唤醒，但 CPU store buffer 使 `req->rc` 尚未可见。用户读到 stale NS_OK（初始化预设值），返回成功，认为 W 已注册。Watcher 实际未加入 waitset → 对应 fd 的事件永远不会被 dispatch。

#### 定位
platform/macos/port.c:94
platform/macos/port.c:202

---

<a id="BROKER-025"></a>

---

### BROKER-025: 文件注释声称 `consume_fn` "在持 watcher_mutex 下调用"，但 `dispatch_pending_events` 明确不取锁 `doc`

- **状态**: 关闭-已修复
- **严重度**: 🟡 中
- **关闭原因**: 文件头注释同步为"consume_fn 由 broker loop 线程在 dispatch 阶段无锁调用"。
- **关闭日期**: 2026-07-05

#### 加注（2026-10-17，watcher 回调化）
`consume_fn` 已随 watcher 回调化更名为 `ns_watcher_handler_fn`，语义从"可选消费钩子"
变为"必填就绪回调"。本条历史结论仍成立：回调由 broker loop 线程在 dispatch 阶段、
无锁调用——文件头 `@section 生命周期` 与 `nanosig_broker.h` 的 `@warning` 已同步改写。
不重开。

---

<a id="BROKER-028"></a>

---

### BROKER-028: `ns_broker_drain_op_queue_shutdown` 死前向声明 `cleanup`

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低
- **关闭原因**: 删除第 516 行冗余前向声明。
- **关闭日期**: 2026-07-05

---

<a id="BROKER-030"></a>

---

### BROKER-030: BROKER-020 修复中第二次 `ns_broker_wait_op_completion` 返回值被丢弃 `bug`

- **状态**: 关闭-已修复
- **严重度**: 🟠 高
- **关闭原因**: 检查第二次 `ns_broker_wait_op_completion` 返回值——若再次超时，销毁 wakeup 后返回 `NS_E_TIMEOUT`，不再读取未被 broker 设置的 `req->rc`。
- **关闭日期**: 2026-07-05

---

<a id="BROKER-020"></a>

---

### BROKER-016: shutdown 延迟无上界：long consume_fn 卡住 loop 退出 (LOW) `cleanup`

- **状态**: 关闭-不适用
- **严重度**: 🟢 较低

#### 关闭原因
用户责任。consume_fn 的"必须非阻塞"约束已在 `nanosig_broker.h:52` 文档化。
阻塞 consume_fn 是用户错误，库不应对此进行防御。加超时/ yield 点增加复杂度而无新增能力。

#### 关闭日期
2026-07-04

#### 加注（2026-10-17，watcher 回调化）
`consume_fn` 已更名为必填 `ns_watcher_handler_fn`；"必须非阻塞"约束原样保留并强化为
`nanosig_broker.h` 的 `@warning`（阻塞会卡住所有 watcher 与 timer 投递）。结论不变，
仍是用户责任，库不防御。不重开。

---

<a id="BROKER-001"></a>

---

### BROKER-001: `ns_broker_add/remove` 中 `g_broker` 无同步读取，使用-后-释放

- **状态**: 关闭-已修复
- **严重度**: 🔴 关键
- **关闭原因**: proxy 模型重构中实现 `shutdown_started` 标志（见 2026-07-04 修复历史）。新增的 `ns_broker_add` / `ns_broker_remove` 在函数入口检查 `shutdown_started`，若已置位直接返回 `NS_E_SHUTDOWN`，不再解引用 broker。这样 `g_broker` 即使在 shutdown 期间被另一线程置 NULL，新调用也安全返回，不需要原子指针或引用计数。`ns_shutdown` 内部序列化：先设置 `shutdown_started`、join loop 线程、最后才将 `g_broker = NULL`，确保顺序正确。
- **关闭日期**: 2026-07-04

---

<a id="BROKER-003"></a>

---

### BROKER-003: `ns_broker_emit_completion` 无互斥保护读 `user_data`，与 `remove` 竞态

- **状态**: 关闭-已修复
- **严重度**: 🟠 高
- **关闭原因**: proxy 模型重构彻底解决了用户-线程与 broker-线程之间的 `user_data` / `waitable` 指针竞争：
  - `ns_broker_add` / `ns_broker_remove` 不再由 user 线程直接操作 broker 链表或 waitset，所有修改都在 broker loop 线程串行执行。
  - user 线程入队 op + 等待 op 完成 wakeup，broker 线程处理后再 signal；user 线程在 broker 处理完之前不会返回，更不会释放 watcher 内存。
  - 文档强约束：watcher 在 pending proxy 期间不能 deinit，否则 UAF。
  - `shutdown_started` 标志确保 shutdown 后新 add/remove 直接返回错误而非永久阻塞。
- **关闭日期**: 2026-07-04

---

<a id="BROKER-004"></a>

---

### BROKER-004: `consume_fn` 同步约束与 level-triggered 忙循环

- **状态**: 关闭-已修复
- **严重度**: 🟠 高
- **关闭原因**:
  - **同步约束**: proxy 模型 + consume_fn 不得 add/remove 文档约束保证 broker 线程上 consume_fn 的 watcher 不会被并发释放。
  - **Level-triggered 忙循环**: 重构后 `ns_broker_dispatch_pending_events` 处理前先检查 `watcher->has_pending_event`，dispatch 后立即清零。如果 consume_fn 返回 0 跳过了 emit，下次 waitset_wait 重新触发该 watcher 时 `ns_broker_queue_event` 会重新设置 `has_pending_event=1`，不会丢失事件（pending_event.triggered_events 用 `|=` 累积）。
- **关闭日期**: 2026-07-04

#### 加注（2026-10-17，watcher 回调化）
合同已修订：`consume_fn` 更名并改为必填 `ns_watcher_handler_fn`，dispatch 退化为
`watcher->handler(watcher, triggered_events)`，"同步约束"由**黑名单式**线程边界取代——
回调内只禁止会等待 broker 线程的 API（`ns_broker_add`/`remove`/`ns_shutdown` 及 teardown），
`ns_signal_emit*` 明确允许。level-triggered 忙循环的缓解方式由"NULL 则 emit 的 pending_event
门控"改为文档化契约：**handler 必须读净电平事件源**。不重开。

---

<a id="BROKER-020"></a>

---

### BROKER-020: `ns_broker_wait_op_completion` 无限等待 + 无恢复路径 `bug`

- **状态**: 关闭-已修复
- **严重度**: 🔴 关键
- **关闭原因**: `ns_broker_wait_op_completion` 改为 500ms 超时。超时后先 `ns_broker_try_dequeue_op`（O(1) link 自环检查）尝试从 op_queue 摘除 req：移除成功说明 broker 未碰过该 req，user 线程安全返回 `NS_E_TIMEOUT`；移除失败说明 broker 已出队处理中（窗口极窄，broker 即将完成），user 线程再等一次 wakeup 结果。`nanosig_status.h` 新增 `NS_E_TIMEOUT = -14`。
- **关闭日期**: 2026-07-05

---
