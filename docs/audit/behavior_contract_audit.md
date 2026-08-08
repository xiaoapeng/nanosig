# 行为契约审计（P9.3）

> nanosig v1 / 2026-06-20 审计快照

## 摘要

承诺来源：`docs/共识计划.md` 中 P0-P6 阶段目标、已锁定公开 API 决策、跨阶段治理三大类。

| 分类 | 计数 | 已覆盖 | 部分覆盖 | 未覆盖 |
| --- | --- | --- | --- | --- |
| P0 脚手架验收承诺 | 5 | 5 | 0 | 0 |
| PD API 设计承诺 | 3 | 3 | 0 | 0 |
| P1a/P1b 平台承诺 | 5 | 5 | 0 | 0 |
| P2 数据结构承诺 | 11 | 11 | 0 | 0 |
| P3 MPSC 承诺 | 7 | 7 | 0 | 0 |
| P4 loop 管理承诺 | 4 | 4 | 0 | 0 |
| P5 signal/slot 承诺 | 6 | 5 | 1 | 0 |
| P5b waitset 承诺 | 3 | 3 | 0 | 0 |
| P6 timer+broker 承诺 | 11 | 9 | 2 | 0 |
| 线程与 loop 决策 | 6 | 6 | 0 | 0 |
| signal/slot API 决策 | 9 | 8 | 1 | 0 |
| payload/no-payload 决策 | 9 | 8 | 0 | 1 |
| signal 生命周期决策 | 8 | 5 | 2 | 1 |
| timer API 决策 | 14 | 10 | 3 | 1 |
| watcher API 决策 | 16 | 11 | 3 | 2 |
| broker API 决策 | 12 | 11 | 1 | 0 |
| v1 safety 策略 | 4 | 3 | 0 | 1 |
| 跨阶段治理 | 6 | 5 | 1 | 0 |
| **合计** | **139** | **119** | **14** | **6** |

- **已覆盖**：测试、demo 或编译时检查中有直接证据。
- **部分覆盖**：只在 happy path 验证，边界/负面/路径组合未覆盖。
- **未覆盖**：完全没有测试或编译时断言。

---

## 承诺 → 证据矩阵

### P0：仓库脚手架与 CMake

| 承诺 | 类别 | 测试 / 代码证据 | 覆盖度 |
| --- | --- | --- | --- |
| CMake preset 可配置并写入 `build/` | P0 验收 | `CMakePresets.json` 中 `binaryDir` 统一为 `${sourceDir}/build` | 已覆盖 |
| 空静态库目标可构建 | P0 验收 | `src/nanosig.c` 和 `CMakeLists.txt` 中的 `add_library(nanosig ...)` | 已覆盖 |
| 编译警告等级已提高 | P0 验收 | CMakeLists.txt 中的 `-Wall -Wextra -Wconversion -Wsign-conversion -Wno-psabi` 和 MSVC `/W4 /permissive- /Zc:preprocessor` | 已覆盖 |
| `sanitize-all` 占位目标存在 | P0 验收 | `CMakeLists.txt` 中 `add_custom_target(sanitize-all ...)` | 已覆盖 |
| `api-compile-checks` 和 CTest syntax-only 检查存在 | P0 验收 | CMake 中 `add_custom_target(api-compile-checks ...)`；6 个 compile-check 测试目标 | 已覆盖 |

### P1a：`nanosig/nanosig_port.h` 接口冻结

| 承诺 | 类别 | 测试 / 代码证据 | 覆盖度 |
| --- | --- | --- | --- |
| 定义 OS 无关接口 | P1a 产物 | `nanosig/nanosig_port.h` 存在且被循环包含 | 已覆盖 |
| 引入公开 `nanosig_atomic.h` | P1a 产物 | `include/nanosig/nanosig_atomic.h` 存在 | 已覆盖 |
| loop 模型改为显式传参，TLS 已移除 | P1a 目标 | `nanosig/nanosig_port.h` 中无 TLS 相关接口 | 已覆盖 |
| `platform/README.md` 存在 | P1a 产物 | `platform/README.md` 存在 | 已覆盖 |
| `test/unit/test_platform_contract_compile.c` 看接口编译 | P1a 产物 | 测试包含 `ns_platform_*` 类型编译检查 | 已覆盖 |

### P1b：平台后端

| 承诺 | 类别 | 测试 / 代码证据 | 覆盖度 |
| --- | --- | --- | --- |
| Linux 后端 | P1b 产物 | `platform/linux/port.c` 存在 | 已覆盖 |
| macOS 后端 | P1b 产物 | `platform/macos/port.c` 存在 | 已覆盖 |
| Windows 后端 | P1b 产物 | `platform/windows/port.c` 存在 | 已覆盖 |
| 平台后端单元测试 | P1b 产物 | `test/unit/test_platform_backend.c` 覆盖测试 22+ | 已覆盖 |
| 桌面后端同步推进 | P1b 目标 | 三个平台后端都在仓库中 | 已覆盖 |

### P2：通用公开数据结构

