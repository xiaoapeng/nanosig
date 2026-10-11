# Implementation Plan: Zephyr 平台后端接入（v4）

- 状态: **已批准**（共识评审 Critic v4 APPROVE；用户于 2026-10-11 接受四项偏差 D1–D4；待实现）
- 输入规格: docs/specs/深度访谈-Zephyr平台后端接入规格.md
- 模式: RALPLAN-DR short（Planner → Architect → Critic，共 4 轮）
- 基线: `59338b1`
- 日期: 2026-10-11

## Requirements Summary

把 Zephyr 接入为 nanosig 第五类平台后端（非 lockstep 增量）。交付分两个构建面：

- **真实构建/测试面（唯一实际编译面）**：以 Zephyr module（`zephyr/module.yml`）被 west/twister 消费，在 `native_sim` 上编译整个库（含 `platform/zephyr/port.c`）并运行 ztest 契约测试。该构建**同时**充当 Zephyr 分支的编译期契约证据。
- **顶层构建面的显式重定向**：顶层 `NANOSIG_PLATFORM=zephyr` 触发带指引的 `FATAL_ERROR`（指向 Zephyr 构建系统），**不尝试**在无 Kconfig 生成头的环境下编译 `port.c`；host 默认构建行为完全不变。

并交付：显式 `NANOSIG_PLATFORM_ZEPHYR` + `ns_platform_abi_zephyr` 探针；`platform/zephyr/port.c` 全原语；waitset 基于 `k_poll`；event 基于 `k_poll_signal`；wakeup 每对象 `k_sem`；`waitset_remove` 契约不变；文档四→五后端。

## RALPLAN-DR Summary

### Principles
1. **平台隔离** —— OS/RTOS 差异只存在于 `platform/`；`src/`、`include/` 不出现 Zephyr 头文件或分支。
2. **契约不扩大** —— `waitset_remove` 对调用方维持 `NS_OK`/`NS_E_INVAL`；broker 零改动。
3. **验证诚实** —— native_sim/POSIX arch 有明确不覆盖清单；不把"编译通过"包装成"运行时已验证"。
4. **默认安全 + 可配置** —— 容量/栈有安全默认且可配置；越界/缺配置在编译期或 `add` 返回错误，不触发内核断言。
5. **尊重 Zephyr 原生语义** —— 不把 FreeRTOS 的 token/purge 模型照搬进电平触发的 `k_poll`；`k_poll_signal` 的 reset 只在 poller 线程发生。

### Decision Drivers
1. Zephyr 成为可独立评审、可独立合并的第五后端。
2. 最小化对既有四后端与 broker 的扰动。
3. 在 PR 门禁内获得**真实 Zephyr 内核**行为验证，且不引入无法验证的机制。

### Viable Options
- **A（选定）：`k_poll` waitset + `k_poll_signal` event + 每对象 `k_sem` wakeup + 显式宏 + native_sim/ztest PR 门禁**
  - Pros：`k_poll` 是 Zephyr 唯一能等待**多个异构对象**的通用设施，与既有 waitable-first 模型对齐；native_sim 免硬件即可门禁；显式宏避免未知目标误判。
  - Cons（如实列出）：① `k_poll` 对单个对象**只允许一个 poller**，且常规 `k_sem_take` 等直取路径**优先级高于** poller，可能饿死 poller（Z18）；② `k_poll_signal` 未同步、reset 只应在 poller 线程执行，禁止在其他线程 reset（Z5/Z6）；③ `ns_platform_waitable_t` 无类型标签，`k_poll` 需要显式 `K_POLL_TYPE_*`，v1 只能**限制为 signal 类型 waitable**（Z4），sem/pipe/socket waitable 列为非目标；④ native_sim 为确定性单核模拟，**覆盖不到**跨线程竞态与硬件相关项；⑤ 引入 CI 下载与构建成本。
- **B：仅编译期契约（不引入 native_sim）** —— 与规格 Round 1 决策冲突。**INVALIDATED**。
- **C：用 Zephyr zsock/epoll 承载 waitset** —— 仅覆盖 socket，无法承载 `event`/`wakeup` 等非 socket waitable。**驳回**。
- **D：用 `k_event` 替代 `k_poll`** —— `k_event_wait` 只能等**单个** event 对象的 bit mask；要承载异构 waitable 需为每个源加适配线程（栈与延迟成本），且无法直接表达 caller-owned handle 契约。**驳回**。
- **E：每 waitset 专用 poller 线程 / `K_POLL_MODE_NOTIFY_ONLY` 隔离**
  - Pros：把 `k_poll` 调用集中到单一线程，天然满足"reset 只在 poller 线程"；可规避调用方误用。
  - Cons：每个 waitset 增加一个线程（栈 + tick 成本）；broker 本身已有迭代线程，重复建线程收益低；`NOTIFY_ONLY` 语义与 `waitset_wait` 调用方阻塞语义不符（需要额外同步）。**驳回**（保留为 Follow-up）。

## Resolved Decisions

