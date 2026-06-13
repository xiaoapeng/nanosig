# nanosig 平台抽象层

状态：P5b waitset 契约已追加；Windows 和 Linux 已验证。

`platform/` 是 nanosig v1 的唯一 OS 耦合点。核心实现只能通过
`platform/port.h` 使用平台能力，不能在 `src/` 或公开头文件中直接包含 OS
头文件或写平台分支。

## P1a 契约（loop-only 原语）

`platform/port.h` 冻结以下能力：

- 平台生命周期：`ns_platform_init`、`ns_platform_shutdown`。
- 内存：`ns_platform_alloc`、`ns_platform_free`。
- TLS：创建 key、销毁 key、当前线程 get/set。
- wakeup：创建、销毁、signal、单个等待。
- 同步：mutex 的创建、销毁、加锁和解锁。
- 时间：单调微秒时钟。

## P5b 契约（waitset 原语）

`platform/port.h` 追加以下 waitset 能力：

- waitable：可等待句柄 union（fd / HANDLE / event_bit），由平台层函数构造。
- waitset：一次等待多个事件源的容器。
- completion：wait 返回的事件结果（waitable + events + user_data）。
- 事件位：`NS_WAITABLE_EVENT_IN`、`NS_WAITABLE_EVENT_OUT`、`NS_WAITABLE_EVENT_ERR`。

接口：

- `ns_platform_waitset_create` / `ns_platform_waitset_destroy`
- `ns_platform_waitset_add`（注册 waitable，重复返回 `NS_E_EXISTS`）
- `ns_platform_waitset_remove`（移除 waitable，未注册返回 `NS_E_INVAL`）
- `ns_platform_waitset_wait`（等待事件，timeout 向上取整到平台原生粒度）

waitset 不包含任何事件源特定函数，也不与 wakeup 耦合。上层直接构造
`ns_platform_waitable_t`（Linux 填 `fd`，Windows 填 `handle`）注册到 waitset。
tcp/udp socket 等原始 fd 也可直接构造 waitable 注册。

timeout 语义：微秒输入，后端向上取整到毫秒 / tick。保证等待至少 `timeout_us`，
实际可能略长。晚 fire 安全，向下取整导致忙等才是 bug。

## 后端映射

Linux 后端：

- TLS 和 mutex 使用 POSIX 线程设施。
- wakeup 使用 eventfd、pipe 或等价单 wakeup 机制。
- 单调时间使用 `clock_gettime` 的 monotonic 时钟。
- 内存分配集中在平台层封装。
- waitset 使用 `epoll_create1` / `epoll_ctl` / `epoll_wait`。
- `edge_triggered=1` 映射 `EPOLLET`。

Windows 后端：

- TLS 使用 Win32 TLS 或等价线程本地存储设施。
- wakeup 使用 auto-reset event。
- 单 wakeup 等待使用 WaitForSingleObject 或等价机制。
- 锁使用 SRWLOCK 或等价 mutex 原语。
- 单调时间使用 QueryPerformanceCounter。
- 内存分配集中在平台层封装。
- waitset 使用 `WaitForMultipleObjects`，容量上限 64 handle。
- `edge_triggered` 参数忽略（WFMO 不支持）。

两个后端必须同步推进，不能让一个 OS 领先另一个完整阶段。

## 生命周期和所有权

平台层 handle 都是不透明类型。创建函数返回的 handle 归调用方所有，必须用匹配
destroy 函数释放。

wakeup、mutex 和 TLS key 的 create/destroy 可以分配和释放资源。emit 路径只能
使用已经创建好的资源，不能触发平台分配。

## 等待语义

带超时的等待函数使用微秒。`NS_PLATFORM_WAIT_INFINITE_US` 表示无限等待，0 表示非阻塞。

单 wakeup 等待（`ns_platform_wakeup_wait`）：等待操作本身成功时返回 `NS_OK`，
具体结果通过 `ns_platform_wait_result_t` 区分 signaled 与 timeout。

waitset 等待（`ns_platform_waitset_wait`）：timeout 向上取整到平台原生粒度（毫秒 /
tick），保证等待至少 `timeout_us` 微秒。completion 数组由调用方提供，`out_count`
返回实际触发数。Windows 后端单次 wait 最多返回 1 个 completion（auto-reset event
语义）。

## v2 扩展路径

v1 不创建空 RTOS 或 MCU 后端目录。未来 v2 如果需要 ISR / RTOS 支持，应先扩展
`platform/port.h` 的契约并补齐文档，再增加对应后端目录。

RTOS 前向兼容：`ns_platform_waitable_t` 的 `event_bit` 字段为 RTOS 预留，waitset
可映射为 event group（FreeRTOS `xEventGroupWaitBits`、Zephyr `k_poll`）。
RTOS ISR 安全是 v2 课题，v1 不承诺。