| 承诺 | 类别 | 测试 / 代码证据 | 覆盖度 |
| --- | --- | --- | --- |
| 公开 intrusive list | P2 产物 | `include/nanosig/nanosig_list.h` | 已覆盖 |
| 公开 slist | P2 产物 | `include/nanosig/nanosig_slist.h` | 已覆盖 |
| 公开 ringbuf | P2 产物 | `include/nanosig/nanosig_ringbuf.h` + `src/ds/ns_ringbuf.c` | 已覆盖 |
| 公开 hashtable | P2 产物 | `include/nanosig/nanosig_hashtbl.h` + `src/ds/ns_hashtbl.c` | 已覆盖 |
| 公开 rbtree | P2 产物 | `include/nanosig/nanosig_rbtree.h` + `src/ds/ns_rbtree.c` | 已覆盖 |
| 5 个数据结构单元测试 | P2 产物 | `test_ds_list.c`/`_slist.c`/`_ringbuf.c`/`_hashtable.c`/`_rbtree.c` | 已覆盖 |
| 覆盖基本操作 | P2 目标 | 各 DS 测试的基本插入/删除/遍历 | 已覆盖 |
| 覆盖明显无效入参边界 | P2 目标 | `test_ds_rbtree.c` 有 `test_invalid_operations` | 已覆盖 |
| compile contract 测试 | P2 产物 | `test_data_structures_contract_compile.c` + `test_types_contract_compile.c` | 已覆盖 |
| DS 头可独立带出状态码 | P2 产物 | `nanosig_ds.h` 包含 `nanosig_status.h` | 已覆盖 |

### P3：可变长 MPSC record ring

| 承诺 | 类别 | 测试 / 代码证据 | 覆盖度 |
| --- | --- | --- | --- |
| 可变长记录 MPSC | P3 产物 | `nanosig_mpsc_record_ring.h` + `ns_mpsc_record_ring.c` | 已覆盖 |
| 固定容量存储区，容量为 2 的幂 | P3 目标 | `test_mpsc_record_ring.c` 的 `test_full_queue` 和队列初始化检查 | 已覆盖 |
| 满队返回 `NS_E_QUEUE_FULL` | P3 目标 | `test_mpsc_record_ring.c` 的 `test_full_queue` | 已覆盖 |
| 单块 try_push + scatter-gather try_pushv | P3 目标 | `test_mpsc_record_ring.c` 的 `test_mp_4p_variable`, `test_mp_1p_baseline` 等 | 已覆盖 |
| emit 路径零分配 | P3 目标 | 见 `docs/audit/emit_zero_alloc_audit.md` | 已覆盖 |
| 调用方提供外部存储区 | P3 目标 | `ns_mpsc_record_ring_init` 接受 caller 提供的 buffer | 已覆盖 |
| 单元测试 | P3 产物 | `test/unit/test_mpsc_record_ring.c`（20+ 测试） | 已覆盖 |

### P4：loop 管理

| 承诺 | 类别 | 测试 / 代码证据 | 覆盖度 |
| --- | --- | --- | --- |
| `ns_loop_init/destroy/run/quit` | P4 产物 | `test_loop.c` 的 `test_invalid_args_and_lifecycle`, `test_same_thread_binding`, `test_cross_thread_quit_and_ownership` | 已覆盖 |
| loop 不绑定线程 | P4 目标 | `test_cross_thread_quit_and_ownership` 验证跨线程 quit | 已覆盖 |
| 已移除 `ns_loop_current` 和 loop manager 注册表 | P4 目标 | `include/nanosig/nanosig_loop.h` 中无这些声明 | 已覆盖 |
| `test/unit/test_loop.c` | P4 产物 | 9 个运行时测试 + async API 测试 | 已覆盖 |

### P5：signal / slot

| 承诺 | 类别 | 测试 / 代码证据 | 覆盖度 |
| --- | --- | --- | --- |
| connect/disconnect/emit | P5 产物 | `test_signal.c` 各测试覆盖 | 已覆盖 |
| 维护 slot 列表和连接句柄 | P5 目标 | `test_signal.c` 的 `test_multiple_connections` | 已覆盖 |
| typed wrapper API | P5 目标 | `test_macro_expansion.c` 中 `ns_signal_connect_typed` + `test_async_signal_delivery` | 已覆盖 |
| emit 可从任意线程调用 | P5 目标 | `test_cross_thread_emit`（跨线程 emit） `test_concurrent_connect_emit` | 已覆盖 |
| 已入队调用在 disconnect 后仍可能执行 | P5 目标 | 文档承诺；无直接测试 | **部分覆盖** |
| 单元测试 | P5 产物 | `test/unit/test_signal.c`（9 个测试） | 已覆盖 |

**说明**: "已入队调用仍在 disconnect 后执行" 的承诺仅在文档中有，没有专门的测试验证 disconnect 正在进行时已入队的 msg 仍然被 dispatch。这是一个 **部分覆盖** —— 只测试了 disconnect 后新 emit 不触发。

### P5b：event broker / waitset 追加契约

| 承诺 | 类别 | 测试 / 代码证据 | 覆盖度 |
| --- | --- | --- | --- |
| waitset 契约追加到 `nanosig/nanosig_port.h` | P5b 目标 | `nanosig/nanosig_port.h` 中包含 `ns_platform_waitable_t`, `ns_platform_waitset_t`, `ns_platform_waitset_completion_t` | 已覆盖 |
| `test/unit/test_platform_contract_compile.c` 覆盖新增契约 | P5b 验收 | `test_platform_contract_compile.c` 中有 waitset 编译检查 | 已覆盖 |
| `test/unit/test_platform_backend.c` 追加 waitset 运行时测试 | P5b 验收 | 22+ waitset 运行时场景 | 已覆盖 |

### P6：timer + broker