- **Z1 单实际编译面 + 顶层显式重定向**：`platform/zephyr/port.c` **只能**在 Zephyr 构建环境（Kconfig 生成的 `autoconf.h` + 生成头目录 + `-imacros`）下编译；Zephyr 源码树本身不含这些生成物，故顶层 CMake **不**提供独立编译路径。顶层 `NANOSIG_PLATFORM=zephyr` 分支输出带指引的 `FATAL_ERROR`（"Zephyr 后端经 Zephyr 构建系统构建，见 platform/README.md"）；host 默认构建行为不变。**Zephyr 分支的编译期契约由 Zephyr module 构建（AC-2/AC-11/AC-13）提供**，其 `zephyr_library()` 目标继承生成头与 `-imacros autoconf.h`。此机制保留规格第 9/11 条的**意图**（有编译期契约覆盖），但改变其**机制**（由 module 构建而非顶层 CMake 承担）——列为 Open Item 待签核（见「与规格的偏差」）。
- **Z2 Zephyr module 集成**：新增 `zephyr/module.yml`（显式 `build: cmake: zephyr`、`kconfig: zephyr/Kconfig`、`tests: test/zephyr`）+ `zephyr/CMakeLists.txt`（`zephyr_library()`）+ `zephyr/Kconfig`。module 侧必须与顶层构建对齐目标属性：
  - PUBLIC `NANOSIG_PLATFORM_ZEPHYR=1`（否则 native_sim 上 `__linux__` 会选错后端）；
  - PUBLIC `NS_MPSC_CACHELINE_ALIGNED`（顶层仅在 `WIN32 OR UNIX` 添加，见 `CMakeLists.txt:151-154`；该宏改变 `ns_mpsc_record_ring.h` 布局，**必须**在 Zephyr 面一致定义）；
  - include dirs（`include/` + 生成的版本头目录）；
  - `target_compile_features(... c_std_11)`；
  - `configure_file` 生成 `nanosig_version.h`（顶层在 `CMakeLists.txt:12-16`；缺失则库无法编译）；
  - `src/ns_formatio.c` 的 `-ffp-contract=off` 目标属性（顶层 `CMakeLists.txt:114-118`）；
  - `platform/zephyr/port.c` 纳入编译。
