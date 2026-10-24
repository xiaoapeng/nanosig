---
review_schema: "nanosig-review-v1"
entries:
  - id: "PORT-018"
    module: "platform-ports"
    status: "closed-fixed"
    severity: "high"
    type: "bug"
    section: "closed"
    closed_date: "2026-10-18"
    location: "docs/review/platform-ports-code-review.md#PORT-018"
    locations:
      - "platform/windows/nanosig_platform_port.h:42"
      - "platform/windows/port.c:315"
      - "include/nanosig/nanosig_port.h:109"
      - "include/nanosig/nanosig_broker.h:147"
  - id: "PORT-019"
    module: "platform-ports"
    status: "closed-fixed"
    severity: "high"
    type: "bug"
    section: "closed"
    closed_date: "2026-10-18"
    location: "docs/review/platform-ports-code-review.md#PORT-019"
    locations:
      - "platform/linux/port.c:528"
      - "platform/linux/port.c:532"
  - id: "PORT-021"
    module: "platform-ports"
    status: "closed-fixed"
    severity: "low"
    type: "doc"
    section: "closed"
    closed_date: "2026-10-24"
    location: "docs/review/platform-ports-code-review.md#PORT-021"
    locations:
      - "platform/README.md:156"
  - id: "PORT-022"
    module: "platform-ports"
    status: "closed-fixed"
    severity: "low"
    type: "cleanup"
    section: "closed"
    closed_date: "2026-10-18"
    location: "docs/review/platform-ports-code-review.md#PORT-022"
    locations:
      - "platform/windows/port.c:302"
      - "platform/linux/port.c:399"
      - "platform/macos/port.c:411"
  - id: "PORT-014"
    module: "platform-ports"
    status: "closed-deferred"
    severity: "medium"
    type: "design"
    section: "closed"
    closed_date: "2026-10-19"
    location: "docs/review/platform-ports-code-review.md#PORT-014"
    locations:
      - "platform/macos/port.c:522-543"
      - "include/nanosig/nanosig_port.h:432-451"
  - id: "PORT-015"
    module: "platform-ports"
    status: "closed-rejected"
    severity: "low"
    type: "bug"
    section: "closed"
    closed_date: "2026-08-02"
    location: "docs/review/platform-ports-code-review.md#PORT-015"
    locations:
      - "platform/windows/port.c:38"
      - "platform/linux/port.c:141"
  - id: "PORT-016"
    module: "platform-ports"
    status: "closed-fixed"
    severity: "low"
    type: "test"
    section: "closed"
    closed_date: "2026-10-24"
    location: "docs/review/platform-ports-code-review.md#PORT-016"
    locations:
      - "test/unit/test_platform_backend.c:905-939"
  - id: "PORT-023"
    module: "platform-ports"
    status: "closed-fixed"
    severity: "high"
    type: "bug"
    section: "closed"
    closed_date: "2026-10-19"
    location: "docs/review/platform-ports-code-review.md#PORT-023"
    locations:
      - "platform/linux/port.c:30-47"
      - "platform/linux/port.c:132-169"
      - "src/ns_broker.c:239"
  - id: "PORT-024"
    module: "platform-ports"
    status: "closed-fixed"
    severity: "high"
    type: "bug"
    section: "closed"
    closed_date: "2026-10-24"
    location: "docs/review/platform-ports-code-review.md#PORT-024"
    locations:
      - "platform/macos/port.c:448"
      - "platform/macos/port.c:463"
      - "platform/macos/port.c:470"
      - "platform/macos/port.c:489"
      - "platform/macos/port.c:505"
      - "platform/macos/port.c:515"
      - "src/ns_broker.c:336"
  - id: "PORT-025"
    module: "platform-ports"
    status: "closed-rejected"
    severity: "high"
    type: "bug"
    section: "closed"
    closed_date: "2026-10-25"
    location: "docs/review/platform-ports-code-review.md#PORT-025"
    locations:
      - "platform/macos/port.c:479"
      - "platform/macos/port.c:492"
      - "platform/macos/port.c:508"
      - "platform/linux/port.c:439"
      - "platform/windows/port.c:331"
      - "include/nanosig/nanosig_port.h:503"
      - "platform/README.md:41"
  - id: "PORT-026"
    module: "platform-ports"
    status: "closed-fixed"
    severity: "medium"
    type: "bug"
    section: "closed"
    closed_date: "2026-10-24"
    location: "docs/review/platform-ports-code-review.md#PORT-026"
    locations:
      - "platform/windows/port.c:352"
      - "platform/windows/port.c:470"
      - "platform/windows/port.c:479"
      - "platform/linux/port.c:402"
      - "platform/macos/port.c:419"
      - "include/nanosig/nanosig_port.h:534"
      - "platform/README.md:175"
      - "test/unit/test_platform_backend.c:908"
  - id: "PORT-027"
    module: "platform-ports"
    status: "closed-fixed"
    severity: "medium"
    type: "bug"
    section: "closed"
    closed_date: "2026-10-24"
    location: "docs/review/platform-ports-code-review.md#PORT-027"
    locations:
      - "platform/macos/port.c:249"
      - "platform/macos/port.c:256"
      - "platform/macos/port.c:271"
      - "platform/macos/port.c:281"
      - "include/nanosig/nanosig_port.h:18"
      - "include/nanosig/nanosig_port.h:307"
      - "platform/linux/port.c:205"
      - "platform/windows/port.c:152"
  - id: "PORT-028"
    module: "platform-ports"
    status: "closed-fixed"
    severity: "medium"
    type: "bug"
    section: "closed"
    closed_date: "2026-10-25"
    location: "docs/review/platform-ports-code-review.md#PORT-028"
    locations:
      - "platform/macos/port.c:529"
      - "platform/macos/port.c:530"
      - "platform/linux/port.c:420"
      - "platform/windows/port.c:438"
      - "include/nanosig/nanosig_port.h:77"
      - "include/nanosig/nanosig_broker.h:55"
  - id: "PORT-029"
    module: "platform-ports"
    status: "closed-fixed"
    severity: "low"
    type: "doc"
    section: "closed"
    closed_date: "2026-10-24"
    location: "docs/review/platform-ports-code-review.md#PORT-029"
    locations:
      - "platform/README.md:135"
      - "platform/README.md:294"
      - "platform/windows/nanosig_platform_port.h:44"
      - "platform/windows/port.c:443"
  - id: "PLATFORM-001"
    module: "platform-ports"
    status: "closed-rejected"
    severity: "high"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-04"
    location: "docs/review/platform-ports-code-review.md#PLATFORM-001"
  - id: "PLATFORM-002"
    module: "platform-ports"
    status: "closed-rejected"
    severity: "high"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-04"
    location: "docs/review/platform-ports-code-review.md#PLATFORM-002"
  - id: "PLATFORM-003"
    module: "platform-ports"
    status: "closed-rejected"
    severity: "medium"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-04"
    location: "docs/review/platform-ports-code-review.md#PLATFORM-003"
  - id: "PLATFORM-004"
    module: "platform-ports"
    status: "closed-rejected"
    severity: "medium"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-04"
    location: "docs/review/platform-ports-code-review.md#PLATFORM-004"
  - id: "PLATFORM-005"
    module: "platform-ports"
    status: "closed-rejected"
    severity: "medium"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-04"
    location: "docs/review/platform-ports-code-review.md#PLATFORM-005"
  - id: "PLATFORM-006"
    module: "platform-ports"
    status: "closed-rejected"
    severity: "low"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-04"
    location: "docs/review/platform-ports-code-review.md#PLATFORM-006"
  - id: "PLATFORM-007"
    module: "platform-ports"
    status: "closed-rejected"
    severity: "low"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-04"
    location: "docs/review/platform-ports-code-review.md#PLATFORM-007"
  - id: "PLATFORM-008"
    module: "platform-ports"
    status: "closed-rejected"
    severity: "low"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-04"
    location: "docs/review/platform-ports-code-review.md#PLATFORM-008"
  - id: "PLATFORM-009"
    module: "platform-ports"
    status: "closed-rejected"
    severity: "medium"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-04"
    location: "docs/review/platform-ports-code-review.md#PLATFORM-009"
  - id: "PLATFORM-010"
    module: "platform-ports"
    status: "closed-rejected"
    severity: "low"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-04"
    location: "docs/review/platform-ports-code-review.md#PLATFORM-010"
  - id: "PLATFORM-011"
    module: "platform-ports"
    status: "closed-rejected"
    severity: "low"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-04"
    location: "docs/review/platform-ports-code-review.md#PLATFORM-011"
  - id: "PORT-020"
    module: "platform-ports"
    status: "closed-fixed"
    severity: "medium"
    type: "bug"
    section: "closed"
    closed_date: "2026-10-19"
    location: "docs/review/platform-ports-code-review.md#PORT-020"
  - id: "PLATFORM-012"
    module: "platform-ports"
    status: "closed-fixed"
    severity: "unknown"
    type: "unknown"
    section: "fixed-history"
    location: "docs/review/platform-ports-code-review.md#PLATFORM-012"
  - id: "PLATFORM-013"
    module: "platform-ports"
    status: "closed-fixed"
    severity: "unknown"
    type: "unknown"
    section: "fixed-history"
    location: "docs/review/platform-ports-code-review.md#PLATFORM-013"
  - id: "PORT-030"
    module: "platform-ports"
    status: "closed-rejected"
    severity: "high"
    type: "bug"
    section: "closed"
    closed_date: "2026-10-20"
    location: "docs/review/platform-ports-code-review.md#PORT-030"
    locations:
      - "platform/linux/port.c:347"
      - "platform/macos/port.c:376"
      - "platform/windows/port.c:270"
      - "platform/freertos/port.c:394"
      - "src/nanosig.c:328"
  - id: "PORT-031"
    module: "platform-ports"
    status: "closed-fixed"
    severity: "high"
    type: "bug"
    section: "closed"
    closed_date: "2026-10-22"
    location: "docs/review/platform-ports-code-review.md#PORT-031"
    locations:
      - "platform/macos/port.c:349"
      - "platform/macos/port.c:390"
      - "platform/linux/port.c:320"
      - "src/nanosig.c:91"
      - "include/nanosig/nanosig_port.h:442"
  - id: "PORT-032"
    module: "platform-ports"
    status: "closed-rejected"
    severity: "high"
    type: "bug"
    section: "closed"
    closed_date: "2026-10-23"
    location: "docs/review/platform-ports-code-review.md#PORT-032"
    locations:
      - "platform/linux/port.c:339"
      - "platform/linux/port.c:344"
      - "platform/macos/port.c:368"
      - "platform/windows/port.c:269"
      - "platform/windows/port.c:271"
      - "platform/freertos/port.c:387"
      - "platform/zephyr/port.c:282"
      - "include/nanosig/nanosig_port.h:480"
  - id: "PORT-033"
    module: "platform-ports"
    status: "closed-rejected"
    severity: "high"
    type: "bug"
    section: "closed"
    closed_date: "2026-10-23"
    location: "docs/review/platform-ports-code-review.md#PORT-033"
    locations:
      - "src/ns_broker.c:551"
      - "platform/README.md:67"
  - id: "PORT-034"
    module: "platform-ports"
    status: "closed-fixed"
    severity: "medium"
    type: "bug"
    section: "closed"
    closed_date: "2026-10-24"
    location: "docs/review/platform-ports-code-review.md#PORT-034"
    locations:
      - "platform/linux/port.c:598"
      - "platform/linux/port.c:630"
      - "test/unit/test_platform_backend.c:1097"
---

# Platform Ports 代码审查报告

**日期**: 2026-07-04
**审查者**: claude-code-review

---

## 修复历史

