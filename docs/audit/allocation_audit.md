# 分配审计（P9.1）

> nanosig v1 / 2026-06-20 审计快照

## 摘要

- **总 alloc / free 调用命中数**：49（定义行除外），其中 alloc 15 处、free 34 处。
- **按对象类型分布**：

| 对象类型 | alloc 次数 | free 次数 | 位置 |
| --- | --- | --- | --- |
| `ns_loop_t`（含内嵌 MPSC storage） | 1 | 2 | `nanosig.c` |
| `ns_event_broker_t` | 1 | 2 | `ns_broker.c` |
| `ns_platform_wakeup_t` | 3 | 7 | 各 `port.c` |
| `ns_platform_mutex_t` | 3 | 7 | 各 `port.c` |
| `ns_platform_thread_t` | 3 | 6 | 各 `port.c` |
| `ns_platform_waitset_t` | 3 | 9 | 各 `port.c` |
| 测试（test） | 1 | 1 | `test_platform_backend.c` |
| 实现定义（`port.c` 中的 `ns_platform_alloc` / `ns_platform_free`） | 定义 | 定义 | 各 `port.c` |

- **发现严重度分布**：

| 级别 | 数量 | 说明 |
| --- | --- | --- |
| Critical | 0 | emit / dispatch 热路径无 alloc |
| Major | 0 | alloc/free 配对完整，失败路径清理正确 |
| Minor | 1 | Windows `waitset_destroy` CloseHandle 返回值未检查 |
| Info | 2 | 跨线程持有边界未文档化；无全局 loop 注册导致 shutdown 无法自动清理 |

## 调用点清单

### 核心层 — `src/nanosig.c`

| 位置 | 调用 | 对象 / 大小 | 所属生命周期 | 配对 free | 失败回滚 | 跨线程 | 备注 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `nanosig.c:132` | `ns_platform_alloc(total_size)` | `ns_loop_t` + MPSC storage | `ns_loop_create` | `destroy:171` | `out_free:156` | 创建线程 → 销毁线程；async 线程仅消费 | 单块分配，struct 与 queue storage 连续 |
| `nanosig.c:156` | `ns_platform_free(loop)` | — | `ns_loop_create` 失败 | — | 是（goto out_free） | — | wakeup_create 失败时 → out_free |
| `nanosig.c:171` | `ns_platform_free(loop)` | — | `ns_loop_destroy` | — | — | 必须由停止后的 owner 线程调用 | 先 destroy wakeup，再 free loop |

### 核心层 — `src/ns_broker.c`

| 位置 | 调用 | 对象 / 大小 | 所属生命周期 | 配对 free | 失败回滚 | 跨线程 | 备注 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `ns_broker.c:291` | `ns_platform_alloc(sizeof(*broker))` | `ns_event_broker_t` | `ns_broker_global_init` | `shutdown:366` | `out_free:340` | 全局 singleton，仅 `ns_init` / `ns_shutdown` 访问 | cascade cleanup 模式（6 级标签） |
| `ns_broker.c:340` | `ns_platform_free(broker)` | — | init 失败（out_free） | — | 是 | — | 前级 cleanup：wakeup 已销毁、waitset 已销毁、mutex 已销毁 |
| `ns_broker.c:366` | `ns_platform_free(broker)` | — | `ns_broker_global_shutdown` | — | — | 线程已 join，无并发 | broker thread、timer mgr、watchers 先清理 |

### 平台层 — `platform/linux/port.c`

| 位置 | 调用 | 对象 | 所属生命周期 | 配对 free | 失败回滚 | 备注 |
| --- | --- | --- | --- | --- | --- | --- |
| `port.c:80` | `alloc` | wakeup | `wakeup_create` | `destroy:103` | `85` | eventfd 失败 → free → NOMEM |
| `port.c:85` | `free` | — | `wakeup_create` 失败 | — | 是 | — |
| `port.c:103` | `free` | — | `wakeup_destroy` | — | — | close(fd) 后 free |
| `port.c:188` | `alloc` | mutex | `mutex_create` | `destroy:209` | `192` | pthread_mutex_init 失败 → free → NOMEM |
| `port.c:192` | `free` | — | `mutex_create` 失败 | — | 是 | — |
| `port.c:205` | `free` | — | `mutex_destroy` 异常路径 | — | — | destroy 失败仍 free（见关键发现 #1） |
| `port.c:209` | `free` | — | `mutex_destroy` 正常 | — | — | — |
| `port.c:262` | `alloc` | thread | `thread_create` | `join:284` | `269` | pthread_create 失败 → free → NOMEM/EINVAL |
| `port.c:269` | `free` | — | `thread_create` 失败 | — | 是 | — |
| `port.c:284` | `free` | — | `thread_join` | — | — | pthread_join 后 free |
| `port.c:311` | `alloc` | waitset | `waitset_create` | `destroy:362` | `316,323,335` | epoll/timerfd 失败 → free |
| `port.c:316` | `free` | — | `waitset_create` 失败 | — | 是 | — |
| `port.c:323` | `free` | — | `waitset_create` 失败 | — | 是 | timerfd 失败 |
| `port.c:335` | `free` | — | `waitset_create` 失败 | — | 是 | epoll_ctl 添加 timerfd 失败 |
| `port.c:362` | `free` | — | `waitset_destroy` | — | — | close 双 fd 后 free |