- **Z3 平台宏分支顺序**：`nanosig_port.h` 平台判定段在 `NANOSIG_PLATFORM_FREERTOS` 之后、`_WIN32` 之前新增 `#if defined(NANOSIG_PLATFORM_ZEPHYR)`；**无 `#else` 兜底**；`ns_platform_abi_zephyr` 声明与导出一致。
- **Z4 handle 分支 + 类型判别（限制为 signal 类 waitable）**：Zephyr 归 handle 侧；`primitive.handle` **只**解释为 `struct k_poll_signal *`。`waitset_wait` 对每个活跃 slot 固定使用 `K_POLL_TYPE_SIGNAL` + `K_POLL_MODE_NOTIFY_ONLY`。v1 **只接受**由 `ns_platform_event_init` 产生的 waitable（event 内嵌的 `k_poll_signal`）；`k_sem`/FIFO/pipe/socket 等异构 waitable 列为**非目标**（需要 `ns_platform_waitable_t` 增加类型标签这一加法扩展，见 Follow-up）。`ns_waitable_handle_is_valid` 对 Zephyr 定义有效判据并排除 `(void *)~0` 哨兵；`NS_WAITABLE_SET/GET` 走 `.handle`；`NS_STATIC_ASSERT` 覆盖指针尺寸。`K_POLL_STATE_SIGNALED` → `NS_WAITABLE_EVENT_IN`；`K_POLL_STATE_NOT_READY` 跳过；`K_POLL_STATE_CANCELLED` 视为错误路径（返回错误码）。
- **Z5 `waitset_remove` 语义（电平触发原生 + 契约等价）**：`remove` 在调用线程执行：① 摘除 slot；② **不** reset `k_poll_signal`、**不**消费任何对象状态。规格的**可观察保证**（"移除返回后该 waitable 不再产生 completion"）**成立**，因为 completion 只来自 `k_poll` 对已注册 slot 的返回，摘除即不再有 completion。差异在"消化残留就绪状态"这一**措辞**：对电平保持的 `k_poll_signal`，`reset` 只允许在 poller 线程执行，故 remove（任意线程）不做 reset；后果是 **remove→re-add 后若信号仍为 signaled 会立即触发**，除非调用方已 `event_drain`。该行为写入文档，并列为 Open Item（规格措辞需按电平语义改写）。若坚持"remove 消化就绪状态"，替代方案是要求 `waitset_remove` 仅在 poller 线程调用并在其中 `k_poll_signal_reset`——代价是收紧调用约束，作为备选记录。返回始终只有 `NS_OK`/`NS_E_INVAL`。
- **Z6 唯一 reset 点**：`ns_platform_event_drain` 是 `k_poll_signal_reset` 的**唯一**调用点，且只应由 poller 线程（broker loop）调用；文档明确"reset 只在 polling 线程"。`waitset_add` 不做 reset/flush。
- **Z7 event = `k_poll_signal`**：`event_init` 分配并初始化 `k_poll_signal`；`waitable.primitive.handle` 与 `signal_handle.handle` 指向同一 `k_poll_signal`；`signal` = `k_poll_signal_raise`；`drain` = `k_poll_signal_reset`（幂等）；`deinit` 仍注册返回 `NS_E_BUSY`。
- **Z8 waitset = `k_poll`（`k_timeout_t`）**：每次 `waitset_wait` 由活跃 slot 构造 `struct k_poll_event events[]`，调用 `k_poll(events, n, timeout)`，其中 `timeout` 为 `k_timeout_t`：`NS_PLATFORM_WAIT_INFINITE_US` → `K_FOREVER`，`0` → `K_NO_WAIT`，其余 → 向上取整的 `K_USEC(...)`（见 Z10）。返回码**单值处理**：`0` 成功；`-EAGAIN` 映射 timeout（`out_count=0`，返回 `NS_OK`）；`-EINTR`（含 `K_POLL_STATE_CANCELLED`）**视为被取消，直接返回负数错误码，不重试**；其他负数原样返回。**删除任何 "timeout_ticks" 表述**。
- **Z9 仅 IN 事件**：含 `OUT`/`ERR` 的 waitable 在 `waitset_add` 返回 `NS_E_INVAL`（同 FreeRTOS）。
- **Z10 tick 与精度**：微秒→tick 转换**必须向上取整**（`K_USEC` / `k_us_to_ticks_ceil64`），禁止 floor。契约测试 `prj.conf` 设 `CONFIG_SYS_CLOCK_TICKS_PER_SEC >= 1000`（数值下限，1 ms）；文档给出建议 ≥ 10000 以支持微秒级 timer，并说明 `native_sim` 默认 100 Hz/10 ms 的粗粒度。断言措辞为"等待**至少** timeout，允许晚 fire"。
- **Z11 clock**：`ns_platform_clock_monotonic_us` 使用 `k_ticks_to_us_floor64(k_uptime_ticks())`（单调、tick 粒度）；tick 粒度误差写入文档；不使用非单调源。
- **Z12 wakeup = 每对象独立 `k_sem`**（上限 1，二值语义）：`signal` = `k_sem_give`（返回 void，已满无可返回状态，语义上视为成功），`wait` = `k_sem_take(timeout)`；禁止共享会互相误消费的原语。
- **Z13 thread = `k_thread_create` + `k_thread_join`**（3.7 LTS 可用）：`ns_platform_thread` 内嵌 `struct k_thread` + **`K_KERNEL_STACK_MEMBER`** 栈成员（`K_THREAD_STACK_DEFINE` 是带 linker section 的顶层数组，**不能**嵌入结构体）；栈尺寸 `NANOSIG_ZEPHYR_THREAD_STACK_SIZE`（默认 4096 字节）。由于内嵌栈需要内核栈对齐（`Z_KERNEL_STACK_OBJ_ALIGN`），`ns_platform_thread` 的分配必须使用满足该对齐的分配（`k_aligned_alloc(Z_KERNEL_STACK_OBJ_ALIGN, size)`），**不得**用普通 `k_malloc`。线程优先级 `NANOSIG_ZEPHYR_THREAD_PRIORITY`（默认 `K_PRIO_PREEMPT(1)`，可覆盖），抢占式；`prj.conf` 保持 `CONFIG_MULTITHREADING=y`。`join` 后释放句柄；重复 join 的行为与 FreeRTOS 后端契约一致（第二次 join 视为无效句柄）。文档写明内嵌栈溢出会破坏相邻堆内存（比独立栈更敏感）的告警。**不依赖 `CONFIG_DYNAMIC_THREAD`**。
- **Z14 Kconfig/头文件依赖**：
  - **`k_poll`/`k_poll_signal_raise` 由 `CONFIG_POLL` 门控（Zephyr 3.7 `kernel/Kconfig`：`config POLL` 默认 n）**。`zephyr/Kconfig` 以 `select POLL` 提供该依赖，`test/zephyr/prj.conf` 显式 `CONFIG_POLL=y`；`port.c` 顶部 `#if !defined(CONFIG_POLL) || !CONFIG_POLL` `#error` 守卫。**修正 v2 的"始终可用"错误表述**。
  - `k_malloc`/`k_free` 需要 `CONFIG_HEAP_MEM_POOL_SIZE > 0`；`test/zephyr/prj.conf` 显式设置；`port.c` 顶部 `#if CONFIG_HEAP_MEM_POOL_SIZE == 0` `#error` 守卫（**编译期强制，为唯一权威路径**）。文档写明目标板 defconfig 常为 0、需在使用本后端前配置堆。
  - `CONFIG_MULTITHREADING=y`（默认 y）与 `CONFIG_THREAD_JOIN`（若该版本门控 join）由 `zephyr/Kconfig` `depends on` 或 `port.c` `#error` 守卫强制。
  - Zephyr 3.5+ 头文件前缀：`port.c` 只 include `<zephyr/kernel.h>` 等 `zephyr/` 前缀头。
- **Z15 配置单一来源**：waitset 容量规范宏为 **`NANOSIG_ZEPHYR_WAITSET_CAP`**；Kconfig 提供 `CONFIG_NANOSIG_ZEPHYR_WAITSET_CAP`，`port.c` 内以固定优先级合并：`CONFIG_*` 优先，其次裸 `-D`，最后默认 32。默认 32 与 `nanosig_port.h:570` 的 `max_completions<=64`、`src/ns_broker.c:48` 的 completion 容量 16 **不冲突**（前者是注册数上限，后者是单次 wait 返回数上限）；文档写明 broker 场景需 `CAP >= 已注册 waitable 数`。
- **Z16 edge_triggered**：忽略（同 Windows），写入文档。
- **Z17 版本钉定**：钉 **Zephyr 3.7 LTS**；SDK 跟随官方 stable。**native_sim 使用宿主工具链**（POSIX arch 用 native host tools 编译），因此 CI 需要 32/64 位 libc（见 Z20），**不安装完整 Zephyr SDK**（见「与规格的偏差」）。
- **Z18 `k_poll` 单 poller / 优先级约束**：同一底层对象不得被其他普通路径（`k_sem_take` 等）与其 poller 并发竞争，否则可能饿死 poller；backend 文档写明该限制，并要求每个 waitable 只注册到一个 waitset（既有契约已要求）。
- **Z19 CI 与门禁**：`ci.yml` 新增 Zephyr job（`ubuntu-latest`）：宿主工具链；**固定使用 `native_sim/native/64` 板**（避免 32 位 multilib 依赖；若 CI 环境不支持则回退 `native_sim` 并强制安装 `gcc-multilib`——回退开关集中一处，AC-12/V3 与之保持一致）；shallow/pin Zephyr 3.7 LTS 源码树并缓存 west 工作区；`timeout-minutes: 30`；运行 `west build -b native_sim/native/64 test/zephyr` 与 ztest。**必需检查由 9 增至 10**（需同步更新分支保护）。nightly Zephyr 运行**标记为可选、非规格要求**。

