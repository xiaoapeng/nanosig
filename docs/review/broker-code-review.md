---
review_schema: "nanosig-review-v1"
entries:
  - id: "BROKER-007"
    module: "broker"
    status: "closed-fixed"
    severity: "unknown"
    type: "bug"
    section: "fixed-history"
    location: "docs/review/broker-code-review.md#BROKER-007"
  - id: "BROKER-011"
    module: "broker"
    status: "closed-rejected"
    severity: "high"
    type: "bug"
    section: "closed"
    closed_date: "2026-10-19"
    location: "docs/review/broker-code-review.md#BROKER-011"
  - id: "BROKER-036"
    module: "broker"
    status: "closed-rejected"
    severity: "medium"
    type: "bug"
    section: "closed"
    closed_date: "2026-10-19"
    location: "docs/review/broker-code-review.md#BROKER-036"
  - id: "BROKER-033"
    module: "broker"
    status: "closed-fixed"
    severity: "low"
    type: "doc"
    section: "closed"
    closed_date: "2026-10-19"
    location: "docs/review/broker-code-review.md#BROKER-033"
  - id: "BROKER-034"
    module: "broker"
    status: "closed-fixed"
    severity: "low"
    type: "cleanup"
    section: "closed"
    closed_date: "2026-10-19"
    location: "docs/review/broker-code-review.md#BROKER-034"
  - id: "BROKER-035"
    module: "broker"
    status: "closed-fixed"
    severity: "low"
    type: "cleanup"
    section: "closed"
    closed_date: "2026-10-19"
    location: "docs/review/broker-code-review.md#BROKER-035"
  - id: "BROKER-031"
    module: "broker"
    status: "closed-rejected"
    severity: "medium"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-18"
    location: "docs/review/broker-code-review.md#BROKER-031"
    locations:
      - "src/ns_broker.c:544-553"
  - id: "BROKER-018"
    module: "broker"
    status: "closed-rejected"
    severity: "critical"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-18"
    location: "docs/review/broker-code-review.md#BROKER-018"
    locations:
      - "src/ns_broker.c:490"
  - id: "BROKER-021"
    module: "broker"
    status: "closed-rejected"
    severity: "high"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-18"
    location: "docs/review/broker-code-review.md#BROKER-021"
  - id: "BROKER-029"
    module: "broker"
    status: "closed-rejected"
    severity: "high"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-18"
    location: "docs/review/broker-code-review.md#BROKER-029"
    locations:
      - "src/ns_broker.c:386"
  - id: "BROKER-017"
    module: "broker"
    status: "closed-rejected"
    severity: "critical"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-05"
    location: "docs/review/broker-code-review.md#BROKER-017"
  - id: "BROKER-022"
    module: "broker"
    status: "closed-rejected"
    severity: "high"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-05"
    location: "docs/review/broker-code-review.md#BROKER-022"
  - id: "BROKER-024"
    module: "broker"
    status: "closed-rejected"
    severity: "medium"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-05"
    location: "docs/review/broker-code-review.md#BROKER-024"
  - id: "BROKER-026"
    module: "broker"
    status: "closed-rejected"
    severity: "low"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-05"
    location: "docs/review/broker-code-review.md#BROKER-026"
  - id: "BROKER-027"
    module: "broker"
    status: "closed-rejected"
    severity: "low"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-05"
    location: "docs/review/broker-code-review.md#BROKER-027"
  - id: "BROKER-009"
    module: "broker"
    status: "closed-rejected"
    severity: "high"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-04"
    location: "docs/review/broker-code-review.md#BROKER-009"
  - id: "BROKER-010"
    module: "broker"
    status: "closed-rejected"
    severity: "high"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-04"
    location: "docs/review/broker-code-review.md#BROKER-010"
  - id: "BROKER-014"
    module: "broker"
    status: "closed-rejected"
    severity: "medium"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-04"
    location: "docs/review/broker-code-review.md#BROKER-014"
  - id: "BROKER-012"
    module: "broker"
    status: "closed-rejected"
    severity: "high"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-04"
    location: "docs/review/broker-code-review.md#BROKER-012"
  - id: "BROKER-002"
    module: "broker"
    status: "closed-fixed"
    severity: "unknown"
    type: "unknown"
    section: "fixed-history"
    location: "docs/review/broker-code-review.md#BROKER-002"
  - id: "BROKER-008"
    module: "broker"
    status: "closed-fixed"
    severity: "unknown"
    type: "unknown"
    section: "fixed-history"
    location: "docs/review/broker-code-review.md#BROKER-008"
  - id: "BROKER-020"
    module: "broker"
    status: "closed-fixed"
    severity: "unknown"
    type: "unknown"
    section: "fixed-history"
    location: "docs/review/broker-code-review.md#BROKER-020"
