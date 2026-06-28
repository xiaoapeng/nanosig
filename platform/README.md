# nanosig 平台抽象层

`platform/` 是 nanosig v1 的唯一 OS 耦合点。核心实现只能通过 `nanosig/nanosig_port.h`
使用平台能力，不能在 `src/` 或公开头文件中直接包含 OS 头文件或写平台分支。

三后端：Linux（epoll + pthread）、macOS（kqueue + pthread）、
Windows（WaitForMultipleObjects + SRWLOCK）。后端必须同步推进，不允许一个 OS
领先另一个完整阶段。

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

## v2 扩展路径

v1 不创建空 RTOS 或 MCU 后端目录。未来 v2 如果需要 ISR / RTOS 支持，应先扩展
`nanosig/nanosig_port.h` 的契约并补齐文档，再增加对应后端目录。

RTOS 前向兼容：`ns_platform_waitable_t` 的 `event_bit` 字段为 RTOS 预留，waitset
可映射为 event group（FreeRTOS `xEventGroupWaitBits`、Zephyr `k_poll`）。
RTOS ISR 安全是 v2 课题，v1 不承诺。

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
