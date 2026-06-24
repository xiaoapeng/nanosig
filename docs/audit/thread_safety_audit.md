# 线程安全审计（P9.5）

> nanosig v1 / 2026-06-20 审计快照

## 摘要

| 维度 | 数量 |
| --- | --- |
| 公开 API（含宏展开函数） | 56 |
| MPM-safe | 9 |
| 单线程 | 23 |
| 需要序列化 | 8 |
| 一次性 / 纯只读辅助 | 16（按 `thread-safety` 标签 N/A） |
| Critical | 0 |
| Major | 4（docs 缺失线程安全标签或前缀说明） |
| Info | 6（命名 / 风格） |

API 总数计算口径：

- `nanosig.h` 入口 3：`ns_init` / `ns_shutdown` / `ns_is_initialized`
- `nanosig_loop.h` 6：`ns_loop_create` / `destroy` / `run` / `quit` / `start` / `stop`
- `nanosig_signal.h` 公开函数 5 + 宏 6 + 类型与辅助宏若干（按"对外产生调用的入口"计 5）
- `nanosig_timer.h` 5
- `nanosig_broker.h` 公开 6
- `nanosig_waitable.h` 1 inline
- 数据结构（`nanosig_ds.h` 聚合）共约 38（mpsc ring 6 / rbtree 16 / hashtable 7 / ringbuf 11 / slist / list 大量 inline）
- `nanosig_atomic.h` 10+ 宏

按并发类分布（仅统计会产生跨对象共享的接口；纯只读 / 内联 trivial helper 不计）：

| 分类 | 个数 | 例子 |
| --- | --- | --- |
| MPM-safe（多线程直接调用，无需额外同步） | 9 | `ns_signal_emit_raw`、`ns_signal_connect`、`ns_signal_disconnect`、`ns_signal_disconnect_all`、`ns_timer_start`、`ns_timer_cancel`、`ns_timer_restart`、`ns_broker_add`、`ns_broker_remove` |
| 单线程（必须由同一"拥有线程"独占调用） | 23 | `ns_loop_run`、所有 `ns_mpsc_record_ring_try_acquire` / `release`、`ns_hashtable_*`（除 `init`）、`ns_rbtree_*`、`ns_ringbuf_*`（除 `init`）等 |
| 需要序列化（库内部有锁，调用方需遵守接口规则，否则 UB） | 8 | `ns_signal_init_raw` / `deinit_raw`、`ns_loop_create` / `destroy` / `start` / `stop`、`ns_init` / `ns_shutdown` |
| N/A（一次性查询 / 纯计算 / 字段读写辅助） | 16 | `ns_is_initialized`、`ns_hash_string`、`ns_waitable_init`、遍历宏、atomic 包装等 |

## 全局 / 生命周期

| API | 并发类 | 理由 | 调用方约束 |
| --- | --- | --- | --- |
| `ns_init` (`nanosig.h:35`) | 需要序列化 | `src/nanosig.c:74` 通过 `g_ns_initialized` 原子标志防重入；但平台层 `ns_platform_init` / `ns_broker_global_init` 内部创建 mutex + 启动 broker 线程，非并发安全。`@pre` 文档已声明须与 `ns_shutdown` / 自身串行 | 必须在程序生命周期单一线程中先于任何其他 API 调用；不得与 `ns_shutdown` 并发 |
| `ns_shutdown` (`nanosig.h:52`) | 需要序列化 | `src/nanosig.c:95` 检查 `g_ns_initialized`、置 0、调 `ns_broker_global_shutdown`（会 join broker 线程、销毁全局 mutex）；头文件 `@pre` 写明"不得与 `ns_init` 或自身并发调用" | 单一线程串行；必须在所有 loop/timer/watcher 销毁之后 |
| `ns_is_initialized` (`nanosig.h:60`) | MPM-safe | `src/nanosig.c:106` 仅做 `ns_atomic_load(acquire)` 读 `g_ns_initialized` | 任意线程 |

## loop

