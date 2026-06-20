# API 一致性审计（P9.7）

> nanosig v1 / 2026-06-20 审计快照

## 摘要

| 类别 | 计数 |
| --- | --- |
| 公开头文件 | 16（`nanosig_ds.h` 为纯聚合，无独立 API） |
| `extern` 函数声明 | 70 |
| `static inline` 函数（header-only helper） | 38 |
| 函数包装宏（小写 `ns_*`） | 4 |
| 声明 / 元数据宏（大写 `NS_*`） | 25+ |
| 公开类型 `ns_*_t` | 28（含 2 opaque 前向声明） |
| 公开常量 `NS_*` or `NANOSIG_VERSION_*` | 35+ |

**问题分布：**

| 严重度 | 计数 |
| --- | --- |
| Critical | 0 |
| Major | 3 |
| Info | 5 |

---

## 1. 命名风格核验

### 1.1 函数包装宏

| 名字 | 实际大小写 | 期望 | 结果 |
| --- | --- | --- | --- |
| `ns_signal_init` | `ns_*` 小写 | `ns_*` 小写 | PASS |
| `ns_signal_deinit` | `ns_*` 小写 | `ns_*` 小写 | PASS |
| `ns_signal_emit` | `ns_*` 小写 | `ns_*` 小写 | PASS |
| `ns_signal_connect_typed` | `ns_*` 小写 | `ns_*` 小写 | PASS |

**结论：全部通过。** 4 个函数包装宏均遵循小写 + 下划线约定。

### 1.2 声明 / 元数据宏

| 名字 | 实际大小写 | 期望 | 结果 |
| --- | --- | --- | --- |
| `NS_SIGNAL_DECLARE` | `NS_*` 大写 | `NS_*` 大写 | PASS |
| `NS_DEFINE_SLOT` | `NS_*` 大写 | `NS_*` 大写 | PASS |
| `NS_SLOT_TYPECHECK` | `NS_*` 大写 | `NS_*` 大写 | PASS |
| `NS_SIGNAL_PAYLOAD_SIZE` | `NS_*` 大写 | `NS_*` 大写 | PASS |
| `NS_SIGNAL_PAYLOAD_PTR_SIZE` | `NS_*` 大写 | `NS_*` 大写 | PASS |
| `NS_NO_PAYLOAD` | `NS_*` 大写 | `NS_*` 大写 | PASS |
| `NS_MPSC_RECORD_RING_ALIGNMENT` | `NS_*` 大写 | `NS_*` 大写 | PASS |
| `NS_LOOP_CONFIG_DEFAULT()` | `NS_*` 大写 | `NS_*` 大写 | PASS |
| `NS_LIST_INITIALIZER` | `NS_*` 大写 | `NS_*` 大写 | PASS |
| `NS_SLIST_INITIALIZER` | `NS_*` 大写 | `NS_*` 大写 | PASS |
| `NS_SLIST_HEAD` | `NS_*` 大写 | `NS_*` 大写 | PASS |

**结论：全部通过。** 声明和元数据宏均为大写 + 下划线。

### 1.3 类型命名

抽样核验：

| 类型 | 格式 | 结果 |
| --- | --- | --- |
| `ns_signal_t` | `ns_*_t` | PASS |
| `ns_connection_t` | `ns_*_t` | PASS |
| `ns_no_payload_t` | `ns_*_t` | PASS |
| `ns_loop_t` | `ns_*_t` | PASS |
| `ns_loop_config_t` | `ns_*_t` | PASS |
| `ns_timer_t` | `ns_*_t` | PASS |
| `ns_time_us_t` | `ns_*_t` | PASS |
| `ns_event_broker_t` | `ns_*_t` | PASS |
| `ns_watcher_t` | `ns_*_t` | PASS |
| `ns_watcher_event_t` | `ns_*_t` | PASS |
| `ns_platform_waitable_t` | `ns_*_t` | PASS |
| `ns_list_node_t` | `ns_*_t` | PASS |
| `ns_slist_t` / `ns_slist_node_t` | `ns_*_t` | PASS |
| `ns_ringbuf_t` | `ns_*_t` | PASS |
| `ns_hashtable_t` / `ns_hashtable_node_t` | `ns_*_t` | PASS |
| `ns_rbtree_t` / `ns_rbtree_node_t` | `ns_*_t` | PASS |
| `ns_mpsc_record_ring_t` | `ns_*_t` | PASS |
| `ns_mpsc_record_part_t` | `ns_*_t` | PASS |
| `ns_capacity_t` | `ns_*_t` | PASS |
| `ns_memory_order_t` | `ns_*_t` | PASS |
| `ns_status_t` | `ns_*_t` | PASS |
| `ns_slot_fn` | `ns_*` 无 `_t` | 例外：函数指针 typedef，允许不按 `_t` 命名 |
| `ns_platform_mutex_t` | `ns_*_t` | PASS |

