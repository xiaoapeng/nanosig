# nanosig 平台抽象层

`platform/` 是 nanosig v1 的唯一 OS 耦合点。核心实现只能通过 `nanosig/nanosig_port.h`
使用平台能力，不能在 `src/` 或公开头文件中直接包含 OS 头文件或写平台分支。

五后端：Linux（epoll + pthread）、macOS（kqueue + pthread）、
Windows（WaitForMultipleObjects + SRWLOCK）、FreeRTOS（queue set + 每对象独立
二值信号量）、Zephyr（k_poll + k_poll_signal + 每对象独立 k_sem）。桌面三平台
后端必须同步推进，不允许一个 OS 领先另一个完整阶段。FreeRTOS 与 Zephyr 后端
面向 RTOS 目标，与桌面后端不是 lockstep 关系（见下面“FreeRTOS 后端”和
“Zephyr 后端”节）。

## loop-only 原语

`nanosig/nanosig_port.h` 冻结以下能力：

- 平台生命周期：`ns_platform_init`、`ns_platform_shutdown`。
- 内存：`ns_platform_alloc`、`ns_platform_free`。
- wakeup：`init`、`deinit`、signal、单个等待。
- 同步：mutex 的 `init`、`deinit`、加锁和解锁。
- 时间：单调微秒时钟。
- 线程：`init`、join（`ns_platform_thread_join` 保留 join 命名：它确实阻塞至线程结束）。

平台对象（wakeup / mutex / thread / waitset）**全部由调用方拥有存储**：
`ns_platform_*_init` 在调用方给出的对象上初始化，`ns_platform_*_deinit` 释放其内部资源，
两者都**不**分配或释放对象本身。对应的类型由构建内头
`platform/<os>/nanosig_platform_port.h` 以**透明别名**形式提供（见“平台类型注入”节）。

## waitset 原语

`nanosig/nanosig_port.h` 追加以下 waitset 能力：

- waitable：可等待描述符，包含平台句柄、用户标签、注册状态和事件配置。
- waitset：一次等待多个事件源的容器。
- completion：wait 返回的事件结果（waitable + events + user_data）。
- 事件位：`NS_WAITABLE_EVENT_IN`、`NS_WAITABLE_EVENT_OUT`、`NS_WAITABLE_EVENT_ERR`。

接口：

- `ns_platform_waitset_init` / `ns_platform_waitset_deinit`
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
- **该就绪状态仅指 waitset 内部记账，不涉及成员对象承载的数据**：fd / socket / HANDLE /
  `k_poll` 对象的未读数据不受 remove 影响，remove 既不消费也不 reset。FreeRTOS queue set
  是**唯一例外（已知取舍）**：后端受 `xQueueRemoveFromSet`“成员必须为空”约束，为摘除成员
  必须先排空它，因此对**数据队列**成员会一并丢弃队列中未取走的消息。该清空是向 queue set
  约束妥协、而非有意设计（见“FreeRTOS 后端”节）。
- 触发侧（如 event 的 signal 侧）与剩余 waitable 不受影响；同一 waitset 中其他
  已注册项的待决就绪状态必须被保留。
- 桌面后端天然满足本契约（`epoll_ctl DEL` / `EV_DELETE` / WFMO 重建数组会一并撤销
  未决状态）。FreeRTOS queue set 后端需要额外处理残留 token，见“FreeRTOS 后端”节。

因此 `src/ns_broker.c` 的 `ns_platform_waitset_remove` 调用点无需感知平台差异，
也不需要 drain-重试或 `NS_E_BUSY` 分支。

waitset 不包含任何事件源特定函数，也不与 wakeup 耦合。上层直接构造
`ns_platform_waitable_t`（把平台句柄赋给 `primitive`）注册到 waitset。
tcp/udp socket 等原始 fd 也可直接构造 waitable 注册。