## Acceptance Criteria（可测）

- [ ] AC-1：顶层 CMake 对 `NANOSIG_PLATFORM=zephyr` 输出带指引的 `FATAL_ERROR`（指向 Zephyr 构建系统），不注册 host-only 测试，不提供无生成头的编译路径；host 默认构建行为不变。
- [ ] AC-2：Zephyr module（`zephyr/module.yml` + `zephyr/CMakeLists.txt` + `zephyr/Kconfig`）在 `native_sim/native/64` 上成功编出 `platform/zephyr/port.c` 与整个库，且注入 PUBLIC `NANOSIG_PLATFORM_ZEPHYR=1` 与 `NS_MPSC_CACHELINE_ALIGNED`。
- [ ] AC-3：`nanosig_port.h` 平台判定段在 FreeRTOS 之后、`_WIN32` 之前判 Zephyr、无 `#else`；handle 侧宏覆盖 Zephyr 并排除哨兵；`ns_platform_abi_zephyr` 声明与导出一致（V1 链接探针）。
- [ ] AC-4：`platform/zephyr/port.c` 实现 `nanosig_port.h` 中**全部** `ns_platform_*`（函数名集合与头文件声明逐一对应）；仅接受 signal 类 waitable（`K_POLL_TYPE_SIGNAL`）并在文档写明该限制；线程优先级与栈对齐按 Z13；`platform/` 外无新增 OS 分支。
- [ ] AC-5：waitset 基于 `k_poll`（`K_POLL_TYPE_SIGNAL`），`waitset_remove` 只摘 slot（不 reset、不消费），返回 `NS_OK`/`NS_E_INVAL`，且移除后同一（未重注册）waitable 不再出现在 completions；`git diff --exit-code src/ns_broker.c` 为 0；四后端回归全绿。
- [ ] AC-6：仅接受 `NS_WAITABLE_EVENT_IN`；含 OUT/ERR 的 waitable 返回 `NS_E_INVAL`。
- [ ] AC-7：`NANOSIG_ZEPHYR_WAITSET_CAP` 默认 32、可经 Kconfig 或 `-D` 覆盖且**合并优先级固定**；超限返回 `NS_E_TOO_MANY_HANDLES`；pending 时 remove 的断言为"返回 `NS_OK`，且被移除 waitable 在同轮及后续 wait 中不再出现在 completions"。
- [ ] AC-8：`k_poll` 超时使用 `k_timeout_t`（`K_FOREVER`/`K_NO_WAIT`/`K_USEC`），微秒→tick **向上取整**；`-EAGAIN` 映射 timeout（`NS_OK` + `out_count=0`）、`-EINTR`/`K_POLL_STATE_CANCELLED` **直接返回负数错误码且不重试**（单一处理）；契约测试断言"等待 ≥ timeout"。
- [ ] AC-9：event（`k_poll_signal`，drain 是唯一 reset 点、只在 poller 线程）、wakeup（每对象 `k_sem`，跨线程 signal→wait）、clock（`k_uptime_ticks` 单调）分别有独立断言；`event_deinit` 未 remove 返回 `NS_E_BUSY`。
- [ ] AC-10：`edge_triggered` 忽略、tick 精度下限（数值：测试 `CONFIG_SYS_CLOCK_TICKS_PER_SEC >= 1000`）、微秒超时向上取整，写入文档与 `prj.conf`/Kconfig。
- [ ] AC-11：`test/zephyr/` 是完整 Zephyr 应用（`CMakeLists.txt`、`prj.conf`、`testcase.yaml`，且 `module.yml` 的 `tests:` 指向它），`west build -b native_sim/native/64 test/zephyr` 能产出并运行 ztest（板名与 Z19/AC-12/V3 一致；回退 `native_sim` + `gcc-multilib` 时同步改本条）。
- [ ] AC-12：native_sim + ztest 契约测试覆盖 waitset/event/wakeup/broker 端到端/clock，作为 `ci.yml` 必需检查在 `ubuntu-latest` 跑通（板名与 Z19 一致：`native_sim/native/64`）；分支保护由 9 项更新为 10 项必需检查；钉 Zephyr 3.7 LTS。
- [ ] AC-13：**编译期契约覆盖 Zephyr 分支**：Zephyr module 构建（`west build -b native_sim/native/64 test/zephyr`）在注入 PUBLIC `NANOSIG_PLATFORM_ZEPHYR=1` 下成功编译 `platform/zephyr/port.c` 与 `src/`（即编译期契约证据），并附 `#error` 守卫验证（缺 `CONFIG_POLL`/`CONFIG_HEAP_MEM_POOL_SIZE==0` 时编译失败）——保留规格第 11 条意图，机制改由 module 构建承担。
- [ ] AC-14：现四后端 Linux/macOS 测试 + `sanitize-all` 回归不受影响（显式 AC，恢复规格第 12 条）。
- [ ] AC-15：`platform/README.md`、`docs/ARCHITECTURE.md`、`docs/plans/共识计划.md` 同步为"五后端"，含 Zephyr 映射、`k_poll` 单 poller/reset 约束、tick 精度与 native_sim 不覆盖清单。