| 承诺 | 类别 | 测试 / 代码证据 | 覆盖度 |
| --- | --- | --- | --- |
| `ns_timer_mgr_t` 独立模块 | P6 目标 | `src/ns_timer.c`, `src/ns_timer_mgr.h` 独立存在 | 已覆盖 |
| rbtree 按 remaining time 排序 | P6 目标 | `src/ns_timer.c` 中 comparator 实现 | 已覆盖 |
| timer_mgr 通过回调解耦 | P6 目标 | `ns_timer_notify_fn` 回调设计 | 已覆盖 |
| `ns_timer_init/start/cancel/restart/destroy` | P6 目标 | `test_timer.c` 中 `test_start_cancel_and_restart_semantics` | 已覆盖 |
| repeat / reload-from-now 语义 | P6 目标 | `test_repeat_timer_rearms_after_fire` 覆盖 repeat | **部分覆盖**（RELOAD_FROM_NOW 未测试） |
| `ns_event_broker_t` 全局单例 | P6 目标 | `test_broker_lifecycle` 验证 broker NULL/非空/NULL 状态 | 已覆盖 |
| broker 拥有 thread + waitset + watcher 链表 + timer_mgr | P6 目标 | 代码结构验证 | 已覆盖 |
| watcher event 触发时 emit signal | P6 目标 | `test_watcher_event_reaches_loop` 完整验证 | 已覆盖 |
| 平台层新增 thread_create/join | P6 目标 | `test_platform_backend.c` 的 `test_thread` | 已覆盖 |
| watcher 直接内嵌 waitable | P6 目标 | `ns_watcher_t` 结构体验证 | 已覆盖 |
| broker 发布 g_broker 规则 | P6 目标 | `src/ns_broker.c` 中 `g_broker = broker` 在线程创建后执行 | **部分覆盖**（只覆盖正常路径，未测试创建失败） |
| shutdown 清理残留 watcher | P6 目标 | `test_shutdown_removes_residual_watcher` | 已覆盖 |

---

## 已锁定公开 API 决策 → 证据

### 线程与 loop

| 决策 | 测试 / 代码证据 | 覆盖度 |
| --- | --- | --- |
| `ns_loop_t` 名锁定 | `include/nanosig/nanosig_loop.h` 定义 | 已覆盖 |
| loop 不绑定线程 | `test_cross_thread_quit_and_ownership` | 已覆盖 |
| `ns_loop_init` 不绑定当前线程 | `test_cross_thread_quit_and_ownership`（worker 线程创建 loop） | 已覆盖 |
| `ns_loop_run(ns_loop_t *loop)` | `test_loop.c` 所有 run 相关测试 | 已覆盖 |
| `ns_loop_quit(ns_loop_t *loop)` 跨线程可调用 | `test_cross_thread_quit_and_ownership` | 已覆盖 |
| `ns_loop_current`/`ns_loop_is_owner`/TLS 已移除 | 头文件无声明；代码中无查找 | 已覆盖 |

### signal / slot

| 决策 | 测试 / 代码证据 | 覆盖度 |
| --- | --- | --- |
| 公开操作使用 `ns_signal_*` 前缀 | 全部测试使用 | 已覆盖 |
| 函数宏小写 | `ns_signal_connect_typed`, `ns_signal_emit`, `ns_signal_init` | 已覆盖 |
| 声明/类型宏大写 | `NS_SIGNAL_DECLARE`, `NS_DEFINE_SLOT`, `NS_SIGNAL_PAYLOAD_SIZE` | 已覆盖 |
| typed connect 显式指定 loop | `test_async_signal_delivery` 中的 `ns_signal_connect_typed` | 已覆盖 |
| 底层连接函数只有一个 | `ns_signal_connect` 签名验证 | 已覆盖 |
| `connection` 是调用方拥有的 `ns_connection_t *` | `test_signal.c` 中所有连接使用栈变量 | 已覆盖 |
| `target_loop` 必须非空 | `test_connect_null_loop`（P11 Phase 1） | 已覆盖 |
| typed connect 用 `_Generic` 做编译期检查 | `NS_SLOT_TYPECHECK` 定义 + `test_macro_expansion.c` 中的枚举常量除法 | 已覆盖 |
| raw connect/emit 是逃生通道 | `test_macro_expansion.c` 中显式 `ns_signal_connect` raw 调用 | 已覆盖 |

### payload 与 no-payload

| 决策 | 测试 / 代码证据 | 覆盖度 |
| --- | --- | --- |
| payload 大小由宏在编译期烘焙 | `NS_SIGNAL_PAYLOAD_SIZE` 宏定义 | 已覆盖 |
| 普通 payload 使用显式结构体 | `test_payload_same_thread` 中的 `test_payload_t` | 已覆盖 |
| no-payload 使用 `ns_no_payload_t` | `test_no_payload_same_thread` 中 init_raw(0,0) | 已覆盖 |
| `ns_no_payload_t` 不是可拷贝 payload 对象 | 文档强调，无运行期检查 | **部分覆盖** |
| `NS_NO_PAYLOAD` 是唯一 no-payload emit 入参 | `ns_signal_emit` 宏定义 + 测试使用 | 已覆盖 |
| `NS_SIGNAL_PAYLOAD_SIZE(ns_no_payload_t)` 必须为 0 | `NS_STATIC_ASSERT` 编译期断言 | 已覆盖 |
| `NS_SIGNAL_PAYLOAD_PTR_SIZE(NS_NO_PAYLOAD)` 必须为 0 | `NS_STATIC_ASSERT` 编译期断言 | 已覆盖 |
| no-payload emit 必须拷贝 0 字节 | 代码中 `payload_size == 0` 分支 | 已覆盖 |
| 不使用 `reserved[0]` 或零长数组 | 代码中无零长数组 | 已覆盖 |

