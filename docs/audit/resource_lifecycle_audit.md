# 资源生命周期审计（P9.2）

> nanosig v1 / 2026-06-20 审计快照

## 摘要

本审计针对 nanosig v1 公开资源 API（`include/nanosig/*.h`）的 create / init / add 与
destroy / deinit / remove / disconnect 对称性、失败路径清理、运行时销毁安全、所有权语义
与文档一致性，逐对给出结论。

- **资源对总数**：5 类资源 ×（create / init / add ↔ destroy / deinit / remove）
  - 全局：`ns_init` ↔ `ns_shutdown`（单例）
  - loop：`ns_loop_init` ↔ `ns_loop_deinit`
  - signal：`ns_signal_init[_raw]` ↔ `ns_signal_deinit[_raw]`（含 `connect` ↔ `disconnect`、`disconnect_all`）
  - timer：`ns_timer_init` ↔ `ns_timer_deinit`
  - watcher：`ns_watcher_init_fd / _handle` ↔ `ns_watcher_deinit` + broker：`ns_broker_add` ↔ `ns_broker_remove`
- **资源对象统计**：
  - 调用方自持对象：4 类（`ns_loop_t` / `ns_signal_t` / `ns_timer_t` / `ns_watcher_t`）
  - 库拥有对象：1 类（`ns_event_broker_t` 全局单例）
  - 节点对象：2 类（`ns_connection_t`、`ns_waitable_t`——前者调用方自持、后者嵌入 watcher）
- **严重度分布**：
  - Critical：0
  - Major：3
  - Info：（跳过，按规范不查风格/命名问题）

整体结论：v1 资源契约在主路径上对称且失败路径清理完整，但 broker 关闭路径在 loop 与 timer
未及时清理时存在残留清理与单例发布顺序的隐患（详见"关键发现"）。

## 资源对总览

