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

### TIMER-001: 关闭时 initialized 标志与 mutex_destroy 的时序问题

- **修复 commit**: 当前 diff（`ns_timer_mgr_global_shutdown` 重排序）
- **修复说明**: `initialized = 0` 提到 mutex_lock 之前，阻断新入者；
  先 drain 再销毁。消除 use-after-free 窗口。
- **来源**: 深度代码审查 2.2

### TIMER-018: `ns_timer_cancel_locked` 返回 `NS_E_INVAL`，语义应是 `NS_E_CORRUPT` (HIGH) `bug`

- **修复 commit**: 当前 diff
- **修复说明**: `ns_timer_cancel_locked` (src/ns_timer.c:142) 中 `NS_E_INVAL` → `NS_E_CORRUPT`，与 `ns_timer_start_locked` 的 rbtree 错误返回风格一致。

---

## 现在打开的问题

### TIMER-019: `fire_expired` 和 `restart` 中 `ns_rbtree_insert` 返回值未检查，与 `start_locked` 不一致

- **状态**: 打开
- **严重度**: 🟢 较低
- **类型**: cleanup

#### 问题描述
`ns_timer_start_locked`（`src/ns_timer.c:125`）检查了 `ns_rbtree_insert` 返回值，但 `ns_timer_mgr_fire_expired`（第 296 行）和 `ns_timer_restart`（第 400 行）中的重新插入均未检查返回值。实际运行中不会失败（remove 后节点已重置），但三处不一致增加维护负担。

#### review 建议
统一三处错误处理风格：要么都检查，要么在 `start_locked` 处添加注释说明 NULL 检查是防御性编程。

#### 作者建议
（待作者补充）

#### 定位
`src/ns_timer.c:296`（fire_expired）、`src/ns_timer.c:400`（restart）；对比 `src/ns_timer.c:125`（start_locked）

---

### TIMER-020: `fire_expired` 文档中关于槽位回调死锁的描述不准确

- **状态**: 打开
- **严重度**: 🟢 较低
- **类型**: doc

#### 问题描述
`ns_timer_mgr_fire_expired` 上方的 `@note` 注释声称"槽位回调中不得调用任何 timer API，否则会死锁"。但 `ns_signal_emit_raw` 仅将调用推入 MPSC 队列（非阻塞），槽位回调在 loop 线程排空队列时执行，此时 `g_timer_mgr.mutex` 未被持有。调用 timer API 不会死锁。

#### review 建议
将注释改为准确描述：本函数在 `g_timer_mgr.mutex` 下调用 `ns_signal_emit_raw`（非阻塞推送）。槽位回调执行时 mutex 已释放，调用 timer API 不会死锁，但可能增加锁竞争。

#### 作者建议
（待作者补充）

#### 定位
`src/ns_timer.c:246-249`

---

## 现在关闭的问题

### TIMER-018: `ns_timer_cancel_locked` 返回 `NS_E_INVAL`，语义应是 `NS_E_CORRUPT` (HIGH) `bug`

- **状态**: 关闭-已修复
- **严重度**: 🟠 高
- **关闭原因**: `src/ns_timer.c:142` 中 `NS_E_INVAL` 改为 `NS_E_CORRUPT`，与 `ns_timer_start_locked` 风格一致。
- **关闭日期**: 2026-07-04

### TIMER-002: `ns_timer_validate_created` 无锁读取 `timer->signal.mutex`

- **状态**: 关闭-已修复
- **严重度**: 🔴 关键
- **关闭原因**: `nanosig_timer.h` 所有 public API 添加 `@attention` 说明
  `init`/`deinit`/`start`/`cancel`/`restart` 不是线程安全的，调用方需保证
  同一 timer 对象无并发操作。已文档化为设计约束。
- **关闭日期**: 2026-07-04

#### 定位

`src/ns_timer.c:85`

---

### TIMER-003: `ns_timer_mgr_notify` 中 `notify_ctx` 的数据竞争

- **状态**: 关闭-已拒绝
- **严重度**: 🟠 高
- **关闭原因**: 作者决定不修。`ns_timer_mgr_global_shutdown` 前调用方必须保证
  没有其他线程使用 timer。`ns_timer.c` 中 `g_timer_mgr` 和 `global_init` 上方的
  注释已文档化生命周期约束。
- **关闭日期**: 2026-07-04

---

### TIMER-004: `ns_timer_mgr_lock()` 中 `initialized` 检查与 `mutex_lock` 之间的 TOCTOU

- **状态**: 关闭-已拒绝
- **严重度**: 🟠 高
- **关闭原因**: 作者决定不修。生命周期约束由调用方保证 shutdown 时其他线程已停止。
  `ns_timer.c` 中 `g_timer_mgr` 上方注释已文档化。
- **关闭日期**: 2026-07-04

---

### TIMER-016: 所有 rbtree insert/remove 返回值被 `(void)` 静默丢弃

