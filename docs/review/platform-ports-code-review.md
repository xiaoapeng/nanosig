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

## 现在打开的问题（2026-07-05 review；PORT-018…022 来自 2026-10-18 review）

### PORT-018: Windows `ns_waitable_handle_is_valid` 把文档化的无效哨兵判为有效

- **状态**: 打开
- **严重度**: 🟠 高
- **类型**: bug
- **关联**: BROKER-036（`ns_watcher_init` 接受 `events == 0`）与本条同源——都是
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

#### 关闭原因
`platform/windows/nanosig_platform_port.h:42` 的 `ns_waitable_handle_is_valid` 已改为
排除 `NULL` 与 `INVALID_HANDLE_VALUE`；`platform/windows/port.c` 的私有判定已删除
（改为直接调用公共谓词的 `ns_waitable_is_invalid`），两处判据不再可能漂移。

- 关闭日期: 2026-10-18
- 状态: 关闭-已修复

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

### PORT-019: Linux `waitset_wait` 在 timerfd 与 waitable 同批就绪时丢弃 completion

- **状态**: 打开
- **严重度**: 🟠 高
- **类型**: bug
- **说明**: 既有缺陷，非 `bafb78d` 引入；按 `docs/review/AGENTS.md` 属本模块
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
（待作者补充）

#### 可重现的失败场景
1. 两个 waitable 以 `edge_triggered = 1` 注册，broker 以有限超时（如 50ms）等待；
2. 恰好某次 timer 到期瞬间，某个 `EPOLLET` fd 变为可读；
3. `epoll_wait` 返回 `[timerfd, fd]` → 代码在 timer 分支 return，fd 的边沿被内核
   消费但从未上报；
4. 该 watcher 的 handler 此后不再被调用，直到用户重新触发另一条边沿。

#### 定位
platform/linux/port.c:532
platform/linux/port.c:521

---

### PORT-020: Zephyr `ns_platform_alloc` 对所有内部分配强制内核栈对齐

- **状态**: 打开
- **严重度**: 🟡 中
- **类型**: design

#### 问题描述
`bafb78d` 让 `ns_platform_alloc` 统一走
`k_aligned_alloc((size_t)Z_KERNEL_STACK_OBJ_ALIGN, size)`，因为堆上的
`ns_loop_t` / `ns_event_broker_t` 现在按值内联了含 `K_KERNEL_STACK_MEMBER` 的
`ns_platform_thread_t`。但同一分配器也服务哈希表节点、格式化缓冲等普通内部分配：
开启 MPU 栈保护时对齐量会随栈尺寸放大，小对象分配可能各自浪费接近一个栈的堆空间，
64KB 堆下数据结构的分配会更早撞到 `NS_E_NOMEM`。

#### review 建议
给 loop/broker 单独提供对齐分配入口（如 `ns_platform_alloc_aligned(size, align)`
仅在这两处使用），通用路径回到 `k_malloc`；若维持现状，至少在
`platform/zephyr/nanosig_platform_port.h` 与 `platform/README.md` 写明这个代价。

#### 作者建议
（待作者补充）

#### 可重现的失败场景
`CONFIG_MPU_STACK_GUARD=y`（栈对齐等于栈大小时）配置 64KB 堆，连续创建若干
`ns_hashtbl` / 格式化大缓冲：每次分配按栈尺寸对齐，堆被快速碎片化并提前返回
`NS_E_NOMEM`，而同样负载在未开 MPU 时正常。

#### 定位
platform/zephyr/port.c:125
platform/zephyr/nanosig_platform_port.h:11

---

### PORT-021: 平台文档仍描述重构前的原语

- **状态**: 打开
- **严重度**: 🟢 较低
- **类型**: doc

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
（待作者补充）

#### 可重现的失败场景
照 README 在 macOS 上调用 wakeup 侧转换宏（`NS_WAITABLE_GET`）→ 编译错误，
宏已不存在；照 Windows 小节期望 `CreateThread` 语义（如继承句柄/线程退出码）→
与实际 `_beginthreadex` 行为不符。

#### 定位
platform/README.md:93
platform/README.md:102
docs/agents/platform/AGENTS.md:67
docs/agents/platform/AGENTS.md:69
docs/ARCHITECTURE.md:271

---

### PORT-022: Windows `ns_platform_waitset_deinit` 丢弃 `CloseHandle` 失败

- **状态**: 打开
- **严重度**: 🟢 较低
- **类型**: cleanup

#### 问题描述
`platform/windows/port.c:302` 直接 `CloseHandle(waitset->timer); return NS_OK;`，
关闭失败被静默吞掉。Linux（`platform/linux/port.c:399`）与 macOS
（`platform/macos/port.c:411`）都会把失败传播成 `NS_E_INVAL`，三后端行为不一致。