### 平台层 — `platform/macos/port.c`

macOS backend 与 Linux 结构相同（kqueue 替代 epoll、pthread 相同），唯一差异是 waitset 没有 timerfd（内建 timeout），因此 waitset_create 只有 2 个失败清理点。

| 位置 | 调用 | 对象 | 配对 free | 失败回滚 | 备注 |
| --- | --- | --- | --- | --- | --- |
| `port.c:123` | `alloc` | wakeup | `destroy:143` | `128` | — |
| `port.c:128` | `free` | — | 失败 | — | — |
| `port.c:143` | `free` | — | destroy | — | — |
| `port.c:209` | `alloc` | mutex | `destroy:230` | `213` | — |
| `port.c:213` | `free` | — | 失败 | — | — |
| `port.c:226` | `free` | — | destroy 异常 | — | — |
| `port.c:230` | `free` | — | destroy | — | — |
| `port.c:283` | `alloc` | thread | `join:305` | `290` | — |
| `port.c:290` | `free` | — | 失败 | — | — |
| `port.c:305` | `free` | — | join | — | — |
| `port.c:327` | `alloc` | waitset | `destroy:355` | `332,338` | — |
| `port.c:332` | `free` | — | 失败 | — | kqueue 失败 |
| `port.c:338` | `free` | — | 失败 | — | cloexec 失败 |
| `port.c:355` | `free` | — | destroy | — | — |

### 平台层 — `platform/windows/port.c`

| 位置 | 调用 | 对象 | 配对 free | 失败回滚 | 备注 |
| --- | --- | --- | --- | --- | --- |
| `port.c:72` | `alloc` | wakeup | `destroy:94` | `77,90` | — |
| `port.c:77` | `free` | — | 失败 | — | CreateEvent 失败 |
| `port.c:90` | `free` | — | destroy 异常 | — | CloseHandle 失败 → 仍 free |
| `port.c:94` | `free` | — | destroy | — | — |
| `port.c:150` | `alloc` | mutex | `destroy:162` | — | SRWLOCK（无需 destroy） |
| `port.c:162` | `free` | — | destroy | — | — |
| `port.c:221` | `alloc` | thread | `join:245` | `228` | — |
| `port.c:228` | `free` | — | 失败 | — | — |
| `port.c:245` | `free` | — | join | — | — |
| `port.c:272` | `alloc` | waitset | `destroy:292` | `277` | — |
| `port.c:277` | `free` | — | 失败 | — | CreateWaitableTimer 失败 |
| `port.c:292` | `free` | — | destroy | — | 未检查 CloseHandle 返回值（见 Minor 发现） |

### 测试

| 位置 | 调用 | 备注 |
| --- | --- | --- |
| `test/unit/test_platform_backend.c:32` | `ns_platform_alloc(16u)` | 测试平台分配原语 |
| `test/unit/test_platform_backend.c:37` | `ns_platform_free(ptr)` | 配对一次 |

### 定义声明（不计入分配事件）

| 文件 | 行号 | 内容 |
| --- | --- | --- |
| `platform/port.h:89` | 声明 | `void *ns_platform_alloc(size_t size)` |
| `platform/port.h:96` | 声明 | `void ns_platform_free(void *ptr)` |
| Linux/macOS/Windows `port.c` | 各 2 行 | `malloc` / `free` 包装实现 |

## 严重度等级

- **Critical**：emit / dispatch 热路径发现 alloc（违反 v1 设计承诺）
- **Major**：配对缺失 / 失败路径漏清理 / 泄漏
- **Minor**：风格或安全检查不一致
- **Info**：跨线程持有边界未文档化 / 架构约束

## 关键发现

### 发现 #1：`pthread_mutex_destroy` 失败后仍释放 struct（Linux/macOS）

- **位置**：`platform/linux/port.c:202-209`、`platform/macos/port.c:223-230`
- **严重度**：Info
- **描述**：`ns_platform_mutex_destroy` 在 `pthread_mutex_destroy` 返回非零值时，仍调用 `ns_platform_free(mutex)` 再返回 `NS_E_INVAL`。这意味着析构者持有"无论 OS 层是否成功，内存层始终释放"的约定。这对逻辑正确性无影响（所有析构失败路径不会重试），但行为与典型 POSIX 资源清理模式（失败时保留资源）不同。建议在注释中显式说明。
- **修复方向**：在 `ns_platform_mutex_destroy` 函数注释中明确说明"即使销毁失败也会释放 struct"或改为不释放让调用者自行决定。

