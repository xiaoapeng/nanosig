# Deep Interview Spec: FreeRTOS 平台后端接入

## Metadata
- Interview ID: di-20261004-freertos-backend
- Rounds: 6
- Final Ambiguity Score: 9.5%
- Type: brownfield
- Generated: 2026-10-04
- Threshold: 0.2
- Threshold Source: default
- Initial Context Summarized: no
- Status: PASSED

## Clarity Breakdown
| Dimension | Score | Weight | Weighted |
|-----------|-------|--------|----------|
| Goal Clarity | 0.95 | 0.35 | 0.3325 |
| Constraint Clarity | 0.90 | 0.25 | 0.2250 |
| Success Criteria | 0.85 | 0.25 | 0.2125 |
| Context Clarity | 0.90 | 0.15 | 0.1350 |
| **Total Clarity** | | | **0.905** |
| **Ambiguity** | | | **9.5%** |

## Topology

| Component | Status | Description | Coverage / Deferral Note |
|-----------|--------|-------------|--------------------------|
| P1 平台中立修复 | active | `waitset_remove` 的 `NS_E_BUSY` 语义 | 决策：改为**后端内部 purge**，契约维持 `NS_OK`/`NS_E_INVAL`；broker 不改，提案 P1 的 broker drain-重试取消，仅剩契约文档 |
| P2 平台判定 | active | `nanosig_port.h` 增加 `NANOSIG_PLATFORM_FREERTOS` | 决策：显式宏；未知目标继续 `FATAL_ERROR`，**不保留 `#else` 兜底** |
| P3 FreeRTOS 后端 | active | `platform/freertos/port.c` 按契约重写 | 按 5.2 契约重写；保留"每 wakeup 独立 binary semaphore"修复 |
| P4 构建接线 | active | 顶层 CMake 平台选择 + 后端常量可配置 | 决策：容量/栈/优先级**全部可配置**（宏或 Kconfig） |
| P5 文档 | active | `platform/README.md` + `ARCHITECTURE.md` 三后端→四后端 | 低风险 |
| P6 host 测试 | active | host 契约测试 | 决策：**FreeRTOS POSIX port**，纳入现有 CI；另加下游硬件冒烟 |

## Goal
把 FreeRTOS 接入为 nanosig 的**第四类平台后端**（与 Linux/macOS/Windows 并列），作为一个可独立推进、非 lockstep 的增量：
- 本仓库侧可**编译期检查**该后端并可用 **FreeRTOS POSIX port** 在 host 上跑 waitset/broker/event 契约测试（进 CI）；
- 目标硬件的运行时冒烟由下游 FLY 负责，不作为本仓库 CI 的门槛；
- 消解原 FreeRTOS 历史提案 §6 的全部 5 个开放问题。

## Constraints
- **平台判定**：新增显式 `NANOSIG_PLATFORM_FREERTOS`（可经 `-D` 定义）；未知目标继续 `FATAL_ERROR`，禁止 `#else` 静默兜底为 FreeRTOS。
- **`waitset_remove` 契约不变**：FreeRTOS 后端在 `remove` 内部 purge 残留 token，对调用方始终返回 `NS_OK`/`NS_E_INVAL`；broker 与 `platform/README.md` 的既有契约语义不变，**不加** broker drain-重试。
- **后端常量全部可配置**：waitset 容量、线程栈 words、任务优先级均暴露为可配置宏（下游可用 toolchain/`-D`/Kconfig 覆盖），并提供安全默认值；`waitset_add` 仍须做容量上限校验，杜绝越界触发内核 `configASSERT` 停机。
- **wakeup = 每对象独立 binary semaphore**，禁止 task notification（loop 唤醒与 broker op-proxy 唤醒可能指向同一任务，notification 共享 per-task 值会互相误消费 → op 滞留 → `req->wakeup` 悬垂 → HardFault）。此修复必须保留。
- **event = binary semaphore**：同一对象既是 waitable 成员又是 signal 目标；`event_deinit` 仍注册时返回 `NS_E_BUSY`（沿用已有 `ns_platform_event_*` 契约）。
- **仅支持就绪（IN）事件**：`waitset_add` 拒绝 `OUT`/`ERR`。
- **平台隔离**：`platform/` 之外不得新增 OS 预处理器分支。
- **不引入公开诊断 API**：`ns_broker_last_shutdown_rc()` 不进公共头文件。
- **无下游品牌泄漏**：不得出现 FLY 等下游标识。