<a id="PLATFORM-012"></a>
### PLATFORM-012: Windows port `ns_platform_waitset_wait` 编译错误

- **修复**: 当前工作树（未提交）—— `->primitive.handle` 补全
- **日期**: 2026-07-04

---

<a id="PLATFORM-013"></a>

<a id="PLATFORM-013"></a>
### PLATFORM-013: Windows 平台 `CreateThread` 改用 `_beginthreadex`

- **修复**: 当前工作树（未提交）—— `CreateThread` → `_beginthreadex` + `#include <process.h>` + 线程函数签名改为 `unsigned __stdcall`
- **日期**: 2026-07-04

---

<a id="PLATFORM-012"></a>

---

---

- **PORT-031（2026-10-22）**: POSIX 后端把栈大小请求向上取整到页粒度（4096）后再交给 pthread_attr_setstacksize，下界判定仍用原始请求；新增非页粒度请求的单测用例 — commit `9b149fd`

- **PORT-024（2026-10-24）**: macOS waitset 变更改为逐条提交，部分失败时反向回滚已生效条目；add/remove 保持全有或全无，并补 test_waitset_partial_add_rollback 回归用例 — commit `3092099`

- **PORT-027（2026-10-24）**: macOS event pipe 写端加 F_SETNOSIGPIPE，读端已关闭时写只返回 EPIPE，不再以 SIGPIPE 终止进程 — commit `f2f64b2`

- **PORT-021（2026-10-24）**: 重新核对后仅 macOS wakeup "暴露为 waitable" 仍过期，已改为只能经 ns_platform_wakeup_wait 等待；其余 4 处已随 GLOBAL-006 修复或原判断有误，定位收敛到 platform/README.md:156 — commit `43fe3ec`

- **PORT-029（2026-10-24）**: platform/README.md 的 Windows 句柄谓词改为 h != NULL && h != INVALID_HANDLE_VALUE；单次 wait 的 completion 条数改为"可返回多个（WFMO 报告首个 + 零超时扫描其余）" — commit `43fe3ec`

- **PORT-016（2026-10-24）**: test_waitset_events_zero 去掉 #if 守卫，Windows 断言 NS_E_INVAL 且不写入注册状态，POSIX 断言 NS_OK + signal 后不触发 — commit `03ee7f1`

- **PORT-026（2026-10-24）**: Windows waitset 与 FreeRTOS/Zephyr 对齐为 IN-only：掩码不含 IN（含 events==0）或含 OUT/ERR 时 add 返回 NS_E_INVAL，不再静默降级为 IN — commit `03ee7f1`

- **PORT-034（2026-10-24）**: epoll_wait 返回后立即 timerfd_settime(it_value=0) 撤弦并清空过期计数，覆盖正常/EINTR/出错全部返回路径；新增 test_waitset_timeout_early_return_disarms — commit `93be19d`

## 现在打开的问题

## 现在关闭的问题

<a id="PLATFORM-001"></a>
### PLATFORM-001: Windows `waitset_wait` 不处理 `WAIT_ABANDONED` / `WAIT_FAILED`

- **状态**: 关闭-已拒绝
- **严重度**: 🟠 高

#### 关闭原因
误报。Windows port 的 waitset 契约限定于事件类句柄（WaitableTimer、Event、HANDLE），不使用 mutex 句柄——`WAIT_ABANDONED` 在本库作用域内不会发生。`WAIT_FAILED` 已被映射到 `NS_E_INVAL` 错误路径，符合 Windows API 文档处理方式。

#### 关闭日期
2026-07-04

---

<a id="PLATFORM-002"></a>
### PLATFORM-002: 时钟失败路径无法区分 "未初始化" 和 "瞬时错误"

- **状态**: 关闭-已拒绝
- **严重度**: 🟠 高

#### 关闭原因
理论性担忧。Linux vDSO 的 `clock_gettime(CLOCK_MONOTONIC)` 在现代系统上几乎不会失败；Windows 的 `QueryPerformanceFrequency` 失败概率同样极低。broker 已在 `ns_timer_mgr_next_timeout` 返回非 OK 时回退到 `NS_PLATFORM_WAIT_INFINITE_US`（不会永久阻塞）。新增 `NS_E_CLOCK` 错误码增加 API 复杂度而无新能力。

#### 关闭日期
2026-07-04

---

<a id="PLATFORM-003"></a>
### PLATFORM-003: `ns_platform_waitset_destroy` 的 `count != 0` 检查有 TOCTOU

- **状态**: 关闭-已拒绝
- **严重度**: 🟡 中

#### 关闭原因
文档化合同。`nanosig_port.h` 已明确文档："waitset 必须没有已注册的 waitable；仍有注册项时返回 `NS_E_EXISTS`"。当前唯一调用方 broker 严格按"先 remove 所有 watcher 再 destroy waitset"顺序执行。调用方负责 destroy 前的同步，是合理的责任划分。

#### 关闭日期
2026-07-04

---

<a id="PLATFORM-004"></a>
### PLATFORM-004: Linux `pthread_join` 不重试 `EINTR`

- **状态**: 关闭-已拒绝
- **严重度**: 🟡 中

#### 关闭原因
误报。POSIX 规定 `pthread_join` 不是因信号返回 `EINTR` 的接口——它是 cancellation point，被信号中断时会内部恢复等待，不会返回 `EINTR`。仅在 thread 已 detach、handle 无效、handle 正在被其他 join 时返回非零错误码。重试 EINTR 是基于对 POSIX 规范的错误解读。

#### 关闭日期
2026-07-04

---

<a id="PLATFORM-005"></a>
### PLATFORM-005: macOS kqueue-as-fd 注册到 kqueue 本身

- **状态**: 关闭-已拒绝
- **严重度**: 🟡 中

#### 关闭原因
架构误读。broker 只使用一个 kqueue（waitset），其上注册 wakeup waitable。wakeup 机制内部使用独立的 kq（非 broker 的 waitset），二者完全隔离，不存在"kqueue 注册到自身"的情况。`ns_macos_user_event_create` 创建隔离的 kq 用于 wakeup 机制，是正确设计。

#### 关闭日期
2026-07-04

---

<a id="PLATFORM-006"></a>
### PLATFORM-006: Linux `wakeup_drain` 无上界循环

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低

#### 关闭原因
不可达边界。eventfd 是 64 位计数器，溢出需 ~1.8e19 次信号写入，实际场景不可能达到。循环在 `EAGAIN`/`EWOULDBLOCK` 时正确终止。增加最大读取次数限制是不必要的防御性悲观化。

#### 关闭日期
2026-07-04

---

<a id="PLATFORM-007"></a>
### PLATFORM-007: `waitset_add` 容量检查中 timer 占用 slot 的契约不明确

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低

#### 关闭原因
架构误读。Timer 不是 waitset 中的 slot——Linux 使用 timerfd（sentinel）但不计入用户可见 slot；Windows port 通过 `NS_PLATFORM_WAITSET_USER_HANDLES = MAX_HANDLES - 1u` 显式预留。Timer deadline 通过 `timeout_us` 参数传入 `waitset_wait`，而非注册为 waitable。容量契约清晰，无隐性 slot 占用。

#### 关闭日期
2026-07-04

---

<a id="PLATFORM-008"></a>
### PLATFORM-008: `wakeup_signal` 的 `EAGAIN → NS_E_QUEUE_FULL` 转换

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低

#### 关闭原因
语义正确。eventfd 写满（EAGAIN）即表示接收方尚未消费之前的信号，等价于"信号队列已满"。当前映射语义准确。emit_raw 路径忽略返回值的设计是有意为之（best-effort 唤醒，不阻塞调用方）。这是设计选择而非缺陷。

#### 关闭日期
2026-07-04

---

<a id="PLATFORM-009"></a>
### PLATFORM-009: Linux Waitset 残余 TimerFD 事件导致空转

- **状态**: 关闭-已拒绝
- **严重度**: 🟡 中

#### 关闭原因
已知行为。broker 循环正确处理 `*out_count == 0`（无 dispatch、无 timer 触发），空转的影响仅是一次额外的 `epoll_wait` 调用，不影响正确性。审查本身也承认"收益很小，建议标为已知行为"。不修复属于合理的性能权衡。

#### 关闭日期
2026-07-04

---

<a id="PLATFORM-010"></a>
### PLATFORM-010: Windows 平台 `SRWLOCK` 不区分普通/递归 mutex

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低

#### 关闭原因
设计意图 + 已验证。审查本身确认"代码中没有递归加锁"。SRWLOCK 是 Windows 平台上最快的 mutex 实现，无递归性是有意的性能优化。增加文档标注是可选改进，但不存在缺陷。

#### 关闭日期
2026-07-04

---

<a id="PLATFORM-011"></a>
### PLATFORM-011: macOS `ns_macos_user_event_create` 使用原始 -1 而非标准错误码

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低

#### 关闭原因
惯用 Unix 风格。该函数是内部辅助函数，遵循 `kqueue()`/`pipe()` 系统调用约定（-1 表示错误，errno 设置）。调用者立即映射为 `NS_E_NOMEM`。改用 `NS_E_NOMEM` 会掩盖底层 Unix 语义，且内部辅助函数的返回值风格可与公开 API 不同。

#### 关闭日期
2026-07-04

<a id="PORT-020"></a>
### PORT-020: Zephyr `ns_platform_alloc` 对所有内部分配强制内核栈对齐

- **状态**: 关闭-已修复
- **严重度**: 🟡 中

#### 关闭原因
作者决定（人话：这是变通，且只对 Zephyr 生效）：把 Zephyr 后端的**线程对象**改成由
port 拥有——`ns_platform_thread_t` 变为指向 `port.c` 内定义的
`struct ns_platform_thread`（内嵌 `K_KERNEL_STACK_MEMBER`）的不透明指针，
`ns_platform_thread_init` 用 `k_aligned_alloc(Z_KERNEL_STACK_OBJ_ALIGN, ...)` 分配，
`ns_platform_thread_join` 回收；`ns_platform_alloc` 回到普通 `k_malloc`。

内核栈的对齐要求因此被约束在这一个分配里，不再经堆上的 `ns_loop_t` / broker 传给
哈希表节点、格式化缓冲等小对象（开 MPU 栈保护时正是这些分配被按栈尺寸对齐）。
这是对"平台对象由调用方拥有存储"约定的一处**有意例外**，已在
platform/zephyr/nanosig_platform_port.h、include/nanosig/nanosig_port.h、
platform/README.md 与 docs/agents/platform/AGENTS.md 文档化；调用方接口形态不变
（仍是声明 `ns_platform_thread_t` 变量并取地址）。

review 建议的 `ns_platform_alloc_aligned` 未采用：对 loop/broker 而言两者等价，
但新增的公共分配入口会把"对齐"这个平台细节泄漏给 src/。另一条备选（把内核栈从
结构体里搬出去、用 `K_KERNEL_STACK_LEN` 手搓缓冲区）也未采用：Zephyr 文档要求栈
必须由 `K_THREAD_STACK_DEFINE` / `K_KERNEL_STACK_DEFINE` 宏定义，手搓长度可能 UB。

