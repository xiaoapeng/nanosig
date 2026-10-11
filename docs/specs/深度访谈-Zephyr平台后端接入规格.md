# Deep Interview Spec: Zephyr 平台后端接入

## Metadata
- Interview ID: di-20261010-zephyr-backend
- Rounds: 6
- Final Ambiguity Score: 9.5%
- Type: brownfield
- Generated: 2026-10-11
- Threshold: 0.2
- Threshold Source: default
- Initial Context Summarized: no
- Status: PASSED
- 规格批准: 2026-10-11（四项评审偏差由用户确认接受，见「评审后修订」）
- 计划: docs/plans/子计划-Zephyr平台后端接入.md（共识评审 APPROVE）

## Clarity Breakdown
| Dimension | Score | Weight | Weighted |
|-----------|-------|--------|----------|
| Goal Clarity | 0.92 | 0.35 | 0.3220 |
| Constraint Clarity | 0.90 | 0.25 | 0.2250 |
| Success Criteria | 0.88 | 0.25 | 0.2200 |
| Context Clarity | 0.92 | 0.15 | 0.1380 |
| **Total Clarity** | | | **0.905** |
| **Ambiguity** | | | **9.5%** |

## Topology

| Component | Status | Description | Coverage / Deferral Note |
|-----------|--------|-------------|--------------------------|
| 平台判定与句柄分支 | active | `nanosig_port.h` 新增 `NANOSIG_PLATFORM_ZEPHYR`、handle 分支与 ABI 探针 `ns_platform_abi_zephyr` | 验收标准第 1 条 |
| Zephyr 后端实现 | active | `platform/zephyr/port.c` 实现全部 `ns_platform_*`，含 waitset=k_poll 的 add/remove/wait 与残留 token 语义 | 验收标准第 2–8 条 |
| 构建接线 | active | CMake 选择 `NANOSIG_PLATFORM=zephyr`、Zephyr module 集成、Kconfig 依赖、可配置常量 | 验收标准第 9 条 |
| host 契约测试 | active | native_sim + ztest harness 复刻 waitset/broker/event 契约，作为 PR 阻塞必需门禁 | 验收标准第 10–11 条 |
| 文档同步 | active | `platform/README.md` + `docs/ARCHITECTURE.md` 由四后端→五后端，写明 Zephyr 映射与限制 | 验收标准第 13 条 |
| 语义边界界定 | active | edge-triggered 支持范围、tick 精度要求、ISR 安全声明 | 验收标准第 6 条 + Constraints |

## Goal
把 Zephyr 接入为 nanosig 的**第 5 类平台后端**（与 Linux/macOS/Windows/FreeRTOS 并列），作为一个可独立推进、非 lockstep 的增量：
- 复用 `nanosig/nanosig_port.h` 冻结接口，不修改任何 `ns_platform_*` 签名，只新增平台扩展点；
- `platform/zephyr/port.c` 提供全部原语映射，waitset 以 `k_poll` 实现；
- 本仓库在 CI 中**跑真实 Zephyr `native_sim`**（ztest 契约测试），作为 PR 阻塞必需检查（仅 `ubuntu-latest`）；
- 目标硬件的真实 tick 精度、栈溢出、HardFault 冒烟归下游，不作为本仓库 CI 门槛。