## Implementation Steps

### Step 1 — 平台判定（`include/nanosig/nanosig_port.h`）
- 平台判定：Zephyr 分支置 FreeRTOS 之后、宿主之前，无 `#else`。
- 新增 `ns_platform_abi_zephyr` 声明 + `NS_PLATFORM_ABI_PROBE` 映射（`NS_PLATFORM_ABI_PROBE` 在 Zephyr 分支展开为 `ns_platform_abi_zephyr`）；该符号由 `platform/zephyr/port.c` **非 static** 定义，供 `test/zephyr` 主 TU 引用。
- handle 侧宏（`ns_waitable_handle_is_valid`/`NS_WAITABLE_SET`/`NS_WAITABLE_GET`）增加 Zephyr 分支 + `NS_STATIC_ASSERT`。

### Step 2 — `platform/zephyr/port.c`
- include 仅 `<zephyr/kernel.h>` 等 `zephyr/` 前缀头；文件顶 `#error` 守卫 `CONFIG_POLL`、`CONFIG_HEAP_MEM_POOL_SIZE>0`、`CONFIG_MULTITHREADING`。
- alloc/free=`k_aligned_alloc`/`k_free`（struct 与内嵌栈按 `Z_KERNEL_STACK_OBJ_ALIGN` 对齐；失败返回 NULL）；init/shutdown；mutex=`k_mutex`；wakeup=`k_sem`（每对象，上限 1）；event=`k_poll_signal`（drain 唯一 reset 点）；waitset=`k_poll`（`K_POLL_TYPE_SIGNAL` + `K_POLL_MODE_NOTIFY_ONLY`）+ slot 表；thread=`k_thread_create`/`k_thread_join` + `K_KERNEL_STACK_MEMBER`（内嵌）+ `NANOSIG_ZEPHYR_THREAD_PRIORITY`；clock=`k_uptime_ticks`。
- `waitset_add`：仅 IN；重复 `NS_E_EXISTS`；容量校验（默认 32）→ `NS_E_TOO_MANY_HANDLES`；不 reset/flush。
- `event_init` 的 `k_poll_signal` 与 waitable 均从堆分配（`k_aligned_alloc`），故 event 生命周期同样受 `CONFIG_HEAP_MEM_POOL_SIZE>0` 约束（与 Z14 一致）。
- `waitset_remove`：仅摘 slot；返回 `NS_OK`/`NS_E_INVAL`。
- `waitset_wait`：构造 `k_poll_event[]`（`K_POLL_TYPE_SIGNAL`）、`k_timeout_t`（向上取整）、`-EAGAIN`→timeout、`-EINTR`→返回错误码（不重试）、`K_POLL_STATE_SIGNALED`→`NS_WAITABLE_EVENT_IN`、`K_POLL_STATE_NOT_READY` 跳过、`K_POLL_STATE_CANCELLED` 错误路径。
- 配置合并：`CONFIG_NANOSIG_ZEPHYR_WAITSET_CAP` 优先，其次 `-D NANOSIG_ZEPHYR_WAITSET_CAP`，默认 32；栈尺寸 `NANOSIG_ZEPHYR_THREAD_STACK_SIZE`（默认 4096）、优先级 `NANOSIG_ZEPHYR_THREAD_PRIORITY`（默认 `K_PRIO_PREEMPT(1)`）。
- 文件头注释自洽；无下游品牌字样；内部 union 统一走 `NS_WAITABLE_SET`。

### Step 3 — 构建接线
- 顶层 `CMakeLists.txt`：对 `NANOSIG_PLATFORM=zephyr` 输出带指引的 `FATAL_ERROR`（Zephyr 经 Zephyr 构建系统构建）；`FATAL_ERROR` 文案更新为五平台；不注册 host-only 测试。
- `zephyr/module.yml`（`build: cmake: zephyr`；`kconfig: zephyr/Kconfig`；`tests: test/zephyr`）+ `zephyr/CMakeLists.txt`（`zephyr_library()` + Z2 列出的 PUBLIC 定义/包含/`configure_file`/`c_std_11`/`-ffp-contract=off`）+ `zephyr/Kconfig`（`NANOSIG_ZEPHYR_WAITSET_CAP`）。
- CI 钉 Zephyr 3.7 LTS（west/pin 常量集中一处）。

### Step 4 — host 契约测试（native_sim + ztest）
- `test/zephyr/`：`CMakeLists.txt`（`find_package(Zephyr)` + ztest）、`prj.conf`（`CONFIG_ZTEST`、`CONFIG_POLL=y`、`CONFIG_HEAP_MEM_POOL_SIZE>0`、`CONFIG_SYS_CLOCK_TICKS_PER_SEC>=1000`、`CONFIG_MULTITHREADING=y`）、`testcase.yaml`。
- 用例：waitset add/remove（含 pending 时）/wait、容量越界、event signal/drain（**含两线程：poller 线程 reset，用 `k_sem` 显式排序两线程（禁止依赖 POSIX arch 调度时序），验证跨线程 signal 后 drain 只在 poller 发生**）、wakeup 跨线程、broker 端到端、clock 单调、`event_deinit` busy 防御、`waitset_wait` 在 `max_completions > CAP` 时的截断行为。
- 主 TU 显式引用 `NS_PLATFORM_ABI_PROBE`（`ns_platform_abi_zephyr`），使 V1 的链接探针在 Zephyr 面上非空。
- 声明不覆盖清单：tick 精度实测、栈溢出、`configASSERT` 停机、单核竞态、HardFault、POSIX arch 的 stack 记账限制；**禁止用例中 busy-wait**（POSIX arch 会挂起）。