句柄有效性用平台头提供的 `ns_waitable_handle_is_valid(h)` 判定：POSIX 为 `h >= 0`，
Windows 为 `h != NULL`，RTOS 为 `h != NULL && h != (void *)~0`。

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
- 单调时间以 `xTaskGetTickCount` 为源：后端把可能回绕的 `TickType_t` 扩展为 64 位累加 tick
  （用无符号回绕差值 `now - last` 累加，`taskENTER_CRITICAL`/`taskEXIT_CRITICAL` 保护），
  再换算微秒，因此 tick 回绕后仍保持单调。**前提**：FreeRTOS 自身的 tick 回绕不能太快——
  相邻两次采样之间必须最多发生一次回绕（采样间隔小于一个 `TickType_t` 回绕周期），否则会
  少计整周期、表现为时间跳变。典型 32 位 tick @`configTICK_RATE_HZ=1000` 回绕周期约 49.7 天；
  `configUSE_16_BIT_TICKS=1` 时周期仅约 65.5 秒 @1kHz，需相应提高采样频率或改用更宽的 tick。
- 内存分配使用 `pvPortMalloc` / `vPortFree`。
- waitset 使用 `xQueueCreateSet` 的 queue set：每个 waitable 的 `primitive`
  是调用方提供的 queue-set-capable FreeRTOS 对象（二值/计数信号量或队列）。
  `waitset_wait` 用 `xQueueSelectFromSet` 取就绪成员并映射回 waitable，
  **不**从成员对象消费数据。
- `ns_platform_event_t` 用二值信号量实现：`waitable.primitive` 与
  `signal_handle` 指向同一信号量；`signal` = `xSemaphoreGive`，
  `drain` = 非阻塞 `xSemaphoreTake`。
- FreeRTOS 后端只支持 `NS_WAITABLE_EVENT_IN`；含 OUT/ERR 的 waitable 在
  `waitset_add` 返回 `NS_E_INVAL`。
- `edge_triggered` 参数忽略（同 Windows/Zephyr）：queue set 没有原生电平/边沿模式，
  其就绪是“每次 send/give 记一次就绪”的 token 语义——`waitset_wait` 取走 token 后
  不消费成员数据，也不对仍非空的成员重复上报。因此它既非可切换的电平也非严格边沿；
  调用方应把每次就绪的成员处理到空，否则已就绪但未处理的剩余部分要等下一次
  `xQueueSend`/`xSemaphoreGive` 才会再次上报（按电平假设的 `wait→处理一部分→再 wait`
  会漏掉剩余就绪）。
- queue set 容量有限：`waitset_add` 按“Σ成员容量 + max(成员容量) ≤ 固定容量”校验，
  超出返回 `NS_E_TOO_MANY_HANDLES`。加入 set 的成员必须为空，否则返回 `NS_E_INVAL`
  （FreeRTOS queue set 特有语义）。
- `waitset_remove`：受 queue set 约束（`xQueueRemoveFromSet` 要求成员为空），后端先**排空
  被移除成员**再 `xQueueRemoveFromSet`；queue set 已存在的残留 token 由后端内部消化，
  调用方只见 `NS_OK`/`NS_E_INVAL`。**注意（已知取舍）**：对**数据队列**成员，排空意味着
  remove 会丢弃队列中所有未取走的消息（payload）——这是为满足 `xQueueRemoveFromSet`“成员
  必须为空”而做的妥协，而非有意设计；需要保留数据的调用方必须先自行取走。该行为是
  FreeRTOS 后端特有，其它后端 remove 不消费成员数据（见上节契约）。
- `waitset_deinit` 仍要求在无注册项时调用（有注册项返回 `NS_E_EXISTS`）。

## 生命周期和所有权

平台对象都是**调用方拥有存储**的透明别名类型（见“平台类型注入”节）。`init` 在调用方
提供的存储上初始化（可能申请内部资源，如 fd / 信号量 / kqueue 实例），`deinit` 释放
这些内部资源。两者都不 malloc/free 对象本身，因此：

- 对象可以是栈、静态或用户结构体成员；**库自身不持有它们的存储**。
- `init` 失败时对象状态未定义，**不得**对其调用 `deinit`（直接返回即可）。
- `deinit` 要求对象已初始化，且其内部资源已释放干净（如 waitset 无注册项、event 无
  所属 waitset）；违约视为 UB，不提供“未初始化 / 已销毁”的状态查询。
- 所有 `ns_*_init` / `ns_*_deinit` 都要求相关中间资源此前已释放。`ns_init` /
  `ns_shutdown` 不能与任何其他 nanosig API 并发调用，且必须在其资源全部释放之后进行，
  违约视为 UB。