## Constraints
- **平台判定**：新增显式 `NANOSIG_PLATFORM_ZEPHYR`（由构建系统注入，例如顶层 CMake 的 `NANOSIG_PLATFORM=zephyr` 给目标 PUBLIC 注入 `-DNANOSIG_PLATFORM_ZEPHYR=1`）；该分支必须排在宿主检测之前；未知目标继续 `FATAL_ERROR`，**禁止** `#else` 静默兜底。
- **句柄语义**：Zephyr 走 handle 分支（`ns_waitable_handle_t.handle` 指向 k_poll 可等待对象），复用 `nanosig_port.h` 既有的 `NS_WAITABLE_SET`/`GET`/`ns_waitable_handle_is_valid` handle 侧宏。
- **ABI 探针**：Zephyr 后端导出 `ns_platform_abi_zephyr`，头文件按平台分支声明，消费 TU 与库平台选择不一致时链接期失败。
- **版本钉定**：钉 **Zephyr 3.7 LTS** tag；Zephyr SDK 跟随官方 stable；只钉一处版本（Zephyr tag），不单独钉 SDK 版本号。
- **waitset 基于 `k_poll`**：`waitset_add`/`waitset_remove`/`waitset_wait` 满足既有平台中立契约；每个 waitable 的 `primitive.handle` 指向 k_poll 可等待对象（`k_poll_signal` / `k_sem` / pipe / socket）。
- **`waitset_remove` 契约不变**：后端在 `remove` 内部 purge 残留 token，对调用方始终返回 `NS_OK`/`NS_E_INVAL`；broker 零改动。
- **仅支持就绪（IN）事件**：含 `OUT`/`ERR` 位的 waitable 在 `waitset_add` 返回 `NS_E_INVAL`（同 FreeRTOS 后端）。
- **容量可配置**：waitset 容量暴露为可配置常量并提供安全默认值；`waitset_add` 超限返回 `NS_E_TOO_MANY_HANDLES`，不得触发内核 `configASSERT` 停机。
- **`edge_triggered` 忽略**：k_poll 无原生边沿触发，`edge_triggered=1` 按忽略处理（同 Windows 后端），并写入文档。
- **tick 精度**：`k_poll` 超时与 `k_uptime_ticks` 为 tick 粒度；要求目标板 `CONFIG_SYS_CLOCK_TICKS_PER_SEC` 达到下限，写入 Kconfig/文档；`waitset_wait` 的微秒超时换算必须**向上取整**到 tick，杜绝向下取整导致的忙等。
- **event = `k_poll_signal`**：同一对象既是 waitable 成员（`k_poll_signal` 等待）又是 signal 目标（`k_poll_signal_raise`）；`drain` = `k_poll_signal_reset`（幂等）；布尔语义（同轮多次 raise 可合并）；`event_deinit` 仍注册时返回 `NS_E_BUSY`。
- **wakeup = 每对象独立 `k_sem`**：计数上限 1（二值语义），`signal` = `k_sem_give`（已满视为成功），`wait` = `k_sem_take(timeout)`；不得复用会互相误消费的共享原语。
- **平台隔离**：`platform/` 之外不得新增 OS 预处理器分支；`src/`、`include/` 不直接包含 Zephyr 头文件。
- **CI 门禁**：Zephyr native_sim 契约测试是 PR 阻塞必需检查，运行在 `ubuntu-latest`；不并入 macOS/Windows runner。

> 注：本节的**版本钉定（SDK）**、**平台判定/编译面**、**`waitset_remove` purge 措辞**、**waitable 类型枚举**四项已由下方「评审后修订（Approved Deviations）」取代；其余条款继续生效。

## Non-Goals
- 与 Linux/macOS/Windows/FreeRTOS 逐阶段 lockstep。
- 提供 ISR 安全 API（`*_from_isr`）或承诺 public emit 可在 ISR 调用（沿用项目既有 v1 立场）。
- 支持 `OUT`/`ERR` 事件（k_poll 上 socket 的可写/错误等待）。
- 用 native_sim 覆盖真实 tick 精度、线程栈溢出、`configASSERT` 停机、单核竞态与硬件 HardFault 场景。
- 目标硬件接入、板级驱动或下游产品集成。
- 在 macOS/Windows runner 上跑 Zephyr CI。
- 引入公开诊断 API（例如 `ns_broker_last_shutdown_rc`）。
- 修改 `nanosig/nanosig_port.h` 的既有接口签名（只允许新增扩展点）。
- 修改其他后端的实现文件。