### Step 5 — CI
- `.github/workflows/ci.yml` 新增 Zephyr job：`ubuntu-latest`、宿主工具链、板名固定 `native_sim/native/64`（回退 `native_sim` + `gcc-multilib` 时与 Z19 一致）、缓存并 pin Zephyr 3.7 LTS west 工作区、`timeout-minutes: 30`、`west build -b native_sim/native/64 test/zephyr` + 运行 ztest；设为必需检查（分支保护 9→10）。
- 可选 nightly Zephyr 运行（标注非规格要求）。

### Step 6 — 文档与治理
- `platform/README.md`：四→五后端；Zephyr 映射（`k_poll`/`k_poll_signal`/`k_sem`）、IN-only、容量约束、单 poller/reset 线程约束、tick 精度、edge_triggered 忽略、native_sim 不覆盖清单、`k_malloc` 堆依赖。
- `docs/ARCHITECTURE.md`、`docs/plans/共识计划.md`：四→五后端与 RTOS 前向路径同步。

## Risks and Mitigations
| # | 风险 | 缓解 |
|---|---|---|
| R1 | `k_poll` 单 poller + 常规等待优先级导致 poller 饿死 | Z18：限制单对象单 poller；文档写明；测试用独立对象 |
| R2 | `k_poll_signal` 跨线程 reset 丢事件 | Z6：reset 唯一于 `event_drain` 且只在 poller 线程；两线程 ztest |
| R3 | `k_poll` 超时类型/返回码误用 | Z8：`k_timeout_t` + `-EAGAIN`/`-EINTR` 明确处理；AC-8 断言 |
| R4 | `k_poll_event[]` 栈开销（CAP×sizeof） | 每次 wait 在调用线程栈构造；CAP 默认 32 → 预算 `32 × sizeof(struct k_poll_event)`（典型 ≈ 512–1 KB，随 Zephyr 版本），文档给出该数值并建议 poller 线程栈 ≥ 4× 该值；不跨调用持有 |
| R5 | 顶层误以为可独立编译 Zephyr 后端 | Z1：顶层 `NANOSIG_PLATFORM=zephyr` 输出带指引的 `FATAL_ERROR`；编译期契约由 module 构建承担（V3） |
| R6 | 双构建系统目标属性不一致（宏/版本头/浮点契约） | Z2：module 侧逐项对齐；AC-2 验证 |
| R7 | `k_malloc` 在目标板堆为 0 → 分配失败 | Z14：`port.c` `#error` 守卫（编译期强制，唯一权威路径）+ `prj.conf` 设 heap + 文档写明目标默认 0 |
| R8 | 平台宏多 TU 不一致 | Z3 + `PUBLIC` 注入 + 符号名链接探针：`test/zephyr/` 的主 TU 显式引用 `NS_PLATFORM_ABI_PROBE`（即 `ns_platform_abi_zephyr`），使 V1 在 Zephyr 面上非空 |
| R9 | CI 时长/下载/漂移 | Z19：host 工具链免 SDK、缓存 west、`timeout-minutes`、pin 3.7 LTS |
| R10 | native_sim 假信（覆盖不到硬件） | Principle 3 + 不覆盖清单（Step 4/AC-15） |
| R11 | 微秒→tick 向下取整导致忙等 | Z10：向上取整；AC-8 |
| R12 | native_sim 默认 10 ms tick 使子 tick 测试无意义 | Z10：测试 `prj.conf` 提到 ≥1000 Hz；断言容忍晚 fire |
| R13 | Zephyr 3.5+ `zephyr/` 头前缀导致编译失败 | Z14：仅用前缀头；Zephyr 面编译即验证 |
| R14 | POSIX arch 不覆盖线程栈语义（pthread 栈，非 Zephyr 栈记账） | 不覆盖清单明示；栈相关归下游硬件冒烟 |
| R15 | 可选 nightly 掩盖 PR 门禁缺失 | nightly 明确标注非规格要求；门禁在 ci.yml |
| R16 | `k_poll` 需显式 `K_POLL_TYPE_*`，waitable 无类型标签 → 错误等待类型 | Z4：v1 限制为 `K_POLL_TYPE_SIGNAL`（仅 event 类 waitable），文档写明；异构 waitable 需加法扩展类型标签（Follow-up） |
| R17 | 内嵌栈 `K_THREAD_STACK_DEFINE` 不能嵌入结构体 / 对齐不足 | Z13：改用 `K_KERNEL_STACK_MEMBER` + `k_aligned_alloc(Z_KERNEL_STACK_OBJ_ALIGN)`；文档告警栈溢出破坏相邻堆 |
| R18 | `CONFIG_POLL` 缺失导致 `k_poll`/`k_poll_signal_raise` 未编译 | Z14：`zephyr/Kconfig` `select POLL` + `prj.conf CONFIG_POLL=y` + `port.c` `#error` 守卫 |

