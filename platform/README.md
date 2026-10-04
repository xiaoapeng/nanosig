# nanosig 平台抽象层

`platform/` 是 nanosig v1 的唯一 OS 耦合点。核心实现只能通过 `nanosig/nanosig_port.h`
使用平台能力，不能在 `src/` 或公开头文件中直接包含 OS 头文件或写平台分支。

四后端：Linux（epoll + pthread）、macOS（kqueue + pthread）、
Windows（WaitForMultipleObjects + SRWLOCK）、FreeRTOS（queue set + 每对象独立
二值信号量）。桌面三平台后端必须同步推进，不允许一个 OS 领先另一个完整阶段。
FreeRTOS 后端面向 RTOS 目标，采用 queue set 语义，与桌面后端不是 lockstep
关系（见下面“FreeRTOS 后端”节）。

## loop-only 原语

`nanosig/nanosig_port.h` 冻结以下能力：

- 平台生命周期：`ns_platform_init`、`ns_platform_shutdown`。
- 内存：`ns_platform_alloc`、`ns_platform_free`。
- wakeup：创建、销毁、signal、单个等待。
- 同步：mutex 的创建、销毁、加锁和解锁。
- 时间：单调微秒时钟。
- 线程：创建、join。

## waitset 原语

`nanosig/nanosig_port.h` 追加以下 waitset 能力：

- waitable：可等待描述符，包含平台句柄、用户标签、注册状态和事件配置。
- waitset：一次等待多个事件源的容器。
- completion：wait 返回的事件结果（waitable + events + user_data）。
- 事件位：`NS_WAITABLE_EVENT_IN`、`NS_WAITABLE_EVENT_OUT`、`NS_WAITABLE_EVENT_ERR`。

接口：

- `ns_platform_waitset_create` / `ns_platform_waitset_destroy`
- `ns_platform_waitset_add`（注册 waitable；同一 waitable 已注册时返回 `NS_E_EXISTS`）
- `ns_platform_waitset_remove`（移除 waitable，未注册返回 `NS_E_INVAL`）
- `ns_platform_waitset_wait`（等待事件，timeout 映射到平台原生等待能力）

### `ns_platform_waitset_remove` 契约

`ns_platform_waitset_remove` 是平台中立契约，所有后端必须满足：

- 返回值只有两种：`NS_OK`（移除成功）和 `NS_E_INVAL`（waitable 为 NULL、句柄无效、
  或未注册到该 waitset）。**不得**向调用方暴露 `NS_E_BUSY` 或其他失败码。
- 移除时，后端负责该 waitable 的**残留就绪 token 生命周期**：后端在内部丢弃或
  消化该 waitable 尚未被 `ns_platform_waitset_wait` 取走的就绪状态，使得移除返回后
  该 waitable 不再产生 completion。这段内部处理对调用方不可见。
- 触发侧（如 event 的 signal 侧）与剩余 waitable 不受影响；同一 waitset 中其他
  已注册项的待决就绪状态必须被保留。
- 桌面后端天然满足本契约（`epoll_ctl DEL` / `EV_DELETE` / WFMO 重建数组会一并撤销
  未决状态）。FreeRTOS queue set 后端需要额外处理残留 token，见“FreeRTOS 后端”节。

因此 `src/ns_broker.c` 的 `ns_platform_waitset_remove` 调用点无需感知平台差异，
也不需要 drain-重试或 `NS_E_BUSY` 分支。

waitset 不包含任何事件源特定函数，也不与 wakeup 耦合。上层直接构造
`ns_platform_waitable_t`（Linux/macOS 填 `fd`，Windows 填 `handle`）注册到 waitset。
tcp/udp socket 等原始 fd 也可直接构造 waitable 注册。

timeout 语义：微秒输入，后端映射到平台原生等待能力。保证等待至少 `timeout_us`，
实际可能略长。晚 fire 安全，向下取整导致忙等才是 bug。

## 后端映射

Linux 后端：