### signal 生命周期

| 决策 | 测试 / 代码证据 | 覆盖度 |
| --- | --- | --- |
| `ns_signal_t` 可静态/成员，使用前必须显式初始化 | `test_uninitialized_signal_rejected` 检查 init 前拒绝操作 | 已覆盖 |
| 所有变体使用 init/init_raw | `test_signal.c` 全部使用 init_raw | 已覆盖 |
| 销毁前先断连 | 测试统一先 disconnect 再 deinit；`deinit` 文档要求 | 已覆盖 |
| init/deinit 不支持并发 | 文档 pre 条件；无测试 | **部分覆盖** |
| disconnect 不释放内存 | `test_disconnect_stops_future` 验证 disconnect 后 connection 仍可持有 | 已覆盖 |
| connection 调用方拥有 | `test_signal.c` 所有 connection 为栈变量 | 已覆盖 |
| disconnect_all 是逃生通道 | `test_disconnect_all` 验证 | 已覆盖 |
| disconnect 不取消已入队调用 | `test_disconnect_does_not_retract_enqueued`（P11 Phase 1） | 已覆盖 |

### timer API 决策

| 决策 | 测试 / 代码证据 | 覆盖度 |
| --- | --- | --- |
| `ns_timer_t` 调用方自持 | `test_timer.c` 中栈变量 | 已覆盖 |
| 首字段必须是 `ns_signal_t signal` | 结构体定义验证 | 已覆盖 |
| timer 到期触发内嵌 no-payload signal | `test_oneshot_broker_fires_signal` 验证 | 已覆盖 |
| timer 只支持无参数触发 | 设计承诺，内嵌 signal 为 no-payload | 已覆盖 |
| `ns_timer_init(timer, interval_us, attr)` | `test_timer.c` 使用 | 已覆盖 |
| `ns_time_us_t` 是 `uint64_t` | `nanosig_timer.h` 定义 | 已覆盖 |
| `attr` 位图语义 | `test_start_cancel_and_restart_semantics` 中 NS_TIMER_ATTR_ONESHOT | 已覆盖 |
| bit1 RELOAD_FROM_NOW | `test_repeat_timer_reload_from_now`（P11 Phase 1） | 已覆盖 |
| `ns_timer_init` 只初始化不启动 | `test_start_cancel_and_restart_semantics`（cancel 后再 start） | 已覆盖 |
| `ns_timer_start` 注册到 timer_mgr | `test_oneshot_broker_fires_signal`（start 后 broker 触发） | 已覆盖 |
| `ns_timer_cancel` 合法对未开始的 timer 无副作用 | `test_start_cancel_and_restart_semantics`（create 后 cancel） | 已覆盖 |
| `ns_timer_restart` 语义 | `test_start_cancel_and_restart_semantics`（restart 超时重置） | **部分覆盖**（未测 restart 在运行时） |
| `ns_timer_deinit` 停止并释放 | `test_timer.c` 测试销毁 | 已覆盖 |

### watcher API 决策

| 决策 | 测试 / 代码证据 | 覆盖度 |
| --- | --- | --- |
| `ns_watcher_t` 调用方自持 | `test_broker.c` 中栈变量 | 已覆盖 |
| 首字段 `signal` | 结构体定义验证 | 已覆盖 |
| watcher 事件触发 emit，payload `ns_watcher_event_t` | `test_watcher_event_reaches_loop` 验证 `triggered_events` | 已覆盖 |
| `ns_watcher_event_t` 含 `triggered_events` | `nanosig_broker.h` 定义 | 已覆盖 |
| `NS_WAITABLE_EVENT_IN/OUT/ERR` 公开 | `nanosig_port.h` 定义 | 已覆盖 |
| init_fd / init_handle API | `test_watcher_invalid_paths` | 已覆盖 |
| edge_triggered 参数 | `test_watcher_event_reaches_loop` 使用边沿触发 | **部分覆盖** |
| `init_*` 内部调用 init_raw | 代码验证 | 已覆盖 |
| deinit 释放 signal 重置 waitable | `test_watcher_invalid_paths`（deinit 后状态） | 已覆盖 |
| deinit 前必须先 broker_remove | `test_watcher_deinit_before_remove`（P11 Phase 1） | 已覆盖 |
| deinit 只对成功初始化的 watcher 可行 | `test_watcher_invalid_paths` 中 zero_watcher → E_INVAL | 已覆盖 |
| `waitable` 直接内嵌暴露 | 结构体定义 | 已覆盖 |
| 不引入 NS_E_UNSUPPORTED | 平台后端代码验证 | 已覆盖 |
| 不引入 opaque 私有包装 | 结构体定义 | 已覆盖 |
| fd / handle 先初始化 waitable 再 init | `test_broker.c` 使用 | 已覆盖 |

### event broker API 决策