---

# Event Broker 代码审查报告

**日期**: 2026-07-05
**审查者**: claude-code-review (view03)

---

## 修复历史

<a id="BROKER-008"></a>
<a id="BROKER-002"></a>
### BROKER-002 / BROKER-008: 关闭顺序导致 notify 回调悬空

- **修复 commit**: pre-existing
- **修复方式**: 在 `ns_broker_global_shutdown` 中将 `ns_timer_mgr_global_shutdown()` 移到 `g_broker = NULL` 之前。

<a id="BROKER-007"></a>
### BROKER-007: `fire_expired` 后不立即重检 timer

- **修复 commit**: pre-existing
- **修复方式**: 在 `ns_timer_mgr_fire_expired()` 之后立即重检 `ns_timer_mgr_next_timeout`，避免等待下次 `waitset_wait`。

<a id="BROKER-020"></a>
### 2026-10-18 — op 等待改无限等待 + shutdown 契约收紧（撤销 BROKER-020/030 的修复）

- **审查范围**: `src/ns_broker.c` 的 op 等待 / shutdown 路径；随契约收紧一并清理死代码。
- **背景**: BROKER-020 实现的 500ms 超时恢复路径（配合 BROKER-030 的返回值检查）在第二次超时后，
  用户线程会销毁栈上的 `req` 与 `wakeup`，而 broker 线程随后仍执行 `req->rc = op_rc` 并向
  `req->wakeup` signal——写已失效的栈帧、向已关闭的 fd 写信号，构成 use-after-free。
  入队侧已能在 signal 失败时持锁回滚（BROKER-019），超时路径只剩这一条不安全分支。
- **改动**:
  1. `ns_broker_wait_op_completion` 改用 `NS_PLATFORM_WAIT_INFINITE_US` 且**失败不返回**（重试；
     提前返回同样会遗留仍挂在 op_queue 中的 `req`）。`ns_broker_try_dequeue_op` 删除。
  2. `ns_broker_add` / `ns_broker_remove` 的超时分支与二次等待删除，公共骨架合并为
     `ns_broker_submit_op`。两者不再返回 `NS_E_TIMEOUT`。
  3. shutdown 契约收紧为「调用方必须在 `ns_shutdown()` 前完成全部 teardown，库不兜底」：
     删除 `ns_broker_drain_op_queue_shutdown`；删除 `ns_broker_remove_all_watchers`，改为
     join 后若 op 队列非空则记一条警告（不救援线程、不持 `op_lock`）。
  4. 删除 broker 侧 `watcher_mutex`、`watcher_head` 链表与 `ns_watcher_t.broker_node` 字段；
     「已注册」判定改用平台层 `registered_waitset`，且收窄为 `== &g_broker->waitset`
     （该字段语义是"注册在某个 waitset"，调用方也可自建 waitset）。
  5. `shutdown_started` 改 `atomic_int`；`ns_broker_run` 的 timer timeout 死分支简化。
  6. 公开头文件补充「同步会合、无等待上界、可多线程并发调用」与 handler 死锁后果说明；
     `ns_shutdown` 的 `@pre` 补「库不再兜底」。
- **行为变更（有意）**: handler 内误调 `ns_broker_add` / `ns_broker_remove` / `ns_shutdown`，
  表现从"500ms 后返回 `NS_E_TIMEOUT`"变为**永久阻塞**。已在 `nanosig_broker.h` 的 `@warning`
  与 `@note` 中写明。
- **BROKER-020 / BROKER-030 处置**: 两条的「已修复」历史**保留不改写**（append-only）；
  本次撤销的是其实现方式，不是判定它们当时误报。BROKER-030 的返回值检查随第二次等待一并删除
  ——无限等待下不存在"读完一次仍超时"的分支。