同一 `ns_platform_waitable_t` 同一时间只能注册到一个 waitset；调用方必须先
`ns_platform_waitset_remove`，再把它注册到另一个 waitset。带注册项的 waitset
`deinit` 会返回 `NS_E_EXISTS`，避免 waitable 内部注册状态悬挂。

平台对象存储必须由调用方提供，因此 `init` 只能创建**已在调用方存储中**的资源；emit
路径只能使用已初始化的资源，不能触发平台分配。

## 平台类型注入

平台类型（`ns_waitable_handle_t` + wakeup / mutex / thread / waitset）定义在五个
**构建内头** `platform/{linux,macos,windows,freertos,zephyr}/nanosig_platform_port.h`
中，每个头只描述一个后端。构建系统通过编译定义 `NANOSIG_PLATFORM_HEADER` 指定当前
平台的头名，`nanosig_port.h` 直接 `#include NANOSIG_PLATFORM_HEADER`，**自身不含任何
平台判定宏**：

- 顶层 `CMakeLists.txt` 设置 `NANOSIG_PLATFORM_DIR` + `NANOSIG_PLATFORM_HEADER_DEFINE`，
  以 PUBLIC 方式把 include 路径与宏一起传播给库和所有消费者。
- Zephyr 走自己的 module 入口（`zephyr/CMakeLists.txt`）设置同名宏。
- 这些头**不参与安装**：`include/` 之外不新增 `install()` 规则，消费者必须经由
  构建系统（CMake target 或 Zephyr module）获得平台类型。

因此平台头可以传递平台 SDK 头（`pthread.h` / `windows.h` / `FreeRTOS.h` /
`zephyr/kernel.h`）：消费者与库必须看到**同一份**类型定义。

### 别名带来的取舍

平台类型是内核原语的**透明别名**（如 POSIX 上 `ns_platform_wakeup_t` 就是 `int` fd，
Windows 上就是 `HANDLE`，Zephyr 上就是 `struct k_sem`）。好处是零包装、零额外堆分配；
代价是：

- 别名之间不再有类型区分（POSIX 上 wakeup / mutex 之外的 `ns_waitable_handle_t`、
  `ns_platform_wakeup_t` 都是 `int`），误用不会被编译器发现。
- 因为没有指针可判空，**“是否已初始化”不能靠对象自身的哨兵值判断**。库内需要该信息
  的位置使用独立的位/布尔成员（如 `ns_signal_t.mutex_initialized`、
  `struct ns_loop.async_started`、`ns_timer_mgr.initialized`）。
- 后端实现直接读句柄本身（POSIX 写 `(*wakeup)`，Windows 写 `(*wakeup)` /
  直接用 `mutex`，Zephyr 直接用 `wakeup` / `mutex`）。

## 等待语义

带超时的等待函数使用微秒。`NS_PLATFORM_WAIT_INFINITE_US` 表示无限等待，0 表示非阻塞。

**空 waitset 的例外**：`ns_platform_waitset_wait` 在 waitset 为空（调用时无任何已注册
waitable）且 timeout 为 `NS_PLATFORM_WAIT_INFINITE_US` 时不会真正永久阻塞——调用时没有
事件源，永久阻塞只会静默挂死线程。此时后端把等待封顶到 `NS_PLATFORM_WAIT_EMPTY_MAX_US`
（默认 1 秒，可编译期覆盖）后返回 `NS_OK`，让调用方周期性重估状态。封顶是等待时间的
**上界**而非固定睡眠：等待期间若另一线程注册的 waitable 就绪，后端可能提前返回该
completion（Windows/Zephyr 用调用时快照，看不到后续注册）。waitable 非空时不受影响，
仍是真无限等待；有限超时本就有界，也不受影响。

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
FreeRTOS 后端复用 `primitive` 承载 queue-set 成员句柄。

RTOS ISR 安全是后续课题；v1 FreeRTOS 后端不承诺 ISR 上下文调用。

### host POSIX harness 覆盖面