| API | 并发类 | 理由 | 调用方约束 |
| --- | --- | --- | --- |
| `ns_loop_create` (`nanosig_loop.h:67`) | 需要序列化 | `src/nanosig.c:114` 内部 `ns_platform_alloc` + 初始化 `mpsc ring` + `wakeup`；`@pre` 写明"不得与 `ns_loop_destroy` / 自身并发"。未使用 mutex 保护 | 单一线程；与同一 loop 的 destroy 串行；与运行中的 run 串行 |
| `ns_loop_destroy` (`nanosig_loop.h:81`) | 需要序列化 | `src/nanosig.c:160` 校验 `running == 0`（原子）、`async_thread == NULL`，然后 `ns_platform_wakeup_destroy` + `ns_platform_free`；`@pre` 写明串行 | 单一线程；先 stop 后 destroy；必须已无 in-flight emit 引用此 loop |
| `ns_loop_run` (`nanosig_loop.h:94`) | 单线程 | `src/nanosig.c:227` 入口检查 `async_thread == NULL`，内部 `ns_atomic_compare_exchange_strong(&running, 0->1)` 防止并发 run | 同一 loop 只允许一个线程 run；`async_thread` 非空时直接 `NS_E_BUSY` |
| `ns_loop_quit` (`nanosig_loop.h:105`) | MPM-safe | `src/nanosig.c:236` 仅原子 store `quit_requested=1 (release)` + `ns_platform_wakeup_signal` | 任意线程；多次调用幂等 |
| `ns_loop_start` (`nanosig_loop.h:119`) | 需要序列化 | `src/nanosig.c:256` 创建后台线程；`@pre` 写明不可重复 start / 已 start 不可 run | 单一线程；与 stop 串行 |
| `ns_loop_stop` (`nanosig_loop.h:130`) | 需要序列化 | `src/nanosig.c:271` `ns_loop_quit` + `ns_platform_thread_join`；start/stop 串行 | 单一线程；与 start 串行；多次 stop 行为未在头声明 |

## signal / slot

| API | 并发类 | 理由 | 调用方约束 |
| --- | --- | --- | --- |
| `ns_signal_init_raw` (`nanosig_signal.h:240`) | 需要序列化 | `src/nanosig.c:289` 写 `payload_size / slot_capacity / debug_name / slot_list` 字段，再 `ns_platform_mutex_create`；`@pre` 已声明不得与 init / deinit / connect / disconnect / emit 并发 | 同一 signal 串行；初始化完成前不得跨线程访问 |
| `ns_signal_deinit_raw` (`nanosig_signal.h:331`) | 需要序列化 | `src/nanosig.c:307` `ns_platform_mutex_destroy`；`@pre` 串行 | 串行；必须先断开连接或保证无并发访问 |
| `ns_signal_connect` (`nanosig_signal.h:261`) | MPM-safe | `src/nanosig.c:319` 在 `signal->mutex` 保护下 `ns_list_push_back` | 任意线程，与同 signal 的 disconnect / emit 并发安全；`target_loop` 生命周期由调用方负责 |
| `ns_signal_disconnect` (`nanosig_signal.h:281`) | MPM-safe | `src/nanosig.c:345` 在 `signal->mutex` 保护下 `ns_list_remove_init` | 任意线程；可能仍有 in-flight slot 调用，调用方负责 `user_data` 长寿 |
| `ns_signal_disconnect_all` (`nanosig_signal.h:297`) | MPM-safe | `src/nanosig.c:360` 在 `signal->mutex` 保护下循环 pop | 任意线程；同上的 in-flight 风险 |
| `ns_signal_emit_raw` (`nanosig_signal.h:315`) | MPM-safe | `src/nanosig.c:379` 在 `signal->mutex` 保护下遍历 slot 列表，对每个 conn 调 `ns_mpsc_record_ring_try_pushv`（MPSC）+ `ns_platform_wakeup_signal` | 任意线程；保证 payload 字节数与 `signal->payload_size` 一致；调用方须保证 `target_loop` 未被 destroy |
| `ns_signal_init` / `ns_signal_deinit` / `ns_signal_emit` / `ns_signal_connect_typed`（宏） | 转发 | 仅展开到 `*_raw` / `_connect` 包装 | 同底层 |

## timer