- **BROKER-009 关联**: 该条以「合同前提保护」关闭-已拒绝。本次把 `shutdown_started` 改
  `atomic_int` 消除了形式上的 C11 竞争，但**不构成 UAF 屏障**——`g_broker` 仍是 plain pointer
  且会被 free，对象生命周期竞争依旧存在，仍依赖调用方的 teardown 契约。
- **BROKER-011 影响**: BROKER-011 的复现路径依赖本次删除的 `ns_broker_remove_all_watchers`
  遍历；改动后同场景退化为 `remove → NS_E_INVAL` + waitset 句柄泄漏，不再是"
  `ns_shutdown` 死循环"。该条目的处置见"现在打开的问题"中 BROKER-011。
- **未修（已登记）**: Linux `ns_linux_wakeup_drain` 在非 EAGAIN/EINTR 的 errno 上返回
  `NS_E_INVAL` 且未消费 eventfd 计数，导致 `poll` 立即再返回——无限等待下表现为 100% CPU
  热自旋。根因在 `platform/`，登记于 `docs/review/platform-ports-code-review.md`。
- **测试**: 六个 broker 单测补 `TIMEOUT 60`（无等待上界使死锁更容易发生）；
  `test_shutdown_removes_residual_watcher` 更名 `test_watcher_deinit_after_shutdown` 并保留。
- **影响文件**: `src/ns_broker.c`、`include/nanosig/nanosig_broker.h`、`include/nanosig/nanosig.h`、
  `test/unit/test_broker_lifecycle.c`、`test/unit/test_broker_contract_compile.c`、
  `test/unit/test_broker_concurrent_remove.c`、`CMakeLists.txt`。

---

### 2026-07-05 — 第四轮审查：Proxy 模型并发深度分析

- **审查范围**: `src/ns_broker.c` — proxy 模型重构后的并发安全性、OS 原语内存序假设、错误路径完整性、文档与实践一致性。
- **审查方法**: 10 角度 × 8 候选并发分析 → 1 轮验证 → sweep → 合并去重 → 15 条 findings（排除已有关闭-已拒绝条目）。
- **新开问题**: `BROKER-017` ~ `BROKER-030`（详见"现在打开的问题"和"现在关闭的问题"）。
- **后续修复**:
  - BROKER-025: 文件头注释 `持 watcher_mutex` → `无锁调用`（dispatch 重构后已不取锁），已修复。
  - BROKER-028: 删除冗余前向声明 `ns_broker_drain_op_queue_shutdown`，已修复。
  - BROKER-020: `ns_broker_wait_op_completion` 改为 500ms 超时，超时返回 `NS_E_TIMEOUT`。超时后调用方先 `ns_broker_try_dequeue_op`（O(1) link 自环检查）尝试从 op_queue 移除 req——移除成功则安全返回；移除失败说明 broker 已出队正在处理（窗口极窄），user 线程再等一次结果。第二次等待返回值也被检查——若再次超时，同样返回 `NS_E_TIMEOUT`。`nanosig_status.h` 新增 `NS_E_TIMEOUT = -14`。已修复。
  - BROKER-030: BROKER-020 修复中第二次 `ns_broker_wait_op_completion` 返回值被 `(void)` 丢弃——超时后读未被 broker 设置的 `req->rc`（仍为 NS_OK）会误报成功。改为检查返回值，超时返回 `NS_E_TIMEOUT`。已修复。

---

### 2026-07-04 — 第三轮修复（review 仲裁 + 代码修复 + 合同补全）

- **BROKER-011**: `ns_watcher_init_common` 入口加 `if(signal.mutex != NULL) return NS_E_EXISTS`，运行时阻止二次 init。

  > **加注（2026-10-17，watcher 回调化）**：`f165902` 按 D13 删除该守卫，改为
  > "重复 init 属调用方 UB"（`nanosig_broker.h` 的 `@warning`）。该决定把已注册
  > watcher 的二次 init 从"返回 `NS_E_EXISTS`"变成"`ns_shutdown` 死循环"，触发
  > 场景根本改变，已按 §3.1 例外 2 以原 ID **重开**，见"现在打开的问题"BROKER-011。