`NANOSIG_BUILD_FREERTOS_POSIX=ON` 会用钉版 FreeRTOS-Kernel（V11.1.0）的 GCC/Posix
port 在开发主机上编译该后端，并注册 `test/freertos/` 下的契约测试
（`nanosig_test_freertos_waitset` / `nanosig_test_freertos_broker`）。该 harness **只覆盖**
队列集与信号语义：waitable/waitset 的 add/remove/wait、event
signal/drain/deinit-busy、stale-token 不产生假完成、broker 端到端往返；并注册编译期平台
契约检查 `nanosig_api_check_platform_contract`（带 FreeRTOS SDK include 目录的
`-fsyntax-only` 检查，随 `ctest` 执行）。

Sanitizer 覆盖：`freertos-debug-asan` / `freertos-debug-ubsan` presets 在该 harness 上
分别启用 ASAN / UBSAN（macOS 实测通过）。TSAN 不可用——把 `NANOSIG_ENABLE_TSAN=ON`
加进 freertos 构建后 POSIX port 起不来（无输出即挂死），故见下方不覆盖清单。

明确**不覆盖**（归下游 MCXN947 硬件冒烟）：

- 线程栈深度/溢出行为（`configCHECK_FOR_STACK_OVERFLOW` 停机）与任务优先级抢占细节。
- `portTICK_PERIOD_MS` 与 `xTaskGetTickCount` 的真实 tick 精度。
- `configASSERT` 停机路径（host harness 会把越界挡在 `add` 返回错误，而非触发断言）。
- 单核竞态与 SMP 宿主差异。
- task notification 共享悬垂导致的硬件 HardFault 场景（后端已从实现上剔除该路径）。
- tick 回绕：POSIX port 的 `TickType_t` 为 `unsigned long`（64 位宿主上为 64 位），
  回绕扩展逻辑与其"回绕不能太快"前提无法在 host harness 触发，需真实 32/16 位 tick 目标验证。
- TSAN：freertos harness 在 `NANOSIG_ENABLE_TSAN=ON` 下 POSIX port 挂死（无输出），故不提供
  `freertos-debug-tsan` preset；线程竞态检查由 host 后端（linux-debug-tsan）承担。

## Zephyr 后端

Zephyr 后端是 v1 的第五个后端，使用 `k_poll` 语义，面向 Zephyr RTOS 目标。
它以 **Zephyr module** 形式接入（仓库根 `zephyr/module.yml`），**不**通过顶层
`NANOSIG_PLATFORM=zephyr` 构建（该取值只打印引导信息）。后端复用
`primitive` 承载 `k_poll` 可等待对象指针。

- 内存使用 `k_malloc` / `k_free`；`ns_platform_alloc` 即 `k_malloc`。
- 线程使用 `k_thread_create` / `k_thread_join`，栈用 `K_KERNEL_STACK_MEMBER`
  内嵌在 `struct ns_platform_thread` 中（不依赖 `CONFIG_DYNAMIC_THREAD`）。
- 线程对象是**唯一不由调用方拥有存储**的平台对象：它内嵌内核栈、对齐要求为
  `Z_KERNEL_STACK_OBJ_ALIGN`，故由 port 按该对齐分配（`ns_platform_thread_init`）、
  由 `ns_platform_thread_join` 回收；调用方仍只声明 `ns_platform_thread_t` 变量并取
  地址。这样该对齐要求不会经堆上的 `ns_loop_t` / broker 传给通用分配器——否则开
  MPU 栈保护（对齐等于栈尺寸）时，哈希表节点、格式化缓冲等小对象分配都会按栈尺寸
  对齐，小堆会提前撞上 `NS_E_NOMEM`。
- mutex 使用 `k_mutex`。
- wakeup 使用**每对象独立 `k_sem`**（上限 1）；`signal` = `k_sem_give`
  （已满视为成功），`wait` = `k_sem_take`。
- 单调时间使用 `k_uptime_ticks` 经 `k_ticks_to_us_floor64` 换算。
- waitset 使用 `k_poll`：每次 `wait` 由活跃 slot 重建 `struct k_poll_event[]`，
  固定 `K_POLL_TYPE_SIGNAL` + `K_POLL_MODE_NOTIFY_ONLY`。
- `ns_platform_event_t` 用 `k_poll_signal` 实现：`waitable.primitive` 与
  `signal_handle` 指向同一 `k_poll_signal`；`signal` =
  `k_poll_signal_raise`（`-EAGAIN` 视为成功），`drain` = `k_poll_signal_reset`。