### 发现 #2：Windows `ns_platform_waitset_destroy` 未检查 CloseHandle 返回值

- **位置**：`platform/windows/port.c:286-293`
- **严重度**：Minor
- **描述**：Windows waitset destroy 函数在 CloseHandle(timer) 后直接 free，未像 macOS/Linux 版本那样检查关闭 fd 的返回值。虽然 CloseHandle 在实际中很少失败，但与平台约定"返回错误码"不一致。
- **修复方向**：增加 CloseHandle 返回值检查，失败时返回 `NS_E_INVAL`（或类似模式），或不返回（void）并保持文档一致。

### 发现 #3：emit 路径零分配完全满足

- **位置**：`nanosig.c:379-417`（`ns_signal_emit_raw`）
- **严重度**：Critical（确认未违反）
- **描述**：`ns_signal_emit_raw` 调用栈全程未使用 `ns_platform_alloc`。slot 遍历使用 `ns_list_for_each`（纯指针操作，无分配），MPSC enqueue 使用 `ns_mpsc_record_ring_try_pushv`（原子 reserve + memcpy，无分配），wakeup signal 用 `eventfd` write / `kevent` NOTE_TRIGGER / `SetEvent`（syscall，无分配）。
- **证据**：Grep 确认 `ns_mpsc_record_ring_try_pushv` 不包含任何 `ns_platform_alloc` / `malloc` / `calloc` 调用路径。Platform `ns_platform_wakeup_signal` 注释行 118 显式承诺"不允许分配内存"。
- **结论**：v1 热路径零分配契约成立。

### 发现 #4：初始化失败级联回滚完整

- **位置**：`src/ns_broker.c:284-342`、`src/nanosig.c:114-158`
- **严重度**：Major（确认无违规）
- **描述**：`ns_broker_global_init` 使用 6 级 goto cascade 标签（`out_timer_mgr` -> `out_mutex` -> `out_wakeup_waitable` -> `out_waitset` -> `out_wakeup` -> `out_free`），每个标签依次销毁已初始化的子对象。`ns_loop_create` 使用 1 级 `out_free` 释放 loop 的完整单块分配。两处都正确设置 NULL 避免双重释放，并在 `ns_platform_free` 之前调用了各子资源析构。

### 发现 #5：无全局 loop 注册机制

- **位置**：`src/nanosig.c` / `src/ns_broker.c`
- **严重度**：Info
- **描述**：`ns_shutdown` 仅清理全局 broker（含 timer manager），不回收到用户已创建但未销毁的 `ns_loop_t` 对象。若用户在 `ns_shutdown` 前未 `loop_destroy`，则发生泄漏。这是架构性选择——loop 由用户非透明指针持有，库无法遍历。
- **修复方向**（v2 可选）：引入内部 loop 注册表（hashtable），`ns_shutdown` 遍历并崩溃/警告残留 loop。当前需在 API 文档中强调用户责任。

### 发现 #6：跨线程所有权边界未文档化

- **位置**：跨文件
- **严重度**：Info
- **描述**：`ns_loop_t` 的创建线程（`ns_loop_create`）是 MPSC 的消费者（SC），`async_thread`（`ns_loop_start`）也是消费者，emit 线程（调用 `ns_signal_emit_raw` 的线程）是生产者。但 loop 的 `ns_loop_destroy` 应在 `ns_loop_stop` 之后由 owner 线程（而非 async 线程）调用。当前的 publicly exported API 未强制这一线程所有权约束。同样，`ns_watcher_t` 的 `init`/`deinit` 调用者预期是用户线程。`ns_broker_global_shutdown` 通过线程 join 保证了安全析构。
- **修复方向**：在公开头文件 API 注释中添加"本对象只能由创建线程销毁"的说明。

## 结论

**v1 分配契约完全成立。**

1. **热路径零分配**：`ns_signal_emit_raw` → `ns_mpsc_record_ring_try_pushv` → `ns_platform_wakeup_signal` 全链路无 `ns_platform_alloc` 或底层 `malloc`。所有分配仅发生在 init / create 路径。
2. **配对完整**：每个 `ns_platform_alloc` 均有对应的 `ns_platform_free`，通过 goto cascade 或 destroy 配对函数实现。无泄漏。
3. **失败回滚正确**：`ns_loop_create` 和 `ns_broker_global_init` 的失败路径正确释放已分配资源并返回 `NS_E_NOMEM`。
4. **错误码一致**：所有 alloc 失败返回 `NS_E_NOMEM`。

**遗留风险**：
- loop/timer/signal/watcher/connection 对象的跨线程所有权需由 API 使用者自行管理，v1 不提供自动泄漏检测。
- Windows `waitset_destroy` 缺少 CloseHandle 检查（Minor）。
- POSIX `mutex_destroy` 失败时释放 struct 的契约尚未在注释中显式说明（Info）。