> **加注（线程栈大小参数改动）**：本条的关闭方案已被**部分取代**。Zephyr 后端
> 改为运行期动态栈（`k_thread_stack_alloc` / `k_thread_stack_free`），线程对象
> 不再内嵌 `K_KERNEL_STACK_MEMBER`，`k_aligned_alloc(Z_KERNEL_STACK_OBJ_ALIGN, ...)`
> 也随之退化为普通 `ns_platform_alloc`（`k_malloc`）。本条**要解决的问题依然成立**
> 且结论更强：栈与对象分离后，对齐要求彻底不再经堆上的 `ns_loop_t` / broker 传给
> 通用分配器。原关闭理由中"Zephyr 文档要求栈必须由 `K_THREAD_STACK_DEFINE` 宏定义"
> 一句已被 Zephyr 的 `k_thread_stack_alloc`（动态线程栈 API）取代。
> 代价是该 API 需要 `CONFIG_DYNAMIC_THREAD` + `CONFIG_DYNAMIC_THREAD_ALLOC`
> （nanosig 的 `zephyr/Kconfig` 已 `select`），内核因此开启 EXPERIMENTAL 动态线程栈、
> 线程栈信息记账与线程监控。详见 `docs/specs/深度访谈-线程栈大小参数规格.md`。
- 修复分支: fix/zephyr-thread-object-owned

#### 关闭日期
2026-10-19

<a id="PORT-014"></a>
### PORT-014: macOS `ns_platform_waitset_from_kevent` 合并同 waitable 多事件行为与 Linux/Windows 不一致

- **状态**: 关闭-延期
- **严重度**: 🟡 中
- **类型**: design
- **关闭原因**: 作者决定延期，三平台当前行为一致，见作者建议。
- **关闭日期**: 2026-10-19

#### 问题描述
macOS `ns_platform_waitset_wait` 对同一 waitable 的多个 kevent 事件执行合并逻辑（同 waitable 只产生一个 completion），Linux 和 Windows 依赖平台隐式合并。三平台当前结果一致，但建议在 `nanosig_port.h` 中将"同一 waitable 最多产生一个 completion"固化为契约。

#### review 建议
在 `nanosig_port.h` 的 `ns_platform_waitset_wait` Doxygen 注释中补充一句声明。

#### 作者建议
暂时不修。当前三平台行为一致，无实际影响。后续若出现平台不兼容再处理。
→ 关闭-延期

#### 定位
platform/macos/port.c:522-543
include/nanosig/nanosig_port.h:432-451

<a id="PORT-015"></a>
### PORT-015: Windows `ns_windows_timeout_ms` 在超大 timeout_us 下有 uint64 加法溢出

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低
- **类型**: bug

#### 问题描述
`platform/windows/port.c:38` 中 `(timeout_us + 999u) / 1000u`，当 `timeout_us` 接近 `UINT64_MAX` 时加法溢出。Linux 路径有同样风险。实际触发条件极不常见，但作为平台原语应正确处理。

#### review 建议
改用不溢出的等价算术：`timeout_ms = timeout_us / 1000u + (timeout_us % 1000u != 0u ? 1u : 0u);`

#### 作者建议
不考虑这种情况，场景不可能。`timeout_us` 在实际使用中不会接近 UINT64_MAX。
→ 关闭-已拒绝

#### 关闭原因
场景不可能。实际使用中 timeout_us 为微秒级超时值，不会接近 UINT64_MAX。

- **关闭日期**: 2026-08-02
- **状态**: 关闭-已拒绝

#### 定位
platform/windows/port.c:38
platform/linux/port.c:141

---

<a id="PORT-030"></a>
### PORT-030: `ns_platform_thread_join` 未拦截“join 自身”：POSIX 未定义行为，TSAN 下 abort 进程、Windows 下永久阻塞

- **状态**: 关闭-已拒绝
- **严重度**: 🟠 高
- **类型**: bug
- **关闭原因**: 合同前提保护：`ns_loop_stop` 在 `include/nanosig/nanosig_loop.h:154` 已文档化「**不得在 loop 自身线程（含 slot 回调）内调用**……违反本前提时行为未定义，**不保证返回错误**」。finding 所述问题仅在调用方违反该前提时出现，属用户错误而非库缺陷。按作者意见改为收紧契约 + 删除违约用例（`test/unit/test_loop.c` 的 `test_async_stop_in_slot`），未在平台层加防御。
- **关闭日期**: 2026-10-20

#### 问题描述

`ns_platform_thread_join` 直接把目标线程句柄交给 `pthread_join`（Linux/macOS）、`WaitForSingleObject(..., INFINITE)`（Windows）、`xSemaphoreTake(..., portMAX_DELAY)`（FreeRTOS），没有检查“调用者就是目标线程”这一情形。

触发路径：`ns_loop_stop` 在 loop 自身线程（slot 回调）内被调用时，`src/nanosig.c:328` 会 join 后台 worker 自身。`nanosig_loop.h` 已把该情形写为调用方前提（“不得在 loop 自身线程（含 slot 回调）内调用”），但平台层没有做防御，违反前提时后果不是“返回错误”而是：

1. POSIX（Linux/macOS）：`pthread_join(self)` 是未定义行为。glibc 返回 EDEADLK，但 ThreadSanitizer 的 `pthread_join` 拦截器在**真实 join 之前**就调用 `ThreadConsumeTid` → `ThreadRegistry::ConsumeThreadUserId`，把目标线程的 user id 从 `live_` map 中 erase（compiler-rt `sanitizer_thread_registry.cpp:347-350`）。于是后续由其他线程发起的**合法** join 再次进入 `ConsumeThreadUserId` 时 `live_.find(user_id)` 返回空，命中 `CHECK(t)`（同文件 348 行）→ `ThreadSanitizer: CHECK failed: ... "((t)) != (0)"` → 整个进程 abort。

2. Windows：`WaitForSingleObject(自身线程句柄, INFINITE)` 永远等不到信号 → loop 线程永久死锁。

3. FreeRTOS：join 信号量由目标线程自己 give，自取会永久阻塞在 `xSemaphoreTake`。

Zephyr 后端不受影响：`z_impl_k_thread_join` 对自 join 返回 `-EDEADLK`。

#### review 建议

在平台层 `ns_platform_thread_join` 入口拦截自 join，返回 `NS_E_INVAL` 且不回收线程资源（由其他线程再次调用完成回收），使行为与 `ns_loop_stop` 已文档化的契约一致：

- Linux/macOS：`if(pthread_equal(thread->thread, pthread_self()) != 0) return NS_E_INVAL;`
- Windows：在 `ns_platform_thread_t` 增加目标线程自身 ID（由目标线程写入，`InterlockedExchange` 保证原子），join 时与 `GetCurrentThreadId()` 比对
- FreeRTOS：`if(xTaskGetCurrentTaskHandle() == thread->task) return NS_E_INVAL;`
- Zephyr：已安全，无需改动

同时在 `include/nanosig/nanosig_port.h` 的 `ns_platform_thread_join` 上补 `@pre`，把“不得 join 自身、此时返回 `NS_E_INVAL`”写进公共契约。

#### 作者建议

调用方违约，不在平台层兜底。收紧 `ns_loop_stop` 的契约：`include/nanosig/nanosig_loop.h` 的 `@pre` 改为「**不得在 loop 自身线程（含 slot 回调）内调用**……违反本前提时行为未定义，**不保证返回错误**」，并指引在 loop 自身线程内改用 `ns_loop_quit()`（异步、不等待线程退出）。同时删除违反该契约的用例 `test_async_stop_in_slot`——它覆盖的合法路径已由既有的 `test_async_quit_in_slot`（slot 内 quit + 他线程 stop）覆盖。→ 关闭-已拒绝

#### 可重现的失败场景

1. `cmake --preset linux-debug-tsan && cmake --build --preset linux-debug-tsan`
2. `ctest --preset linux-debug-tsan -R nanosig_test_loop`
3. 观察：`ThreadSanitizer: CHECK failed: sanitizer_thread_registry.cpp:348 "((t)) != (0)"`，调用栈为 `pthread_join` ← `ns_platform_thread_join` (platform/linux/port.c:347) ← `ns_loop_stop` (src/nanosig.c:315) ← `test_async_stop_in_slot` (test/unit/test_loop.c:426)。

该失败在 master 上已经复现：`nightly` workflow 的 `linux-tsan` job 自 2026-10-18 起持续失败（run 39106875930、39218169382、39288768722）。

Windows 等价场景：`ns_loop_stop` 在 slot 内调用 → `WaitForSingleObject(self, INFINITE)` → loop 线程永久挂起。

#### 定位
platform/linux/port.c:347
platform/macos/port.c:376
platform/windows/port.c:270
platform/freertos/port.c:394
src/nanosig.c:328

---

---

<a id="PORT-018"></a>
### PORT-018: Windows `ns_waitable_handle_is_valid` 把文档化的无效哨兵判为有效

- **状态**: 关闭-已修复
- **严重度**: 🟠 高
- **类型**: bug
- **关联**: BROKER-036（`ns_watcher_init` 接受 `events == 0`）与本条同源——都是
- **关闭原因**: `platform/windows/nanosig_platform_port.h:42` 的 `ns_waitable_handle_is_valid` 已改为排除 `NULL` 与 `INVALID_HANDLE_VALUE`；`platform/windows/port.c` 的私有判定已删除（改为直接调用公共谓词的 `ns_waitable_is_invalid`），两处判据不再可能漂移。
- **关闭日期**: 2026-10-18
  "无效输入未被公共层拒绝"，但本条的具体承诺来自平台谓词，记在本模块。

#### 问题描述
`ns_waitable_init` 把 `primitive` 写成全 1（`include/nanosig/nanosig_port.h:109`），
在 Windows 上全 1 **就是** `INVALID_HANDLE_VALUE`。但
`platform/windows/nanosig_platform_port.h:42` 的谓词只有 `((h) != NULL)`，
于是 Windows 成为唯一一个"谓词与 `ns_waitable_init` 互相矛盾"的后端：

- 违反 `ns_waitable_init` docstring"句柄型平台对应无效句柄"；
- 违反 `nanosig_broker.h:146-148`"watcher 为 NULL 时返回全 1 哨兵，经
  `ns_waitable_handle_is_valid` 判定为无效"；
- `platform/windows/port.c:315-321` 自己实现了正确规则
  （`NULL || INVALID_HANDLE_VALUE`），但私有重复，未用于公共谓词。

后果：未绑定的 waitable 能通过 `ns_watcher_init`，直到 `ns_broker_add` 才失败；
`test/test_helpers.h` 的 `test_raw_waitable_is_valid` 在 Windows 上也会把
未绑定的 waitable 报成有效（测试假阳性）。

#### review 建议
```c
#define ns_waitable_handle_is_valid(h) \
    (((h) != NULL) && ((h) != INVALID_HANDLE_VALUE))
```
并删除 `platform/windows/port.c` 内的私有 `ns_waitable_handle_is_invalid`，
`waitset_add` / `waitset_remove` 改用公共谓词，避免两处判据再次漂移。

#### 作者建议
采纳 review 建议：公共谓词补 `INVALID_HANDLE_VALUE` 排除，删除 `port.c` 内的私有
副本，`waitset_add` / `waitset_remove` 改用公共谓词。→ 关闭-已修复

#### 可重现的失败场景
Windows：

```c
ns_platform_waitable_t w;
ns_waitable_init(&w);                     /* primitive == INVALID_HANDLE_VALUE */
ns_watcher_init(&watcher, w.primitive, NS_WAITABLE_EVENT_IN, 0, h); /* 现在返回 NS_E_INVAL */
```

#### 定位
platform/windows/nanosig_platform_port.h:42
platform/windows/port.c:315
include/nanosig/nanosig_port.h:109
include/nanosig/nanosig_broker.h:147

---

<a id="PORT-019"></a>
### PORT-019: Linux `waitset_wait` 在 timerfd 与 waitable 同批就绪时丢弃 completion

- **状态**: 关闭-已修复
- **严重度**: 🟠 高
- **类型**: bug
- **说明**: 既有缺陷，非 `bafb78d` 引入；按 `docs/review/AGENTS.md` 属本模块
- **关闭原因**: `platform/linux/port.c` 的 timerfd sentinel 分支在 `timer_armed` 时改为 `continue`（原为 `return NS_OK`），同一批 `epoll_wait` 结果里排在 timerfd 之后的就绪 fd 不再被丢弃；电平触发与边沿触发两条路径行为一致。
- **关闭日期**: 2026-10-18
  打开问题（PORT-015 是无关的超时换算溢出，已关闭-已拒绝，不构成重复）。