| API | 并发类 | 理由 | 调用方约束 |
| --- | --- | --- | --- |
| `ns_timer_create` (`nanosig_timer.h:79`) | 需要序列化 | `src/ns_timer.c:268` `ns_signal_init_raw`（新建 mutex）+ 初始化 `interval_us / attr / rb_node`；未对 timer 自身加锁 | 同一 timer 串行；调用前不可被其他线程访问 |
| `ns_timer_start` (`nanosig_timer.h:90`) | MPM-safe | `src/ns_timer.c:289` 内部 `ns_timer_mgr_lock`（`g_timer_mgr.mutex`）+ `rbtree_insert` + `ns_timer_mgr_notify` | 任意线程；同一 timer 重复 start 返回 `NS_E_EXISTS` |
| `ns_timer_cancel` (`nanosig_timer.h:101`) | MPM-safe | `src/ns_timer.c:312` 加 `g_timer_mgr.mutex` 调 `ns_timer_cancel_locked` | 任意线程；未运行也成功（no-op） |
| `ns_timer_restart` (`nanosig_timer.h:112`) | MPM-safe | `src/ns_timer.c:335` 加 `g_timer_mgr.mutex`、remove + insert | 任意线程 |
| `ns_timer_destroy` (`nanosig_timer.h:124`) | 需要序列化 | `src/ns_timer.c:372` 内部 `ns_timer_cancel` + `ns_signal_deinit_raw` + `ns_rbtree_node_init`；头 `@pre` 建议先显式 `ns_signal_disconnect` | 同一 timer 串行；调用方须保证无 in-flight slot 调用 |

## watcher / broker

| API | 并发类 | 理由 | 调用方约束 |
| --- | --- | --- | --- |
| `ns_watcher_init_fd` (`nanosig_broker.h:72`) | 需要序列化 | `src/ns_broker.c:97` 调 `ns_signal_init_raw`（建 mutex）+ 写 `waitable.fd` + 初始化 `broker_node` | 同一 watcher 串行；需 `ns_init()` 已调用 |
| `ns_watcher_init_handle` (`nanosig_broker.h:87`) | 需要序列化 | `src/ns_broker.c:112` 同上，写 `waitable.handle` | 同上 |
| `ns_watcher_deinit` (`nanosig_broker.h:98`) | 需要序列化 | `src/ns_broker.c:127` 校验未注册，`ns_signal_deinit_raw` + `ns_waitable_init` | 串行；须先 `ns_broker_remove` |
| `ns_broker` (`nanosig_broker.h:107`) | MPM-safe | `src/ns_broker.c:141` 返回静态 `g_broker` 指针。读 `g_broker` 是裸指针读，无原子，但只在 `ns_init` 后有效且 `ns_shutdown` 前调用方约定不会并发改变 | 任意线程；不可在 `ns_init` 前 / `ns_shutdown` 后调用 |
| `ns_broker_add` (`nanosig_broker.h:119`) | MPM-safe | `src/ns_broker.c:209` 在 `broker->watcher_mutex` 保护下 `ns_platform_waitset_add` + `ns_list_push_back` | 任意线程；调用前 watcher 已 init |
| `ns_broker_remove` (`nanosig_broker.h:131`) | MPM-safe | `src/ns_broker.c:239` 同上 mutex 保护 | 任意线程 |

## 数据结构（`ns_*_t` 直接使用）

| API | 并发类 | 理由 | 调用方约束 |
| --- | --- | --- | --- |
| `ns_mpsc_record_ring_init` (`nanosig_mpsc_record_ring.h:91`) | 需要序列化 | `src/ds/ns_mpsc_record_ring.c:239` 写 `storage / capacity / reserve_pos / write_pos / read_pos`，无并发保护 | 单一线程；与所有 push/acquire 串行 |
| `ns_mpsc_record_ring_capacity` / `free_capacity` / `max_record_size` (`*.h:103/115/127`) | MPM-safe (loose) | `free_capacity` 走 `reserve_pos (relaxed) / read_pos (relaxed)`，并发时是乐观估计；其它两者是字段读 | 任意线程；返回值仅做参考 |
| `ns_mpsc_record_ring_try_push` / `try_pushv` (`*.h:145/169`) | MPM-safe (producer) | 头文件显式声明"可被多个生产者并发调用"；实现用 acq/rel CAS + 顺序定序保证多生产者 | 多生产者并发安全；调用方负责 `record` 内存只读到 publish 完成 |
| `ns_mpsc_record_ring_try_acquire` (`*.h:197`) | 单线程 | 头文件显式声明"仅限单个消费者线程" | 单消费者；不得并发 acquire |
| `ns_mpsc_record_ring_release` (`*.h:216`) | 单线程 | 紧接 acquire 后的释放路径，依赖 read_pos 单写 | 单消费者 |
| `ns_rbtree_*`（`nanosig_rbtree.h` 全套） | 单线程 | `src/ds/ns_rbtree.c` 中所有插入 / 删除 / 旋转均为裸指针写，无 mutex / 原子 | 调用方全权负责同步 |
| `ns_hashtable_*`（`nanosig_hashtable.h`） | 单线程 | `src/ds/ns_hashtable.c` 基于 `ns_slist_t`，无锁；`init` 是字段写 | 单一线程；与 bucket 数组共享访问串行 |
| `ns_ringbuf_*`（`nanosig_ringbuf.h`） | 单线程 | `src/ds/ns_ringbuf.c` `w/r` 是裸 uint32；头文件在 `clear` 注释已写"单读单写安全"，`reset` 注释"单读单写不安全" | 单一线程；reset 须调用方保证无并发 |
| `ns_slist_*` / `ns_list_*`（inline） | 单线程 | 头内 inline，所有插入 / 删除为裸指针写 | 单一线程 |