**结论：通过。** 所有 struct/enum typedef 均为 `ns_*_t`。`ns_slot_fn` 为函数指针类型，未用 `_t` 是可接受的约定例外。

### 1.4 常量命名

| 常量组 | 格式 | 结果 |
| --- | --- | --- |
| `NS_OK`, `NS_E_QUEUE_FULL`, ... | `NS_*` | PASS |
| `NS_CAPACITY_1` ... `NS_CAPACITY_1073741824` | `NS_*` | PASS |
| `NS_TIMER_ATTR_ONESHOT`, `NS_TIMER_ATTR_REPEAT`, ... | `NS_*` | PASS |
| `NS_WAITABLE_EVENT_IN`, `_OUT`, `_ERR` | `NS_*` | PASS |
| `NS_RBTREE_RED`, `NS_RBTREE_BLACK` | `NS_*` | PASS |
| `NANOSIG_VERSION_MAJOR`, `_MINOR`, `_PATCH` | `NANOSIG_VERSION_*` | PASS（版本号宏使用不同于 `NS_*` 的前缀是可接受的命名空间例外） |
| `ns_offsetof`, `ns_same_type`, `ns_read_once`, `ns_align_up`, `ns_ctz`, ... | `ns_*` 小写 | PASS（表达式辅助宏遵循小写约定） |

**结论：通过。** `NANOSIG_VERSION_*` 使用独立前缀（区别于 `NS_*`）是可接受的设计。表达式辅助宏用 `ns_*` 小写延续函数包装宏惯例。

---

## 2. 声明样式

### 2.1 公开函数是否全部 `extern`？

**是。** 70 个公开函数声明全部使用 `extern` 关键字。已验证：

- `nanosig.h`: `extern int ns_init(void)`, `ns_shutdown`, `ns_is_initialized`
- `nanosig_signal.h`: `extern int ns_signal_init_raw`, `ns_signal_connect`, `ns_signal_disconnect`, `ns_signal_disconnect_all`, `ns_signal_emit_raw`, `ns_signal_deinit_raw`
- `nanosig_loop.h`: `extern int ns_loop_create`, `ns_loop_destroy`, `ns_loop_run`, `ns_loop_quit`, `ns_loop_start`, `ns_loop_stop`
- `nanosig_broker.h`: `extern int ns_watcher_init_fd`, `ns_watcher_init_handle`, `ns_watcher_deinit`, `extern ns_event_broker_t *ns_broker`, `extern int ns_broker_add`, `ns_broker_remove`
- `nanosig_timer.h`: `extern int ns_timer_create`, `ns_timer_start`, `ns_timer_cancel`, `ns_timer_restart`, `ns_timer_destroy`
- `nanosig_rbtree.h`: 17 个 `extern` 函数
- `nanosig_ringbuf.h`: 13 个 `extern` 函数
- `nanosig_hashtable.h`: 7 个 `extern` 函数
- `nanosig_mpsc_record_ring.h`: 7 个 `extern` 函数

**结论：PASS。**

### 2.2 `static inline` 是否仅用于 header-only helper？

**是。** 38 个 `static inline` 函数全部位于 header-only 辅助模块中：

| 文件 | 函数 | 性质 |
| --- | --- | --- |
| `nanosig_list.h` | 13 个（`ns_list_init`, `ns_list_empty`, `ns_list_push_front`, ...） | intr. 双向链表，纯内联 |
| `nanosig_slist.h` | 22 个（`ns_slist_init`, `ns_slist_push_front`, ...） | intr. 单向链表，纯内联 |
| `nanosig_waitable.h` | 1 个（`ns_waitable_init`） | waitable 初始化，纯内联 |
| `nanosig_types.h` | 2 个（`ns_ctz_u32`, `ns_clz_u32`） | 平台类型辅助，纯内联 |

没有任何 `extern` 函数被错误地声明为 `static inline`。

**结论：PASS。**

---

## 3. 错误码

### 3.1 错误码集合

所有状态码定义在 `include/nanosig/nanosig_status.h`：