#### 问题描述
`ns_platform_waitset_wait` 把 timerfd 永久注册进 epoll。有限超时 armed 时，
若同一次 `epoll_wait` 中 timerfd 与某个 waitable 同时就绪、且 timer 事件在
`events[]` 里排在前面，循环遇到 sentinel 直接 `return NS_OK`，同一批里后面的
就绪 fd 根本不会被转换成 completion：

- 电平触发（默认）：下一次 `waitset_wait` 会重新报告，可自愈；
- 边沿触发（`edge_triggered != 0` → `EPOLLET`）：内核已把该边沿消费给本次
  `epoll_wait`，不会再报告 → completion 永久丢失，watcher 永不 dispatch。

#### review 建议
遇到 sentinel 且 `timer_armed` 时记录"本轮已超时"并 `continue`，循环结束后
统一返回 `NS_OK`；非 armed 路径（INFINITE/0）的 timerfd 残余 readiness drain
保持现状。

#### 作者建议
按 review 建议修：sentinel 分支在 `timer_armed` 时不再 `return NS_OK`，改为 `continue`
把同一批 epoll 事件里剩余的 fd 全部转成 completion，循环末尾统一返回 `NS_OK`。
→ 关闭-已修复

#### 可重现的失败场景
1. 两个 waitable 以 `edge_triggered = 1` 注册，broker 以有限超时（如 50ms）等待；
2. 恰好某次 timer 到期瞬间，某个 `EPOLLET` fd 变为可读；
3. `epoll_wait` 返回 `[timerfd, fd]` → 修复前代码在 timer 分支 return，fd 的边沿被内核
   消费但从未上报；
4. 该 watcher 的 handler 此后不再被调用，直到用户重新触发另一条边沿。

#### 定位
platform/linux/port.c:528
platform/linux/port.c:532

---

<a id="PORT-022"></a>
### PORT-022: Windows `ns_platform_waitset_deinit` 丢弃 `CloseHandle` 失败

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低
- **类型**: cleanup
- **关闭原因**: `platform/windows/port.c` 的 `ns_platform_waitset_deinit` 改为 `return (CloseHandle(waitset->timer) != 0) ? NS_OK : NS_E_INVAL;`，关闭失败不再被吞掉，三后端失败语义一致。
- **关闭日期**: 2026-10-18

#### 问题描述
`platform/windows/port.c:302` 直接 `CloseHandle(waitset->timer); return NS_OK;`，
关闭失败被静默吞掉。Linux（`platform/linux/port.c:399`）与 macOS
（`platform/macos/port.c:411`）都会把失败传播成 `NS_E_INVAL`，三后端行为不一致。

#### review 建议
```c
return (CloseHandle(waitset->timer) != 0) ? NS_OK : NS_E_INVAL;
```

#### 作者建议
按 review 建议修，与 Linux/macOS 对齐。→ 关闭-已修复

#### 可重现的失败场景
timer handle 已被外部关闭或句柄损坏时，`ns_platform_waitset_deinit` 返回 `NS_OK`，
调用方误以为资源已释放，与 Linux/macOS 上的结果不同。

#### 定位
platform/windows/port.c:302
platform/linux/port.c:399
platform/macos/port.c:411

---

<a id="PORT-023"></a>
### PORT-023: Linux `ns_linux_wakeup_drain` 失败时未消费 eventfd 计数，调用方陷入热自旋

- **状态**: 关闭-已修复
- **严重度**: 🟠 高
- **类型**: bug
- **关联**: 与 PLATFORM-006（Linux `wakeup_drain` 无上界循环，关闭-已拒绝）**不是同一问题**，
  见下方"为何非重复"。

#### 问题描述
`ns_linux_wakeup_drain`（L30-47）只在 `read` 返回 `EAGAIN`/`EWOULDBLOCK` 时返回 `NS_OK`；
遇到其他 errno 直接 `return NS_E_INVAL`，**此时 eventfd 计数器未被读走**。
而 `ns_platform_wakeup_wait`（L132-169）在同一轮已从 `poll` 得到 `POLLIN`，
drain 失败即把该错误返回给调用方。

后果：调用方若按"失败即重试"处理（`src/ns_broker.c` 的 `ns_broker_wait_op_completion`
在无限等待下正是如此），下一轮 `poll` 会**立即**再返回 `POLLIN`——因为计数器仍为正。
这不是"重试前先阻塞一会儿"的慢自旋，而是 **100% CPU、零等待**的热自旋。
Linux 是 CI 运行的平台。

broker 侧的处置：等待失败不返回（否则会遗留仍在 `op_queue` 中的 `req`，构成
use-after-free），只能重试，因此无法在 broker 层消除该自旋。

#### review 建议
`ns_linux_wakeup_drain` 在非 `EAGAIN`/`EINTR` 的 errno 上，先尝试一次非阻塞读以耗尽
计数器（或显式 `read` 到 `EAGAIN` 再返回错误），使"已消费 POLLIN"与"已消费计数"
一致；返回错误码仍保留。若认为该 errno 不可恢复，则在返回错误前把 eventfd
计数清零，保证调用方重试时不会立即再次触发。

#### 作者建议
（待作者补充）

#### 为何非重复（AGENTS.md §3.1）
- **根因不同**：PLATFORM-006 讲的是循环上界（eventfd 是 64 位计数器，溢出需 ~1.8e19
  次写入，不可达）；本条讲的是**错误返回路径未消费计数**。
- **修复方向不同**：PLATFORM-006 的候选修复是"加最大读取次数上限"；本条的修复是
  "在返回错误前消费掉计数"。
- **可达性不同**：PLATFORM-006 的边界不可达；本条只在 `read` 返回非 `EAGAIN`/`EINTR`
- **关闭原因**: Linux wakeup/event drain 的两处逐字重复实现合并为 ns_linux_eventfd_drain：先读空计数再报错；EBADF 例外只告警一次提示调用方不得热重试，调用方不再陷入 100% CPU 热自旋。commit e4264a4。
- **关闭日期**: 2026-10-19
  错误时触发（如 EBADF、EIO），门槛低得多。

#### 可重现的失败场景
Linux，令 wakeup 的 eventfd 处于可读状态，`ns_platform_wakeup_wait` 走
`poll → POLLIN → ns_linux_wakeup_drain`，drain 内的 `read` 返回 `EBADF`：
- drain 返回 `NS_E_INVAL`，计数器未减；
- `ns_broker_wait_op_completion` 重试 → `poll` 立即返回 `POLLIN` → 再次 `EBADF`；
- 调用线程 100% CPU 自旋，`ns_broker_add` 永不返回。

#### 定位
platform/linux/port.c:30-47
platform/linux/port.c:132-169
src/ns_broker.c:239

---

<a id="PORT-031"></a>
### PORT-031: macOS `pthread_attr_setstacksize` 拒绝非 4096 倍数，但 `ns_platform_thread_stack_range` 发布的区间包含这些值

- **状态**: 关闭-已修复
- **严重度**: 🟠 高
- **类型**: bug
- **关闭原因**: 修复 commit 9b149fd
- **关闭日期**: 2026-10-22

#### 问题描述

Darwin 的 `pthread_attr_setstacksize` **拒绝**（EINVAL）任何不是 4096 整数倍的尺寸，而不是向上取整。

但 `ns_platform_thread_stack_range` 在 POSIX 后端发布 `min = PTHREAD_STACK_MIN`、`max = SIZE_MAX`，即宣称"区间内任意值都���受"。于是落在区间内部、但不是 4096 倍数的值（例如 10000、20000、100000）会通过 `ns_loop_config_validate` 的区间检查，在 `ns_loop_create` 阶段被放行，到 `ns_loop_start` 才被 `ns_platform_thread_init` 拒绝并返回 `NS_E_INVAL` —— 没有任何诊断信息。

`include/nanosig/nanosig_loop.h:53` 的字段注释承诺"超出后端可接受区间时 `ns_loop_create` 返回 `NS_E_INVAL`"，隐含"区间内的值可用"；`platform/README.md:34` 声明"后端按自身粒度向上取整" —— 在 macOS 上这两条都不成立。

#### review 建议

两处都要改：

1. POSIX 后端在调用 `pthread_attr_setstacksize` 之前把请求**向上取整到后端粒度**（Darwin：4096 的整数倍；取整后再用取整后的值复核 `PTHREAD_STACK_MIN`），这样就兑现了"实际分配的栈永远不小于请求值"的公共契约（`include/nanosig/nanosig_port.h:463`）。取整粒度应当是一个后端常量，而不是散落的魔数。

2. `ns_platform_thread_stack_range` 的语义要与之自洽：要么发布真实可接受的集合，要么在文档中明确"区间是必要条件而非充分条件"。

3. `platform/README.md:34` 与 `include/nanosig/nanosig_port.h:463` 的"向上取整"表述在 (1) 落地前是错的，需同步。

Linux 侧（glibc）的 `pthread_attr_setstacksize` 只检查 `size < PTHREAD_STACK_MIN`，大概率无此问题，但需在 Linux CI 上用一个非页倍数值确认后再关闭本条。

#### 作者建议

采纳方案 A：POSIX 后端在调用 pthread_attr_setstacksize 之前，按页粒度（4096，硬编码）把请求向上取整，兑现公共契约"实际分配的栈永远不小于请求值"。粒度用常量 4096 而非 getpagesize()：主流 POSIX 平台页大小恒为 4096，行为可预测。下界判定仍用原始请求（min-1 必被拒），取整只影响上送值。同时加防溢出上界。

#### 可重现的失败场景

macOS（本机实测，x86_64 与 arm64 均成立）：

```
PTHREAD_STACK_MIN = 8192
setstacksize(   8192) -> 0    OK
setstacksize(  10000) -> 22   EINVAL
setstacksize(  16384) -> 0    OK
setstacksize(  20480) -> 0    OK
setstacksize( 100000) -> 22   EINVAL
setstacksize(   4096) -> 22   EINVAL
```

库级复现：`ns_init()` → `cfg = NS_LOOP_CONFIG_DEFAULT(); cfg.stack_size = 100000;` → `ns_loop_create(&loop, &cfg)` 返回 `NS_OK`（放行）→ `ns_loop_start(loop)` 返回 `NS_E_INVAL`（拒绝，且无诊断）。

#### 定位
platform/macos/port.c:349
platform/macos/port.c:390
platform/linux/port.c:320
src/nanosig.c:91
include/nanosig/nanosig_port.h:442

---

---

<a id="PORT-033"></a>
### PORT-033: `NANOSIG_BROKER_STACK_SIZE` 没有任何范围校验，文档推荐的 4096 在 host 构建上让 `ns_init()` 直接失败

- **状态**: 关闭-已拒绝
- **严重度**: 🟠 高
- **类型**: bug
- **关闭原因**: 作者仲裁：文档部分已修（README 与规格不再推荐 host 上非法的 4096，改为推荐留 0；若显式锁死须 >= 目标平台最小栈，见 e216c92），构建期校验按作者决定**不加**——文档已不再把用户引向陷阱，而为探测 PTHREAD_STACK_MIN 引入 configure 期编译探测的复杂度不值得。嵌入式默认同时改为 2 KB（e216c92）。
- **关闭日期**: 2026-10-23

#### 问题描述