- **状态**: 关闭-已修复
- **严重度**: 🟠 高
- **关闭原因**: 7 处 `(void)` 强制转换全部移除：
  - `cancel_locked`：返回 `remove != NULL ? NS_OK : NS_E_INVAL`
  - `start_locked`：insert 返回 NULL 时返回 `NS_E_CORRUPT`
  - `shutdown` drain 循环：remove 返回 NULL 时 break
  - `fire_expired`/`restart`：放开返回值以供后续检查
- **关闭日期**: 2026-07-04

---

### TIMER-005: `ns_timer_deinit` 在 cancel 失败时泄漏信号互斥锁

- **状态**: 关闭-已拒绝
- **严重度**: 🟡 中
- **关闭原因**: 作者决定不修。cancel 失败意味着生命周期已混乱（如 shutdown 后调用），
  此时保留资源是安全行为。`nanosig_timer.h` 中 `ns_timer_deinit` 文档已说明
  cancel 失败时 signal 资源不清理。
- **关闭日期**: 2026-07-04

---

### TIMER-006: `ns_timer_restart` 在时钟验证前移除定时器

- **状态**: 关闭-已修复
- **严重度**: 🟡 中
- **关闭原因**: `nanosig_timer.h` 中 `ns_timer_restart` 文档已标注
  失败时 timer 从"运行中"变为"已停止"的状态变更。
- **关闭日期**: 2026-07-04

---

### TIMER-007: `fire_expired` 中缓存的 `now` 在信号发射后过期

- **状态**: 关闭-已修复
- **严重度**: 🟡 中
- **关闭原因**: `ns_timer.c` 中 `fire_expired` 上方注释说明 `ns_signal_emit_raw`
  是非阻塞 MPSC 队列推送，不会阻塞。槽位回调本身应保持简短。
- **关闭日期**: 2026-07-04

---

### TIMER-008: `ns_timer_mgr_global_init` 双重初始化竞态

- **状态**: 关闭-已修复
- **严重度**: 🟡 中
- **关闭原因**: `ns_timer.c` 中 `ns_timer_mgr_global_init` 上方添加
  `@note` 注释说明本函数不是线程安全的，调用方必须在单线程上下文中使用。
- **关闭日期**: 2026-07-04

---

### TIMER-009: Global shutdown 遗留未清理的定时器信号资源

- **状态**: 关闭-已修复
- **严重度**: 🟡 中
- **关闭原因**: `ns_timer.c` 中 `g_timer_mgr` 和 `ns_timer_mgr_global_init` 上方
  注释添加生命周期规则：调用方必须在 shutdown 前完成所有 timer 的 deinit。
- **关闭日期**: 2026-07-04

---

### TIMER-010: `ns_timer_remaining_us` 实现定义转换

- **状态**: 关闭-已拒绝
- **严重度**: 🟡 中
- **关闭原因**: 作者决定先不考虑。所有现代平台（补码）正确产生期望的负值。
- **关闭日期**: 2026-07-04

---

### TIMER-011: 信号发射在锁下进行，缺少文档约束

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低
- **关闭原因**: `ns_timer.c` 中 `fire_expired` 上方添加文档，说明 emit 在
  `g_timer_mgr.mutex` 下进行，槽位回调中不得调用 timer API，否则死锁。
- **关闭日期**: 2026-07-04

---

### TIMER-012: `ns_timer_cancel_locked` 始终返回 `NS_OK`

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低
- **关闭原因**: 改为 `return (ns_rbtree_remove(...) != NULL) ? NS_OK : NS_E_INVAL;`
- **关闭日期**: 2026-07-04

---

### TIMER-013: `initialized = 0` 在第 166 行和第 182 行各设置一次

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低
- **关闭原因**: 移除第 182 行冗余赋值（第 166 行已在 shutdown 入口处设置一次）。
- **关闭日期**: 2026-07-04

---

### TIMER-014: 注释"新入者"违反注释规范

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低
- **关闭原因**: "新入者" 改写为 "every new caller to ns_timer_mgr_lock() will see
  initialized == 0 and return NS_E_SHUTDOWN immediately"。
- **关闭日期**: 2026-07-04

---

### TIMER-015: `fire_expired` 仅记录第一个 emit 失败，后续失败静默丢弃

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低
- **关闭原因**: `ns_timer.c` 中 `fire_expired` 上方添加注释说明
  first-error-only 是有意取舍：批量触发场景下报告第一个错误，避免通过
  累积错误码增加 API 复杂度。
- **关闭日期**: 2026-07-04

---

### TIMER-017: 创建定时器后立即重启时的 notify 竞态（设计约束）

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低
- **关闭原因**: 作者指出此设计考量不成立——notify 更新的是当前树的最左节点，
  因此即使有另一个线程插入更早到期的定时器，本次 notify 获取到的也是正确的最小值。
- **关闭日期**: 2026-07-04