- Zephyr 后端只支持 `NS_WAITABLE_EVENT_IN`，且 v1 只接受 signal 类
  （`k_poll_signal`）waitable；含 OUT/ERR 的 waitable 在 `waitset_add` 返回
  `NS_E_INVAL`。异构 waitable（`k_sem`/FIFO/pipe/socket）为非目标。
- `k_poll_signal` 是**电平触发**：`raise` 后保持 signaled，直到 `event_drain`
  调用 `k_poll_signal_reset`。`reset` 是唯一复位点，且按 Zephyr 建议只应由 poller
  线程（调用 `ns_platform_waitset_wait` 的线程）执行。
- `waitset_remove` 只摘除 slot、**不** reset 底层信号；移除后该 waitable 不再产生
  completion，但若仍为 signaled，re-add 后会立即再次触发，直到调用方 drain。
  返回值仍只有 `NS_OK`/`NS_E_INVAL`。
- `edge_triggered` 参数忽略（`k_poll` 无原生边沿触发，同 Windows 后端）。
- 微秒→tick 转换使用 `K_USEC`（**向上取整**），目标频率由
  `CONFIG_SYS_CLOCK_TICKS_PER_SEC` 决定；禁止向下取整导致忙等。
- waitset 容量由 `CONFIG_NANOSIG_ZEPHYR_WAITSET_CAP`（或裸
  `-DNANOSIG_ZEPHYR_WAITSET_CAP`）配置，默认 32；溢出返回
  `NS_E_TOO_MANY_HANDLES`。
- `platform/zephyr/port.c` 用 `#error` 强制 `CONFIG_POLL=y`、
  `CONFIG_MULTITHREADING=y`、`CONFIG_HEAP_MEM_POOL_SIZE > 0`。

### native_sim 契约测试覆盖面

`test/zephyr/` 是 ztest 应用，在 `native_sim/native/64` 上运行（Linux 宿主）。
CI 的 `zephyr` job 钉版 Zephyr 3.7 LTS，用宿主工具链构建，不下载 Zephyr SDK。
覆盖 waitable/waitset 的 add/remove/wait、event 生命周期与 drain 幂等、IN-only、
容量溢出、电平触发与 remove-不-reset 语义、wakeup/mutex/thread/clock、broker
端到端往返。

明确**不覆盖**（归硬件冒烟）：真实 tick 精度、线程栈溢出停机、SMP 竞态、
非 signal 类 k_poll 对象的就绪映射。

RTOS ISR 安全是后续课题；v1 Zephyr 后端不承诺 ISR 上下文调用（不提供
`*_from_isr`）。

## 新增后端清单

新增一个平台后端时，需要：

1. **平台类型头**：在 `platform/<platform>/` 下创建 `nanosig_platform_port.h`，提供
   `ns_waitable_handle_t`、`ns_waitable_handle_is_valid`、`ns_platform_{wakeup,mutex,thread,waitset}_t`
   与 `NS_PLATFORM_ABI_PROBE`（见“平台类型注入”节）。
2. **实现文件**：在 `platform/<platform>/` 下创建 `port.c`，实现 `nanosig/nanosig_port.h` 中所有 `extern` 函数（`ns_platform_*`）。
3. **CMake 注册**：在顶层 `CMakeLists.txt` 的 `NANOSIG_PLATFORM_DIR` / `NANOSIG_PLATFORM_SOURCES` 条件块中增加分支。使用自带构建系统的 RTOS（如 Zephyr）改为提供 `zephyr/module.yml` + `zephyr/CMakeLists.txt` 作为 module 入口，顶层只给出引导式错误。
4. **编译检查**：`cmake --build` 零警告通过。
5. **平台契约测试**：`ctest -R nanosig_test_platform_backend` 全通过（lifecycle / add-remove / wait timeout / wait signal / multi waitable）。
6. **完整构建 + 测试**：`cmake --build <preset> --target api-compile-checks` + `ctest <preset>` 全通过。
7. **bench 基线**（可选）：跑 1 轮 bench 归档到 `bench/results/`。

新增后端**不允许**：
- 在 `src/` 或 `include/nanosig/` 中新增 OS 分支。
- 修改 `nanosig/nanosig_port.h` 的接口签名（只能新增扩展点）。
- 修改其他后端的实现文件。