`NANOSIG_BROKER_STACK_SIZE` 被原样注入并直接交给 `ns_platform_thread_init`（`src/ns_broker.c:551`），**没有与后端自身可接受区间做任何校验**。一个后端会拒绝的值，于是变成 `ns_init()` 返回 `NS_E_INVAL` —— 整个库初始化失败，且没有任何诊断信息或恢复路径。

问题在于这个旋钮是**跨目标共享的单一编译期常量**，而各平台的最小栈大小差异很大：

| 平台 | 最小可接受请求 | 4096 是否可接受 |
|---|---|---|
| macOS x86_64 | 8192（`PTHREAD_STACK_MIN`） | ✗ 拒绝 |
| macOS arm64 | 16384（`PTHREAD_STACK_MIN`） | ✗ 拒绝 |
| Linux | 16384 | ✗ 拒绝 |
| FreeRTOS | 1024 words | ✓ 接受 |
| Zephyr | 512（nanosig 策略值） | ✓ 接受 |

而 `platform/README.md:67` 与 `docs/specs/深度访谈-线程栈大小参数规格.md:163` 恰恰把 **4096** 作为推荐值写给用户（"嵌入式目标若要显式锁死，建议设 4096"）。在嵌入式目标上它是对的；同一个值搬到 host 构建上就让库起不来。

另外，arm64 macOS 上 4096 会被 `pthread_attr_setstacksize` **静默向上取整**到 16 KB —— 也就是说这个"编译期锁死"在不同架构上锁住的根本不是同一个值。

#### review 建议

1. **在构建期校验，而不是运行期**：顶层 `CMakeLists.txt` 里当 `NANOSIG_BROKER_STACK_SIZE != 0` 时，用 `check_c_source_compiles` 探测当前平台的最小可接受栈大小，低于它就 `FATAL_ERROR`（Zephyr 侧已有 Kconfig `range`，可参照）。这样配错在 configure 阶段就暴露，而不是让 `ns_init()` 在运行期莫名其妙地失败。

2. 若走运行期校验路线，则需要把后端最小值暴露到 `src/` —— 而这与"src/ 不做平台判断"的硬约束冲突，故 (1) 是更干净的方案。

3. 修正 `platform/README.md:67` 与规格文档的推荐值：要么改成一个跨平台可接受的值（如 65536），要么明确写清"该值必须 ≥ 目标平台的最小栈大小，且仅适用于嵌入式目标"。

4. 顺带修正"编译期锁死"的表述 —— 在会向上取整的平台上，锁住的是"请求值"而非"实际栈大小"，这个区别应写进文档。

#### 作者建议

作者决定：嵌入式默认栈改为 **2048 字节**（FreeRTOS 与 Zephyr 一致），host 保持外部宏 0 = 平台默认。
为此 FreeRTOS 后端的下限也从 configMINIMAL_STACK_SIZE（1024 words，FreeRTOS 给 idle 任务的惯例值）改为 **nanosig 策略值 1024 字节**，否则 2048 字节（= 256 words @8B/word harness）仍会被旧下限拒绝。
代价（作者已知悉并接受）：broker 线程的栈小于 FreeRTOS 惯例的 idle 任务栈，而它还要执行用户的 watcher handler；由 configCHECK_FOR_STACK_OVERFLOW + 用户自行评估 handler 深度兜底。下限同时改为字节制，顺带消除 FREERTOS-008 里 min 的字节/words 混合语义。

#### 可重现的失败场景

macOS 实测（arm64 构建，`-DNANOSIG_BROKER_STACK_SIZE=4096`）：

```
$ cmake -S . -B build -DNANOSIG_PLATFORM=host -DNANOSIG_BROKER_STACK_SIZE=4096 -DCMAKE_BUILD_TYPE=Release
$ cmake --build build --target nanosig_test_broker_lifecycle
$ ./build/nanosig_test_broker_lifecycle
EXPECT failed at test/unit/test_broker_lifecycle.c:118: ns_init() == NS_OK
exit=1
```

即 `ns_init()` 返回 `NS_E_INVAL`，整个库无法初始化。

#### 定位

src/ns_broker.c:551
CMakeLists.txt:55
CMakeLists.txt:172
platform/README.md:67

---

<a id="PORT-032"></a>
### PORT-032: 线程对象状态重置排在参数校验之后，init 失败后 join 在 4/5 后端仍是 UB 或永久阻塞

- **状态**: 关闭-已拒绝
- **严重度**: 🟠 高
- **类型**: bug
- **关闭原因**: 合同前提保护：include/nanosig/nanosig_port.h 的 ns_platform_thread_join @note 现已文档化「ns_platform_thread_init 失败后，thread 的内容未定义：调用方必须检查其返回值，不得对失败的 thread 调用本函数（否则未定义行为）」。作者决定不为调用方错误兜底（同 C 库惯例）。本轮删除了原先的反向假承诺与两处依赖它的测试断言。
- **关闭日期**: 2026-10-23

#### 问题描述

本次改动为"init 失败后 join 返回 `NS_E_INVAL`"引入了状态标记（POSIX 的 `created`、FreeRTOS 的 `join_sem == NULL`、Windows 的 `thread == NULL`），但**状态重置被放在了参数校验之后**：

- `platform/linux/port.c:339` / `platform/macos/port.c:368`：`if((thread == NULL) || (entry == NULL)) return NS_E_INVAL;` 在 `thread->created = 0;`（:344 / :373）**之前**返回 → `created` 保持调用方存储里的旧值。
- `platform/freertos/port.c:387`：同一顺序 → `join_sem` 保持旧值。
- `platform/windows/port.c:269`：同一顺序；且新加的栈大小校验（:271-275）也在 `thread->thread` 赋值（:281）之前返回 → HANDLE 保持旧值。

只有 Zephyr 做对了：`platform/zephyr/port.c:282` 的 `*thread = NULL;` 在 `entry == NULL` 检查**之前**。

后果（`entry == NULL` 这条是显式处理的错误路径，不是未文档化的前提）：调用方传入未初始化的线程存储 → init 返回 `NS_E_INVAL` → 随后的 join 拿到的是垃圾状态：

| 后端 | 后续 join 行为 |
|---|---|
| Linux / macOS | `pthread_join(垃圾 pthread_t)` → 未定义行为 |
| Windows | `WaitForSingleObject(垃圾 HANDLE, INFINITE)` → 永久阻塞或崩溃 |
| FreeRTOS | `xSemaphoreTake(垃圾句柄, portMAX_DELAY)` → 永久阻塞 |

`include/nanosig/nanosig_port.h:480` 明确承诺"`ns_platform_thread_init` **失败**后不得视为线程已创建：此时 join 返回 `NS_E_INVAL`（不阻塞、不崩溃）。五个后端一致。" —— 该承诺在 4/5 后端为假。

新增测试之所以没抓到：`test/unit/test_platform_backend.c:158` 复用的是同一个 `thread` 变量，而它在此之前已被成功创建并 join 过（:140-149），`created` 已被清零；`entry == NULL` 那条（:116）之后从未跟 join。

#### review 建议

1. 把状态重置挪到 `ns_platform_thread_init` 的**最前面**（`thread == NULL` 检查之后、任何其它提前返回之前），五个后端统一照 Zephyr 的写法：`thread->created = 0;`（POSIX）、`thread->task = NULL; thread->join_sem = NULL;`（FreeRTOS）、`thread->thread = NULL;`（Windows）。

2. 补测试：每种失败模式用**全新的**线程存储（先 memset 成毒值），覆盖 `entry == NULL`、`stack_size` 低于 `min`、`stack_size` 高于 `max` 三条路径，断言随后的 `ns_platform_thread_join` 一律返回 `NS_E_INVAL`。当前测试复用同一个变量，正是它掩盖了这个缺陷。

3. 公共头 `include/nanosig/nanosig_port.h:480` 的"五个后端一致"表述在 (1) 落地前是错的，需同步。

#### 作者建议

作者意见：调用方本来就会判断 init 的返回值。init 失败后仍去 join 属调用方错误，与 C 库惯例一致（fopen 返回 NULL 后不得 fclose），库没有义务兜底。因此不改代码，改为在公共头里把这条前提**明确文档化**（原先是反向承诺——写了「五个后端一致返回 NS_E_INVAL」，那是假的），并删除两处依赖该假承诺的测试断言（它们实际依赖线程对象的复用历史，属于对未定义行为的过度规定）。

#### 可重现的失败场景

macOS 实测：

```c
ns_platform_thread_t t;
memset(&t, 0xFF, sizeof(t));                        /* 未初始化的调用方存储 */
ns_platform_thread_init(&t, NULL, NULL, "x", 0u);   /* -> NS_E_INVAL */
printf("t.created = %d\n", t.created);              /* -> -1（残留值，应为 0） */
/* 此刻 ns_platform_thread_join(&t) 会对垃圾 pthread_t 调 pthread_join → UB */
```

Windows 另有一条本次新增的触发路径：`ns_platform_thread_init(&t, entry, arg, "x", 1024)`（< 16 KB 策略下限）→ `NS_E_INVAL`，而 `t.thread` 从未被赋值 → 随后的 join 对垃圾 HANDLE 调 `WaitForSingleObject`。

#### 定位
platform/linux/port.c:339
platform/linux/port.c:344
platform/macos/port.c:368
platform/windows/port.c:269
platform/windows/port.c:271
platform/freertos/port.c:387
platform/zephyr/port.c:282
include/nanosig/nanosig_port.h:480

---

<a id="PORT-024"></a>
### PORT-024: macOS `waitset_add` 在 kqueue 变更列表部分失败时泄漏 knote，且该 knote 无法回收

- **状态**: 关闭-已修复
- **严重度**: 🟠 高
- **类型**: bug
- **置信度**: 高（本机 macOS 实测复现）
- **关闭原因**: 修复 commit 3092099
- **关闭日期**: 2026-10-24

#### 问题描述
`ns_macos_waitset_apply_changes`（platform/macos/port.c:448-468）把整批 kevent 变更一次提交，
用"返回值非 0 即整体失败"处理。但 `kevent()` 对变更列表是**逐条应用、遇到第一条错误即停止**：
错误条目之后的变更不生效，**之前的变更已经生效**（本机三组探针确认：[无效, 有效] 时有效条目
不生效；[有效, 无效] 时有效条目已生效）。

而 `ns_macos_waitset_make_changes`（:419-446）在 `events` 同时含 IN 与 OUT 时生成 2 条变更
（EVFILT_READ + EVFILT_WRITE），并非每种 fd 都同时支持两个 filter：kqueue fd 的 EVFILT_WRITE
返回 EINVAL、`/dev/null` 两个 filter 都返回 EINVAL（本机实测）。

后果（`ns_platform_waitset_add` :470-495）：

- 第一条变更（EVFILT_READ）已在 kqueue 上注册并携带 `udata = &caller_waitable`，但函数返回
  `NS_E_INVAL`，`waitset->count` 不增、`waitable->registered_waitset` 保持 NULL；
- 该 waitable 在库视角"未注册"，但 kqueue 里仍有它的 knote，`ns_macos_waitset_wait`（:584-605）
  只按下标读 `udata`，会**照常为它产出 completion**；
- `ns_platform_waitset_remove`（:505-521）因 `registered_waitset != waitset` 立即返回
  `NS_E_INVAL`，调用方**没有任何 API 能摘掉这条 knote**；
- 调用方收到 `NS_E_INVAL` 后按合同可立即释放该 waitable（`ns_watcher_t` 常为栈上对象），
  kqueue 中 `udata` 即成悬垂指针；下一次 `waitset_wait` 把该 completion 交给
  `ns_broker_dispatch_pending_events`（src/ns_broker.c:336）解引用 `w->user_data` → use-after-free。