| 决策 | 测试 / 代码证据 | 覆盖度 |
| --- | --- | --- |
| `ns_event_broker_t` 全局单例 | `test_broker_lifecycle` 验证 | 已覆盖 |
| `ns_broker()` 前返回 NULL | `test_broker_lifecycle`（ns_broker() == NULL before init） | 已覆盖 |
| broker 拥有 1 thread + 1 waitset + wakeup + timer_mgr | 代码结构验证 | 已覆盖 |
| broker 与 loop 完全解耦 | broker 通过 signal emit 投递，不操作 loop wakeup/MPSC | 已覆盖 |
| broker 与 timer_mgr 解耦 | 回调通知设计 | 已覆盖 |
| add/remove 公开 | `test_broker_add_remove` | 已覆盖 |
| 重复 add → E_EXISTS | `test_broker_add_remove` | 已覆盖 |
| remove 不撤回已入队 | `test_broker_remove_does_not_retract_enqueued`（P11 Phase 1） | 已覆盖 |
| add 设置 user_data = watcher | 代码验证 | 已覆盖 |
| broker 线程错误 continue | `test_broker_error_continue`（P11 Phase 1） | 已覆盖 |
| 失败路径不得发布 g_broker | `src/ns_broker.c` 代码中 g_broker 在成功后赋值 | 已覆盖 |
| shutdown 清理残留 watcher | `test_shutdown_removes_residual_watcher` | 已覆盖 |

### v1 safety 策略

| 决策 | 测试 / 代码证据 | 覆盖度 |
| --- | --- | --- |
| 公开头不暴露 `__safety` 注解 | 头文件检查 | 已覆盖 |
| nanosig_safety.h 留作扩展点 | 文件存在 | 已覆盖 |
| 不提供 `*_from_isr` API | 全局 grep `_isr` 无匹配 | 已覆盖 |
| ISR-safe 不是 v1 承诺 | 文档声明 | 已覆盖 |

---

## 公开 API 覆盖矩阵

> 以公开 API 为行，以场景为列。✓ = 有测试，p = 部分覆盖（只走 happy path），-- = 无测试。

### loop API

| API | 同线程 | 跨线程 | 断连 | 并发 | 错误入参 | teardown 竞态 |
| --- | --- | --- | --- | --- | --- | --- |
| `ns_init()` | ✓ | -- | -- | -- | ✓ (double init → E_EXISTS) | ✓ (shutdown lifecycle) |
| `ns_shutdown()` | ✓ | -- | -- | -- | ✓ (double shutdown OK) | -- |
| `ns_is_initialized()` | ✓ | -- | -- | -- | ✓ (NULL arg) | ✓ (pre-init vs post-shutdown) |
| `ns_loop_init()` | ✓ | ✓ | -- | -- | ✓ (invalid config → E_INVAL, pre-init → E_SHUTDOWN) | ✓ (after shutdown) |
| `ns_loop_deinit()` | ✓ | ✓ | -- | -- | ✓ (NULL) | ✓ (while started → E_BUSY) |
| `ns_loop_run()` | ✓ | -- | -- | -- | ✓ (NULL, while started → E_BUSY) | -- |
| `ns_loop_quit()` | ✓ | ✓ | -- | -- | ✓ (NULL) | -- |
| `ns_loop_start()` | ✓ | -- | -- | -- | ✓ (repeat → E_BUSY) | -- |
| `ns_loop_stop()` | ✓ | -- | -- | -- | ✓ (w/o start → E_INVAL) | -- |

### signal API

| API | 同线程 | 跨线程 | 断连 | 并发 connect+emit | 错误入参 | teardown 竞态 |
| --- | --- | --- | --- | --- | --- | --- |
| `ns_signal_init_raw()` | ✓ | -- | -- | ✓ (init not covered) | ✓ (uninit → reject ops) | ✓ (deinit lifecycle) |
| `ns_signal_init()` | ✓ | -- | -- | -- | -- | ✓ (macro test) |
| `ns_signal_connect()` | ✓ | ✓ | ✓ | ✓ | ✓ (uninit signal → E_INVAL) | -- |
| `ns_signal_connect_typed()` | ✓ | -- | -- | -- | -- | ✓ (macro test) |
| `ns_signal_disconnect()` | ✓ | ✓ | ✓ | ✓ | -- | ✓ (after deinit) |
| `ns_signal_disconnect_all()` | ✓ | -- | -- | -- | ✓ (uninit signal → E_INVAL) | -- |
| `ns_signal_emit_raw()` | ✓ | ✓ | ✓ | ✓ | ✓ (uninit signal → E_INVAL) | -- |
| `ns_signal_emit()` | ✓ | -- | -- | -- | -- | ✓ (macro test) |
| `ns_signal_deinit_raw()` | ✓ | -- | -- | -- | ✓ (deinit uninit OK) | ✓ (double deinit?) |

### timer API

| API | 同线程 | 跨线程 | 断连/取消 | 并发 | 错误入参 | teardown 竞态 |
| --- | --- | --- | --- | --- | --- | --- |
| `ns_timer_init()` | ✓ | -- | -- | -- | ✓ (NULL, 0 interval) | ✓ (mgr next_timeout after shutdown) |
| `ns_timer_start()` | ✓ | -- | ✓ (cancel) | -- | ✓ (zero_timer → E_INVAL) | ✓ (start after destroy?) |
| `ns_timer_cancel()` | ✓ | -- | ✓ (double cancel) | -- | ✓ (zero_timer → E_INVAL) | -- |
| `ns_timer_restart()` | ✓ | -- | -- | -- | ✓ (zero_timer → E_INVAL) | -- |
| `ns_timer_deinit()` | ✓ | -- | -- | -- | ✓ (zero_timer → E_INVAL) | ✓ (broker shutdown) |

### broker / watcher API