## Acceptance Criteria
- [ ] `include/nanosig/nanosig_port.h` 提供显式 `NANOSIG_PLATFORM_ZEPHYR`（handle 分支 + ABI 探针 `ns_platform_abi_zephyr`），分支排在宿主检测之前；未定义且非 host/freertos 时保持 `FATAL_ERROR` 语义。
- [ ] `platform/zephyr/port.c` 实现 `nanosig_port.h` 中所有 `ns_platform_*`；`platform/` 之外无新增 OS 分支。
- [ ] waitset 基于 `k_poll`：add/remove/wait 满足既有契约；`waitset_remove` 内部 purge 残留 token，调用方只见 `NS_OK`/`NS_E_INVAL`；`broker` 代码零改动。
- [ ] 仅支持 `NS_WAITABLE_EVENT_IN`；含 `OUT`/`ERR` 的 waitable 在 `waitset_add` 返回 `NS_E_INVAL`。
- [ ] waitset 容量可配置并提供安全默认值；超限返回 `NS_E_TOO_MANY_HANDLES`（不触发内核断言）。
- [ ] `edge_triggered` 忽略语义与 tick 精度下限（`CONFIG_SYS_CLOCK_TICKS_PER_SEC`）写入文档/Kconfig；`waitset_wait` 微秒超时向上取整到 tick。
- [ ] event 用 `k_poll_signal` 实现 signal/drain；`event_deinit` 在未 `waitset_remove` 时返回 `NS_E_BUSY`。
- [ ] wakeup 使用每对象独立 `k_sem`，跨线程 signal→wait 生效。
- [ ] 顶层 `CMakeLists.txt` 支持 `NANOSIG_PLATFORM=zephyr`（编入 `platform/zephyr/port.c`，注入 `NANOSIG_PLATFORM_ZEPHYR` 与可配置常量），并可在 Zephyr 构建系统中以 module（`zephyr/module.yml`）被 west 消费。
- [ ] native_sim + ztest 契约测试覆盖 waitset / event / wakeup / broker 端到端 / clock，并作为 `ci.yml` 必需检查在 `ubuntu-latest` 上跑通。
- [ ] 编译期契约（`api-compile-checks` 或等价）覆盖 Zephyr 分支。
- [ ] 现四后端（Linux/macOS/Windows/FreeRTOS）测试与 sanitize 管道不受影响（回归）。
- [ ] `platform/README.md`、`docs/ARCHITECTURE.md` 同步为"五后端"，并写明 Zephyr 映射、`k_poll` 限制与 tick 精度要求。

## 评审后修订（Approved Deviations，2026-10-11）

以下四项偏差在 `docs/plans/子计划-Zephyr平台后端接入.md` 共识评审中被识别，并经用户确认接受，作为本规格的生效修订：

| # | 原规格条款 | 修订后 | 理由 |
|---|-----------|--------|------|
| D1 | Constraints 版本钉定：「完整 Zephyr SDK」 | `native_sim`（POSIX arch）改用**宿主工具链**，CI **不安装完整 Zephyr SDK**；版本仍只钉 Zephyr 3.7 LTS tag | native_sim 不消费 SDK，安装只会增大 CI 下载与耗时；SDK 仅在将来构建真实目标板时需要 |
| D2 | Constraints 平台判定 / AC 第 9、11 条：顶层 CMake 支持 `NANOSIG_PLATFORM=zephyr` 并覆盖该分支编译 | 顶层 `NANOSIG_PLATFORM=zephyr` 输出带指引的 `FATAL_ERROR` 重定向；**编译期契约由 Zephyr module 构建承担**（`west build` 编译整个库） | `platform/zephyr/port.c` 依赖 Kconfig 生成的 `autoconf.h` 与生成头，Zephyr 源码树本身不含这些产物，顶层 CMake 无法独立编译该分支 |
| D3 | Constraints / AC 第 3 条：「`waitset_remove` 内部 purge 残留 token」 | `waitset_remove` **只摘除 slot**，不 reset 信号、不消费对象状态；可观察保证（移除后不再产生 completion）成立，文档写明 remove→re-add 行为 | 电平触发的 `k_poll_signal` 上 `reset` 只允许在 poller 线程执行，remove 可由任意线程调用；详见计划 Z5 |
| D4 | Constraints 第 47 行 waitable 类型：`k_poll_signal` / `k_sem` / pipe / socket | v1 **只接受 `k_poll_signal`（event 类）waitable**；`k_sem`/FIFO/pipe/socket 列为非目标 | `ns_platform_waitable_t` 无类型标签，`k_poll` 需显式 `K_POLL_TYPE_*`；异构 waitable 需后续加法类型标签扩展 |

其余约束（仅 IN 事件、容量可配置、edge_triggered 忽略、tick 精度向上取整、`k_poll_signal` 事件语义、每对象 `k_sem` wakeup、平台隔离、CI 门禁 ubuntu-only）保持不变。