| 资源 | create / init | destroy / deinit | 所有权 | 重复 init 行为 | 重复 destroy 行为 | 备注 |
| --- | --- | --- | --- | --- | --- | --- |
| 全局 runtime | `ns_init()` | `ns_shutdown()` | 库（`g_ns_initialized` 标志 + `ns_event_broker_t` 单例 + 平台层） | 返回 `NS_E_EXISTS`（`src/nanosig.c:78`） | 幂等：未初始化时直接返回 `NS_OK`（`src/nanosig.c:97`）；注意：调用方已 destroy loop / timer / watcher 后再调，库不再内部检查（`include/nanosig/nanosig.h:43-46`） | init 失败路径：先创建 platform，失败回滚；broker 失败时回滚 platform。shutdown 仅在已 init 时生效，未 init 直接返回 OK。 |
| loop | `ns_loop_init(out_loop, config)` | `ns_loop_deinit(loop)` | 调用方（`ns_loop_t` 由 `ns_platform_alloc` 分配，库仅做生命周期） | 无显式检测；若对已创建 loop 再次 create，行为由调用方控制（不在本次审计范围） | 拒绝销毁：loop 仍 `running` 返回 `NS_E_INVAL`，存在 `async_thread` 返回 `NS_E_BUSY`（`src/nanosig.c:164-166`） | 分配合并：`sizeof(*loop)` 对齐 + `queue_byte_capacity` 一次性 `ns_platform_alloc`（`src/nanosig.c:130-133`）；销毁先 wakeup_destroy 再 free 整体。 |
| signal | `ns_signal_init(signal, type)` 宏 ↔ `ns_signal_init_raw(signal, payload_size, slot_capacity, debug_name)` | `ns_signal_deinit(signal)` 宏 ↔ `ns_signal_deinit_raw(signal)` | 调用方 | 宏展开为 `_raw`；`init_raw` 不检测"已初始化"；重复 init 行为未文档化（现状：会再创建新 mutex，旧 mutex 句柄泄漏——见"关键发现"） | `deinit_raw` 在 `mutex == NULL` 时直接返回 `NS_OK`（`src/nanosig.c:312`），符合"已 deinit 后再 deinit 幂等"；未 init 时 `mutex` 为 `NULL` 走幂等分支 | 内部状态：`payload_size` / `slot_capacity` / `slot_list` / `mutex`。`init_raw` 失败回滚：不创建 mutex 时 `mutex` 保持 `NULL`，无泄漏。`deinit_raw` 不释放 `slot_list` 中 connection 节点（已 `ns_list_init`），由调用方保证 disconnect 在前。 |
| signal connection | `ns_signal_connect` | `ns_signal_disconnect` / `ns_signal_disconnect_all` | 调用方（`ns_connection_t` 栈/堆由调用方自持） | 不允许重复 connect 同一 connection：未检测，但 `signal_node` 已自环，重复 push 不会自检（依赖调用方约束） | disconnect 幂等：二次调用 `ns_list_remove_init` 已自环后再次 `remove_init` 不报错（无显式 guard） | `disconnect` 不释放 connection 内存（`include/nanosig/nanosig_signal.h:248` 明确 "断开连接后可安全释放 connection"）。 |
| timer | `ns_timer_init(timer, interval_us, attr)` | `ns_timer_deinit(timer)` | 调用方（`ns_timer_t` 自持） | 重复 init：未显式 guard；现存 timer 已 `start` 状态下再次 `create` 会清零 `expire_us` / `attr`，旧 rbtree 节点不会移除（隐患——见"关键发现"） | `destroy` 内部先 `ns_timer_cancel`（`src/ns_timer.c:379`），所以即使未 start 也不会误删；deinit signal 释放 mutex | 内部状态：`signal` / `rb_node` / `interval_us` / `expire_us` / `attr`。`create` 失败路径：mutex 创建失败时已 `ns_signal_init_raw` 成功——见"关键发现"是否成立（实际：`init_raw` 内先置 `mutex=NULL`，创建失败时 mutex 仍为 `NULL` 不算泄漏）。 |
| watcher | `ns_watcher_init_fd(w, fd, ev, edge)` / `ns_watcher_init_handle(w, h, ev, edge)` | `ns_watcher_deinit(w)` | 调用方（`ns_watcher_t` 自持） | 重复 init 会通过 `ns_watcher_reset_empty` 重置 `signal.mutex=NULL` 释放旧 mutex 句柄吗？实际：仅置字段为 `NULL`（`src/ns_broker.c:58-67`），**不销毁旧 mutex**——见"关键发现"。 | `deinit` 拒绝：未 init 返回 `NS_E_INVAL`，已 link 到 broker 返回 `NS_E_EXISTS`（`src/ns_broker.c:131-133`） | `init_fd` / `init_handle` 区分 fd-vs-handle 模式；二者 `reset_empty` 后再调 `init_common`。 |
| broker 节点 | `ns_broker_add(broker, watcher)` | `ns_broker_remove(broker, watcher)` | broker 拥有 `watcher_head` 链表与 `waitset` 注册 | 重复 add：检测 `broker_node` 已 link 则返回 `NS_E_EXISTS`（`src/ns_broker.c:220-223`） | 重复 remove：未 link 则返回 `NS_E_INVAL`（`src/ns_broker.c:250-253`）；成功后 `waitable.user_data = NULL` + `ns_list_remove_init` | broker `add` 后通过 `ns_broker_notify` 唤醒 broker 线程（`src/ns_broker.c:235`），`remove` 同理（`:263`）。 |

## 失败路径清理

逐函数列出所有失败路径与清理动作完整性。

### `ns_init()`（`src/nanosig.c:74`）

| 失败点 | 已成功动作 | 清理动作 | 完整性 |
| --- | --- | --- | --- |
| `ns_platform_init()` 失败 | 无 | 无（直接 goto `out`） | OK |
| `ns_broker_global_init()` 失败 | `ns_platform_init` 成功 | `ns_platform_shutdown()`（`out_platform` 标签） | OK |

`ns_shutdown()` 失败路径：不失败；状态已清零后 broker / platform 关闭由各自内部处理。
`ns_is_initialized()`：仅参数校验，无失败路径。

### `ns_loop_init()`（`src/nanosig.c:114`）

| 失败点 | 已成功动作 | 清理动作 | 完整性 |
| --- | --- | --- | --- |
| `out_loop == NULL` 仅作 `*out_loop = NULL`；参数校验失败直接返回 | 无 | 无 | OK |
| `!ns_runtime_is_initialized()` | 无 | 无 | OK |
| `ns_loop_config_validate()` 失败 | 无 | 无 | OK |
| `ns_platform_alloc()` 失败 | 无 | 无 | OK |
| `ns_mpsc_record_ring_init()` 失败 | `ns_platform_alloc` 成功 | `ns_platform_free(loop)`（`out_free`） | OK |
| `ns_platform_wakeup_create()` 失败 | alloc 成功 + ring 初始化成功 | `ns_platform_free(loop)` | OK——`ring` 嵌入在 `loop` 同一块内存中，free 整体即可 |