#### review 建议
```c
return (CloseHandle(waitset->timer) != 0) ? NS_OK : NS_E_INVAL;
```

#### 作者建议
（待作者补充）

#### 可重现的失败场景
timer handle 已被外部关闭或句柄损坏时，`ns_platform_waitset_deinit` 返回 `NS_OK`，
调用方误以为资源已释放，与 Linux/macOS 上的结果不同。

#### 定位
platform/windows/port.c:302
platform/linux/port.c:399
platform/macos/port.c:411

---

### PORT-014: macOS `ns_platform_waitset_from_kevent` 合并同 waitable 多事件行为与 Linux/Windows 不一致

- **状态**: 关闭-延期
- **严重度**: 🟡 中
- **类型**: design

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

---

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

- 关闭日期: 2026-08-02
- 状态: 关闭-已拒绝

#### 定位
platform/windows/port.c:38
platform/linux/port.c:141

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
test/unit/test_platform_backend.c:690-714

---

### PORT-017: `ns_platform_event_*` 生命周期契约不完整（重复 init 泄漏 / 未初始化调用）

- **状态**: 关闭-已修复
- **严重度**: 🟡 中
- **类型**: doc
- **关闭原因**: 已按"文档保护"处置：include/nanosig/nanosig_port.h:316-346 的 event 文档块补 `@warning`（重复 init 泄漏底层 fd/HANDLE）与 `@pre`（signal/drain/deinit 前须成功 init，deinit 另需已 waitset_remove）。无代码改动，未加运行时守卫。
- **关闭日期**: 2026-09-21

#### 问题描述

`ns_platform_event_t` 是本次 event 重构新增的 caller-owned 平台原语。其公共文档块
include/nanosig/nanosig_port.h:316-346 只描述字段语义与四条"硬语义"，未声明两条
生命周期前提：

1. **init 非幂等**：对同一 event 重复调用 `ns_platform_event_init` 而不先 `deinit`
   会覆盖 `waitable` / `signal_handle`，泄漏第一次分配的资源（Linux `eventfd`
   一个 fd、macOS `pipe` 两个 fd、Windows `CreateEvent` 一个 HANDLE）。三个后端
   init 都是"无条件新建资源、不检查旧状态"，见 platform/linux/port.c:199-219、
   platform/macos/port.c:240-266、platform/windows/port.c:151-170。
2. **未初始化调用**：在未成功 `init` 的对象上调用 `signal` / `drain` / `deinit`
   会读取未初始化的 `waitable.registered_waitset` 与 `signal_handle`，属未定义行为，
   但头文件未声明 `@pre`。

仓库对同类问题（signal / watcher 重复 init）在审计处置中选择"文档免责 +
头文件 `@warning`"（见 P9 F1/F2），本问题应与之一致处置。

#### review 建议

在 `nanosig_port.h` 的 event 文档块补充生命周期约束：

- `@pre`：`signal` / `drain` / `deinit` 前必须已成功 `init`；`deinit` 另需已
  `ns_platform_waitset_remove`（后者已在硬语义条目中）。
- `@warning`：对同一 event 重复 `init` 而未 `deinit` 会泄漏底层 fd / HANDLE；
  需 `init` 一次、`deinit` 一次。

#### 作者建议

（2026-09-21 作者决定：文档保护，不加运行时守卫。）理由：① 与 P9 F1/F2（signal / watcher 重复 init）的处置保持一致；② `ns_platform_event_t` 是 caller-owned 且不要求清零，运行时守卫无法可靠区分"未初始化垃圾句柄"与"已初始化合法句柄"，可靠检测需强制 zero-init 或新增 magic/状态字段，代价与收益不成比例；③ 重复 init 仅在调用方违规时发生，对正常路径无正确性影响。处置方式：仅补 `@warning` + `@pre` 文档。

#### 可重现的失败场景

```c
ns_platform_event_t e;
ns_platform_event_init(&e, "x");   /* 分配 eventfd / pipe / CreateEvent #1 */
ns_platform_event_init(&e, "x");   /* 覆盖句柄，资源 #1 永久泄漏 */
```

`grep -nE "@pre|@warning" include/nanosig/nanosig_port.h`（event 段落 :316-346）
→ 仅硬语义条目，无 init 配对与前置条件声明。

#### 定位
include/nanosig/nanosig_port.h:316-346
platform/linux/port.c:199-219
platform/macos/port.c:240-266
platform/windows/port.c:151-170

---

### PORT-023: Linux `ns_linux_wakeup_drain` 失败时未消费 eventfd 计数，调用方陷入热自旋

- **状态**: 打开
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