## 严重度等级

- **Critical** —— 文档承诺 MPM-safe，但实现是单线程（误导调用方）。
- **Major** —— 并发类未在头文件 / 文档中说明，调用方无法判断。
- **Info** —— 命名 / 风格。

## 关键发现

1. **未发现 Critical**：所有已声称"可多线程"的接口在实现中确实由 mutex / 原子 / MPSC 守护。`ns_signal_emit_raw` 的 slot 列表遍历在 `signal->mutex` 内执行，向 `conn->target_loop->queue`（MPSC）的 `try_pushv` 是 lock-free 但属于"多生产者对单消费者"，与 mutex 不冲突。
2. **Major-1（`ns_broker` 返回指针无文档）**：`ns_broker()` 在 `src/ns_broker.c:141` 是裸指针读 `g_broker`，调用方无法从公开头判断这是不是 lock-free / atomic。建议在头加 `@thread-safety` 标签，写明"必须在 `ns_init` 与 `ns_shutdown` 之间调用，返回值在生命周期内恒定"。
3. **Major-2（`ns_loop_stop` 多次调用未定义）**：`ns_loop_stop` (`src/nanosig.c:271`) 在 `async_thread == NULL` 时返回 `NS_E_INVAL`，但头文件 `@pre` 未声明此约束；二次 stop 会返回错误。
4. **Major-3（`ns_watcher_init_fd/handle` 与 `deinit` 串行但未在头声明）**：`src/ns_broker.c:97-125` 在 init 时无条件 `ns_watcher_reset_empty` 再 `ns_signal_init_raw`，不持有 mutex；并发 init/deinit 与 slot 访问会破坏内嵌 signal。
5. **Major-4（DS 公共 API 完全没有 `@thread-safety`）**：除 `nanosig_mpsc_record_ring.h` 在 `try_push` / `try_pushv` / `try_acquire` 显式声明线程语义外，`nanosig_rbtree.h` / `nanosig_hashtable.h` / `nanosig_ringbuf.h` / `nanosig_slist.h` / `nanosig_list.h` 完全没有同步约束说明，调用方需自行阅读实现。
6. **Info-1（命名）**：`ns_signal_lock` / `ns_signal_unlock` (`src/nanosig.c:41/47`) 是 static，命名带 ns_ 前缀但实为内部 helper，建议改名 `signal_lock_internal` 避免误以为是公开 API。
7. **Info-2（reset 命名）**：`ns_ringbuf_reset` 头注释 "单读单写不安全" 应升级为显式 `@warning not thread-safe even in SPSC`，与 `ns_ringbuf_clear` 区分。
8. **Info-3（`ns_loop_quit` 文档）**：头说"可由拥有该 loop 的线程调用，也可由跨线程控制路径调用"，实际实现是 MPM-safe（atomic + wakeup_signal），描述正确但未使用 `@thread-safety` 标签。
9. **Info-4（broker thread 拥有性）**：`ns_broker` 全局 broker 线程由 `ns_broker_global_init` 创建、`ns_broker_global_shutdown` join，调用方约定为"broker 永远在 `ns_init` / `ns_shutdown` 之间可用"，头未明示。
10. **Info-5（`ns_signal_t.mutex` 暴露）**：`nanosig_signal.h:50` 公开了 `ns_platform_mutex_t *mutex` 字段，调用方可见但不可写。`@internal` 注释缺失。
11. **Info-6（`ns_event_broker_t` 不透明）**：`nanosig_broker.h:30` 已声明不透明（仅前向声明 + 隐藏字段），但 `g_broker` 是模块全局可变指针，shutdown 期间被置 NULL；建议在头加 "shutting down" 注释。

## 已知未覆盖 / 未测试场景

