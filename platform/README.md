# nanosig 平台抽象层

状态：P1b loop-only 后端已落地；Windows 和 Linux 已验证。

`platform/` 是 nanosig v1 的唯一 OS 耦合点。核心实现只能通过
`platform/port.h` 使用平台能力，不能在 `src/` 或公开头文件中直接包含 OS
头文件或写平台分支。

## P1a 契约

`platform/port.h` 冻结以下能力：

- 平台生命周期：`ns_platform_init`、`ns_platform_shutdown`。
- 内存：`ns_platform_alloc`、`ns_platform_free`。
- TLS：创建 key、销毁 key、当前线程 get/set。
- wakeup：创建、销毁、signal、reset、单个等待。
- 同步：mutex 的创建、销毁、加锁和解锁。
- 时间：单调微秒时钟。

线程创建 / join、condvar、wait-many、fd / socket readiness 和平台 handle
waitset 不在 P1b loop 后端范围内。后续进入 `ns_event_broker_t` 或后台服务实现时
再按实际需求追加。

P1b 已实现 loop-only Linux / Windows 后端源码，并新增平台后端运行时 smoke test。
当前 Windows preset 会构建 Windows 后端，Linux preset 会构建 Linux 后端；两边均已复验。

## 后端映射

Linux 后端在 P1b 中按以下 loop-only 方向实现：

- TLS 和 mutex 使用 POSIX 线程设施。
- wakeup 使用 eventfd、pipe 或等价单 wakeup 机制。
- 单调时间使用 `clock_gettime` 的 monotonic 时钟。
- 内存分配集中在平台层封装。

Windows 后端在 P1b 中按以下 loop-only 方向实现：

- TLS 使用 Win32 TLS 或等价线程本地存储设施。
- wakeup 使用 auto-reset event。
- 单 wakeup 等待使用 WaitForSingleObject 或等价机制。
- 锁使用 SRWLOCK 或等价 mutex 原语。
- 单调时间使用 QueryPerformanceCounter。
- 内存分配集中在平台层封装。

两个后端必须同步推进，不能让一个 OS 领先另一个完整阶段。

## 生命周期和所有权

平台层 handle 都是不透明类型。创建函数返回的 handle 归调用方所有，必须用匹配
destroy 函数释放。

wakeup、mutex 和 TLS key 的 create/destroy 可以分配和释放资源。emit 路径只能
使用已经创建好的资源，不能触发平台分配。

## 等待语义

带超时的 wakeup 等待函数使用微秒。`NS_PLATFORM_WAIT_INFINITE_US` 表示无限等待。
等待操作本身成功时返回 `NS_OK`，具体结果通过 `ns_platform_wait_result_t`
区分 signaled 与 timeout。

P1b loop-only 后端不提供 wait-many。Windows `WaitForMultipleObjects` 64 handle
限制留到后续 `ns_event_broker_t` / waitset 契约追加时处理。

## v2 扩展路径

v1 不创建空 RTOS 或 MCU 后端目录。未来 v2 如果需要 ISR / RTOS 支持，应先扩展
`platform/port.h` 的契约并补齐文档，再增加对应后端目录。