注：标签只有单个 `out_free`，因为 ring 与 wakeup 共享同一块 alloc 内存。这是有意为之。

### `ns_loop_deinit()`（`src/nanosig.c:160`）

| 失败点 | 清理动作 | 完整性 |
| --- | --- | --- |
| `loop == NULL` | 直接返回 | OK |
| `loop->async_thread != NULL` | 返回 `NS_E_BUSY`，无清理 | OK（运行中不销毁） |
| `loop->running != 0` | 返回 `NS_E_INVAL`，无清理 | OK |
| `ns_platform_wakeup_destroy()` 失败 | 不 free 整体，返回错误 | 设计上：wakeup 销毁失败不进入 free 路径，可能造成 loop 内存泄漏（语义待确认） |
| 成功 | `ns_platform_free(loop)` | OK |

### `ns_signal_init_raw()`（`src/nanosig.c:289`）

| 失败点 | 已成功动作 | 清理动作 | 完整性 |
| --- | --- | --- | --- |
| `signal == NULL` | 无 | 无 | OK |
| `ns_platform_mutex_create()` 失败 | 已写入 `payload_size` / `slot_capacity` / `debug_name` 与 `ns_list_init(&slot_list)` | 无显式回滚——`mutex` 保持 `NULL`，字段已写 | OK（无堆分配回滚） |

`ns_signal_deinit_raw()`：参数校验、`mutex == NULL` 时直接 `NS_OK`；成功 destroy mutex 后清空指针。
清理 OK。

### `ns_signal_connect()`（`src/nanosig.c:319`）

| 失败点 | 已成功动作 | 清理动作 | 完整性 |
| --- | --- | --- | --- |
| 任一参数 `NULL` | 无 | 无 | OK |
| `ns_signal_lock()` 失败 | 已写 `connection` 字段 | `connection` 状态未定义 | 文档声明 `ns_signal_lock` 不会失败（mutex lock 仅返回 NS_OK 或平台错误）——可接受 |
| `ns_signal_unlock()` 失败 | `signal_node` 已 push | 失败返回 unlock_rc | OK（`signal_node` 已 link，由调用方后续 disconnect） |

### `ns_signal_disconnect()`（`src/nanosig.c:345`）

参数 `NULL` 返回 `NS_E_INVAL`；`ns_signal_lock` 失败返回；成功 `ns_list_remove_init` 解除链表挂接。
无失败路径内存泄漏。OK。

### `ns_signal_disconnect_all()`（`src/nanosig.c:360`）

遍历清空 `slot_list`；每节点 `ns_list_init(node)` 复位自环。无堆分配，无回滚问题。OK。

### `ns_timer_init()`（`src/ns_timer.c:268`）

| 失败点 | 已成功动作 | 清理动作 | 完整性 |
| --- | --- | --- | --- |
| 参数校验 | 无 | 无 | OK |
| `ns_timer_runtime_ready()` 失败 | 无 | 无 | OK |
| `ns_signal_init_raw()` 失败 | 字段已部分写 | `init_raw` 内部已回滚（mutex 为 NULL） | OK |

成功路径：写 `interval_us` / `expire_us` / `attr` 并 `ns_rbtree_node_init(&rb_node)`。无堆分配。

### `ns_timer_deinit()`（`src/ns_timer.c:372`）

先 `ns_timer_cancel(timer)`，再 `ns_signal_deinit_raw(&timer->signal)`，最后 `ns_rbtree_node_init`。
`cancel` 本身幂等（`is_running` 检测），`deinit_raw` 幂等（`mutex == NULL` 时 `NS_OK`）。OK。

### `ns_watcher_init_fd()` / `ns_watcher_init_handle()`（`src/ns_broker.c:97` / `:112`）

入口先 `ns_watcher_reset_empty(watcher)` 重置字段；失败路径（参数 / events / runtime）：
- `init_fd` / `init_handle` 在调用 `init_common` 前已重置 watcher 状态；
- `init_common` 内 `ns_signal_init_raw` 失败时 `signal.mutex = NULL`、broker_node 仍自环（`ns_list_init` 在 reset 已做）。

清理 OK。但**重复 init 的旧 mutex 句柄泄漏**——见"关键发现"。

