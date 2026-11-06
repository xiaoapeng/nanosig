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
  - id: "TIMER-021"
    module: "timer"
    status: "closed-fixed"
    severity: "medium"
    type: "bug"
    section: "closed"
    closed_date: "2026-11-06"
    location: "docs/review/timer-code-review.md#TIMER-021"
    locations:
      - "src/ns_timer.c:50"
      - "src/ns_timer.c:131"
      - "src/ns_timer.c:153"
      - "src/ns_timer.c:323"
      - "src/ns_timer.c:337"
      - "src/ns_timer.c:357"
  - id: "TIMER-022"
    module: "timer"
    status: "open"
    severity: "low"
    type: "cleanup"
    section: "open"
    location: "docs/review/timer-code-review.md#TIMER-022"
    locations:
      - "src/ns_timer.c:141"
      - "src/ns_timer.c:440"
      - "src/ns_timer.c:444"
      - "include/nanosig/nanosig_timer.h:117"
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

- **TIMER-021（2026-11-06）**: init 和 start/restart 共用校验拒绝零值及大于 INT64_MAX 的间隔；补充公共范围约定和边界回归测试。 — commit `fix/timer-021-interval-bound`

## 现在打开的问题

<a id="TIMER-022"></a>
### TIMER-022: restart 重复已有 start_locked 的重新装载逻辑

- **状态**: 打开
- **严重度**: 🟢 较低（前提：按当前仓库实现与公开契约静态走查；仅维护性重复，未发现由重复本身导致的功能故障）
- **首次发现日期**: 2026-11-06
- **类型**: cleanup

#### 问题描述

restart 移除旧节点后，又手写一次读时钟、计算截止时间、插入树和判断最早节点。ns_timer_start_locked 已封装这一流程，可直接复用。

#### review 建议

保留 restart 的入口校验、加锁和旧节点删除，将后续 refresh/add/became_first 块替换为 ns_timer_start_locked(timer, &became_first)。保留原来的解锁和 notify 条件；不要调用公开 ns_timer_start，以免重复加锁。

#### 作者建议

（待作者补充）

#### 可重现的失败场景

分别对未启动 timer 和已启动 timer 调用 restart。后一种状态先由 ns_rbtree_del 重置节点，随后两种状态都满足 start_locked 的未运行前提；当前重复执行同一装载流程。无功能故障主张。

#### 影响面

公开 ns_timer_restart 及调用者；ns_timer_start 继续使用现有 helper。下级 clock、rbtree_add 及 notify 不变。test/unit/test_timer.c:85、:327、:389、:479 覆盖启动/重启/最早节点行为；本轮未运行。

#### 文档仲裁与历史去重

TIMER-019 拒绝的是返回值未检查的错误诊断；本项不改变返回值处理或 fire_expired 的独立批量语义。TIMER-006 的先删除再读时钟失败后停止约定也保持不变。

#### 定位
src/ns_timer.c:141
src/ns_timer.c:440
src/ns_timer.c:444
include/nanosig/nanosig_timer.h:117

---

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

<a id="TIMER-021"></a>
### TIMER-021: interval 超出有符号差值半区间后被接受并立即到期

- **状态**: 关闭-已修复
- **严重度**: 🟡 中（前提：interval 至少为 2^63 微秒，实际时长极大；公共 uint64_t 参数未设此上界；固定正常时钟的裸机 harness 实测）
- **首次发现日期**: 2026-11-04
- **类型**: bug
- **关闭原因**: 修复 commit fix/timer-021-interval-bound
- **关闭日期**: 2026-11-06

#### 问题描述

超过差值算法有效范围的 interval 会立即到期；REPEAT timer 使 fire_expired 一直重插已到期节点，阻塞 timer 服务和裸机 poll。init/start/restart 只排除零值，而剩余时间把无符号差转为 int64_t，最高位为 1 的正常参数便被当作负数。

#### review 建议

公开并校验 0 < interval <= INT64_MAX。init 与 start/restart 的公共校验都覆盖该条件，避免公开 interval 字段修改绕过；保留既有补码差值算法。

#### 作者建议

作者于 2026-11-06 确认：限制 0 < interval_us <= INT64_MAX；在 init 及 start/restart 共用校验中拒绝越界值，保留现有差值算法，补充公共文档和边界回归测试。

#### 可重现的失败场景

固定 now=100；ns_timer_init(&timer,UINT64_C(0x8000000000000000),0) 和 ns_timer_start 都返回 0；next_timeout 返回 0，fire_expired 返回 0 后 timer 已被移除（next_timeout=-10）。属性改为 NS_TIMER_ATTR_REPEAT 时同样 init/start 成功，但 fire_expired 不返回；子进程实测在 2 秒超时后终止。时钟本身不在 UINT64_MAX 附近。

#### 影响面

ns_timer_remaining_us 被树比较器、next_timeout、fire_expired、重复装载使用；影响 timer init/start/restart、broker 定时器服务及裸机 poll/next_timeout。共享 start 校验必须覆盖公开 interval 的后续修改。现有 timer/裸机测试未覆盖 2^63 边界。

#### 文档仲裁与历史去重

TIMER-010 拒绝的是 unsigned 转 signed 的实现定义可移植性，理由为现代补码正确。本项在现代补码上确定复现，根因是接受超出算法半区间的参数，不要求推翻该历史决定。公共 timer 头没有 INT64_MAX 上界，亦无相同历史条目。

#### 定位
src/ns_timer.c:50
src/ns_timer.c:131
src/ns_timer.c:153
src/ns_timer.c:323
src/ns_timer.c:337
src/ns_timer.c:357

---