- **BROKER-013**: `ns_watcher_set_consume_handle` 文档重写为**持久句柄语义**——句柄可在一早设置、跨多次 dispatch 有效、生命周期由调用方保证。
- **BROKER-015**: 删除 `ns_broker_add` 和 `ns_broker_remove` 末尾的 `ns_broker_notify`（proxy 已 wake 循环，重复通知仅造成徒劳 wakeup）。
- **BROKER-018**: `ns_broker_dispatch_watcher` 中 `has_pending_event` 注释与代码行为同步（提前清零是设计意图）。
- **BROKER-009/010/014 合同补全**: `nanosig_broker.h` 中 `ns_broker_add`/`ns_broker_remove` 加入 `@warning 不得与 ns_shutdown() 并发调用`。`nanosig.h` 中 `ns_init`/`ns_shutdown` 已有 `@pre` 约束。

---

### 2026-07-04 — Proxy 模型重构

- **核心改动**: `ns_broker_add` / `ns_broker_remove` 改为 proxy 模式，通过 `ns_broker_op_queue` 把所有 add/remove 操作委托给 broker loop 线程执行
- **新机制**:
  - 每个 op_request 自带 `ns_platform_wakeup_t *wakeup`（用户线程创建/销毁，loop 仅 signal）
  - broker 线程先 drain `op_queue` 再 dispatch pending events，确保 add/remove 在事件派发之前完成
  - `is_pending_remove` 标记：remove 处理后置位，dispatch 阶段跳过此 watcher 的 pending_event
  - `ns_event_broker.shutdown_started` 标志：shutdown 路径置位后新 add/remove 直接返回 `NS_E_SHUTDOWN`
  - `ns_broker_drain_op_queue_shutdown` 在 loop join 后 drain 剩余 op，全部 signal `NS_E_SHUTDOWN`
- **影响文件**: `src/ns_broker.c`（重写）, `include/nanosig/nanosig_broker.h`（watcher struct 增 3 个 broker 内部字段）

<a id="BROKER-002"></a>

---

<a id="BROKER-016"></a>

---

---

## 现在打开的问题


---


## 现在关闭的问题

<a id="BROKER-025"></a>
---
<a id="BROKER-035"></a>
### BROKER-035: `ns_broker_dispatch_pending_events` 中的死转换 `(void)broker;`

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低
- **类型**: cleanup
- **关闭原因**: 代码审查确认 `(void)broker;` 之后第 390 行即解引用 `broker`，转换为死代码，已删除该行。不影响功能，仅清理误导性代码。
- **关闭日期**: 2026-10-19

---
<a id="BROKER-034"></a>
### BROKER-034: 源码注释引用仅存在于 docs/ 的计划决策编号

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低
- **类型**: cleanup
- **关闭原因**: 已删除 `src/ns_broker.c:165` 注释中的 `（D14）` 计划决策编号，源码中不再出现阶段/计划编号。
- **关闭日期**: 2026-10-19

---
<a id="BROKER-033"></a>
### BROKER-033: `ns_watcher_init` 的 `handle` 参数文档仍是联合体写法

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低
- **类型**: doc
- **关闭原因**: 已在 `include/nanosig/nanosig_broker.h:89` 将 `@param handle` 改为"传平台原语句柄本身（同 `ns_platform_waitable_t::primitive`）"，与 `demos/demo_watcher_ready.c` 的既有写法一致。
- **关闭日期**: 2026-10-19

---
<a id="BROKER-036"></a>
### BROKER-036: `ns_watcher_init` 接受 `events == 0`，host 三后端注册成功后永不触发

- **状态**: 关闭-已拒绝
- **严重度**: 🟡 中
- **类型**: bug
- **关闭原因**: 合同前提保护：`ns_watcher_events_are_valid` 公共头已文档化"`@param events` `NS_WAITABLE_EVENT_*` 位组合，**至少包含一个事件位**"。`events == 0` 属调用方违反文档化前提，行为未定义，不属库缺陷。
- **关闭日期**: 2026-10-19

---
<a id="BROKER-011"></a>
### BROKER-011: 二次 `ns_watcher_init` 让已注册 watcher 的 waitset 槽位无法注销（回归，重开）