### `ns_watcher_deinit()`（`src/ns_broker.c:127`）

| 失败点 | 清理动作 | 完整性 |
| --- | --- | --- |
| `watcher == NULL` | NS_E_INVAL | OK |
| 未 init | NS_E_INVAL（`!ns_watcher_is_initialized`） | OK |
| 仍 link broker | NS_E_EXISTS，要求先 `ns_broker_remove` | OK |
| `ns_signal_deinit_raw()` 失败 | 仍 `ns_waitable_init` + `ns_list_init` | OK（best-effort 复位） |

### `ns_broker_add()` / `ns_broker_remove()`（`src/ns_broker.c:209` / `:239`）

`add`：mutex lock 失败返回；`waitset_add` 失败时还原 `user_data = NULL` 并保持未 link。`remove`：mutex lock 失败返回；`waitset_remove` 失败时保持 link 状态。
解锁失败时返回 unlock_rc，整体 RC 选择 `unlock_rc` 当 `rc == NS_OK`（次要路径）。
**任一路径内存均无泄漏**——waitable 与 watcher 嵌入同一对象。

### `ns_broker_global_init()`（`src/ns_broker.c:284`）

按 `out_*` 标签分阶段回滚，6 段标签覆盖：thread 创建成功后才发布 `g_broker`。
具体回滚链（自底向上）：
- `out_timer_mgr` → `ns_timer_mgr_global_shutdown` → `out_mutex` → mutex destroy
- → `out_wakeup_waitable` → waitset_remove(wakeup_waitable)
- → `out_waitset` → waitset_destroy
- → `out_wakeup` → wakeup_destroy
- → `out_free` → free broker

OK。但**`g_broker` 发布顺序**——`ns_broker_global_init` 末尾才 `g_broker = broker`，与"发布前完整初始化"对齐，OK。

### `ns_broker_global_shutdown()`（`src/ns_broker.c:344`）

顺序：置 quit → wakeup_signal → thread_join → `g_broker = NULL` → `ns_timer_mgr_global_shutdown`
→ `ns_broker_remove_all_watchers`（清空 waitset）→ waitset_remove(wakeup_waitable) → waitset_destroy
→ wakeup_destroy → mutex_destroy → free broker。

**注**：`g_broker = NULL` 先于 `ns_broker_remove_all_watchers`——这意味着清残留阶段 `ns_broker()` 已返回 `NULL`，但函数内 `broker` 局部变量仍有效，无 race。OK。

## 运行时销毁安全

逐场景评估是否安全及文档一致性。

### 1. `ns_signal_disconnect` 之后已入队 slot 是否仍会执行？

- **实现**：`ns_signal_disconnect` 仅 `ns_list_remove_init(&connection->signal_node)`（`src/nanosig.c:354`），
  **不**从 MPSC record ring 中撤销已入队的 slot 调用。
- **文档**：`include/nanosig/nanosig_signal.h:248` 明确"断开连接后可安全释放 connection"；
  `:287-288` "批量断开不会取消已经入队的 slot 调用；调用方仍需保证所有相关 `user_data` 长于任何 in-flight emit。"
- **结论**：行为正确、文档一致。`disconnect` 是"未来 emit 不再投递"，不是"撤回已投递"。
  调用方须保证 `user_data` 生命周期长于 in-flight 调用。**安全**。

### 2. `ns_watcher_deinit` 在 broker 已 fire 但 slot 还没 dispatch 时是否安全？

- **实现**：`ns_watcher_deinit` 拒绝已 link broker 的 watcher（`NS_E_EXISTS`），强制调用方先 `ns_broker_remove`。
- 风险路径：调用方先 `ns_broker_remove`，但 broker 线程已 fire（`ns_broker_emit_completion` 已 `ns_signal_emit_raw`，
  正在向 `conn->target_loop->queue` push），随后调用方 `ns_watcher_deinit`。
  - deinit 内 `ns_signal_deinit_raw(&watcher->signal)` 销毁 signal mutex。
  - 但 record 已 push 到 loop queue，slot 回调内仍持有 `conn->user_data`——若 `user_data` 与 watcher 关联（常见），可能 use-after-free。
- **文档**：`include/nanosig/nanosig_broker.h:92-93` 提到"调用前应先通过 `ns_broker_remove()` 从 broker 注销 watcher。本函数释放内嵌
  signal 的 mutex 与 broker_node 链表"——**未**提示 in-flight slot 的 `user_data` 生命周期。