同一根因在删除路径方向相反：`EV_DELETE` 列表若第一条已注册、第二条未注册，`kevent` 返回 -1，
但第一条**已经被删除**——调用方看到 `NS_E_INVAL`（认为仍注册），实际该 waitable 已从 kqueue 摘除
（本机实测）。

#### review 建议
不要再把 `kevent()` 的返回值当作"整批成败"：
1. 每条变更单独提交（`change_count == 1`），失败时对**已生效**的变更做反向回滚
   （add 失败回滚 EV_DELETE，delete 失败回滚 EV_ADD）；或
2. 提交时带 `EV_RECEIPT`，逐条读回错误事件，按实际生效的条目更新内部状态与 `udata`；或
3. 最小改动：`add` 失败后对该 fd 本次涉及的 filter 集合补发一次 `EV_DELETE` 清场，保证不留下
   无法回收的 knote；`remove` 失败时不要跳过 `count--` 与 `registered_waitset = NULL`，而应按
   "部分摘除"的实际结果重算。

方向 1 最干净，且与 Linux（单 fd 单 `epoll_ctl`，原子）的行为模型一致。

#### 作者建议
采纳方向 1：逐条提交 + 反向回滚。已实现于 platform/macos/port.c——新增
`ns_macos_waitset_revert_change()`（ADD 回滚为 DELETE；DELETE 回滚为
`EV_ADD|EV_ENABLE`，`edge_triggered` 时补回 `EV_CLEAR`），`apply_changes` 每轮只提交一条
变更，失败时先逐条回滚已生效条目再返回 `NS_E_INVAL`；`remove` 路径改为把 `waitable`
传给 `apply_changes`，使 DELETE 的回滚能还原 `EV_CLEAR`。对外行为不变：add/remove 仍是
全有或全无，失败时 `count` 与 `registered_waitset` 均不改变。回归测试
`test_waitset_partial_add_rollback`（kqueue fd + `IN|OUT`，修复前必失败）。

#### 可重现的失败场景
macOS 实测（直接调用平台层，非经 broker）：

```c
int inner = kqueue();   /* kqueue fd：EVFILT_READ 可注册，EVFILT_WRITE 返回 EINVAL */
ns_platform_waitset_init(&ws);
ns_waitable_init(&w);
w.primitive = inner;
w.events = NS_WAITABLE_EVENT_IN | NS_WAITABLE_EVENT_OUT;
w.user_data = (void *)0xDEAD;

ns_platform_waitset_add(&ws, &w);              /* -> -3 (NS_E_INVAL) */
/* 此时 w.registered_waitset == NULL，但 kqueue 上留有一条 EVFILT_READ knote */

/* 触发 inner kqueue 的 EVFILT_USER，使其可读 */
ns_platform_waitset_wait(&ws, 300000u, cc, 4, &cnt);
/* cnt == 1，cc[0].waitable == &w，user_data == 0xDEAD —— 为"未注册"的 waitable 产出了 completion */

ns_platform_waitset_remove(&ws, &w);           /* -> -3：摘不掉 */
```

实测输出：`add -> -3`、`wait -> cnt=1 / user_data=0xdead`、`remove -> -3`、
`registered_waitset` 全程为 NULL。其余 macOS 版本与 Apple Silicon/Intel 差异未复核，
但"逐条应用、首错停止"是 kevent 内核语义。

#### 定位
platform/macos/port.c:448
platform/macos/port.c:463
platform/macos/port.c:470
platform/macos/port.c:489
platform/macos/port.c:505
platform/macos/port.c:515
src/ns_broker.c:336

---

<a id="PORT-027"></a>
### PORT-027: macOS `event_signal` 写已关闭的 pipe 读端会触发 SIGPIPE，进程直接终止

- **状态**: 关闭-已修复
- **严重度**: 🟡 中
- **类型**: bug
- **置信度**: 高（本机 macOS 实测：写已关闭读端的 pipe → 进程被 SIGPIPE 杀死，退出码 141）
- **关闭原因**: 修复 commit f2f64b2
- **关闭日期**: 2026-10-24

#### 问题描述
macOS 的 event 用 pipe 实现（platform/macos/port.c:232-246），`signal_handle` 是写端 fd。
`ns_platform_event_signal`（:249-263）直接 `write(fds[1], ...)`，**没有任何 SIGPIPE 防护**：
全树 `grep -rn "SIGPIPE\|F_SETNOSIGPIPE"` 在 src/、platform/、include/、test/ 下 **0 命中**。

一旦读端已关闭而写端仍被写入，macOS 默认投递 SIGPIPE——**进程立即终止**，不是返错。
`ns_platform_event_deinit`（:271-287）会关闭两个 fd，而 `nanosig_port.h:18` 明确
"`ns_platform_event_signal` 允许跨线程调用"，头文件的 thread-safety 段落只禁止同一对象上的
init/deinit 并发与等待类调用并发，**从未**声明 `signal` 不得与 `deinit` 并发；调用方按字面
理解在另一线程 signal、主线程 deinit，就会命中该路径。

这也与文档承诺冲突：`nanosig_port.h:307-308` 写"signal 遇底层缓冲已满（macOS pipe EAGAIN）
视为成功返回 NS_OK，**仅真实写错误才返回错误码**"——EPIPE 正是"真实写错误"，但进程先死，
调用方永远拿不到那个错误码。

三宿主横向对比：Linux eventfd 写已关闭 fd 得 `EBADF` → `NS_E_INVAL`；Windows `SetEvent`
在已关闭 handle 上返回 0 → `NS_E_INVAL`；只有 macOS 会**杀死进程**。wakeup 侧不受影响
（`ns_macos_user_event_signal` 写的是 kqueue fd，不产生 SIGPIPE）。

#### review 建议
在 `ns_platform_event_init` 创建 pipe 后对写端关闭 SIGPIPE 语义：

```c
#ifdef F_SETNOSIGPIPE
    if(fcntl(fds[1], F_SETNOSIGPIPE, 1) < 0) { /* 回滚两个 fd 并返回 NS_E_NOMEM */ }
#endif
```

本机实测：`fcntl(F_SETNOSIGPIPE, 1)` 在 pipe 写端返回 0，之后对已关闭读端的写入返回
`-1/EPIPE` 且进程存活（原有 errno 分支随即返回 `NS_E_INVAL`）。这样"文档承诺返回错误码"
重新成立，且不改变正常路径。若作者判断该并发调用的前提本就应由调用方保证，退一步的方案是
在 `nanosig_port.h` 的 event 段落补一条 `@warning`（`signal` 不得与 `deinit` 并发）——
但库内 API 能被一次正常调用杀死进程，仍建议加这一行加固。

#### 作者建议
采纳加固方案：`ns_platform_event_init` 创建 pipe 后对写端执行
`fcntl(fds[1], F_SETNOSIGPIPE, 1)`（`#ifdef F_SETNOSIGPIPE` 内，失败则关闭两个 fd 返回
`NS_E_NOMEM`）。读端关闭后再写只得到 `EPIPE`，进程不再被 SIGPIPE 终止，原有 errno 分支
即可返回 `NS_E_INVAL`。未补测试：验证该路径需要真的杀掉宿主进程或违反 signal/deinit
不并发的调用前提，不适合放进进程内单元测试。

#### 可重现的失败场景
```c
int fds[2]; pipe(fds);
close(fds[0]);                       /* 等价于 event 的 waitable 侧已释放 */
write(fds[1], "x", 1);               /* 进程收到 SIGPIPE */
```

本机运行结果：进程被信号终止，退出码 141（128+13），后续语句不执行——**不是**返回 -1。
同一写操作若先 `fcntl(fds[1], F_SETNOSIGPIPE, 1)`，则返回 -1 / `EPIPE`，进程继续运行。
落到 nanosig 上即：线程 A `ns_platform_event_signal(&e)` 与线程 B
`ns_platform_event_deinit(&e)` 并发 → 进程死亡而非 `NS_E_INVAL`。

#### 定位
platform/macos/port.c:249
platform/macos/port.c:256
platform/macos/port.c:271
platform/macos/port.c:281
include/nanosig/nanosig_port.h:18
include/nanosig/nanosig_port.h:307
platform/linux/port.c:205
platform/windows/port.c:152

---

<a id="PORT-021"></a>
### PORT-021: 平台文档仍描述重构前的原语

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低
- **类型**: doc
- **关闭原因**: 修复 commit 43fe3ec
- **关闭日期**: 2026-10-24

#### 问题描述
`bafb78d` 改写了平台所有权模型，但以下描述未同步：

- `platform/README.md:93`："wakeup 使用 kqueue `EVFILT_USER`，通过 wakeup 自身的
  kqueue fd 暴露为 waitable"——重构后 wakeup 不再暴露 waitable，`NS_WAITABLE_GET`
  已删除；
- `platform/README.md:102`：Windows 线程写 `CreateThread`，实现是
  `_beginthreadex`（`platform/windows/port.c:256`），属 PLATFORM-013 已修文档的回归；
- `docs/agents/platform/AGENTS.md:67`：同上 `CreateThread`；
- `docs/agents/platform/AGENTS.md:69`：仍写 "`k_malloc`/`k_free` for internal state"，
  与 PORT-020 的实际实现（`k_aligned_alloc`）不一致；
- `docs/ARCHITECTURE.md:271`：跨模块表格同样写 `CreateThread`。

#### review 建议
按上述逐行更新；`docs/ARCHITECTURE.md` 的同一措辞记入 `global-review.md`
（跨模块问题按规范写全局文档）。

#### 作者建议
修订后重新核对，本条实际只剩 **1 处** 仍然过期；其余 3 处在写条目之后已被修复，或原本就判断有误：

- platform/README.md:156（原 :93）：macOS wakeup "暴露为 waitable" —— **仍过期，本次已改**；
- platform/README.md:102 的 `CreateThread`：已随 GLOBAL-006 同批改为 `_beginthreadex`；
- docs/agents/platform/AGENTS.md:67：现写作 `_beginthreadex`（not `CreateThread`），已修；
- docs/agents/platform/AGENTS.md:69 的 `k_malloc`/`k_free`：**当时即误报**——platform/zephyr/port.c:128
  至今仍是 `k_malloc`，文档与代码一致，不存在 `k_aligned_alloc`；
- docs/ARCHITECTURE.md:271：已随 GLOBAL-006 改为 `_beginthreadex`。

#### 影响面
纯文档，无代码调用者。受影响的是照 README 编写 macOS wakeup 等待代码的读者
（会去找一个不存在的 waitable 转换）。

#### 可重现的失败场景
照 README 在 macOS 上调用 wakeup 侧转换宏（`NS_WAITABLE_GET`）→ 编译错误，
宏已不存在；照 Windows 小节期望 `CreateThread` 语义（如继承句柄/线程退出码）→
与实际 `_beginthreadex` 行为不符。

#### 定位
platform/README.md:156

---

<a id="PORT-029"></a>
### PORT-029: platform/README.md 两处描述与实现不一致（句柄谓词、Windows completion 条数）

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低
- **类型**: doc
- **置信度**: 高
- **关闭原因**: 修复 commit 43fe3ec
- **关闭日期**: 2026-10-24

#### 问题描述
- platform/README.md:72 写"句柄有效性用平台头提供的 `ns_waitable_handle_is_valid(h)` 判定：
  POSIX 为 `h >= 0`，**Windows 为 `h != NULL`**，RTOS 为 `h != NULL && h != (void *)~0`"。
  但 `platform/windows/nanosig_platform_port.h:44-45` 的谓词（随 PORT-018 修复）已变为
  `h != NULL && h != INVALID_HANDLE_VALUE`；