## Non-Goals
- 与 Linux/macOS/Windows 逐阶段 lockstep（非强制）。
- 在 host 上跑 FreeRTOS 之外的模拟器（QEMU/Windows simulator）。
- 为"多个独立 waitable 事件源"实现具体消费者（仅保证原语可用）。
- 引入公开诊断 API（`ns_broker_last_shutdown_rc`）。
- 覆盖已先行合入的两笔提交（`aa0fe2b`、`f9bd1c3`）。

## Acceptance Criteria
- [ ] `include/nanosig/nanosig_port.h` 提供显式 `NANOSIG_PLATFORM_FREERTOS`；未定义且非 host 时保持原有 `FATAL_ERROR` 语义（顶层 CMake）。
- [ ] 顶层 `CMakeLists.txt` 能在显式选择时编入 `platform/freertos/port.c`，并更新 `FATAL_ERROR` 文案为四平台；`platform/` 之外无新增 OS 分支。
- [ ] `api-compile-checks` 覆盖 FreeRTOS 分支（编译期契约），host 上可通过。
- [ ] `waitset_remove` 在 FreeRTOS 后端内部 purge，调用方永远看到 `NS_OK`/`NS_E_INVAL`；broker 代码零改动，Linux/macOS/Windows 回归全绿。
- [ ] `waitset_add` 对超过可配置容量返回错误（不得触发 `configASSERT`）。
- [ ] 后端常量（容量/栈/优先级）可经宏配置，且有安全默认值。
- [ ] FreeRTOS POSIX port 在现有 CI runner 跑通 waitset/broker/event 契约测试（含 wakeup 独立 semaphore、event signal/drain/去注册 busy 防御）。
- [ ] 现三后端（Linux/macOS/Windows）测试与 sanitize 管道不受影响（回归）。
- [ ] `platform/README.md`、`docs/ARCHITECTURE.md` 同步为"四后端"，并写明 FreeRTOS 映射与限制。

## Assumptions Exposed & Resolved
| Assumption | Challenge | Resolution |
|------------|-----------|------------|
| FreeRTOS 必须与其它三后端 lockstep（否则不能进仓库） | Round 1：lockstep 是否强制 | 否，接受第四后端但**非 lockstep**；P6 可延后 |
| "完成"= 目标硬件跑通 | Round 2：本仓库侧可查边界 | 编译期契约 + **host 模拟器契约测试** + 硬件冒烟（硬件不进本仓库 CI 门槛） |
| host 验证需要 QEMU/目标内核 | Round 3：模拟器选型 | 用 **FreeRTOS POSIX port**，进现有 Linux/macOS CI |
| 裸机免配置 → `#else` 兜底 FreeRTOS | Round 4（Contrarian）：未知 host 会否误判 | 否，**显式宏 + 未知目标报错**；去掉 `#else` 兜底 |
| `waitset_remove` 需要公开返回 `NS_E_BUSY` | Round 5：契约 vs 后端自消化 | **后端内部 purge，对调用方透明**；broker 与契约不变，提案 P1 broker 改动取消 |
| 常量应可配置（提案建议"或在 add 时校验"二选一） | Round 6（Simplifier）：真需要可配置吗 | **全部可配置**（容量/栈/优先级），交下游 toolchain 决定 |

## Technical Context
- 基线 HEAD：`f9bd1c3`（`aa0fe2b` + `f9bd1c3` 已先行合入）；`ns_platform_event_*` 原语已在基线落地。
- 原提案来源：FLY 嵌入式集成场景下游补丁 `001-freertos-integration.patch`（765 行，仅供实现参考，不建议直接套用）。
- 平台判定位置：`include/nanosig/nanosig_port.h`（`_WIN32`/`__APPLE__`/`__linux__|__unix__`，无 `#else`）。
- 构建平台选择：`CMakeLists.txt:39-53`（`WIN32`/`APPLE`/`UNIX AND NOT APPLE`，else `FATAL_ERROR`）。
- 平台目录：`platform/{linux,macos,windows}`；无 `platform/freertos`。
- `ns_platform_waitset_remove` 现有契约：`NS_OK` / `NS_E_INVAL`（未注册）；`NS_E_BUSY = -11`（`nanosig_status.h:33`）。
- 下游 `port.c` 关键事实：alloc/free=`pvPortMalloc`/`vPortFree`；mutex=`xSemaphoreCreateMutex`；wakeup=每对象 binary semaphore；event=binary semaphore；waitset=`xQueueCreateSet`+slot 表；thread=`xTaskCreate`+自删/轮询 join；clock=`xTaskGetTickCount`；仅 IN 事件。
- CI preset：`linux-*`/`macos-*`/`windows-*`（无 FreeRTOS host 目标）。