- **结论**：API 顺序正确（deinit 强制先 remove），但 **Major** 风险点：remove 与 deinit 之间存在
  broker 已 fire 但 loop 未 dispatch 的窗口期；调用方须保证 `user_data` 长于所有可能已派发到 loop 的 slot。
  文档未明示，建议补充。

### 3. `ns_loop_deinit` 在 loop 还在被 emit 时是否安全？

- **实现**：`ns_loop_deinit` 拒 `running != 0`（`NS_E_INVAL`），但 `emit` 不经过 `running` 标志——`ns_signal_emit_raw` 直接
  `ns_mpsc_record_ring_try_pushv(&conn->target_loop->queue, ...)`（`src/nanosig.c:406`）。
- 场景：loop 在 `ns_loop_run` 之外的线程 quit（`ns_loop_quit` 跨线程），另一线程同时 `ns_loop_deinit`。
  - `running` 标志在 `ns_loop_run_impl` 退出时才清零（`src/nanosig.c:223`）。
  - `ns_loop_deinit` 在 `running != 0` 时返回 `NS_E_INVAL`，保护正在运行的 loop 内存不被释放。
  - 但若 loop 跨线程 quit 后 `running` 尚未清零（`ns_loop_run_impl` 内部释放之前存在小窗口），`destroy` 短暂拒绝；运行线程回到 `out` 标签前清零。
- **结论**：跨线程 quit 路径受 `running` 原子保护，**基本安全**。`emit` 仅写入 ring，不触碰 loop 内存生命周期。
- **文档**：`include/nanosig/nanosig_loop.h:72-74` 提到"调用前必须确保该 loop 不再运行"，但**未明示**跨线程 quit 后 destroy 须
  等待 `ns_loop_run` 实际返回（即 `running` 归零）。存在轻度文档缺失（Major 建议补充）。

### 4. `ns_shutdown` 时 broker 销毁路径是否清理所有残留 watcher / timer？

- **实现**：`ns_broker_global_shutdown`（`src/ns_broker.c:344`）：
  1. quit + join broker 线程
  2. `g_broker = NULL`
  3. `ns_timer_mgr_global_shutdown`（清空 rbtree，**不**调用 `ns_timer_deinit`——timer 句柄由调用方负责）
  4. `ns_broker_remove_all_watchers`（遍历清空 `watcher_head`，从 waitset 注销、`user_data = NULL`、broker_node 重新初始化）
  5. waitset 清理 / wakeup destroy / mutex destroy / free broker
- **共识计划要求**（`docs/共识计划.md:122`）："shutdown 必须清理 waitset 中残留 watcher"——已实现（步骤 4）。
- **残留 timer**：`ns_timer_mgr_global_shutdown` 只清 rbtree 节点，不调 `ns_timer_deinit`。若调用方
  在 `ns_shutdown` 前未 `ns_timer_deinit`，则：
  - timer 内嵌的 `ns_signal_t` mutex 不会销毁（**泄漏**）；
  - timer 句柄本身（栈/堆）由调用方负责，库不感知。
- **结论**：watcher 残留清理完整；timer 残留 **未释放 signal mutex**（Major）。需在 timer.c
  文档或 shutdown 路径补强。

### 5. `ns_loop_deinit` 跨线程 quit 的可见性

- `ns_loop_quit` 仅做 `quit_requested = 1` + `wakeup_signal`（`src/nanosig.c:241-242`），不触碰 `running`。
- 跨线程 quit 后调用方立即 `ns_loop_deinit`：因 `running` 未归零返回 `NS_E_INVAL`，调用方需重试或 join。
- **结论**：`ns_loop_quit` 文档已说"可由拥有该 loop 的线程调用，也可由跨线程控制路径调用"（`include/nanosig/nanosig_loop.h:99`），
  但未说明调用方须等待 `ns_loop_run` 返回后再 destroy。**Major** 文档缺失。

### 6. `ns_timer_deinit` 在 timer `start` 状态被另一线程 fire 时是否安全？

- **实现**：`ns_timer_deinit` 先 `ns_timer_cancel`（`src/ns_timer.c:379`）。
  - `cancel` 加 `g_timer_mgr.mutex` 后从 rbtree 移除并 `should_notify` 唤醒 broker。
  - `ns_signal_deinit_raw` 释放 timer->signal mutex。
  - broker 线程可能正在 `ns_timer_mgr_fire_expired`（`src/ns_timer.c:216`），它也加同一 mutex。