- mutex 使用 POSIX pthread_mutex。
- 线程使用 pthread_create / pthread_join。
- wakeup 使用 eventfd、pipe 或等价单 wakeup 机制。
- 单调时间使用 `clock_gettime` 的 monotonic 时钟。
- 内存分配集中在平台层封装。
- waitset 使用 `epoll_create1` / `epoll_ctl` / `epoll_wait`。
- `edge_triggered=1` 映射 `EPOLLET`。

macOS 后端：

- mutex 使用 POSIX pthread_mutex。
- 线程使用 pthread_create / pthread_join。
- wakeup 使用 kqueue `EVFILT_USER`，通过 wakeup 自身的 kqueue fd 暴露为 waitable。
- 单调时间使用 `clock_gettime` 的 monotonic 时钟。
- 内存分配集中在平台层封装。
- waitset 使用 `kqueue` / `kevent`，普通 fd 通过 `EVFILT_READ` / `EVFILT_WRITE` 注册。
- timeout 直接使用 `kevent` 的 `timespec` 参数，不需要额外 timer waitable。
- `edge_triggered=1` 映射 `EV_CLEAR`。

Windows 后端：

- 线程使用 CreateThread / WaitForSingleObject(join)。
- wakeup 使用 auto-reset event。
- 单 wakeup 等待使用 WaitForSingleObject 或等价机制。
- 锁使用 SRWLOCK 或等价 mutex 原语。
- 单调时间使用 QueryPerformanceCounter。
- 内存分配集中在平台层封装。
- waitset 使用 `WaitForMultipleObjects`，容量上限 64 handle。
- `edge_triggered` 参数忽略（WFMO 不支持）。

桌面后端必须同步推进，不能让一个 OS 领先另一个完整阶段。

FreeRTOS 后端：

- 线程使用 `xTaskCreate`；`ns_platform_thread_join` 用一次性 join 信号量等待任务结束，
  被 join 的任务在 `vTaskDelete(NULL)` 之前 signal 该信号量。
- mutex 使用 `xSemaphoreCreateMutex`（`xSemaphoreTake` / `xSemaphoreGive`）。
- wakeup 使用**每对象独立的二值信号量**；`ns_platform_wakeup_signal` 调用
  `xSemaphoreGive`（已满视为成功）。**禁止**使用 task notification 实现 wakeup：
  loop 与 broker op-proxy 的唤醒共享每任务 notification value 会互相误消费并触发
  HardFault。
- 单调时间使用 `xTaskGetTickCount` 乘以 tick 周期（微秒）。
- 内存分配使用 `pvPortMalloc` / `vPortFree`。
- waitset 使用 `xQueueCreateSet` 的 queue set：每个 waitable 的 `primitive.handle`
  是调用方提供的 queue-set-capable FreeRTOS 对象（二值/计数信号量或队列）。
  `waitset_wait` 用 `xQueueSelectFromSet` 取就绪成员并映射回 waitable，
  **不**从成员对象消费数据。
- `ns_platform_event_t` 用二值信号量实现：`waitable.primitive.handle` 与
  `signal_handle.handle` 指向同一信号量；`signal` = `xSemaphoreGive`，
  `drain` = 非阻塞 `xSemaphoreTake`。
- FreeRTOS 后端只支持 `NS_WAITABLE_EVENT_IN`；含 OUT/ERR 的 waitable 在
  `waitset_add` 返回 `NS_E_INVAL`。
- queue set 容量有限：`waitset_add` 按“Σ成员容量 + max(成员容量) ≤ 固定容量”校验，
  超出返回 `NS_E_TOO_MANY_HANDLES`。加入 set 的成员必须为空，否则返回 `NS_E_INVAL`
  （FreeRTOS queue set 特有语义）。
- `waitset_remove` 先 drain 被移除成员、再 `xQueueRemoveFromSet`；queue set 已存在的
  残留 token 由后端内部消化，调用方只见 `NS_OK`/`NS_E_INVAL`（见上节契约）。
- `waitset_destroy` 仍要求在无注册项时调用（有注册项返回 `NS_E_EXISTS`）。

## 生命周期和所有权

平台层 handle 都是不透明类型。创建函数返回的 handle 归调用方所有，必须用匹配
destroy 函数释放。