- **未测试**：多线程同时 `ns_signal_init` / `deinit` 同一 signal —— `@pre` 已禁止，单元测试中无该场景。
- **未测试**：`ns_loop_quit` 在 run 尚未开始时被另一线程调用；`running == 0` 状态下 quit 不会唤醒任何线程。
- **未测试**：`ns_timer_destroy` 在 timer signal 仍有 in-flight slot 调用时调用（`@pre` 已要求先 disconnect，但未断言）。
- **未测试**：`ns_broker_add` / `ns_broker_remove` 与 broker 自身遍历 `watcher_head` 并发——实现中 `ns_broker_remove_all_watchers` 在 shutdown 路径上才会无锁遍历，已在 mutex 内，但 `ns_broker_run` 的 completion 路径（`ns_broker_emit_completion` → `ns_signal_emit_raw`）不持 `watcher_mutex`，仅持 `signal->mutex`；并发 add+emit 路径未做端到端测试。
- **未测试**：`ns_mpsc_record_ring` 单消费者约束违反——`test_mpsc_record_ring_stress.c` 假定单消费者，无多 consumer 用例。
- **未测试**：跨平台 waitable（`event_bit` for RTOS 路径为 v2 占位）目前未实现并发测试。
- **未覆盖**：未提供 v1 不承诺的 ISR-safe 路径；任何在 ISR 中调用 `ns_signal_emit` / `ns_timer_start` 等 API 行为未定义。

## 结论

v1 线程安全契约**整体成立**：

- 公共高阶 API（`ns_signal_emit_raw`、`connect` / `disconnect`、`ns_timer_start/cancel/restart`、`ns_broker_add/remove`）由库内 mutex 守护，跨线程安全。
- `ns_loop_t` 设计承诺兑现：内部 `running` / `quit_requested` 用原子，单 run 线程约束由 `compare_exchange` 强制；`quit` / `start`（不同线程协作）由原子 + wakeup 守护。
- timer 全局 manager 由 `g_timer_mgr.mutex` 守护，`ns_timer_create/destroy` 仍依赖调用方串行（对象级）。
- broker watcher 注册表由 `watcher_mutex` 守护。
- MPSC record ring 的多生产者并发已由 lock-free 算法 + 显式头注释保证。

主要**遗留风险**集中在**文档层**而非实现层：

1. 多个头文件（`nanosig_rbtree.h` / `nanosig_hashtable.h` / `nanosig_ringbuf.h` / `nanosig_slist.h` / `nanosig_list.h`）缺乏 `@thread-safety` 注释，调用方需自己翻实现。
2. `ns_broker()` 裸指针读的并发语义未在头声明。
3. `ns_loop_stop` / `ns_watcher_init_fd|handle|deinit` 的串行约束未在 `@pre` 写明。

**建议**：

- 在 `nanosig_rbtree.h` / `nanosig_hashtable.h` / `nanosig_ringbuf.h` / `nanosig_slist.h` / `nanosig_list.h` 顶部增加一行：`@thread-safety 单一调用方线程 / 不提供内部同步`。
- 在 `nanosig_broker.h:107` `ns_broker` 旁加 `@thread-safety` 注释。
- 在 `nanosig_loop.h:130` `ns_loop_stop` `@pre` 追加"不得并发 stop 同一 loop"。
- 在 `nanosig_broker.h:72/87/98` watcher init/deinit `@pre` 追加"与同一 watcher 的其他 watcher API 串行"。
- 在 `nanosig_signal.h:50` 内嵌 mutex 字段加 `@internal`。
- 在 README / 共识计划追加"v1 公开 API 并发类速查表"（可由本审计 §摘要 直接抽取）。

结论：v1 不存在会误导调用方的 Critical 级问题；Major 级问题全部为文档缺失，可在 v1.0 之前用最低成本补齐。

---

## P11.2 补丁记录（2026-06-24）

### T1. timerfd sentinel 过滤 — broker 线程安全补丁

- **位置**：`platform/linux/port.c:481-493`
- **变更**：`ns_platform_waitset_wait` 中 timerfd sentinel 过滤从 `timer_armed && wp == SENTINEL` 改为 `wp == SENTINEL`（始终过滤）。非 armed 路径增加 `read()` drain timerfd。
- **线程安全影响**：无新增并发约束。sentinel 过滤和 timerfd drain 都在 broker 线程内部执行，不涉及跨线程共享状态。broker 线程安全模型不变。
- **结论**：线程安全审计结论不受影响，无需修订。