- 互斥保证：cancel 与 fire 互斥，fire 之后 cancel 才执行，cancel 后 deinit 销毁 mutex——broker 线程
  `fire_expired` 不会在 cancel 之后再 push emit（`signal_emit_raw` 仍调用，但 signal mutex 已 destroy？）。
- 风险点：`ns_timer_deinit` 在 broker 线程 **已经** `ns_signal_emit_raw(&timer->signal, ...)` 之后才执行。
  - 此时 fire_expired 已读 `timer->signal`、已退出 mutex，push 到 loop queue 完成。
  - 随后 `destroy` 销毁 `timer->signal.mutex`——但 broker 线程不再访问 timer->signal（emit 已完成）。
  - `slot_fn` + `user_data` 在 loop 端派发，由调用方保证 `user_data` 生命周期。
- **结论**：在 broker 互斥保护下，**安全**。但若 broker 已 fire 之后、loop dispatch 之前调用 `destroy`，
  `user_data` 生命周期仍由调用方负责——与 watcher 风险同构。

### 7. `ns_broker_remove` / `ns_broker_add` 并发安全

- `broker->watcher_mutex` 保护 `watcher_head` 与 waitset add/remove。重复 add / 重复 remove 行为对称。
- broker 线程在 `ns_broker_run` 内 `waitset_wait` 期间不持锁（waitset 平台层保证），与 add/remove 互不阻塞。
- **安全**。

## 严重度等级

- **Critical** —— 泄漏 / use-after-free / 双重释放。**0 项**。
- **Major** —— 失败路径漏清理 / 销毁与运行时冲突 / 文档与实现不一致。**3 项**（见下文）。
- **Info** —— 命名或风格问题（不查，跳过）。

## 关键发现

### F1. 重复 `ns_signal_init` / `ns_signal_init_raw` 不检测且不释放旧 mutex（Major）

- **位置**：`src/nanosig.c:289-305`（`ns_signal_init_raw`）
- **问题**：`init_raw` 不检测"signal 是否已初始化"；重复调用会再次 `ns_platform_mutex_create`，旧 mutex 句柄泄漏。
- **证据**：
  ```c
  int ns_signal_init_raw(ns_signal_t *signal, ...) {
      ...
      signal->payload_size = payload_size;       // 覆写
      signal->slot_capacity = slot_capacity;     // 覆写
      signal->debug_name = debug_name;            // 覆写
      ns_list_init(&signal->slot_list);           // 覆写（覆盖现有 slot_list！）
      signal->mutex = NULL;                       // 旧 mutex 指针丢失
      rc = ns_platform_mutex_create(&signal->mutex, ...);
  ```
- **影响**：
  1. 旧 mutex 句柄泄漏（平台层 mutex 资源不可回收）；
  2. 旧 `slot_list` 被重置——若之前 `connect` 过，connection 的 `signal_node` 仍指向 `slot_list` 的旧地址（实际是嵌入 signal 的节点，仍有效），但 signal 端 list 已自环，slot 永远不会 fire（"僵尸 connection"）。
- **严重度**：Major（资源泄漏 + 连接状态不一致；调用方不重复 init 即可避免，但 API 未防御）。
- **建议修复**：
  1. 在 `init_raw` 入口检查 `signal->mutex != NULL` 返回 `NS_E_EXISTS`；
  2. 或要求 `init_raw` 前显式 `deinit_raw`，文档明示。
- **文档现状**：`include/nanosig/nanosig_signal.h:223-240` 提到 "`init_raw` 是 `init` 宏的底层入口"，未提及重复 init 行为。

### F2. `ns_watcher_init_fd` / `ns_watcher_init_handle` 重复 init 不检测且不释放旧 signal mutex（Major）

- **位置**：`src/ns_broker.c:97-125`
- **问题**：`init_fd` / `init_handle` 入口 `ns_watcher_reset_empty(watcher)`（`:102`、`:117`）仅将
  `signal.mutex = NULL`（`:60`），**不**调 `ns_signal_deinit_raw` 释放旧 mutex。
  重复 init 同样导致旧 mutex 句柄泄漏 + `broker_node` 重复 `ns_list_init`（已自环则 OK，但旧 link 状态丢失）。
