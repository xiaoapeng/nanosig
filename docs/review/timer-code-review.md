---
review_schema: "nanosig-review-v1"
entries:
  - id: "TIMER-001"
    module: "timer"
    status: "closed-fixed"
    severity: "unknown"
    type: "bug"
    section: "fixed-history"
    location: "docs/review/timer-code-review.md#TIMER-001"
  - id: "TIMER-018"
    module: "timer"
    status: "closed-fixed"
    severity: "unknown"
    type: "bug"
    section: "fixed-history"
    location: "docs/review/timer-code-review.md#TIMER-018"
  - id: "TIMER-019"
    module: "timer"
    status: "closed-rejected"
    severity: "low"
    type: "cleanup"
    section: "closed"
    closed_date: "2026-10-19"
    location: "docs/review/timer-code-review.md#TIMER-019"
  - id: "TIMER-020"
    module: "timer"
    status: "closed-fixed"
    severity: "low"
    type: "doc"
    section: "closed"
    closed_date: "2026-10-19"
    location: "docs/review/timer-code-review.md#TIMER-020"
  - id: "TIMER-003"
    module: "timer"
    status: "closed-rejected"
    severity: "high"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-04"
    location: "docs/review/timer-code-review.md#TIMER-003"
  - id: "TIMER-004"
    module: "timer"
    status: "closed-rejected"
    severity: "high"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-04"
    location: "docs/review/timer-code-review.md#TIMER-004"
  - id: "TIMER-005"
    module: "timer"
    status: "closed-rejected"
    severity: "medium"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-04"
    location: "docs/review/timer-code-review.md#TIMER-005"
  - id: "TIMER-010"
    module: "timer"
    status: "closed-rejected"
    severity: "medium"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-04"
    location: "docs/review/timer-code-review.md#TIMER-010"
  - id: "TIMER-017"
    module: "timer"
    status: "closed-rejected"
    severity: "low"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-04"
    location: "docs/review/timer-code-review.md#TIMER-017"
---

# Timer 代码审查报告

**日期**: 2026-07-04
**审查者**: claude-code-review

---

## 修复历史

### 2026-07-04 — 第二轮修复（文档同步 + 代码改进）

- **TIMER-012**: `cancel_locked` 传递 `ns_rbtree_remove` 返回值替代 `(void)` + 硬编码 `NS_OK`
- **TIMER-013**: 删除 `ns_timer_mgr_global_shutdown` 末尾冗余的 `g_timer_mgr.initialized = 0`
- **TIMER-014**: "新入者" 改写为英文可观测行为描述
- **TIMER-016**: 7 处 `(void)ns_rbtree_insert/remove` 全部移除 void 转换：
  - `start_locked` 检查 insert 返回并返回 CORRUPT
  - `shutdown` drain 中 remove 失败 break 终止循环
  - `cancel_locked` 已由 TIMER-012 覆盖
  - 其余 site 保留返回值可供检查
- **文档**: `nanosig_timer.h` 所有 public API 添加 `@attention` 线程安全约束；
  `ns_timer.c` 中 `g_timer_mgr`、`global_init`、`fire_expired` 上方添加生命周期约束、
  锁持有说明、first_error 有意取舍注释

<a id="TIMER-001"></a>
### TIMER-001: 关闭时 initialized 标志与 mutex_destroy 的时序问题

- **修复 commit**: 当前 diff（`ns_timer_mgr_global_shutdown` 重排序）
- **修复说明**: `initialized = 0` 提到 mutex_lock 之前，阻断新入者；
  先 drain 再销毁。消除 use-after-free 窗口。
- **来源**: 深度代码审查 2.2

<a id="TIMER-018"></a>
### TIMER-018: `ns_timer_cancel_locked` 返回 `NS_E_INVAL`，语义应是 `NS_E_CORRUPT` (HIGH) `bug`