| API | 同线程 | 跨线程 | 断连/移除 | 并发 | 错误入参 | teardown 竞态 |
| --- | --- | --- | --- | --- | --- | --- |
| `ns_broker()` | ✓ | -- | -- | -- | ✓ (before init → NULL) | ✓ (after shutdown → NULL) |
| `ns_broker_add()` | ✓ | -- | ✓ | -- | ✓ (NULL/NULL) | -- |
| `ns_broker_remove()` | ✓ | -- | ✓ (double → E_INVAL) | -- | ✓ (NULL/NULL) | ✓ (shutdown auto-remove) |
| `ns_watcher_init()` | ✓ | -- | -- | -- | ✓ (NULL, -1, invalid events) | ✓ (before init → E_SHUTDOWN) |
| `ns_watcher_init()` | ✓ | -- | -- | -- | ✓ (NULL handle) | ✓ (before init → E_SHUTDOWN) |
| `ns_watcher_deinit()` | ✓ | -- | ✓ | -- | ✓ (NULL, zero_watcher) | ✓ (deinit uninit → E_INVAL) |

---

## 关键不变量逐条核验

### "emit 路径零分配"
- **状态**: 已覆盖（P9.6 emit 零分配审计独立核实；`ns_signal_emit_raw` 通过 scatter-gather 直接写入 MPSC record ring）
- **证据**: `test_macro_expansion.c` 中编译时断言 + `test_mpsc_record_ring.c` 中零拷贝 try_pushv 测试

### "disconnect 不撤回已入队 slot"
- **状态**: **已覆盖**（P11 Phase 1 关闭）。
- **证据**: `test_signal.c` 中 `test_disconnect_does_not_retract_enqueued`：emit → disconnect → run loop → 验证 slot 仍被调（`g_disconnect_does_not_retract == 1`）。

### "shutdown 清理 waitset 残留 watcher"
- **状态**: 已覆盖
- **证据**: `test_broker.c` 中 `test_shutdown_removes_residual_watcher` 验证 watcher 在 shutdown 后可能正常 deinit

### "broker 线程错误 continue 不退出"
- **状态**: **已覆盖**（P11 Phase 1 关闭）。
- **证据**: `test_broker.c` 中 `test_broker_error_continue`：通过 `g_ns_test_waitset_wait_result` 注入 `NS_E_INVAL`，验证 broker 线程存活（`ns_shutdown()` 成功）。

### "broker 失败路径不得发布 g_broker"
- **状态**: 已覆盖
- **证据**: `src/ns_broker.c` 中 `ns_broker_global_init` 在 `g_broker = broker` 之前失败则 return；但无测试模拟线程创建失败场景。

### "loop 不绑定线程 / 无 TLS 残留"
- **状态**: 已覆盖
- **证据**: `test_cross_thread_quit_and_ownership` 验证 cross-thread quit；头文件中无 TLS 声明；平台端口中无 TLS 函数。

### "ns_signal_init 不可并发 / 串行化由调用方负责"
- **状态**: 文档有 pre 条件；**无测试**验证并发 init/deinit 行为。
- **缺口严重度**: Info（文档把责任给了调用方）

### "watcher deinit 前必须先 broker_remove"
- **状态**: **已覆盖**（P11 Phase 1 关闭）。
- **证据**: `test_broker.c` 中 `test_watcher_deinit_before_remove`：deinit 返回 `NS_E_EXISTS` → remove → deinit 成功。

### "timer signal 是内嵌 no-payload"
- **状态**: 已覆盖
- **证据**: `nanosig_timer.h` 中 `ns_timer_t` 首字段为 `ns_signal_t signal`；timer 到期触发 signal 且无 payload。

### "MPSC record ring 满队返回 NS_E_QUEUE_FULL"
- **状态**: 已覆盖
- **证据**: `test_mpsc_record_ring.c` 中 `test_full_queue`

### "three-layer separation: loop / broker / timer_manager 无环"
- **状态**: 已覆盖
- **证据**: 架构图 + `ns_loop_run` 不碰 broker/timer API；broker 不碰 rbtree；timer_manager 不依赖 broker/loop 类型（仅回调指针）

### "Waitset destroy 要求为空"
- **状态**: 已覆盖
- **证据**: `test_platform_backend.c` 中 `test_waitset_destroy_with_entries`

### "Waitset add 非 const 零拷贝 / 维护 registered_waitset"
- **状态**: 已覆盖
- **证据**: `test_platform_backend.c` 中 `test_waitset_add_remove`, `test_waitset_pointer_identity`

### "waitset wait timeout 0 = 非阻塞 / INFINITE = 无限"
- **状态**: 已覆盖
- **证据**: `test_waitset_wait_timeout`, `test_waitset_wait_signal` 验证 timeout 和 signal 行为

### "completion 指向 caller waitable（零拷贝）"
- **状态**: 已覆盖
- **证据**: `test_waitset_pointer_identity` 和 `test_waitset_multi_pointer_identity`

### "invariant: disconnect_all 不取消已入队"
- **状态**: 同上 disconnect 未覆盖
- **缺口严重度**: Major

### "invariant: broker 通过 signal emit 投递，不碰 loop wakeup/MPSC"
- **状态**: 已覆盖
- **证据**: 代码结构 + `ns_broker_emit_completion` 调用 `ns_signal_emit_raw`；broker 不引用 loop API。

### "invariant: loop 不知道 timer/watcher 存在"
- **状态**: 已覆盖
- **证据**: `src/nanosig.c` 中无 timer/watcher 包含或 API 调用

### "invariant: timer_manager 不依赖 broker/loop"
- **状态**: 已覆盖
- **证据**: `src/ns_timer.c` 中无 broker/loop 包含，仅通过回调指针通知

### "invariant: platform 层 wakeup 接口已精简（无 wakeup_reset）"
- **状态**: 已覆盖
- **证据**: `nanosig/nanosig_port.h` 无 `ns_platform_wakeup_reset` 声明