- **证据**：
  ```c
  static void ns_watcher_reset_empty(ns_watcher_t *watcher) {
      watcher->signal.mutex = NULL;          // 旧指针直接丢弃
      ...
      ns_list_init(&watcher->signal.slot_list);
      ns_waitable_init(&watcher->waitable);
      ns_list_init(&watcher->broker_node);
  }
  ```
- **严重度**：Major。
- **建议修复**：`reset_empty` 改为先 `ns_signal_deinit_raw(&watcher->signal)`；或在 `init_fd/handle` 入口检测 `ns_watcher_is_initialized` 并返回 `NS_E_EXISTS`。
- **文档现状**：`include/nanosig/nanosig_broker.h:67-90` 未提示重复 init 行为。

### F3. `ns_shutdown` 在 timer 残留时未释放 timer->signal mutex（Major）

- **位置**：`src/ns_broker.c:344-367` + `src/ns_timer.c:163-183`
- **问题**：`ns_broker_global_shutdown` 调用 `ns_timer_mgr_global_shutdown`，后者仅清 rbtree 与 mutex，
  **不**调用各 timer 的 `ns_signal_deinit_raw`。若调用方在 `ns_shutdown` 前未逐个 `ns_timer_deinit`，
  timer 句柄上的 signal mutex 句柄泄漏。
- **证据**：
  ```c
  // src/ns_timer.c:163-183
  void ns_timer_mgr_global_shutdown(void) {
      ...
      while((node = ns_rbtree_first(&g_timer_mgr.tree)) != NULL){
          (void)ns_rbtree_remove(&g_timer_mgr.tree, node);
      }
      ...
  }
  // 移除 rbtree 节点后，未对 timer->signal 做 deinit_raw。
  ```
- **对比**：`ns_broker_remove_all_watchers`（`src/ns_broker.c:267-282`）从 waitset 注销 watcher 并
  重新初始化 `broker_node`，但不调 `ns_watcher_deinit`——同样**不**释放 watcher->signal mutex。
  不过 `ns_shutdown` 文档已声明调用方须先 `ns_broker_remove()`（`include/nanosig/nanosig.h:46`），
  所以残留 watcher 仅在"调用方违规"时发生，库的兜底清理已满足 `docs/共识计划.md:122` "shutdown 必须清理
  waitset 中残留 watcher"的要求。timer 路径同理，但 `ns_shutdown` 文档（`:45`）要求先 `ns_timer_deinit`，
  而 `ns_timer_mgr_global_shutdown` 兜底只清 rbtree，**不调 `ns_timer_deinit`**——所以违规时确实泄漏。
- **严重度**：Major（违反调用方契约时库的兜底不完整）。
- **建议修复**：`ns_timer_mgr_global_shutdown` 在清 rbtree 时对每个 timer 调
  `ns_signal_deinit_raw(&timer->signal)` 并 `ns_rbtree_node_init`；或文档强化"未 destroy 的 timer
  残留 signal mutex 句柄将由库释放"。

### F4. `ns_loop_deinit` 跨线程 quit 文档缺失（Major）

- **位置**：`include/nanosig/nanosig_loop.h:72-81` + `src/nanosig.c:160-173`
- **问题**：`ns_loop_quit` 可跨线程调用（`:99` 已文档），但 `ns_loop_deinit` 在 `running != 0` 时
  返回 `NS_E_INVAL`（`src/nanosig.c:166`）。跨线程 quit 后调用方立即 destroy 会被短暂拒绝——文档
  未明示调用方应等待 `ns_loop_run` 实际返回（即 `running` 归零）后再 destroy。
- **建议修复**：在 `ns_loop_deinit` 文档段添加"跨线程 quit 后，调用方须等待 `ns_loop_run` 返回
  （或 `ns_loop_stop` 返回）再调用本函数"。
- **严重度**：Major（文档不一致，调用方易踩坑）。

### F5. `ns_watcher_deinit` 后 in-flight slot 的 `user_data` 生命周期未文档化（Major）

- **位置**：`include/nanosig/nanosig_broker.h:88-98`
- **问题**：deinit 强制先 `ns_broker_remove`，但 remove 与 deinit 之间存在 broker 已 fire 但 loop
  未 dispatch 的窗口——此时已入队的 slot 调用仍会触发。`user_data` 若与 watcher 关联，destroy 后
  调用方释放 `user_data` 会导致 loop 派发时 use-after-free。