- **状态**: 关闭-已拒绝
- **严重度**: 🟠 高
- **类型**: bug
- **关闭原因**: 合同前提保护：头文件 `@warning` 已明确文档化"不检测重复初始化，属调用方错误（未定义行为）"。AGENTS.md §4.4.5 三关键词中，数据竞争与内存安全均不成立，仅剩资源泄漏，不构成防御性加固的充分理由。
- **关闭日期**: 2026-10-19
<a id="BROKER-017"></a>
### BROKER-017: `op_lock` 在 `g_broker = NULL` 之前被销毁，争用线程可能锁定已销毁互斥锁 `bug`

- **状态**: 关闭-已拒绝
- **严重度**: 🔴 关键
- **关闭原因**: 合同免责。`ns_broker_add`/`ns_broker_remove` 的 `@warning 不得与 ns_shutdown() 并发调用` 前提覆盖此场景。`ns_broker_global_shutdown` 是关闭阶段操作，用户线程不得在 shutdown 进行中调用 `ns_broker_add/remove`。`shutdown_started` 置位后新调用直接返回 `NS_E_SHUTDOWN`，不接触 `op_lock`。竞态仅在用户违反合同（在 shutdown 后仍调用 API）时触发。
- **关闭日期**: 2026-07-05

---

<a id="BROKER-022"></a>
### BROKER-022: Shutdown 后 `ns_broker_remove_all_watchers` 的 `watcher_head` 遍历与用户线程 `ns_watcher_deinit` 竞态 `bug`

- **状态**: 关闭-已拒绝
- **严重度**: 🟠 高
- **关闭原因**: 合同免责。`ns_broker_global_shutdown` 是关闭阶段操作，此时用户线程不得持有仍在 broker 中注册的 watcher——调用方必须在 shutdown 前 remove 并 deinit 所有 watcher。`ns_watcher_deinit` 与 `ns_broker_remove_all_watchers` 并发是用户违反合同，不在库防御范围内。
- **关闭日期**: 2026-07-05

---

<a id="BROKER-024"></a>
### BROKER-024: Proxy 模型为每次 add/remove 分配/销毁 OS wakeup，热路径回归 `cleanup`

- **状态**: 关闭-已拒绝
- **严重度**: 🟡 中
- **关闭原因**: 设计权衡。Proxy 模型用独立 wakeup 隔离 user 线程的等待语义，避免在 broker 的 waitset 上做条件广播，是当前架构下的合理选择。`add/remove` 不是热路径——watcher 生命周期操作的频率远低于事件 dispatch，对 fd 消耗（一次性 eventfd / CreateEvent，用完即销毁）和 syscall 开销可接受。过早优化（预分配池）会引入复杂的状态管理，收益有限。
- **关闭日期**: 2026-07-05

---

<a id="BROKER-026"></a>
### BROKER-026: `ns_broker_drain_op_queue` 和 `ns_broker_drain_op_queue_shutdown` 重复代码维护风险 `cleanup`

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低
- **关闭原因**: 两个 drain 函数共享队列弹出逻辑，但语义不同：`drain_op_queue` 需要传 `completions` 参数给 `do_remove`，`drain_op_queue_shutdown` 直接设 `rc = NS_E_SHUTDOWN` 不执行实际操作。强行合并需额外抽象层（回调或 flag），增加理解成本，对仅 20+40 行的两个函数得不偿失。保持独立实现更清晰。
- **关闭日期**: 2026-07-05

---

<a id="BROKER-027"></a>
### BROKER-027: 测试 Hook `g_ns_test_waitset_wait_result` 使用 `volatile int` 而非 `atomic_int` `test`

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低
- **关闭原因**: 设计权衡。当前测试模式在 `ns_init()` 前写入 `g_ns_test_waitset_wait_result`，`pthread_create` 提供 happens-before 保证，使用安全。改为 `_Atomic` 会引入测试代码对 `<stdatomic.h>` 的依赖，且容易误导未来测试在运行中修改此变量（`atomic` 暗示线程安全可随时写）。保留 `volatile` 但维护现状：只在 init 前一次性写入。
- **关闭日期**: 2026-07-05

