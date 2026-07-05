# Platform Ports 代码审查报告

**日期**: 2026-07-04
**审查者**: claude-code-review

---

## 修复历史

### PLATFORM-012: Windows port `ns_platform_waitset_wait` 编译错误

- **修复**: 当前工作树（未提交）—— `->primitive.handle` 补全
- **日期**: 2026-07-04

### PLATFORM-013: Windows 平台 `CreateThread` 改用 `_beginthreadex`

- **修复**: 当前工作树（未提交）—— `CreateThread` → `_beginthreadex` + `#include <process.h>` + 线程函数签名改为 `unsigned __stdcall`
- **日期**: 2026-07-04

---

## 现在打开的问题（2026-07-05 review）

### PORT-014: macOS `ns_platform_waitset_from_kevent` 合并同 waitable 多事件行为与 Linux/Windows 不一致

- **状态**: 打开
- **严重度**: 🟡 中
- **类型**: design

#### 问题描述
macOS `ns_platform_waitset_wait` 对同一 waitable 的多个 kevent 事件执行合并逻辑（同 waitable 只产生一个 completion），Linux 和 Windows 依赖平台隐式合并。三平台当前结果一致，但建议在 `nanosig_port.h` 中将"同一 waitable 最多产生一个 completion"固化为契约。

#### review 建议
在 `nanosig_port.h` 的 `ns_platform_waitset_wait` Doxygen 注释中补充一句声明。

#### 作者建议
（待作者补充）

#### 定位
`platform/macos/port.c:522-543`, `include/nanosig/nanosig_port.h:432-451`

---

### PORT-015: Windows `ns_windows_timeout_ms` 在超大 timeout_us 下有 uint64 加法溢出

- **状态**: 打开
- **严重度**: 🟢 较低
- **类型**: bug

#### 问题描述
`platform/windows/port.c:38` 中 `(timeout_us + 999u) / 1000u`，当 `timeout_us` 接近 `UINT64_MAX` 时加法溢出。Linux 路径有同样风险。实际触发条件极不常见，但作为平台原语应正确处理。

#### review 建议
改用不溢出的等价算术：`timeout_ms = timeout_us / 1000u + (timeout_us % 1000u != 0u ? 1u : 0u);`

#### 作者建议
（待作者补充）

#### 定位
`platform/windows/port.c:38`, `platform/linux/port.c:141`

---

### PORT-016: 测试 `test_waitset_events_zero` 未覆盖 Windows 路径

- **状态**: 打开
- **严重度**: 🟢 较低
- **类型**: test

#### 问题描述
`test_waitset_events_zero` 用 `#if defined(__linux__) || defined(__APPLE__)` 条件编译，仅在 POSIX 平台运行，Windows 路径未覆盖。

#### review 建议
去掉条件编译守卫，改为三平台通用测试。

#### 作者建议
（待作者补充）

#### 定位
`test/unit/test_platform_backend.c:690-714`

## 现在关闭的问题

### PLATFORM-001: Windows `waitset_wait` 不处理 `WAIT_ABANDONED` / `WAIT_FAILED`

- **状态**: 关闭-已拒绝
- **严重度**: 🟠 高

#### 关闭原因
误报。Windows port 的 waitset 契约限定于事件类句柄（WaitableTimer、Event、HANDLE），不使用 mutex 句柄——`WAIT_ABANDONED` 在本库作用域内不会发生。`WAIT_FAILED` 已被映射到 `NS_E_INVAL` 错误路径，符合 Windows API 文档处理方式。

#### 关闭日期
2026-07-04

---

### PLATFORM-002: 时钟失败路径无法区分 "未初始化" 和 "瞬时错误"

- **状态**: 关闭-已拒绝
- **严重度**: 🟠 高

#### 关闭原因
理论性担忧。Linux vDSO 的 `clock_gettime(CLOCK_MONOTONIC)` 在现代系统上几乎不会失败；Windows 的 `QueryPerformanceFrequency` 失败概率同样极低。broker 已在 `ns_timer_mgr_next_timeout` 返回非 OK 时回退到 `NS_PLATFORM_WAIT_INFINITE_US`（不会永久阻塞）。新增 `NS_E_CLOCK` 错误码增加 API 复杂度而无新能力。

#### 关闭日期
2026-07-04

---

### PLATFORM-003: `ns_platform_waitset_destroy` 的 `count != 0` 检查有 TOCTOU

- **状态**: 关闭-已拒绝
- **严重度**: 🟡 中

#### 关闭原因
文档化合同。`nanosig_port.h` 已明确文档："waitset 必须没有已注册的 waitable；仍有注册项时返回 `NS_E_EXISTS`"。当前唯一调用方 broker 严格按"先 remove 所有 watcher 再 destroy waitset"顺序执行。调用方负责 destroy 前的同步，是合理的责任划分。