## Assumptions Exposed & Resolved
| Assumption | Challenge | Resolution |
|------------|-----------|------------|
| 照搬 FreeRTOS 的 FetchContent 内核 harness 即可 | Round 1：本仓库侧"完成"门槛 | 不适用；Zephyr 是完整 west 工作区，改为真实 `native_sim` + SDK，仅 `ubuntu-latest` |
| native_sim 足够代表目标行为 | Round 4（Contrarian）：反向质询 | 承认只覆盖 queue/k_poll/信号语义；tick/栈/HardFault 归下游硬件冒烟，不作 CI 门槛 |
| 未知目标可 `#else` 兜底为 Zephyr | 沿用 FreeRTOS 决议 | 否，显式宏 + 未知目标报错 |
| `waitset_remove` 需要公开返回 `NS_E_BUSY` | 沿用既有契约 | 后端内部 purge，对调用方透明；broker 与契约不变 |
| k_poll 有原生边沿触发 | Round 3：语义边界 | 没有；`edge_triggered=1` 忽略（同 Windows） |
| 必须同时钉 Zephyr SDK 版本号 | Round 5（Simplifier）：最简钉定 | 否，只钉 Zephyr 3.7 LTS tag，SDK 跟随官方 stable |
| 版本应跟随最新 release 以减少维护 | Round 6：具体版本 | 否，钉 3.7 LTS，选择长期支持线 |
| ISR 安全必须本次拍板 | Round 3（未选中该项） | 沿用项目既有 v1 立场：不承诺 ISR、不提供 `*_from_isr`，本次不重新决策 |
| 线程 join 必须本次拍板 | Round 3（未选中该项） | 归为实现细节：优先 `k_thread_join`，目标版本不可用时回退一次性信号量方案 |

## Technical Context
- 平台选择与 FATAL_ERROR：`CMakeLists.txt:61-95`（当前仅 `host` / `freertos` 两值，`CMakeLists.txt:37-38`）。
- FreeRTOS 可配置常量先例：`CMakeLists.txt:43-49`（`NANOSIG_FREERTOS_WAITSET_CAP` / `NANOSIG_FREERTOS_THREAD_STACK_WORDS` / `NANOSIG_FREERTOS_TASK_PRIORITY`），编译定义注入 `CMakeLists.txt:130-145`。
- ABI 探针：宿主三后端导出 `ns_platform_abi_host`（`platform/linux/port.c:44`、`platform/macos/port.c:42`、`platform/windows/port.c:37`），FreeRTOS 导出 `ns_platform_abi_freertos`（`platform/freertos/port.c:94`）；头文件分支 `include/nanosig/nanosig_port.h:66-73`。
- 平台判定与句柄宏：`include/nanosig/nanosig_port.h:41-49`（判定）、`:89-92`（handle 断言）、`:162-199`（`ns_waitable_handle_is_valid` / `NS_WAITABLE_SET` / `NS_WAITABLE_GET`）。
- 生产代码零 fd/handle 依赖：`src/ns_broker.c:168,196` 只用 `NS_WAITABLE_SET`/`NS_WAITABLE_GET`；`:419,423` 用 `completion.waitable` 指针身份回映射；`grep primitive src/` 零命中。fd 语义只存在于 host-only 测试（`test/test_helpers.h`、`test/unit/test_broker_consume_fn.c` 等），不计入 Zephyr 图。
- broker completion 预算：`src/ns_broker.c:48` 容量 16，`include/nanosig/nanosig_port.h:570` 上限 64；broker 按返回 `count` 迭代（`src/ns_broker.c:541`），不假设恰好 1 个 completion。
- FreeRTOS 后端规模与结构：`platform/freertos/port.c` 638 行（wakeup/mutex/thread/event/waitset 各成节），可作为 Zephyr 后端的骨架参照。
- FreeRTOS host harness：`test/freertos/`、`cmake/FreeRTOSPosix.cmake`（FetchContent freertos_kernel V11.1.0）、CI job `freertos`（`.github/workflows/ci.yml:72-91`）。Zephyr 需替代方案：`native_sim` + 官方 SDK + west。
- 仓库内 Zephyr 唯一既有提及：`docs/audit/emit_zero_alloc_audit.md:263`（"后续新增平台后端（如 RTOS、Zephyr）时…"），无任何 k_poll / native_sim / 后端代码。
- 基线 HEAD：`59338b1`。