| 错误码 | 含义 | 出现位置 | 备注 |
| --- | --- | --- | --- |
| `NS_OK` | 成功 | 全部函数 | = 0 |
| `NS_E_QUEUE_FULL` | 队列满 | `ns_mpsc_record_ring_try_push`, `ns_mpsc_record_ring_try_pushv` | = -1 |
| `NS_E_NOMEM` | 内存不足 | `ns_signal_init_raw`, `ns_loop_create` | = -2 |
| `NS_E_INVAL` | 参数无效 | 几乎所有函数 | = -3 |
| `NS_E_TOO_MANY_HANDLES` | 句柄数超限 | Windows waitset | = -4 |
| `NS_E_SHUTDOWN` | 系统已关闭 | 全局相关函数 | = -5 |
| `NS_E_EXISTS` | 重复注册 | `ns_loop_run`, `ns_broker_add`, `ns_hashtable_insert` | = -6 |
| `NS_E_NO_LOOP` | 缺少 loop | 保留/内部 | = -7 |
| `NS_E_EMPTY` | 队列/容器为空 | `ns_mpsc_record_ring_try_acquire` | = -8 |
| `NS_E_CORRUPT` | 内部数据损坏 | `ns_mpsc_record_ring_try_acquire` / `release` | = -9 |
| `NS_E_NO_TIMER` | 无 timer | `ns_timer_mgr_next_timeout`（内部模块） | = -10 |
| `NS_E_BUSY` | 资源忙 | `ns_loop_run`（重复 start）, `ns_loop_start` | = -11 |

### 3.2 问题

| 问题 | 严重度 | 说明 |
| --- | --- | --- |
| 共识计划文档中提及 `NS_E_AGAIN` / `NS_E_NOT_FOUND` / `NS_E_UNSUPPORTED`，但实际头文件中未定义 | Info | 共识计划说"等"，不是穷举列表；当前集合覆盖了 v1 所有实际使用场景 |
| 没有函数混用 `0` 或 `-1` 作为错误码替代 | PASS | 全部返回 `int` 的函数使用 `NS_*` 枚举值；比较器返回 `-1/0/1` 是内部细节，不属于错误码 |

**结论：通过。** 错误码集合完整，无含义重叠，无魔数混用。

---

## 4. 参数顺序

按 API 分组，所有公开函数的参数顺序遵循 **对象/句柄先行，配置参数随后** 的约定。

### 4.1 全局生命周期 API

| 函数 | 签名模式 | 一致性 |
| --- | --- | --- |
| `ns_init(void)` | 无参 | PASS |
| `ns_shutdown(void)` | 无参 | PASS |
| `ns_is_initialized(int *out)` | 单一输出参数 | PASS |

### 4.2 Signal API

| 函数 | 参数顺序（signal, fn/config) | 一致性 |
| --- | --- | --- |
| `ns_signal_init_raw(signal, payload_size, slot_capacity, debug_name)` | signal 对象先行 | PASS |
| `ns_signal_connect(signal, slot_fn, target_loop, user_data, connection)` | signal 对象先行 | PASS |
| `ns_signal_disconnect(connection)` | 单一参数 | PASS |
| `ns_signal_disconnect_all(signal)` | 单一参数 | PASS |
| `ns_signal_emit_raw(signal, payload, payload_size)` | signal 对象先行 | PASS |
| `ns_signal_deinit_raw(signal)` | 单一参数 | PASS |

`ns_signal_connect` 的参数顺序与共识计划完全一致：`signal, slot_fn, target_loop, user_data, connection`。PASS。

### 4.3 Loop API

| 函数 | 参数顺序 | 一致性 |
| --- | --- | --- |
| `ns_loop_create(out_loop, config)` | 输出指针在前 | PASS |
| `ns_loop_destroy(loop)` | 单一参数 | PASS |
| `ns_loop_run(loop)` | 单一参数 | PASS |
| `ns_loop_quit(loop)` | 单一参数 | PASS |
| `ns_loop_start(loop)` | 单一参数 | PASS |
| `ns_loop_stop(loop)` | 单一参数 | PASS |

### 4.4 Timer API

| 函数 | 参数顺序 | 一致性 |
| --- | --- | --- |
| `ns_timer_create(timer, interval_us, attr)` | 对象先行 | PASS |
| `ns_timer_start(timer)` ~ `ns_timer_destroy(timer)` | 单一参数 | PASS |

### 4.5 Broker API