## Verification Steps
- **V1（AC-3）**：`test/zephyr/` 主 TU 引用 `NS_PLATFORM_ABI_PROBE`（`ns_platform_abi_zephyr`）并链接通过（PUBLIC 未传播即链接失败）；`rg -n "NANOSIG_PLATFORM_ZEPHYR" include platform zephyr CMakeLists.txt` 确认无 `#else` 兜底；`rg -n "ns_waitable_handle_is_valid|NS_WAITABLE_SET|NS_WAITABLE_GET" include/nanosig/nanosig_port.h` 确认 Zephyr 分支存在且排除 `(void *)~0`。
- **V2（AC-1/AC-13）**：`cmake -DNANOSIG_PLATFORM=zephyr -B build-x` 断言输出带指引的 `FATAL_ERROR`；编译期契约由 V3 的 module 构建承担；另以"移除 `CONFIG_POLL`/置 `CONFIG_HEAP_MEM_POOL_SIZE=0` 使构建失败"验证 `#error` 守卫。
- **V3（AC-2/AC-11/AC-12/AC-13）**：`west build -b native_sim/native/64 test/zephyr && west build -b native_sim/native/64 test/zephyr -t run`（ztest 全绿，含编译整个库）；检查构建命令确认 `NANOSIG_PLATFORM_ZEPHYR=1` 与 `NS_MPSC_CACHELINE_ALIGNED` 已注入（AC-2）；CI job 同名检查出现。
- **V4（AC-4）**：脚本比对 `port.c` 定义的 `ns_platform_*` 与 `nanosig_port.h` 声明集合（差集为空）；`rg -n "zephyr|k_poll|k_sem|Zephyr" src include | rg -v "include/nanosig/nanosig_port.h"` → 0 命中。
- **V5（AC-5/AC-14）**：`git diff --exit-code src/ns_broker.c`；`cmake --preset <host>-release && ctest` + `sanitize-all` 回归；另跑 `cmake --preset freertos-release && ctest --preset freertos-release -LE nightly`，使"四后端回归全绿"被真正覆盖。
- **V6（AC-6/AC-7/AC-8/AC-9/AC-10）**：V3 的 ztest 内对应用例逐一断言；AC-10 另以 `rg` 检查文档与 `prj.conf` 含数值下限。
- **V7（AC-15）**：`rg -n "四后端|four backend" platform/README.md docs/ARCHITECTURE.md docs/plans/共识计划.md` → 0 命中，且文档含 `k_poll` 限制与不覆盖清单小节。
- **V8（R8/R9）**：`ci.yml` 差异审查（job 名、`timeout-minutes`、cache key、pin）；分支保护更新记录（9→10）。

## ADR

- **Decision**：把 Zephyr 接入为第五类平台后端。真实构建/测试与编译期契约由 Zephyr module（`native_sim/native/64` + ztest，PR 阻塞门禁，ubuntu）承担；顶层 `NANOSIG_PLATFORM=zephyr` 输出带指引的 `FATAL_ERROR` 重定向（因 Zephyr 生成头只存在于 board 构建）。waitset=`k_poll`（`K_POLL_TYPE_SIGNAL`，`k_timeout_t`，向上取整），event=`k_poll_signal`（`drain` 为唯一 reset 点且限 poller 线程），wakeup=每对象 `k_sem`，thread=`k_thread_create`/`k_thread_join` + `K_KERNEL_STACK_MEMBER` 内嵌栈 + 优先级配置。`waitset_remove` 只摘 slot，契约可观察行为不变。钉 Zephyr 3.7 LTS，`CONFIG_POLL`/heap 由 Kconfig 与 `#error` 守卫强制。
- **Drivers**：可独立评审合并、最小扰动、PR 门禁内真实内核验证且不含无法验证的机制。
- **Alternatives considered**：B（仅编译期）、C（zsock/epoll）、D（`k_event`）、E（专用 poller 线程）。
- **Why chosen**：`k_poll` 是唯一能等待多异构对象的通用设施；native_sim 免硬件即可门禁；显式宏避免未知目标误判；移除 FreeRTOS 式 token/purge 避免电平触发下的错误 reset。
- **Consequences**：新增平台面（port.h 分支 + port.c + Zephyr module + test/zephyr + CI job + 分支保护更新）；非 lockstep；硬件相关项归下游冒烟；需修订 3 处治理文档；CI 必需检查 9→10。
- **Follow-ups**：① 专用 poller 线程/`NOTIFY_ONLY`（Option E）评估；② `ns_platform_waitable_t` 加法类型标签以支持 sem/pipe/socket waitable；③ Zephyr `OUT`/`ERR` 事件评估；④ 目标板 tick 精度与栈预算实测；⑤ ISR 安全能力评估；⑥ Zephyr module 治理文档（`.rst`/license header）按需补齐。

## 偏差接受记录（D1–D4，用户于 2026-10-11 确认接受）
1. **Zephyr SDK 是否安装**（Z17）：本计划按 native_sim 实际需要改为宿主工具链、不装 SDK；与规格 Round 2 答复不一致，已于 2026-10-11 确认接受（若坚持 SDK，Z19 加装）。
2. **Zephyr 分支编译期契约的机制**（Z1/AC-13）：规格第 9/11 条要求顶层 CMake 支持 `NANOSIG_PLATFORM=zephyr` 并覆盖该分支；因 Zephyr 源码树不含 Kconfig 生成头，本计划改为**顶层 FATAL_ERROR 重定向 + module 构建承担编译期契约**（意图保留、机制变更），已于 2026-10-11 确认接受。
3. **`waitset_remove` 的"purge 残留就绪状态"措辞**（Z5）：规格约束要求后端"消化"就绪状态；电平触发的 `k_poll` 上 `reset` 只允许在 poller 线程，故 remove 不改动信号，改为"摘除即不再产生 completion + 文档写明 re-add 行为"。已于 2026-10-11 确认接受措辞改写。
4. **waitable 类型限制为 signal 类**（Z4/R16）：规格约束列出 `k_poll_signal / k_sem / pipe / socket`，且公开头文件允许上层直接构造原始 fd waitable；v1 只接受 `k_poll_signal`（event 类）waitable，`k_sem`/FIFO/pipe/socket 列为非目标。此为**能力范围收窄**，已于 2026-10-11 确认接受（后续以加法类型标签扩展，见 Follow-up ②）。
5. **分支保护 9→10**（Z19/AC-12）：需仓库管理员操作，超出代码改动范围。
6. **native_sim 在 macOS 不可用**：与规格一致，确认 ubuntu-only 门禁；记录以免误解为遗漏。