- platform/README.md:225 写"Windows 后端单次 wait **最多返回 1 个 completion**（auto-reset event
  语义）"。实际 `ns_platform_waitset_wait`（platform/windows/port.c:429-452）在 WFMO 报告首个
  signaled handle 之后，还会用 `WaitForSingleObject(handles[i], 0u)` 扫描其余用户 handle，把
  同时 signaled 的成员一并写入 completions——**单次可返回多个** completion。

本条与 PORT-021（讲 platform/README.md:93 的 wakeup waitable 与 :102 的 `CreateThread`）同属
"文档落后于实现"，但**具体行与具体主张均不同**，非 §3.1 意义上的重复：第一条是 PORT-018 修复后
未同步的文档遗留，第二条是 Windows 多 completion 扫描（早于本次修复即存在）从未反映到文档。

#### review 建议
- platform/README.md:72 改为"Windows 为 `h != NULL && h != INVALID_HANDLE_VALUE`"；
- platform/README.md:225 改为"单次 wait 可返回多个 completion：WFMO 报告首个，其余同时就绪的
  成员由零超时等待扫描捕获"，并删除"auto-reset event 语义 ⇒ 最多 1 个"的推论。

#### 作者建议
采纳，两处均已按实现改写：句柄谓词改为 `h != NULL && h != INVALID_HANDLE_VALUE`；
Windows completion 说明改为"可返回多个（WFMO 报告首个 + 零超时扫描其余）"。

#### 影响面
纯文档，无代码调用者。受影响的是按 README 规划 Windows waitset 行为的调用方
（句柄有效性判断、completions 数组容量规划）。

#### 可重现的失败场景
- 照 README:72 的"Windows 只需判 NULL"口径编写调用方，把 `ns_waitable_init` 产出的
  `INVALID_HANDLE_VALUE` 哨兵交给 `ns_platform_waitset_add`：期望注册成功（文档口径），
  实际返回 `NS_E_INVAL`（实现口径）；
- 照 README:225 假定 Windows 每次至多 1 个 completion 编写 `completions` 下标规划：两个用户
  handle 同时 signaled 时一次拿到 2 个 completion，数组规划（如 `count <= 1`）失效。

#### 定位
platform/README.md:135
platform/README.md:294
platform/windows/nanosig_platform_port.h:44
platform/windows/port.c:443

---

<a id="PORT-016"></a>
### PORT-016: 测试 `test_waitset_events_zero` 未覆盖 Windows 路径

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低
- **类型**: test
- **关闭原因**: 修复 commit 03ee7f1
- **关闭日期**: 2026-10-24

#### 问题描述
`test_waitset_events_zero` 用 `#if defined(__linux__) || defined(__APPLE__)` 条件编译，仅在 POSIX 平台运行，Windows 路径未覆盖。

#### review 建议
去掉条件编译守卫，改为三平台通用测试。

#### 作者建议
与 PORT-026 一并修：Windows 改为 IN-only（掩码不含 IN 或含 OUT/ERR 时 `add` 返回
`NS_E_INVAL`），因此 `events == 0` 在 Windows 上的期望是"注册被拒绝"，在 Linux/macOS 上
是"注册成功且永不触发"。用例已去掉 `#if` 守卫，按平台断言各自的契约。

#### 影响面
`test/unit/test_platform_backend.c` 的 `test_waitset_events_zero`，Windows CI 首次编译并运行该用例。

#### 可重现的失败场景

test/unit/test_platform_backend.c 的 test_waitset_events_zero 被
`#if defined(__linux__) || defined(__APPLE__)` 包裹，Windows 构建不编译该测试，
waitset events=0 路径无覆盖。

#### 定位
test/unit/test_platform_backend.c:905-939

---

<a id="PORT-026"></a>
### PORT-026: Windows 后端完全忽略 `waitable->events`，恒报 `NS_WAITABLE_EVENT_IN`

- **状态**: 关闭-已修复
- **严重度**: 🟡 中
- **类型**: bug
- **置信度**: 高（代码走查；本机为 macOS，未在 Windows 上运行）
- **关闭原因**: 修复 commit 03ee7f1
- **关闭日期**: 2026-10-24

#### 问题描述
`ns_platform_waitset_add`（platform/windows/port.c:323-339）**从不读取** `waitable->events`：
既不校验掩码，也不记录关注位。`ns_platform_waitset_wait`（:374-457）在报告 completion 时
硬编码 `triggered_events = NS_WAITABLE_EVENT_IN`（:438、:447）。于是 Windows 的行为是
"任何 waitable 一律按 IN 上报"：

- 只注册 `NS_WAITABLE_EVENT_OUT` 或只注册 `NS_WAITABLE_EVENT_ERR` 的 watcher 在 Windows 上
  `add` 返回 `NS_OK`，随后只要该 handle 变为 signaled 就以 `NS_WAITABLE_EVENT_IN` 触发
  handler——调用方按"可读"分支去读一个只申请了"可写"的句柄；
- Linux（platform/linux/port.c:402-411，掩码映射 `EPOLLIN/EPOLLOUT/EPOLLERR`）与 macOS
  （platform/macos/port.c:419-446，掩码决定是否生成 `EVFILT_READ/EVFILT_WRITE`）都按掩码
  注册，掩码在三宿主中语义不一致；
- FreeRTOS / Zephyr 对含 OUT/ERR 的掩码直接返回 `NS_E_INVAL`，这一点已文档化
  （include/nanosig/nanosig_port.h:506-508）。Windows 既未跟随 RTOS 的"拒绝"，也未实现
  掩码语义，属第三种行为。

公共文档未规定 Windows 忽略事件掩码：`include/nanosig/nanosig_port.h:77-79` 只定义三个位，
platform/README.md:100-109 的 Windows 小节只写"`edge_triggered` 参数忽略"，未提 `events`，
故不属 §2.4 文档仲裁范围。测试侧同样未覆盖：test/unit/test_broker_event.c:221-223 的
OUT 投递用例在 `_WIN32` 下直接 `return 0`（socketpair 不可用），Windows 的掩码行为无断言。

#### 为何非重复（AGENTS.md §3.1）
BROKER-036（`关闭-已修复`）讲的是 **`events == 0`** 被 host 后端接受、RTOS 后端拒绝；
本条讲的是**非零掩码中的 OUT/ERR 位在 Windows 上被静默降级为 IN**。输入不同、代码位置不同
（broker 校验函数 vs `platform/windows/port.c` 的 add/wait）、修复方向不同。

#### review 建议
三选一，需作者决策后统一到文档（这是公共契约变更，不是纯实现修复）：

1. **（推荐）Windows `waitset_add` 拒绝非 IN 位**：与 FreeRTOS/Zephyr 对齐，`add` 返回
   `NS_E_INVAL`；`nanosig_port.h:506-508` 的 `@note` 把 Windows 一并列入 IN-only 后端。
   代价：今天在 Windows 上用 `IN|OUT` 掩码"能跑"的调用方会收到新错误码。
2. **实现 OUT 语义**：WFMO 无法表达"可写/错误"，只能对 `NS_WAITABLE_EVENT_OUT` 额外用
   `WaitForSingleObject(handle, 0)` 试探——但句柄的 signaled 状态被消费会破坏已有的
   auto-reset 语义，不建议。
3. **纯文档化**：明确写"Windows 忽略 OUT/ERR，一律按 IN 上报"，并在 `nanosig_port.h` 的
   `NS_WAITABLE_EVENT_OUT/ERR` 处标注该限制。

无论选哪个，都应补一个覆盖 `_WIN32` 的掩码用例（当前 `_WIN32` 分支被跳过）。

#### 作者建议
采纳方案 1：Windows 与 FreeRTOS / Zephyr 对齐，`ns_platform_waitset_add` 增加掩码校验——
不含 `NS_WAITABLE_EVENT_IN`（含 `events == 0`）或含 `OUT`/`ERR` 时返回 `NS_E_INVAL`。
不在 `ns_watcher_init` 层拦截，保持契约落在"该后端是否支持这个掩码"的地方。

同步更新：`nanosig_port.h` 的 `ns_platform_waitset_add` `@note` 把 Windows 列入 IN-only；
`platform/README.md` 的 Windows 小节补一条 IN-only 说明；`test_waitset_events_zero` 去掉
`#if` 守卫，Windows 断言 `NS_E_INVAL`、POSIX 断言 `NS_OK` + 不触发（即 PORT-016）。

#### 影响面
`ns_platform_waitset_add` 的调用者：src/ns_broker.c 的 `ns_broker_add`（watcher 注册路径）。
公共契约收紧：Windows 上今天用 `IN|OUT` 或只 `OUT` 能注册成功的调用方，之后会收到
`NS_E_INVAL`，且错误发生在 `ns_broker_add` 而不是 `ns_watcher_init`。

#### 可重现的失败场景
Windows（按代码走查构造，未实测）：

```c
ns_watcher_t w = {0};
ns_watcher_init(&w, handle, NS_WAITABLE_EVENT_OUT, 0, on_writable);  /* NS_OK */
ns_broker_add(&w);                                                   /* NS_OK */
/* handle 被 signal 后 handler 收到 triggered_events == NS_WAITABLE_EVENT_IN(0x1)，
   而 Linux 上是 0x2(OUT) —— 调用方按 IN 分支去 read() 一个只申请了可写的句柄 */
```

同一段代码在 FreeRTOS / Zephyr 上 `ns_broker_add` 返回 `NS_E_INVAL`。

#### 定位
platform/windows/port.c:352
platform/windows/port.c:470
platform/windows/port.c:479
platform/linux/port.c:402
platform/macos/port.c:419
include/nanosig/nanosig_port.h:534
platform/README.md:175
test/unit/test_platform_backend.c:908

---

<a id="PORT-034"></a>
### PORT-034: Linux waitset 提前返回后未解散 timerfd，残留定时器在后续无限等待中造成虚假唤醒

- **状态**: 关闭-已修复
- **严重度**: 🟡 中（前提：连续两次 wait，第一次带超时且被 fd 提前唤醒，第二次为无限等待；仅 Linux 后端；修复前由新增用例实测复现）
- **类型**: bug
- **关闭原因**: 修复 commit 93be19d
- **关闭日期**: 2026-10-24

#### 问题描述
Linux 后端的微秒超时用常驻 epoll 的 timerfd 实现：每次 wait 前 `timerfd_settime` 上弦，
然后以 `epoll_wait(..., -1)` 无限等。当某个 fd 在定时器到期前就绪（或 EINTR）而提前返回时，
内核里的倒计时**仍在继续**，却没有任何路径去解散它。

下一次调用若是无限等待，代码不会再上弦，也不会撤掉上一个，于是旧定时器到点自己 fire，
`epoll_wait` 返回一个只含 timerfd sentinel 的结果：`out_count == 0`，函数返回 `NS_OK`。
调用方看到"等待成功但没有任何事件"，且远早于预期，broker 白跑一轮再回去等。

旧代码只在 `timer_armed == 0` 分支里 drain timerfd，armed 时直接 `continue`，因此复现路径一直存在。
本条与 PORT-019（同一次 `epoll_wait` 内排在后面的就绪事件被丢弃）不是同一问题：
PORT-019 是**同批丢失**，本条是**跨调用残留**。

#### review 建议
`epoll_wait` 一返回（无论正常、EINTR 还是出错）就在 `timer_armed` 为真时立即解散定时器：

```c
struct itimerspec off = {0};          /* it_value 全 0 = 撤弦，同时清空过期计数 */
if(timerfd_settime(waitset->timer_fd, 0, &off, NULL) < 0){
    /* 撤不掉就兜底读走计数，避免电平触发的 timerfd 持续就绪变成热自旋 */
    do { rd = read(waitset->timer_fd, &dummy, sizeof(dummy)); } while(rd < 0 && errno == EINTR);
}
```