| 函数 | 参数顺序 | 一致性 |
| --- | --- | --- |
| `ns_watcher_init_fd(watcher, fd, events, edge_triggered)` | 对象先行 | PASS |
| `ns_watcher_init_handle(watcher, handle, events, edge_triggered)` | 对象先行 | PASS |
| `ns_watcher_deinit(watcher)` | 单一参数 | PASS |
| `ns_broker()` | 无参 | PASS |
| `ns_broker_add(broker, watcher)` | broker 先行 | PASS |
| `ns_broker_remove(broker, watcher)` | broker 先行 | PASS |

### 4.6 数据结构 API

| 文件 | 模式 | 一致性 |
| --- | --- | --- |
| `nanosig_ringbuf.h` | 全部 `ns_ringbuf_t *ringbuf` 作为第一个参数 | PASS |
| `nanosig_hashtable.h` | 全部 `ns_hashtable_t *table` 作为第一个参数 | PASS（clear 除外，`table` 为唯一参数） |
| `nanosig_rbtree.h` | 全部 `ns_rbtree_t *tree` 或 `ns_rbtree_node_t *node` 作为第一个参数 | PASS |
| `nanosig_mpsc_record_ring.h` | 全部 `ns_mpsc_record_ring_t *ring` 作为第一个参数 | PASS |

**结论：PASS。** 所有 API 的参数顺序遵循"对象/句柄先行，配置参数随后"的约定。

---

## 5. 文档完整度

### 5.1 无 Doxygen 注释的公开符号

**所有 70 个 extern 函数都有 @brief 注释。** 零缺失。

### 5.2 缺 @param / @return 的函数

以下函数的 Doxygen 注释缺少 `@param` 或 `@return` 标签：这些函数的参数通过函数名自解释，但严格来说仍应标记。

**nanosig_rbtree.h — 12 个函数缺 @param 或 @return：**

| 函数 | 缺失项 |
| --- | --- |
| `ns_rbtree_node_init` | 缺 `@param node` |
| `ns_rbtree_empty` | 缺 `@param tree`, `@return` |
| `ns_rbtree_node_is_linked` | 缺 `@param node`, `@return` |
| `ns_rbtree_insert` | 缺 `@param tree`, `@param node` |
| `ns_rbtree_remove` | 缺 `@param tree`, `@param node` |
| `ns_rbtree_first` | 缺 `@param tree`, `@return` |
| `ns_rbtree_last` | 缺 `@param tree`, `@return` |
| `ns_rbtree_next` | 缺 `@param node`, `@return` |
| `ns_rbtree_prev` | 缺 `@param node`, `@return` |
| `ns_rbtree_next_match` | 缺 `@return` |
| `ns_rbtree_find_add` | 缺 `@param node`, `@param tree` |
| `ns_rbtree_first_postorder` | 缺 `@param tree`, `@return` |
| `ns_rbtree_next_postorder` | 缺 `@param node`, `@return` |

**nanosig_ringbuf.h — 7 个函数缺 @param 或 @return：**

| 函数 | 缺失项 |
| --- | --- |
| `ns_ringbuf_clear` | 缺 `@param ringbuf` |
| `ns_ringbuf_reset` | 缺 `@param ringbuf` |
| `ns_ringbuf_total_size` | 缺 `@param ringbuf`, `@return` |
| `ns_ringbuf_size` | 缺 `@param ringbuf`, `@return` |
| `ns_ringbuf_free_size` | 缺 `@param ringbuf`, `@return` |
| `ns_ringbuf_write` | 缺 `@param ringbuf`, `@param buf`, `@param len` |
| `ns_ringbuf_write_skip` | 缺 `@param ringbuf`, `@param len` |
| `ns_ringbuf_read` | 缺 `@param ringbuf`, `@param buf`, `@param len` |
| `ns_ringbuf_read_skip` | 缺 `@param ringbuf`, `@param len` |

**nanosig_hashtable.h — 2 个函数缺 @param 或 @return：**

| 函数 | 缺失项 |
| --- | --- |
| `ns_hash_string` | 缺 `@param key`, `@return` |
| `ns_hashtable_clear` | 缺 `@param table` |

### 5.3 @warning / @note 使用情况

**公开头中没有任何 `@warning` 或 `@note` 标签。** 使用 `@pre` 取代了预处理语义的表达：