## Ontology (Key Entities)
| Entity | Type | Fields | Relationships |
|--------|------|--------|---------------|
| ns_platform_* interface | core domain | frozen signatures | implemented by every backend |
| Zephyr backend (port.c) | external/enabling | primitives mapping | implements ns_platform_*; 5th backend |
| waitset (k_poll) | core domain | add/remove/wait, stale token purge, configurable capacity | holds waitables; backed by k_poll |
| wakeup | core domain | single block wait, per-object k_sem | used by loop |
| event | core domain | waitable + signal_handle, k_poll_signal raise/reset | drives broker |
| native_sim harness | supporting | ztest, PR-blocking gate, ubuntu-latest | verifies backend in CI |
| platform detection (NANOSIG_PLATFORM_ZEPHYR) | supporting | explicit macro, ABI probe ns_platform_abi_zephyr | selects backend |
| build wiring (module.yml/Kconfig) | supporting | platform selection, configurable constants | compiles backend |
| Zephyr SDK | supporting | official stable version | builds native_sim |
| west workspace | supporting | pinned Zephyr 3.7 LTS | hosts backend module |

## Ontology Convergence
| Round | Entity Count | New | Changed | Stable | Stability Ratio |
|-------|-------------|-----|---------|--------|-----------------|
| 1 | 8 | 8 | - | - | N/A |
| 2 | 10 | 2 | 0 | 8 | 80% |
| 3 | 10 | 0 | 0 | 10 | 100% |
| 4 | 10 | 0 | 0 | 10 | 100% |
| 5 | 10 | 0 | 0 | 10 | 100% |
| 6 | 10 | 0 | 0 | 10 | 100% |

本体自 Round 3 起完全收敛，核心实体 6 轮零漂移。

## Interview Transcript
<details>
<summary>Full Q&A (Rounds 0–6)</summary>

### Round 0
**Q:** 拓扑确认：平台判定与句柄分支 / Zephyr 后端实现 / 构建接线 / host 契约测试 / 文档同步 / 语义边界界定，共 6 个顶层组件是否正确？
**A:** 就按这 6 个走。
**Ambiguity:** not scored yet

### Round 1
**Q:** 本仓库侧「Zephyr 接入完成」的门槛该怎么定？
**A:** 本仓库 CI 跑真实 Zephyr native_sim。
**Ambiguity:** 34%（Goal 0.75, Constraints 0.50, Criteria 0.55, Context 0.90）

### Round 2
**Q:** native_sim harness 的环境该怎么钉定？
**A:** 钉具体 release tag + 完整 Zephyr SDK。
**Ambiguity:** 27%（Goal 0.78, Constraints 0.72, Criteria 0.55, Context 0.90）

### Round 3
**Q:** 哪些语义边界必须写进验收标准？
**A:** 选中 5 项：edge_triggered 忽略+文档 / tick 精度下限写入 Kconfig+文档 / 仅支持 IN（OUT/ERR 在 add 返回 NS_E_INVAL）/ 容量可配置+超限 NS_E_TOO_MANY_HANDLES / remove 残留 token 内部 purge（NS_OK|NS_E_INVAL）。未选：真实现 edge、ISR 声明、OUT/ERR 支持、k_thread_join 拍板。
**Ambiguity:** 19%（Goal 0.82, Constraints 0.78, Criteria 0.78, Context 0.90）

### Round 4 (Contrarian)
**Q:** native_sim 契约测试在 CI 里怎么定位？
**A:** 必需门禁（PR 阻塞）+ 仅 ubuntu-latest。
**Ambiguity:** 15%（Goal 0.88, Constraints 0.78, Criteria 0.85, Context 0.90）

### Round 5 (Simplifier)
**Q:** 版本钉定的最小集合怎么定？
**A:** 只钉 Zephyr release tag。
**Ambiguity:** 11%（Goal 0.90, Constraints 0.88, Criteria 0.88, Context 0.92）

### Round 6
**Q:** 具体钉哪个 Zephyr 版本？
**A:** 固定钉 Zephyr 3.7 LTS。
**Ambiguity:** 9.5%（Goal 0.92, Constraints 0.90, Criteria 0.88, Context 0.92）

</details>