用 `settime` 而非 `read`，是因为它同时清空"已到期计数"，正常路径不需要额外 read。
同时删除 sentinel 分支里 `if(timer_armed) continue;` 的特判与残留 drain 分支。

#### 作者建议
采纳，按上述实现：撤弦放在 `epoll_wait` 返回后、处理事件前，覆盖全部返回路径；
`settime` 失败时兜底 `read` 并打日志。新增
`test_waitset_timeout_early_return_disarms`（200ms 定时器被 fd 提前唤醒 → 无限等待必须睡满
600ms），修复前该用例 `cnt == 0`、耗时约 200ms，必然失败。

#### 影响面
`ns_platform_waitset_wait` 的唯一实现点（platform/linux/port.c）。
上层调用者：src/ns_broker.c 的 broker 等待循环，以及直接调用 `ns_platform_waitset_wait` 的用户代码。
仅 Linux 受影响：macOS 用 `kevent` 的 `timespec` 参数、Windows 用 `WaitableTimer` 句柄，
都不存在跨调用的残留定时器。

#### 可重现的失败场景
1. signal 一个 eventfd，以 200ms 超时等待 → fd 已就绪，立即返回（定时器尚未到期）；
2. 把 eventfd 读空，发起 `NS_PLATFORM_WAIT_INFINITE_US` 等待，另一线程 600ms 后才 signal；
3. 修复前：约 200ms 时残留定时器 fire，本轮返回 `out_count == 0`，用例的 `cnt == 1` 与
   耗时 ≥ 550ms 两条断言同时失败；
4. 修复后：必须等到 fd 就绪，`cnt == 1`、耗时约 600ms。

#### 定位

platform/linux/port.c:598
platform/linux/port.c:630
test/unit/test_platform_backend.c:1097

---

<a id="PORT-025"></a>
### PORT-025: macOS 允许两个 waitable 注册同一 fd，`remove` 其中一个会静默废掉另一个

- **状态**: 关闭-已拒绝
- **严重度**: 🟠 高
- **类型**: bug
- **置信度**: 高（本机 macOS 实测复现）
- **关闭原因**: 作者决定不修（同一 fd 重复注册属调用方错误；按 fd 判重需额外映射）。决策已随代码落地：platform/macos/port.c:573 的 shortcut 注释声明了限制与升级条件（出现丢事件或 waitset 无法 deinit 时改为按 fd 判重），reviewer 依 AGENTS.md §2.8 不得重复提出。
- **关闭日期**: 2026-10-25

#### 问题描述
`ns_platform_waitset_add`（platform/macos/port.c:470-495）只用
`waitable->registered_waitset != NULL` 判重，**不检查该 fd 是否已被另一个 waitable 注册**。
kqueue 的 knote 以 (fd, filter) 为键，第二次 `EV_ADD` 直接覆盖 `udata`（本机实测），于是：

- 两个不同 waitable 对同一 fd `add` 都返回 `NS_OK`，但只有**后注册者**能收到 completion，
  先注册者静默永不触发；
- `remove` 先注册者时，`ns_macos_waitset_make_changes(..., EV_DELETE)`（:508-513）把该
  (fd, filter) 的 knote 整个删掉，后注册者的 knote 随之消失，但它自己的
  `registered_waitset` 仍指向该 waitset——此后它**既不触发、也摘不掉**（后续 `remove`
  因 knote 已不在而返回 `NS_E_INVAL`，`remove` 的前置检查通过但底层 knote 已丢失）。

对照另外两个宿主后端：Linux `epoll_ctl(EPOLL_CTL_ADD)` 对已注册 fd 返回 `EEXIST` →
`NS_E_EXISTS`（platform/linux/port.c:439-440）；Windows 用 `ns_waitset_find` 按 handle 判重 →
`NS_E_EXISTS`（platform/windows/port.c:331）。macOS 是三宿主里唯一不判重的后端，同一段调用方
代码在 Linux/Windows 上拿到 `NS_E_EXISTS`、在 macOS 上拿到 `NS_OK` 并静默失效。

`include/nanosig/nanosig_port.h:503-504` 与 platform/README.md:41 只承诺"同一 **waitable** 已注册
返回 `NS_E_EXISTS`"，未规定"两个 waitable 注册同一句柄/fd"的行为，故不属 `docs/review/AGENTS.md`
§2.4 的"已文档化设计"。与 PORT-014（关闭-延期，讲同一 waitable 的多事件合并）不同：本条讲两个
不同 waitable 撞同一 fd 时的判重缺失及其连带失效，根因与修复方向均不同。

#### review 建议
在 `ns_platform_waitset_add` 增加按 fd 的判重（仿 Windows 的 `ns_waitset_find`），命中返回
`NS_E_EXISTS`，并把"同一 fd 不可被两个 waitable 同时注册"补进 `nanosig_port.h` 中
`ns_platform_waitset_add` 的契约（三宿主一致）。若不打算判重，则至少要文档化该情形为未定义，
但 `remove` 的连带失效（静默废掉另一个已注册 waitable）仍需修复——后者本身就是缺陷。

#### 作者建议

不修。同一 fd 由调用方重复注册属违反使用约定；按 fd 判重需要维护 fd→waitable 映射，代价与收益不匹配。已在代码现场留下决策标记（platform/macos/port.c:573 的 shortcut 注释），声明升级条件：出现丢事件或 waitset 无法 deinit 时改为按 fd 判重。

#### 可重现的失败场景
macOS 实测：

```c
pipe(p);
ns_waitable_init(&a); a.primitive = p[0]; a.events = NS_WAITABLE_EVENT_IN;
ns_waitable_init(&b); b.primitive = p[0]; b.events = NS_WAITABLE_EVENT_IN;

ns_platform_waitset_add(&ws, &a);     /* macOS: NS_OK；Linux/Windows: NS_E_EXISTS */
ns_platform_waitset_add(&ws, &b);     /* macOS: NS_OK */
write(p[1], "x", 1);
ns_platform_waitset_wait(&ws, 200000u, cc, 4, &cnt);
/* cnt == 1，cc[0].waitable == &b —— a 永不触发 */

ns_platform_waitset_remove(&ws, &a);  /* NS_OK，但把 b 的 knote 一并删掉 */
write(p[1], "x", 1);
ns_platform_waitset_wait(&ws, 200000u, cc, 4, &cnt);
/* cnt == 0，而 b.registered_waitset 仍指向 ws */

ns_platform_waitset_remove(&ws, &b);  /* NS_E_INVAL：b 再也摘不掉 */
```

实测输出依次为：`add A=0`、`add B=0`、`wait cnt=1 -> B`、`remove A=0`、`wait cnt=0`、`remove B=-3`。

#### 定位
platform/macos/port.c:479
platform/macos/port.c:492
platform/macos/port.c:508
platform/linux/port.c:439
platform/windows/port.c:331
include/nanosig/nanosig_port.h:503
platform/README.md:41

---

---

<a id="PORT-028"></a>
### PORT-028: 对端有序关闭（EOF）的事件位映射三宿主不一致

- **状态**: 关闭-已修复
- **严重度**: 🟡 中
- **类型**: bug
- **置信度**: 中（macOS 侧本机实测；Linux 侧为 epoll 语义推断，本机为 Darwin 未能实测）
- **关闭原因**: 修复 commit d696a3f：macOS 删除 EV_EOF→ERR 映射，三宿主 EOF 语义统一为只置 IN；ERR 的定义写入 nanosig_port.h；新增 POSIX 用例 test_waitset_eof_reports_in_only（修复前在 macOS 上实测 0x5 失败）。
- **关闭日期**: 2026-10-25

#### 问题描述
`triggered_events` 的位语义在三宿主不统一，同一"对端正常关闭"场景报出的位不同：

- Linux（platform/linux/port.c:413-423）：`EPOLLHUP → NS_WAITABLE_EVENT_IN`，
  `EPOLLERR → NS_WAITABLE_EVENT_ERR`。对端关闭后的连接/管道（EPOLLHUP / EPOLLRDHUP）
  只报 IN，**不会报 ERR**；
- macOS（platform/macos/port.c:523-533）：`EV_EOF → NS_WAITABLE_EVENT_ERR`，同时
  `EVFILT_READ → IN`。对端**有序半关闭**（管道写端 close、socket `shutdown(SHUT_WR)`）
  在 kqueue 上必然带 `EV_EOF`，于是 macOS 把每个正常 EOF 都报成 `IN|ERR`；
- Windows（platform/windows/port.c:438、446-448）：恒报 `IN`。

后果：`handler(watcher, triggered_events)`（include/nanosig/nanosig_broker.h:55）中按
`triggered_events & NS_WAITABLE_EVENT_ERR` 判定"出错/断开"的代码，在 macOS 上会把**正常的对端
关闭**当错误处理，在 Linux 上则永远不进 ERR 分支——同一段 handler 代码两端走不同分支。

公共文档未规定该映射：`include/nanosig/nanosig_port.h:77-79` 只有"可读 / 可写 / 错误"三行
注释，platform/README.md:36 只列出位名，故不属 §2.4 文档仲裁范围。

#### review 建议
先定契约再对齐实现。建议在 `nanosig_port.h` 的 `NS_WAITABLE_EVENT_ERR` 处补一句
"对端有序关闭（EOF）只置 `NS_WAITABLE_EVENT_IN`；`NS_WAITABLE_EVENT_ERR` 仅在底层等待原语
报告真实错误（EPOLLERR / EV_ERROR）时置位"，随后 macOS 删除 `EV_EOF → ERR` 映射
（EV_EOF 的语义"该向不再可读/可写"已由 filter 本身与 IN/OUT 位表达），Linux 保持
`EPOLLHUP → IN`。若确实需要区分"对端关闭"，应新增独立事件位而非复用 ERR——那属公共头变更，
需作者决策。

#### 作者建议

采纳 review 建议：macOS 删除 EV_EOF→ERR 映射，EV_EOF 的语义由 filter 位表达为 IN（与 Linux 的 EPOLLHUP→IN 一致）；nanosig_port.h 注明 ERR 只表示底层真实错误（EPOLLERR / EV_ERROR / WAIT_FAILED），handler 不得用 ERR 判断连接断开。新增 test_waitset_eof_reports_in_only 锁定契约。

#### 可重现的失败场景
macOS 实测：

```c
pipe(p);
ns_waitable_init(&w); w.primitive = p[0]; w.events = NS_WAITABLE_EVENT_IN;
ns_platform_waitset_add(&ws, &w);
write(p[1], "abc", 3);
close(p[1]);                       /* 有序 EOF，且仍有 3 字节未读 */
ns_platform_waitset_wait(&ws, 300000u, cc, 4, &cnt);
/* macOS: cnt == 1, cc[0].triggered_events == 0x5 (IN|ERR) */
/* 把数据读空后再次 wait：仍为 0x5 —— 正常关闭持续被报成"错误" */
```

Linux 同一场景为 `EPOLLIN|EPOLLHUP → 0x1 (IN)`，不含 ERR（本机未实测，按 epoll 语义推断）。

#### 影响面
`triggered_events` 的产生点在三个后端的 `waitset_wait`（platform/macos/port.c、
platform/linux/port.c、platform/windows/port.c），消费方是 src/ns_broker.c 的
`ns_broker_dispatch_pending_events` 与用户的 `handler(watcher, triggered_events)`。
受影响的是所有用 `triggered_events & NS_WAITABLE_EVENT_ERR` 判断"连接断开/出错"的 handler：
macOS 上正常关闭会误入错误分支，Linux/Windows 永不进入。

#### 定位
platform/macos/port.c:529
platform/macos/port.c:530
platform/linux/port.c:420
platform/windows/port.c:438
include/nanosig/nanosig_port.h:77
include/nanosig/nanosig_broker.h:55

---

---