---

<a id="BROKER-009"></a>
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

<a id="BROKER-010"></a>
### BROKER-010: `g_broker` 是 plain pointer，与 shutdown 的 NULL+free 竞争 (HIGH) `bug`

- **状态**: 关闭-已拒绝
- **严重度**: 🟠 高

#### 关闭原因
合同前提保护：与 BROKER-009 同类——`ns_broker_add`/`ns_broker_remove` 的
"不得与 `ns_shutdown()` 并发调用"前提覆盖同一场景。

#### 关闭日期
2026-07-04

---

<a id="BROKER-014"></a>
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

<a id="BROKER-012"></a>
### BROKER-012: `consume_fn` → `ns_broker_add/remove` 死锁 — 无运行时防护

- **状态**: 关闭-已拒绝
- **严重度**: 🟠 高
- **关闭原因**: 文档仲裁：consume_fn 不得调用 ns_broker_add/remove 的约束已在 `nanosig_broker.h:53-55` 明确文档化——"不得调用任何 nanosig API"——属已知设计约束。proxy 模型重构后此约束通过 user-线程不入 broker 状态维护路径自然保证（详见 BROKER-004 关闭原因）。运行时检测为作者未来可选工作，非当前阻塞项。
- **关闭日期**: 2026-07-04

#### 加注（2026-10-17，watcher 回调化）
合同措辞已由"不得调用任何 nanosig API"修订为**黑名单式**："不得调用任何会等待 broker
线程的 nanosig API（`ns_broker_add`/`ns_broker_remove`/`ns_shutdown` 及 teardown）"，
`ns_signal_emit*` 明确允许（非阻塞）。条目结论不变：无运行时防护、依赖文档化合同；
故不重开，也不新增重开的运行时守卫条目。

<a id="BROKER-018"></a>
### BROKER-018: `ns_broker_drain_op_queue_shutdown` 丢弃 `wakeup_signal` 返回值 `bug`

- **状态**: 关闭-已拒绝
- **严重度**: 🔴 关键

#### 问题描述
第 443 行 `(void)ns_platform_wakeup_signal(req->wakeup)` 丢弃返回值。若 signal 失败（Linux eventfd EAGAIN 计数器溢出、Windows SetEvent 失败、macOS kqueue NOTE_TRIGGER 被丢弃），用户线程将永久阻塞在 `ns_broker_wait_op_completion`（第 241 行 `NS_PLATFORM_WAIT_INFINITE_US`）。此时 shutdown 已推进到第 691 行销毁 waitset/wakeup 等资源，用户线程的 `req` 和 `wakeup` 指向已释放或孤立的句柄——没有外部救援路径。

这是第 684 行 shutdown 后 drain 的最后一道屏障——若此处失败，等待的用户没有恢复手段。

#### review 建议
在信号失败时给一个回退路径：使用一个共享的 shutdown condvar 或一个 `op_shutdown_done` 原子计数，让所有等待的用户线程能自检退出（而非依赖每个 `req.wakeup` 的个别信号）。最低成本方案：让 `ns_broker_wait_op_completion` 检查 `req->wakeup` 是否已被唤醒（用非阻塞 poll），同时定期（如 100ms）自检 `req->rc != NS_OK` 或一个阶段计数器。

#### 作者建议
合同前提保护。`drain_op_queue_shutdown` 执行时：
1. `shutdown_started` 已置位，新 add/remove 直接返回 `NS_E_SHUTDOWN`，不会再入队
2. 队列中残留的 op 虽在 shutdown 前入队，但 `nanosig_broker.h` 已文档化 `@warning 不得与 ns_shutdown() 并发调用`——调用方在 shutdown 期间不应假设 add/remove 能正常完成
3. 残留 op 数量极少（shutdown_started → loop join 的窗口极窄），eventfd 的 uint64_t 计数器不可能在 drain 数条 op 的场景下溢出
4. Shutdown drain 是清理路径，平台级 signal 失败在此上下文中不可恢复
→ 关闭-已拒绝

#### 关闭原因
合同前提保护：`ns_broker_add`/`ns_broker_remove` 的 `@warning 不得与 ns_shutdown() 并发调用` 前提覆盖此路径。shutdown drain 仅处理窗口期内残存的少数 op，eventfd 计数器溢出在 drain 场景中不可达。