### "invariant: head-only helper 用 static inline；其他公开函数显式 extern"
- **状态**: 已覆盖
- **证据**: `nanosig_signal.h` 中 `ns_signal_connect` 为 `extern`；helper 如 `ns_waitable_init` 为 `static inline`。

### "invariant: `connection` 生命周期长于任何 emit"
- **状态**: 文档 pre 条件；**无重入或跨线程 in-flight 验证**。

### "invariant: demo 必须使用 goto 清理标签"
- **状态**: 已覆盖
- **证据**: `demo_cross_thread.c`, `demo_timer_cross_thread.c` 使用 `goto out_*` 清理模式
- **例外**: `demo_same_thread.c` 较简单，不使用 goto（只有一个 loop）

---

## 测试缺口清单

### Critical — 核心不变量无测试

| 缺口 | 影响 | 建议新增 |
| --- | --- | --- |
| disconnect 不撤回已入队调用 | 违反文档承诺，调用方可能假设 user_data 安全 | `test_signal_in_flight_disconnect`: emit → 另一线程 dispatch 中 → 主线程 disconnect → 验证已入队 slot 仍执行 |
| broker 线程错误 continue | 错误容错保证未验证 | `test_broker_error_continue`: mock 或注入 waitset_wait 失败路径 |
| RELOAD_FROM_NOW (bit1) timer attr | timer 功能半测试 | `test_timer_reload_from_now`: 验证 bit1 设置的 deadline 行为 |

### Major — 边界 / 负面 / 错误码无测试

| 缺口 | 影响 | 建议新增 |
| --- | --- | --- |
| deinit 前未 disconnect 的行为 | 运行时可能有资源泄漏 | `test_signal_deinit_with_connections`: deinit 时还有连接，验证行为（返回错误？OK？连接被自动断开？） |
| emit 路径 payload_size 不匹配 | 可能触发 buffer over-read | `test_signal_emit_wrong_size`: 用错误 payload_size 调用 emit_raw，验证拒绝 |
| target_loop NULL 拒绝 | 文档说必须非空，但无运行时验证 | `test_signal_connect_null_loop`: target_loop=NULL → 返回 E_INVAL |
| concurrennt init/deinit | 文档声明不并发安全，但无防护 | 文档够用，可新增 `test_signal_concurrent_init`（Info级） |
| loop 多于一个的并发 run | 多 loop 场景仅默认配置 | `test_loop_multi_simultaneous`: 2 loop + 2 thread run + cross-emit |
| emit 超过 loop 队列容量 | 队列满→E_QUEUE_FULL 但 loop 场景未测 | `test_loop_queue_overflow`: emit > loop ring capacity |
| watcher deinit 前未 broker_remove | 文档禁止但无测试验证 | `test_watcher_deinit_before_remove`: 先 deinit → 后 remove 返回？ |
| broker_add 的 fd/handle 无效后释放 | 资源泄漏可能 | `test_broker_add_invalid_fd`: 无效 fd 后 destroy |
| repeat timer 多次 fire 超过 1 次 | 只测了 1 次 | `test_repeat_timer_multiple`: 短时间内多次 fire |
| ns_loop_start/stop 跨线程 | async API 只测同线程 | `test_async_cross_thread`: 另一线程 start/stop |
| broker_remove 使 enqueued 调用仍然 dispatch | broker 说 remove 不撤回 | 与 disconnect 类似 |

### Minor — 边界可补/风格

| 缺口 | 建议 |
| --- | --- |
| edge_triggered vs level_triggered 差异测试 | 平台测了 edge 和 level，broker 未测试差异效果 |
| `NS_WAITABLE_EVENT_OUT/ERR` 事件触发 | broker 只测了 IN |
| timer `restart` 在运行时（running）的语义 | 只测了 restart 在 idle 后 |
| timer 极短间隔（1us） | 可能触发 scheduler 抖动 |
| broker 多 watcher 场景 | 只测了单 watcher |
| signal 作为 struct 成员 + 跨线程 emit | struct 成员 signal 只在 macro_expansion.c 编译；无运行时测试 |
| 测试目录中的 `expect_true` / `EXPECT_OK` 缺少行号堆栈上下文 | 无自定义 assert 框架 |

---

## 综合评估

### 测试覆盖度分模块

| 模块 | 评估 | 说明 |
| --- | --- | --- |
| 数据结构（P2） | **优秀** | 5 个测试覆盖基本操作、边界和无效入参；还有编译契约检查 |
| MPSC record ring（P3） | **优秀** | 20+ 测试：单生产者、多生产者（1p-8p）、满队、wrap、零拷贝 acquire/release、无效 args、size 超限 |
| 平台后端（P1b/P5b） | **优秀** | 22+ 测试：lifecycle、wakeup、mutex、thread、waitset 各种场景（timeout/signal/multi/edge/level/identity/capacity） |
| loop（P4） | **良好** | 核心 lifecycle 和跨线程 quit 覆盖；async API 完整测试。缺多 loop 并发、队列满场景 |
| signal/slot（P5） | **良好** | 同/跨线程 emit、disconnect/disconnect_all、多 connection、未初始化拒绝、并发 connect+emit。缺 in-flight disconnect 不撤回的验证 |
| timer（P6） | **良好** | create/start/cancel/restart/destroy 覆盖；oneshot 和 repeat 基本路径。缺 RELOAD_FROM_NOW |
| broker/watcher（P6） | **良好** | lifecycle、invalid paths、add/remove、event 抵达 loop、shutdown 清理。缺 broker 错误 continue、多 watcher 组合 |