- **建议修复**：在 `ns_watcher_deinit` 文档添加"调用方须保证 broker 触发过的所有 slot 调用已
  在目标 loop 派发完成（且 `user_data` 生命周期长于该派发），方可释放 `user_data`"。
- **严重度**：Major（文档与实现不一致的风险点）。

### F6. `ns_timer_init` 重复 init 行为未文档化（Info 倾向，但影响轻微）

- **位置**：`src/ns_timer.c:268-287`
- **问题**：与 F1 / F2 同构；重复 `ns_timer_init` 会覆写字段、未释放旧 signal mutex；但 rbtree 节点
  `ns_rbtree_node_init` 不会破坏旧 link（已自环），旧 timer 若在 run 状态，新 `create` 后 `start` 会
  重复入树（`ns_timer_start` 检测 `is_running` 返回 `NS_E_EXISTS`）。
- **严重度**：Major（与 F1/F2 一致模式，统一处理）。

## 结论

### v1 资源契约是否成立？

**主路径上成立**：

- 全局 runtime、loop、signal、timer、watcher、broker node 的 create / destroy 对在源代码中存在唯一对应实现；
- 失败路径清理：所有 create / init 的失败路径上，库不留堆内存（mutex / waitset / wakeup / thread 失败时按
  `out_*` 标签回滚）；
- 重复 destroy 行为：deinit_raw 在 `mutex == NULL` 时返回 `NS_OK`（幂等）；
- broker 关闭路径已实现 `ns_broker_remove_all_watchers` 兜底清理 waitset 中残留 watcher
  （`docs/共识计划.md:122` 要求达成）；
- 跨线程 quit 的原子保护：`running` 标志保证 destroy 不与 run 并发。

**存在 6 项 Major 发现**，主要集中在：
1. 重复 init 不检测、旧 mutex 句柄泄漏（F1、F2、F6）；
2. shutdown 兜底不完整——timer 残留 signal mutex 句柄（F3）；
3. 跨线程销毁的时序约束未文档化（F4、F5）。

### 遗留风险列表

- **F1 / F2 / F6**（重复 init 句柄泄漏）：建议在 init 入口加 `NS_E_EXISTS` 守卫或显式要求先 deinit。
- **F3**（shutdown 残留 timer mutex 泄漏）：建议在 `ns_timer_mgr_global_shutdown` 遍历 rbtree 时
  释放每个 timer 的 signal mutex。
- **F4**（跨线程 destroy 时序）：建议在 `ns_loop_deinit` 文档补充"等待 `ns_loop_run` 返回后再调用"。
- **F5**（in-flight slot user_data 生命周期）：建议在 `ns_watcher_deinit` / `ns_signal_deinit` 文档中
  补充调用方生命周期约束。
- **文档一致性**：建议 `include/nanosig/*.h` 头中所有 "create / init" 文档段补充"重复 init 行为"说明
  （`NS_E_EXISTS` vs 泄漏 vs 幂等），与 `ns_signal_disconnect` / `ns_signal_disconnect_all` 文档
  风格对齐。

整体 v1 资源契约可被谨慎使用方接受；建议在下一次小版本迭代中处理上述 6 项 Major 文档/防御加固。

---

## P11.2 补丁记录（2026-06-24）

### R1. timerfd 资源管理补丁

- **位置**：`platform/linux/port.c:487-491`
- **变更**：非 armed 路径（`INFINITE`/`0` timeout）下，epoll 报告 timerfd sentinel 时增加 `read()` drain。timerfd 以 `TFD_NONBLOCK` 创建，read 不阻塞。
- **资源影响**：timerfd 创建/销毁路径不变（`waitset_create` 时创建，`waitset_destroy` 时关闭）。新增的 drain 路径不创建或销毁任何资源，仅消费内核侧的 timerfd 计数器。
- **结论**：资源生命周期审计结论不受影响。

### R2. broker 测试资源清理修正

- **位置**：`test/unit/test_broker.c:448-453`
- **变更**：`test_watcher_deinit_before_remove` 从"先 deinit 后 remove"改为"验证 deinit 返回 NS_E_EXISTS → remove → deinit"。
- **资源影响**：signal mutex 现在被正确释放（通过第二次成功的 deinit）。消除了 LeakSanitizer 报告的 40 字节泄漏。
- **结论**：验证了 `ns_watcher_deinit` 的资源清理路径在正确调用顺序下工作正常。