- **关闭日期**: 2026-07-18
- **状态**: 关闭-已拒绝

#### 可重现的失败场景
1000 个并发 `ns_broker_add` 调用堆叠在 op_queue 上。`ns_shutdown()` 触发 drain → 第 443 行 eventfd_write 因内核计数器饱和返回 EAGAIN（Linux eventfd 最大计数值为 `UINT64_MAX-1`）。1000 个用户线程全部永久阻塞。

#### 定位
src/ns_broker.c:490

<a id="BROKER-021"></a>
### BROKER-021: `ns_broker_remove_all_watchers` 在 watcher_mutex 锁失败时静默返回，watcher 残留链表节点 `bug`

- **状态**: 关闭-已拒绝
- **严重度**: 🟠 高

#### 问题描述
第 705 行 `ns_platform_mutex_lock(broker->watcher_mutex)` 在锁失败时（返回非 NS_OK，如 EDEADLK、ERROR_ABANDONED_WAIT_0），函数直接返回，不执行任何清理。随后第 698 行直接 `ns_platform_free(broker)`——所有仍链接在 `watcher_head` 上的 watcher 的 `broker_node` 指针变成悬空指针。用户线程后续调用 `ns_watcher_deinit` 时第 171 行的 `ns_broker_node_is_linked` 会读到 stale 链表节点。

同时，`ns_broker_drain_op_queue`（第 386 行）和 `ns_broker_drain_op_queue_shutdown`（第 430 行）在 `op_lock` 锁失败时也有相同的静默返回问题。

#### review 建议
统一修复三处锁失败：至少 `assert`（debug build）或 `abort`（release build），因为这些 locks 都是在内部路径使用，失败表示严重故障（锁对象被破坏、死锁、多线程重复等）。不推荐静默返回，因为状态不一致比 abort 更危险（内存损坏 → 遗漏侧信道）。最低成本方案：拉出 `watcher_head` 链表后释放锁再截断链表，即使锁失败也采取紧急清理（走 force-unlink 路径，不做 waitset_remove 但至少断链）。

#### 作者建议
三个平台的 `ns_platform_mutex_lock`：
- **Linux/macOS**：`pthread_mutex_lock` 在正确使用的非递归 mutex 上不会失败。EDEADLK 意味着调用方有 bug（同线程重复加锁）；EAGAIN 意味着内核完全 OOM——两者均不可恢复。
- **Windows**：`AcquireSRWLockExclusive` 无返回值（void），永远成功。
- 锁失败等价于不可恢复的系统级错误，静默返回已在所有路径上提供了最轻的失败模式。改为 `assert`/`abort` 是合理强化但不是 bug fix。
→ 关闭-已拒绝

#### 关闭原因
合同前提保护：三个平台上 `ns_platform_mutex_lock` 在正确使用下不会失败。锁失败表示调用方违反前提（如 double-lock）或系统 OOM——均不可恢复，静默返回已是最轻处理。

- **关闭日期**: 2026-07-18
- **状态**: 关闭-已拒绝

#### 可重现的失败场景
Lock 实现被信号中断后返回 `EINTR`（某些较旧的 POSIX 平台）→ `ns_platform_mutex_lock` 返回非 NS_OK → `ns_broker_remove_all_watchers` 直接 return。Broker 被 freed。Watcher 的 `broker_node` 成为悬空指针。用户线程 `ns_watcher_deinit` → `ns_broker_node_is_linked` 读到无效节点 → UAF 或无限循环。

#### 定位

---

<a id="BROKER-029"></a>
### BROKER-029: `ns_broker_drain_op_queue` 中 op_lock 锁失败时静默返回导致待处理 op 未被 signal `bug`

- **状态**: 关闭-已拒绝
- **严重度**: 🟠 高

#### 问题描述
第 386 行 `if(ns_platform_mutex_lock(broker->op_lock) != NS_OK) return;`——在 drain 循环中途，若 `op_lock` 加锁失败（EDEADLK、错误的中断等），函数直接返回，不解锁、不继续、不 error log。此时至少有一个 `req` 已经被从 op_queue 中取出（第 391-396 行）但尚未 signal `req->wakeup`——用户线程永久阻塞。