## Consensus Log
- **Planner v1**：初稿。
- **Architect v1**：REVISE。最强反方：`k_poll_signal` 跨线程 reset 丢事件、native_sim 恰好覆盖不到该风险；综合路径：单 reset 点 + 两线程 ztest。另指出 standalone 构建不可编译、module CMake 欠规格、`k_poll` 超时类型错误、CI 缺 `gcc-multilib`、native_sim 不需 SDK、10 项 must-fix。
- **Critic v1**：REVISE。六项标准多不通过：Principle 2/5 自相矛盾；Option D 属浅层驳回、缺 Option E；R-Z5r/R-Z11r 等风险无机制；AC-6/8/9/10 不可测；V2/V3/V4/V5/V6 与 AC 不匹配；规格第 11 条缺失；13 项 must-fix。
- **Planner v2**：落实全部 must-fix（standalone→syntax-only 契约面、module CMake 全量属性、`k_timeout_t`+错误码、reset 单点+两线程测试、配置合并优先级、Kconfig 依赖与静态栈、CI `gcc-multilib`/免 SDK/`timeout-minutes`/9→10、`test/zephyr` 完整应用、恢复规格 11/12 条、风险改 R 编号、AC 可测化、V1–V8 重映射、新增 Option E 与真实 Cons、标注 SDK 偏差为 Open Item）。
- **Architect v2**：REVISE。闭环 5/10，PARTIAL 4/10（standalone 契约面仍不可实现、`CONFIG_POLL` 缺失、编译期契约机制、id 引用）。新发现：`K_THREAD_STACK_DEFINE` 不能嵌入结构体且对齐不足；waitable 无类型标签→`k_poll` 类型判别缺失；`CONFIG_POLL` 默认 n 门控 `k_poll`；Z5 与规格 purge 措辞偏差；线程优先级未定；CI 板名不一致；Option A Cons 引用错 id（应为 Z18）。8 项 must-fix。
- **Planner v3**：落实 8 项：Z1 改为单实际编译面 + 顶层 FATAL_ERROR 重定向（编译期契约由 module 构建承担）；Z4 限制为 `K_POLL_TYPE_SIGNAL`（异构 waitable 列非目标 + Follow-up 加法类型标签）；Z5 改为"摘除即不再产生 completion + 文档写明 re-add 行为"并列为 Open Item；Z13 改 `K_KERNEL_STACK_MEMBER` + `k_aligned_alloc(Z_KERNEL_STACK_OBJ_ALIGN)` + 优先级/join 语义；Z14 增 `CONFIG_POLL` 依赖与 `#error` 守卫；Z19/AC-12/V3 统一板名 `native_sim/native/64`；修正 Option A Cons 引用；两线程测试用 `k_sem` 排序；新增 R16–R18；Open Items 扩为 4 项。
- **Critic v3**：REVISE。7/8 闭环；AC-11 板名残留、"signal-only waitable"偏差未列入 Open Item、R5 陈旧、`k_aligned_alloc` 参数形式、heap `#error` 与运行期 NULL 自相矛盾、`-EINTR` 非单值、R8 残留 meta 文本、V5 未覆盖 freertos 预设。8 项 must-fix。
- **Planner v4（终稿）**：全部落实——新增 Open Item 5（能力范围收窄，需签核）；AC-11 板名统一；R5 重写为顶层重定向；`k_aligned_alloc(align,size)` 修正；heap 以 `#error` 为唯一权威路径；`-EINTR` 单值（返回错误码、不重试）；清理 R8 并指定 `test/zephyr` 主 TU 引用 ABI 探针；V5 增 freertos 预设；R4 给出具体栈预算数值；V1/V3 断言补齐。
- **Critic v4**：**APPROVE**。8/8 must-fix CLOSED；无阻塞缺陷；仅文档卫生 nit（重复 Open Item、两处偏差节、AC-2 板名、ABI 符号定义位置、event 堆依赖说明），已在定稿中修复。

## Changelog
- v1：初稿。
- v2：见 Consensus Log；修复 v1 的机制错误、可测性问题与规格覆盖缺口。
- v3：修复 Architect v2 的 8 项 must-fix（编译面机制、waitable 类型判别、remove 语义与规格措辞、栈宏与对齐、`CONFIG_POLL`、板名一致性、id 引用、线程优先级）；新增 R16–R18；Open Items 扩为 4 项。
- v4：修复 Critic v3 的 8 项（新增 Open Item 5、AC-11 板名、R5 重写、`k_aligned_alloc` 参数、heap 单一权威路径、`-EINTR` 单值、R8 清理 + ABI 探针绑定 TU、V5 覆盖 freertos、R4 具体栈预算、V1/V3 断言补齐）。Critic v4 APPROVE，定稿并应用全部非阻塞 nit。