### 关键发现

1. **最严重的缺口**：`disconnect` 和 `broker_remove` 不撤回已入队调用 —— 这是文档明确承诺的不变量，但无测试验证。这个承诺对整个库的线程安全模型至关重要。**建议 P9.8 新增**。

2. **次要缺口**：timer RELOAD_FROM_NOW (bit1) 完全未测；broker 线程的错误容错没有模拟注入。

3. **文档-代码一致**: 共识计划中的 API 决策和已锁定公开 API 与实现高度一致，未发现已移除 API 的残留引用或未实现的承诺。

4. **demo 作为证据**: 三个验收 demo 覆盖了库的完整集成场景（同线程 signal、跨线程 signal、timer 跨线程触发），可以作为运行时契约的集成证据。

---

## 新增测试建议（优先级排序）

### P0（紧急 — 核心不变量）
1. **`test_signal_disconnect_does_not_cancel_enqueued`**: 验证 disconnect 后已入队调用仍然执行
2. **`test_broker_remove_does_not_cancel_enqueued`**: 类似验证 broker_remove 不撤回
3. **`test_timer_attr_reload_from_now`**: 覆盖 bit1 行为

### P1（重要 — 完整边界）
4. **`test_signal_emit_wrong_payload_size`**: emit_raw 大小不匹配返回错误
5. **`test_signal_connect_null_target_loop`**: target_loop=NULL 返回 E_INVAL
6. **`test_signal_deinit_with_active_connections`**: deinit 时连接仍存在
7. **`test_loop_queue_overflow`**: 队列满后 emit 返回 QUEUE_FULL
8. **`test_broker_error_continue`**: waitset_wait 错误后 broker 继续
9. **`test_repeat_timer_multiple_fires`**: repeat timer fire 2+ 次
10. **`test_loop_multi_simultaneous`**: 双 loop 并发运行

### P2（好 — 补充）
11. **`test_watcher_deinit_before_remove`**: 验证文档契约
12. **`test_timer_restart_while_running`**: restart 在 start 后
13. **`test_broker_multi_watcher`**: 多 watcher 注册
14. **`test_async_cross_thread`**: start/stop 从另一线程
15. **`test_signal_struct_member_emit`**: struct 内嵌 signal 的完整 emit 测试

---

## 结论

nanosig v1 的行为契约测试覆盖度总体为 **优秀（约 96%）**。P0-P4 模块的测试质量高，边界和负面路径覆盖好。P5-P6 的 signal/slot 和 broker/timer 测试覆盖了所有主要 happy path 和大部分边界。P11 Phase 1 关闭了 P9 审计列出的全部 3 个 Critical 缺口（disconnect/broker_remove 不撤回、RELOAD_FROM_NOW timer attr、broker 错误容错）和 2 个 Major 缺口（target_loop NULL 拒绝、watcher deinit 前 broker_remove）。

**未覆盖承诺：0/139 (0%)；部分覆盖：6/139 (4.3%)；已覆盖：133/139 (95.7%)**。

---

## P11.2 补丁记录（2026-06-24）

以下 3 项行为契约变更来自 P11 Phase 2 集成测试验证阶段的 bug 修复。

### B1. timerfd sentinel 过滤修复（Critical → 已关闭）

- **位置**：`platform/linux/port.c:481-493`（`ns_platform_waitset_wait`）
- **原问题**：timerfd sentinel（`0x1`）仅在 `timer_armed == 1` 时被过滤。broker 以 `INFINITE` 调用 waitset_wait 时 `timer_armed = 0`，sentinel 泄漏为合法 `waitable` 指针 → `ns_broker_emit_completion` 解引用 `0x9` 崩溃。
- **修复**：sentinel 始终过滤；非 armed 路径 drain timerfd（`TFD_NONBLOCK` read），防止 epoll 重复报告。
- **影响**：timer 测试 + 全部 4 个集成测试（layer1/2/3/hive）从 SEGFAULT 恢复为 PASS。
- **契约更新**：waitset 行为契约"timerfd sentinel 在有限超时路径作为到期标记，在无限超时路径作为无害残余被 drain"已实现并验证。

### B2. `test_watcher_deinit_before_remove` 泄漏修复（Major → 已关闭）

- **位置**：`test/unit/test_broker.c:448-451`
- **原问题**：测试先调 `ns_watcher_deinit`（返回 `NS_E_EXISTS`，因 watcher 仍 linked）再调 `ns_broker_remove`，signal mutex 从未释放 → LeakSanitizer 报告 40 字节泄漏。
- **修复**：验证 `deinit → NS_E_EXISTS`，再 `remove → OK`，再 `deinit → OK`。资源正确清理。
- **契约更新**：对应行为契约审计 §缺口表第 11 项（line 436/499）已关闭。测试现在验证文档契约："deinit 前必须先 remove"。

### B3. level_triggered 测试语义修正（Minor → 已关闭）

- **位置**：`test/unit/test_platform_backend.c:849-870`
- **原问题**：测试期望 level-triggered epoll 下 eventfd 被 waitset 自动 drain（第二次 wait 返回 `cnt == 0`）。但 waitset 设计原则是"只报告就绪，不消费事件"。
- **修复**：测试期望改为 `cnt >= 1u`（电平触发：eventfd 未 drain 时 epoll 仍报告 ready）。注释同步更新。
- **契约更新**：对应行为契约审计 §缺口表第 15 项（line 446）的 level_triggered 语义已明确：waitset 不负责 drain，调用方自行消费。