- **修复 commit**: 当前 diff
- **修复说明**: `ns_timer_cancel_locked` (src/ns_timer.c:142) 中 `NS_E_INVAL` → `NS_E_CORRUPT`，与 `ns_timer_start_locked` 的 rbtree 错误返回风格一致。

---

## 现在打开的问题


## 现在关闭的问题

<a id="TIMER-020"></a>
### TIMER-020: `fire_expired` 文档中关于槽位回调死锁的描述不准确

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低
- **类型**: doc
- **关闭原因**: 已在 `src/ns_timer.c:255-269` 的 `@note` 注释中修正准确描述：槽位回调执行时 `g_timer_mgr.mutex` 已释放，调用 timer API 不会死锁，但可能增加锁竞争。
- **关闭日期**: 2026-10-19

---
<a id="TIMER-019"></a>
### TIMER-019: `fire_expired` 和 `restart` 中 `ns_rbtree_insert` 返回值未检查，与 `start_locked` 不一致

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低
- **类型**: cleanup
- **关闭原因**: 审核确认 `restart` 已检查返回值，`fire_expired` 有注释说明设计意图（批量触发场景不需要 leftmost 信息）。三处语义各不相同，非一致性 bug，属调用方责任范围。
- **关闭日期**: 2026-10-19

- **状态**: 关闭-已修复
- **严重度**: 🟠 高
- **关闭原因**: `src/ns_timer.c:142` 中 `NS_E_INVAL` 改为 `NS_E_CORRUPT`，与 `ns_timer_start_locked` 风格一致。
- **关闭日期**: 2026-07-04

<a id="TIMER-003"></a>
### TIMER-003: `ns_timer_mgr_notify` 中 `notify_ctx` 的数据竞争

- **状态**: 关闭-已拒绝
- **严重度**: 🟠 高
- **关闭原因**: 作者决定不修。`ns_timer_mgr_global_shutdown` 前调用方必须保证
  没有其他线程使用 timer。`ns_timer.c` 中 `g_timer_mgr` 和 `global_init` 上方的
  注释已文档化生命周期约束。
- **关闭日期**: 2026-07-04

---

<a id="TIMER-004"></a>
### TIMER-004: `ns_timer_mgr_lock()` 中 `initialized` 检查与 `mutex_lock` 之间的 TOCTOU

- **状态**: 关闭-已拒绝
- **严重度**: 🟠 高
- **关闭原因**: 作者决定不修。生命周期约束由调用方保证 shutdown 时其他线程已停止。
  `ns_timer.c` 中 `g_timer_mgr` 上方注释已文档化。
- **关闭日期**: 2026-07-04

---

<a id="TIMER-005"></a>
### TIMER-005: `ns_timer_deinit` 在 cancel 失败时泄漏信号互斥锁

- **状态**: 关闭-已拒绝
- **严重度**: 🟡 中
- **关闭原因**: 作者决定不修。cancel 失败意味着生命周期已混乱（如 shutdown 后调用），
  此时保留资源是安全行为。`nanosig_timer.h` 中 `ns_timer_deinit` 文档已说明
  cancel 失败时 signal 资源不清理。
- **关闭日期**: 2026-07-04

---

<a id="TIMER-010"></a>
### TIMER-010: `ns_timer_remaining_us` 实现定义转换

- **状态**: 关闭-已拒绝
- **严重度**: 🟡 中
- **关闭原因**: 作者决定先不考虑。所有现代平台（补码）正确产生期望的负值。
- **关闭日期**: 2026-07-04

---

<a id="TIMER-017"></a>
### TIMER-017: 创建定时器后立即重启时的 notify 竞态（设计约束）

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低
- **关闭原因**: 作者指出此设计考量不成立——notify 更新的是当前树的最左节点，
  因此即使有另一个线程插入更早到期的定时器，本次 notify 获取到的也是正确的最小值。
- **关闭日期**: 2026-07-04