此问题与 BROKER-021（remove_all_watchers 锁失败）是同类型的锁失败静默返回问题。

#### review 建议
将锁失败变成硬失败（`assert` debug build，`abort` release build），或至少记录 fatal error 日志。因为锁失败表示严重的内部状态问题（死锁、销毁后使用），静默恢复是不可能的——任何不完整的 drain 都会导致至少一个用户线程永久挂起。

#### 作者建议
与 BROKER-021 同根因。三个平台上 `ns_platform_mutex_lock` 在正确使用下不会失败。锁失败意味着内部 bug 或系统 OOM，均不可恢复。静默返回已是可用的最轻处理。
→ 关闭-已拒绝

#### 关闭原因
与 BROKER-021 相同：合同前提保护。`ns_platform_mutex_lock` 在三个平台上均不会因正常操作失败。锁失败表示不可恢复的系统级错误。

- **关闭日期**: 2026-07-18
- **状态**: 关闭-已拒绝

#### 可重现的失败场景
Broker 线程在 drain 循环中，第 386 行 op_lock 加锁因 EDEADLK 失败。req 已被取出（第 391-396 行），但 `req->rc` 未设置，`req->wakeup` 未 signal。用户线程将永远等待。Broker 继续循环的下一次 `waitset_wait` 后静默工作（缺少上述 req 的处理不会使 broker 崩溃），但那个用户线程的栈和资源再也得不到释放。

#### 定位
src/ns_broker.c:386

---

<a id="BROKER-031"></a>
### BROKER-031: `ns_broker_run` 在 `waitset_wait` 失败后将未初始化的 `completions`/`count` 传入 dispatch 路径

- **状态**: 关闭-已拒绝
- **严重度**: 🟡 中
- **类型**: bug

#### 问题描述
`ns_broker_run`（`ns_broker.c:489`）中，当 `ns_platform_waitset_wait` 返回错误时，代码跳过 completion 处理块但仍调用 `ns_broker_dispatch_pending_events(broker, completions, count)`。虽然 `count` 被初始化为 `0u` 保护了本次不遍历，但 `completions` 数组仍包含上一轮遗留的旧数据。如果平台后端在错误路径上将 `count` 写为非零值但未填充 `completions`，dispatch 会解引用旧 completion 条目，导致重复 signal emit。

`ns_broker_drain_op_queue` 在错误后仍执行是正确的（保护 op_queue），但 `dispatch_pending_events` 在错误后执行是不必要的。

#### review 建议
将 `dispatch_pending_events` 调用放在 `rc == NS_OK` 的条件分支内，同时在错误路径上将 `count` 强制设为 `0u`：
```c
if(rc == NS_OK){
    // ... wakeup handling ...
} else {
    count = 0u;
}
ns_broker_drain_op_queue(broker, completions, count);
if(rc == NS_OK){
    ns_broker_dispatch_pending_events(broker, completions, count);
}
```

#### 作者建议
当前代码（`src/ns_broker.c:544-546`）已在错误路径上显式设置 `count = 0u`，`dispatch_pending_events` 以 `count=0` 调用时函数内部立即返回，无任何副作用。加 `if(rc == NS_OK)` 包裹是代码整洁层面的优化，不影响正确性。
→ 关闭-已拒绝

#### 关闭原因
`count = 0u` 已在错误路径上设置，`dispatch_pending_events` 在 `count=0` 时无操作，不存在"重复 signal emit"的风险。平台后端在错误路径上写 `count` 为非零值且不填充 `completions` 是平台层的 bug，不在 broker 的防御范围内。

- **关闭日期**: 2026-07-18
- **状态**: 关闭-已拒绝

#### 可重现的失败场景
1. 注册 watcher W1，W1 触发事件 → `waitset_wait` 成功，`completions[0]` 包含 W1。
2. 下一轮：`waitset_wait` 被信号中断返回 EINTR，`count` 保持 0u。
3. 若平台后端在错误路径上将 `count` 写为上一轮的值，`dispatch_pending_events` 读取旧 completion，W1 的 slot 被重复调用。

#### 定位
src/ns_broker.c:544-553

---

---