## Ontology (Key Entities)
| Entity | Type | Fields | Relationships |
|--------|------|--------|---------------|
| wakeup | core domain | binary semaphore（每对象独立） | 被 loop 单等待；被 broker op-proxy 单等待 |
| waitable | core domain | primitive(handle)、events、edge_triggered、user_data、registered_waitset | 注册进 waitset；由 event 内嵌 |
| event | core domain | waitable + signal_handle（同 binary semaphore） | broker waitset 自唤醒；实现 ns_platform_event_* |
| waitset | core domain | xQueueCreateSet + slots[]（member→waitable） | 持有多个 waitable；容量可配置 |
| broker | core domain | thread/waitset/event/watcher 链 | 由 event 驱动；remove 语义对 FreeRTOS 透明 |
| FreeRTOS backend | external/enabling | port.c 原语映射 | 实现 `ns_platform_*`；第四类后端 |
| queue-set / binary-semaphore | supporting | FreeRTOS kernel 原语 | 支撑 waitset 与 wakeup 的底层实现 |

## Ontology Convergence
| Round | Entity Count | New | Changed | Stable | Stability Ratio |
|-------|-------------|-----|---------|--------|----------------|
| 1 | 7 | 7 | - | - | N/A |
| 2 | 7 | 0 | 0 | 7 | 100% |
| 3 | 7 | 0 | 0 | 7 | 100% |
| 4 | 7 | 0 | 0 | 7 | 100% |
| 5 | 7 | 0 | 0 | 7 | 100% |
| 6 | 7 | 0 | 0 | 7 | 100% |

本体从 Round 1 起完全收敛，6 个组件与 7 个实体在后续轮次零漂移。

## Interview Transcript
<details>
<summary>Full Q&A (Rounds 0–6)</summary>

### Round 0
**Q:** 6 个顶层组件（P1 平台中立修复 / P2 平台判定 / P3 FreeRTOS 后端 / P4 构建接线 / P5 文档 / P6 host 测试）是否正确？
**A:** 准确，就按这 6 个走。
**Ambiguity:** not scored yet

### Round 1
**Q:** 是否接受 FreeRTOS 为第四后端？是否强制 lockstep？
**A:** 接受，但不强制 lockstep；FreeRTOS 可独立推进，P6 允许降级/延后，验收以能在目标硬件跑通为准。
**Ambiguity:** 27%（Goal 0.92, Constraints 0.65, Criteria 0.45, Context 0.90）

### Round 2
**Q:** 本仓库侧"完成"的可检查项有哪些？
**A:** 编译期契约 + host 模拟器跑 waitset/broker/event 契约测试，另加下游硬件冒烟。
**Ambiguity:** 21%（Goal 0.95, Constraints 0.70, Criteria 0.60, Context 0.90）

### Round 3
**Q:** P6 的 host 契约测试用哪个 FreeRTOS 模拟器？
**A:** FreeRTOS POSIX port。
**Ambiguity:** 21%（Goal 0.95, Constraints 0.62, Criteria 0.68, Context 0.90）

### Round 4 (Contrarian)
**Q:** `#else` 兜底是否该保留？更安全的方向是不是显式宏 + 未知目标报错？
**A:** 显式宏 + 未知目标继续报错。
**Ambiguity:** 17%（Goal 0.95, Constraints 0.75, Criteria 0.70, Context 0.90）

### Round 5
**Q:** `waitset_remove` 返回 `NS_E_BUSY` 该作为公开契约，还是后端内部消化？
**A:** 后端内部 purge，对调用方透明。
**Ambiguity:** 15%（Goal 0.95, Constraints 0.82, Criteria 0.72, Context 0.90）

### Round 6 (Simplifier)
**Q:** waitset 容量 / 线程栈 / 任务优先级，v1 做到什么程度？
**A:** 全部可配置。
**Ambiguity:** 9.5%（Goal 0.95, Constraints 0.90, Criteria 0.85, Context 0.90）

</details>