同一 `ns_platform_waitable_t` 同一时间只能注册到一个 waitset；调用方必须先
`ns_platform_waitset_remove`，再把它注册到另一个 waitset。带注册项的 waitset
destroy 会返回 `NS_E_EXISTS`，避免 waitable 内部注册状态悬挂。

wakeup、mutex、线程和 waitset 的 create/destroy 可以分配和释放资源。emit 路径只能
使用已经创建好的资源，不能触发平台分配。

## 等待语义

带超时的等待函数使用微秒。`NS_PLATFORM_WAIT_INFINITE_US` 表示无限等待，0 表示非阻塞。

单 wakeup 等待（`ns_platform_wakeup_wait`）：等待操作本身成功时返回 `NS_OK`，
具体结果通过 `ns_platform_wait_result_t` 区分 signaled 与 timeout。

waitset 等待（`ns_platform_waitset_wait`）：timeout 映射到平台原生等待能力；
Linux 使用 `timerfd`，macOS 使用 `kevent` timeout，Windows 使用 WaitableTimer。
completion 数组由调用方提供，`out_count` 返回实际触发数。Windows 后端单次 wait
最多返回 1 个 completion（auto-reset event 语义）。

## FreeRTOS 后端

FreeRTOS 后端是 v1 的第四个后端，使用 queue set 语义（见“后端映射”节），
不是空目录占位。它面向真实 RTOS 目标构建，通过顶层 `NANOSIG_PLATFORM=freertos`
选择，并由 `NANOSIG_FREERTOS_INCLUDE_DIRS` 提供 FreeRTOS SDK 头文件路径。
FreeRTOS 后端复用 `primitive.handle` 承载 queue-set 成员句柄。

RTOS ISR 安全是后续课题；v1 FreeRTOS 后端不承诺 ISR 上下文调用。

### host POSIX harness 覆盖面

`NANOSIG_BUILD_FREERTOS_POSIX=ON` 会用钉版 FreeRTOS-Kernel（V11.1.0）的 GCC/Posix
port 在开发主机上编译该后端，并注册 `test/freertos/` 下的契约测试
（`nanosig_test_freertos_waitset` / `nanosig_test_freertos_broker`）。该 harness **只覆盖**
队列集与信号语义：waitable/waitset 的 add/remove/wait、event
signal/drain/deinit-busy、stale-token 不产生假完成、broker 端到端往返。

明确**不覆盖**（归下游 MCXN947 硬件冒烟）：

- 线程栈深度/溢出行为（`configCHECK_FOR_STACK_OVERFLOW` 停机）与任务优先级抢占细节。
- `portTICK_PERIOD_MS` 与 `xTaskGetTickCount` 的真实 tick 精度。
- `configASSERT` 停机路径（host harness 会把越界挡在 `add` 返回错误，而非触发断言）。
- 单核竞态与 SMP 宿主差异。
- task notification 共享悬垂导致的硬件 HardFault 场景（后端已从实现上剔除该路径）。

## 新增后端清单

新增一个平台后端时，需要：

1. **实现文件**：在 `platform/` 下创建 `<platform>/port.c`，实现 `nanosig/nanosig_port.h` 中所有 `extern` 函数（`ns_platform_*`）。
2. **CMake 注册**：在顶层 `CMakeLists.txt` 的 `NANOSIG_PLATFORM_SOURCES` 条件块中增加分支。
3. **编译检查**：`cmake --build` 零警告通过。
4. **平台契约测试**：`ctest -R nanosig_test_platform_backend` 全通过（lifecycle / add-remove / wait timeout / wait signal / multi waitable）。
5. **完整构建 + 测试**：`cmake --build <preset> --target api-compile-checks` + `ctest <preset>` 全通过。
6. **bench 基线**（可选）：跑 1 轮 bench 归档到 `bench/results/`。

新增后端**不允许**：
- 在 `src/` 或 `include/nanosig/` 中新增 OS 分支。
- 修改 `nanosig/nanosig_port.h` 的接口签名（只能新增扩展点）。
- 修改其他后端的实现文件。