#### 关闭日期
2026-07-04

---

### PLATFORM-004: Linux `pthread_join` 不重试 `EINTR`

- **状态**: 关闭-已拒绝
- **严重度**: 🟡 中

#### 关闭原因
误报。POSIX 规定 `pthread_join` 不是因信号返回 `EINTR` 的接口——它是 cancellation point，被信号中断时会内部恢复等待，不会返回 `EINTR`。仅在 thread 已 detach、handle 无效、handle 正在被其他 join 时返回非零错误码。重试 EINTR 是基于对 POSIX 规范的错误解读。

#### 关闭日期
2026-07-04

---

### PLATFORM-005: macOS kqueue-as-fd 注册到 kqueue 本身

- **状态**: 关闭-已拒绝
- **严重度**: 🟡 中

#### 关闭原因
架构误读。broker 只使用一个 kqueue（waitset），其上注册 wakeup waitable。wakeup 机制内部使用独立的 kq（非 broker 的 waitset），二者完全隔离，不存在"kqueue 注册到自身"的情况。`ns_macos_user_event_create` 创建隔离的 kq 用于 wakeup 机制，是正确设计。

#### 关闭日期
2026-07-04

---

### PLATFORM-006: Linux `wakeup_drain` 无上界循环

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低

#### 关闭原因
不可达边界。eventfd 是 64 位计数器，溢出需 ~1.8e19 次信号写入，实际场景不可能达到。循环在 `EAGAIN`/`EWOULDBLOCK` 时正确终止。增加最大读取次数限制是不必要的防御性悲观化。

#### 关闭日期
2026-07-04

---

### PLATFORM-007: `waitset_add` 容量检查中 timer 占用 slot 的契约不明确

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低

#### 关闭原因
架构误读。Timer 不是 waitset 中的 slot——Linux 使用 timerfd（sentinel）但不计入用户可见 slot；Windows port 通过 `NS_PLATFORM_WAITSET_USER_HANDLES = MAX_HANDLES - 1u` 显式预留。Timer deadline 通过 `timeout_us` 参数传入 `waitset_wait`，而非注册为 waitable。容量契约清晰，无隐性 slot 占用。

#### 关闭日期
2026-07-04

---

### PLATFORM-008: `wakeup_signal` 的 `EAGAIN → NS_E_QUEUE_FULL` 转换

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低

#### 关闭原因
语义正确。eventfd 写满（EAGAIN）即表示接收方尚未消费之前的信号，等价于"信号队列已满"。当前映射语义准确。emit_raw 路径忽略返回值的设计是有意为之（best-effort 唤醒，不阻塞调用方）。这是设计选择而非缺陷。

#### 关闭日期
2026-07-04

---

### PLATFORM-009: Linux Waitset 残余 TimerFD 事件导致空转

- **状态**: 关闭-已拒绝
- **严重度**: 🟡 中

#### 关闭原因
已知行为。broker 循环正确处理 `*out_count == 0`（无 dispatch、无 timer 触发），空转的影响仅是一次额外的 `epoll_wait` 调用，不影响正确性。审查本身也承认"收益很小，建议标为已知行为"。不修复属于合理的性能权衡。

#### 关闭日期
2026-07-04

---

### PLATFORM-010: Windows 平台 `SRWLOCK` 不区分普通/递归 mutex

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低

#### 关闭原因
设计意图 + 已验证。审查本身确认"代码中没有递归加锁"。SRWLOCK 是 Windows 平台上最快的 mutex 实现，无递归性是有意的性能优化。增加文档标注是可选改进，但不存在缺陷。

#### 关闭日期
2026-07-04

---

### PLATFORM-011: macOS `ns_macos_user_event_create` 使用原始 -1 而非标准错误码

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低

#### 关闭原因
惯用 Unix 风格。该函数是内部辅助函数，遵循 `kqueue()`/`pipe()` 系统调用约定（-1 表示错误，errno 设置）。调用者立即映射为 `NS_E_NOMEM`。改用 `NS_E_NOMEM` 会掩盖底层 Unix 语义，且内部辅助函数的返回值风格可与公开 API 不同。

#### 关闭日期
2026-07-04

### PLATFORM-012: Windows port `ns_platform_waitset_wait` 编译错误

- **状态**: 关闭-已修复
- **关闭原因**: 补上 `->primitive.handle` 前缀，当前工作树（未提交），对应 `platform/windows/port.c:385`
- **关闭日期**: 2026-07-04

### PLATFORM-013: Windows 平台 `CreateThread` 改用 `_beginthreadex`

- **状态**: 关闭-已修复
- **关闭原因**: 替换为 `_beginthreadex` + 线程函数签名匹配 `unsigned __stdcall(void *)` + 添加 `#include <process.h>`，当前工作树（未提交）
- **关闭日期**: 2026-07-04