| 文件 | `@pre` 使用情况 |
| --- | --- |
| `nanosig.h` | `ns_init`(1), `ns_shutdown`(4) |
| `nanosig_signal.h` | `ns_signal_init`, `ns_signal_init_raw`, `ns_signal_connect`, `ns_signal_disconnect`, `ns_signal_disconnect_all`, `ns_signal_emit_raw`, `ns_signal_deinit_raw` |
| `nanosig_loop.h` | `ns_loop_create`, `ns_loop_destroy` |
| `nanosig_timer.h` | 无 `@pre` |
| `nanosig_broker.h` | 无 `@pre` |
| `nanosig_rbtree.h` | 无 `@pre` |
| `nanosig_mpsc_record_ring.h` | 无 `@pre`（用 `@pre` 文本替代） |

**结论：有 3 个 Major 的文档缺失。** 2 个 Info 的 `@warning`/`@note` 一致性建议（参见第 6 节）。

---

## 6. API 风格聚合检查

### 6.1 `NS_*` 与 `ns_*` 未混用

**通过。** 函数包装宏全部小写 `ns_*`，声明/元数据宏全部大写 `NS_*`，无交叉混用。

### 6.2 `payload` 概念一致性

| 元素 | 状态 |
| --- | --- |
| `ns_no_payload_t` — 零 payload 类型标记 | PASS |
| `NS_NO_PAYLOAD` — emit 时使用的 payload 常量 | PASS |
| `NS_SIGNAL_PAYLOAD_SIZE` — 编译期 payload 大小 | PASS |
| `NS_SIGNAL_PAYLOAD_PTR_SIZE` — 编译期 payload 指针大小 | PASS |
| `ns_signal_emit(signal, payload_ptr)` — 宏自动推导 payload 大小 | PASS |
| `ns_signal_emit_raw(signal, payload, payload_size)` — 原始 emit 接口 | PASS |
| `ns_watcher_event_t` — watcher 事件 payload 类型（非 no-payload） | PASS |
| `ns_timer_t.signal` — timer 使用 no-payload emit（`NS_NO_PAYLOAD`） | PASS |

**结论：通过。**

### 6.3 init / deinit / create / destroy 命名一致性

| 对象 | 初始化 | 销毁 | 性质 |
| --- | --- | --- | --- |
| `ns_signal_t` | `ns_signal_init_raw` / `ns_signal_init`（宏） | `ns_signal_deinit_raw` / `ns_signal_deinit`（宏） | 调用方拥有存储，alloc mutex |
| `ns_loop_t` | `ns_loop_create` | `ns_loop_destroy` | 不透明句柄，堆分配 |
| `ns_timer_t` | `ns_timer_create` | `ns_timer_destroy` | 调用方拥有存储，alloc mutex（内嵌 signal） |
| `ns_watcher_t` | `ns_watcher_init_fd` / `ns_watcher_init_handle` | `ns_watcher_deinit` | 调用方拥有存储，alloc mutex（内嵌 signal） |
| `ns_ringbuf_t` | `ns_ringbuf_init` | (无，调用方管理存储) | 纯初始化，无 alloc |
| `ns_hashtable_t` | `ns_hashtable_init` | (无，调用方管理存储) | 纯初始化，无 alloc |
| `ns_rbtree_t` | `ns_rbtree_init` | (无) | 纯初始化，无 alloc |
| `ns_mpsc_record_ring_t` | `ns_mpsc_record_ring_init` | (无，调用方管理存储) | 纯初始化，无 alloc |

| 问题 | 严重度 | 说明 |
| --- | --- | --- |
| timer 和 signal 都是调用方拥有存储且都分配 mutex，但 timer 用 `_create`/`_destroy`，signal 用 `_init`/`_deinit` | Info | `ns_timer_create` 覆盖 init+start 准备状态，语义上比 `ns_signal_init_raw` 更重；但两者基础模式相同。建议在 API 设计文档中明确区分规则。 |
| watcher 使用 `ns_watcher_init_fd` / `ns_watcher_init_handle` 而非 `ns_watcher_create_fd`，但使用 `ns_watcher_deinit` 而非 `destroy` | Info | init 前缀一致（与 signal 对齐），`deinit` 与 init 对称。合理但值得注意。 |

### 6.4 公开头中无 `__safety` 注解

**通过。** `nanosig_safety.h` 仅包含占位注释，无实际宏定义：

```c
/* nanosig v1 当前公开 API 不沿用 eventhub_os 的 `__safety` 或 ISR 注解。*/
/* 该头文件仅作为未来 v2 MCU/ISR 审计的扩展点保留 */
```

无任何其他公开头包含 `__safety`。

### 6.5 无 C++ 保留命名冲突

所有 `extern "C"` 块正确包裹除 `nanosig_status.h`、`nanosig_waitable.h`、`nanosig_safety.h` 外的所有头文件。

| 文件 | `extern "C"` 包裹 | 备注 |
| --- | --- | --- |
| `nanosig_status.h` | 无 | 仅有 enum 定义，C++ 兼容，无函数声明 |
| `nanosig_waitable.h` | 有 | 含 `static inline` |
| `nanosig_safety.h` | 有 | 纯注释占位 |

### 6.6 DS `_write` / `_read` 命名冲突

`nanosig_ringbuf.h` 的 `ns_ringbuf_write` / `ns_ringbuf_read` 与 POSIX `write` / `read` 同名但非冲突（属于不同命名空间，C 语言层不冲突）。

**结论：Info，无需修改。**

---

## 7. 严重度等级

### Critical（0）

无。

### Major（3）

1. **nanosig_rbtree.h 文档不完整：** 12 个函数缺 `@param` / `@return`。签名简单但应补齐 Doxygen 标签。

2. **nanosig_ringbuf.h 文档不完整：** 7 个函数缺 `@param` / `@return` 标签（`clear`, `reset`, 查询类函数）。

3. **nanosig_hashtable.h 文档不完整：** 2 个函数缺 `@param` / `@return`（`ns_hash_string`, `ns_hashtable_clear`）。

### Info（5）

1. `ns_timer_create`/`ns_timer_destroy` vs `ns_signal_init_raw`/`ns_signal_deinit_raw` 命名模式不一致：两者都是调用方拥有存储 + 内部 alloc mutex，但一对用 create/destroy，另一对用 init/deinit。

2. `ns_watcher_init_fd`/`ns_watcher_init_handle` 使用 `init_*` 而非 `create_*` 做前置动作，但对应的析构是 `deinit`（而非 `destroy`）。内部一致但与其他模块比对有差异。

3. 公开头中未使用 `@warning` / `@note` 标签。建议在 `ns_signal_emit_raw`（emit 路径零分配承诺）、`ns_loop_destroy`（销毁前需断开连接等约束）等场合加入 `@warning` 提升可见性。

4. `nanosig_ringbuf.h` 的 `ns_ringbuf_write` / `ns_ringbuf_read` 与 POSIX 系统调用同名，但 C 链接层无冲突。建议在文档中注明。

5. 共识计划列出 `NS_E_AGAIN` / `NS_E_NOT_FOUND` / `NS_E_UNSUPPORTED` 但实际头文件未定义：当前 v1 未使用，无可操作性影响。

---

## 8. 关键发现

1. **命名风格 100% 合规：** 函数包装宏小写 `ns_*` vs 声明/元数据宏大写 `NS_*` 的约定被严格执行，零违规。
2. **声明样式 100% 合规：** 70 个公开函数全部使用 `extern`，38 个 `static inline` 全部为 header-only helper，无混合。
3. **错误码合规：** 全部使用 `NS_OK` / `NS_E_*`，无魔数。11 个错误码覆盖了 v1 所有场景，无含义重叠。
4. **参数顺序合规：** 全部遵循"对象/句柄先行，配置参数随后"的约定。
5. **文档缺失（3 个 Major）：** `rbtree.h`、`ringbuf.h` 和 `hashtable.h` 的数据结构函数缺少 `@param` / `@return` Doxygen 标签。**这 3 个文件累计 21 个标注点缺失，建议在 P10 终稿阶段补齐。**
6. **生命周期命名异同（2 个 Info）：** signal 用 init/deinit 而 timer 用 create/destroy，虽可解释但无形式化约定。

---

## 9. 结论

**v1 API 风格契约整体成立。** 没有 Critical 问题 —— 命名约定、错误码、声明样式、参数顺序、`__safety` 抑制、`payload` 概念一致性全部通过审计。

需要优先关注的改进项：

- **P10 阶段必须补齐：** `nanosig_rbtree.h`（12 处）、`nanosig_ringbuf.h`（7 处）、`nanosig_hashtable.h`（2 处）的 `@param` / `@return` Doxygen 标注。影响 DS 的 API 文档完整性。
- **建议立项明确** `_init`/`_deinit` vs `_create`/`_destroy` 的形式化区分规则，写入 `docs/API_DESIGN.md` 或 `docs/CONVENTIONS.md`。

剩余 Info 级别建议可在终稿阶段酌情处理，不影响当前 v1 API 冻结。
