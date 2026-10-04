# Implementation Plan: FreeRTOS 平台后端接入

- 状态: **已实施**（FreeRTOS 后端 + 构建接线 + host 契约测试落在当前工作树，待提交；CI 验证待完成）
- 输入规格: docs/specs/深度访谈-FreeRTOS平台后端接入规格.md
- 来源: FLY 嵌入式集成场景下游补丁 `001-freertos-integration.patch`（原 FreeRTOS 历史提案已由本实施计划取代）
- 模式: RALPLAN-DR short
- 基线: `f9bd1c3`
- 日期: 2026-10-04

## Requirements Summary

把 FreeRTOS 接入为 nanosig 第四类平台后端（非 lockstep 增量）。本仓库侧交付：
1. 显式 `NANOSIG_PLATFORM_FREERTOS` 平台判定（构建注入、PUBLIC 可见；未知目标继续报错）。
2. `platform/freertos/port.c` 按后端契约重写（alloc/mutex/wakeup/event/waitset/thread/clock）。
3. `waitset_remove` 由后端处理 token 生命周期，调用方契约不变。
4. 顶层 CMake 能显式选择并按可配置常量编入 FreeRTOS 后端。
5. host 侧以 FreeRTOS POSIX port 跑队列集语义契约测试并纳入 CI（并明确其不能覆盖的边界）。
6. 文档由"三后端"更新为"四后端"，并消解与既有治理条款的冲突。

## RALPLAN-DR Summary

### Principles
1. **平台隔离** —— OS/RTOS 差异只存在于 `platform/`。
2. **契约不扩大** —— `waitset_remove` 对调用方维持 `NS_OK`/`NS_E_INVAL`；token 生命周期由后端负责。
3. **回归安全** —— 现三后端与 broker 行为不变，由现有 CI 全量回归。
4. **保留硬件已验证修复** —— 每 wakeup 独立 binary semaphore，禁用 task notification。
5. **可配置但有安全默认** —— 容量/栈/优先级可配置；容量校验按 **token 容量**（非成员数）在 `add` 处返回错误，杜绝 `configASSERT` 停机。
6. **验证诚实** —— host 只声明队列集语义覆盖；硬件相关项显式归属下游冒烟。

### Decision Drivers
1. 让 FreeRTOS 成为**可独立评审、可独立合并**的第一类后端。
2. 最小化对既有三后端与 broker 的扰动。
3. host 侧可验证（FreeRTOS POSIX port），无目标硬件也能进 CI。

### Viable Options
- **A（选定）：后端处理 token + 显式平台宏 + POSIX port host 测试**
  - Pros：契约与 broker 调用点零改动；平台判定语义安全；host 可 CI 验证。
  - Cons：后端需定义 token 生命周期（必须精确，见 H2/H5）；容量需按 token 语义校验。
- **B：`#else` 兜底 FreeRTOS + broker drain-重试**（提案原建议）
  - Cons：`#else` 使未知 host 被静默误判（Round 4 否决）；扩大 broker/契约面。**INVALIDATED**。
- **B′：显式宏 + broker 平台中立 drain-重试（无 `#else`）**
  - Pros：`NS_E_BUSY` 语义显式、后端实现简单。
  - Cons：为仅 FreeRTOS 会触发的路径扩大所有平台的契约与 broker 代码；与 Principle 2 冲突。**驳回**（Round 5 选择后端自消化）。
- **C：只保留已合入的两笔修复，不做后端** —— 不满足目标。**INVALIDATED**（Round 1）。
- **架构子选型对照（采纳 vs 既有共识）**：`docs/plans/共识计划.md:606-615` 与 `platform/README.md:109-110` 原定 RTOS 走 **event group + `event_bit`**；本计划改走 **queue-set + `handle`**，理由：queue-set 直接映射 `waitset`（多事件源等待）且与 `ns_platform_event_*` 的 waitable-first 模型一致，event group 无法表达"多 waitable 注册"。**P5 必须同步修订共识计划中该前向路径**（见 H6）。

## Resolved Decisions（针对首轮 must-fix）

- **D1 平台选择机制（H1/H9/P4）**：新增 CMake cache 变量 `NANOSIG_PLATFORM`（取值 `host`|`freertos`，默认 `host` 时按宿主 `WIN32/APPLE/UNIX` 选择）。FreeRTOS 分支判定必须置于 `WIN32/APPLE/UNIX` **之前**；选择 FreeRTOS 时以 **`PUBLIC`** 注入 `NANOSIG_PLATFORM_FREERTOS=1`（沿用 `CMakeLists.txt:83` 的 PUBLIC 先例），保证 `src/*.c` 与消费者 TU 展开同一分支。
- **D2 平台宏分支顺序（H1）**：`nanosig_port.h` 平台判定段先判 `NANOSIG_PLATFORM_FREERTOS`，再判 `_WIN32/__APPLE__/__linux__|__unix__`；**无** `#else` 兜底。POSIX host 上编译 FreeRTOS 后端时，因宏先判，正确选到 FreeRTOS 分支。
- **D3 `waitset_remove` 语义（H2/H5）**：定义后端契约为"**排空被移除成员 → 从 set 摘除 → 容忍一条 stale token**"：
  - 仅对**被移除的那个成员**做非阻塞排空（`xSemaphoreTake`/`xQueueReceive` 至空），表示"remove 丢弃该 watcher 的未处理就绪事件"，与平台中立语义（disconnect 不撤回已入队调用）一致；
  - **禁止**用 `xQueueSelectFromSet` 去"摘 token"（会先吃掉他人 token）；
  - wait 阶段对 slot 表未命中的 stale token 直接跳过；
  - `waitset_add` 增加 **stale-token flush**：非阻塞 select 并丢弃"成员句柄不在任何活跃 slot"的 token，防止 re-add/句柄复用产生假完成（H5）。
  - 排空/摘除失败则返回既有 `NS_E_INVAL`（不新增 `NS_E_BUSY`）。
- **D4 容量校验（H3）**：`waitset_add` 按 queue-set 约束 `CAP >= Σ成员容量 + max(成员容量)` 校验 token 容量；broker 场景成员均为 binary semaphore（容量 1），要求 `CAP >= 成员数 + 1`（含 broker event）。`NANOSIG_FREERTOS_WAITSET_CAP` 默认值由构建给出（建议 32，且 `add` 显式校验，越界返回 `NS_E_TOO_MANY_HANDLES`）。
- **D5 host 测试边界（H4）**：POSIX port 契约测试**只覆盖** waitable/waitset/event/broker 的队列集与信号语义。**明确不覆盖**：线程栈深度、`portTICK_PERIOD_MS` 精度、`configASSERT` 停机、单核竞态、notification 共享悬垂（硬件 HardFault 场景）、SMP 宿主差异——这些归属下游 MCXN947 硬件冒烟。
- **D6 治理对齐（H6）**：P5 强制修订 `platform/README.md:4-8/78`（"同步推进"增加非 lockstep 例外与理由）、`platform/README.md:104-111`（RTOS 前向路径）、`docs/plans/共识计划.md:615`（"不暴露编译期平台宏"——澄清 `NANOSIG_PLATFORM_FREERTOS` 为构建注入的平台选择宏，非头文件内平台特性宏）、`docs/ARCHITECTURE.md:277-278`（三→四后端）。
- **D7 shutdown 泄漏（H8）**：不新增公开诊断 API；在 `ns_broker.c` shutdown 路径对 `waitset_destroy`/`event_deinit` 的失败补 `ns_merrln(BROKER, ...)` 日志（沿用现有 `do_remove` 的日志先例），并在 FreeRTOS host harness 用泄漏检测断言"init/shutdown 无泄漏"。
- **D8 P6 钉版本**：FreeRTOS-Kernel 钉 **V11.1.0**（与目标一致）；harness 目标/preset 名与命令见 Verification。
- **D9 FreeRTOS 构建面隔离（复核阻塞项 1）**：`NANOSIG_PLATFORM=freertos` 时，主构建只编 `platform/freertos/port.c` 变体，**不注册** host-only 测试（`test/` 下依赖 `__linux__`/`eventfd`/POSIX fd 的用例）。该选择**要求提供 FreeRTOS SDK 头文件路径**（`NANOSIG_FREERTOS_INCLUDE_DIRS` 或 toolchain），缺失时 CMake `FATAL_ERROR`；`platform/freertos/port.c` 的编入由 `NANOSIG_PLATFORM=freertos` 决定，**与** host harness 选项解耦。`NANOSIG_BUILD_FREERTOS_POSIX`（默认 `OFF`）仅门控 host POSIX harness 目标/测试。host 默认构建（`NANOSIG_PLATFORM=host`）行为完全不变。
- **D10 多 TU 一致性验证（复核阻塞项 2）**：改用**符号名编码**的链接期探针——C 无跨 TU 类型检查，"值编码"只能做运行期断言，无效。库按所选平台导出不同**符号名**（如 `ns_platform_abi_freertos` / `ns_platform_abi_host`）；测试/harness TU 引用与期望平台匹配的符号。若 `NANOSIG_PLATFORM_FREERTOS` 未 PUBLIC 传播，库与消费者选到不同分支 → 所引用符号名不存在 → **链接错误**。

## Acceptance Criteria（可测）

- [ ] AC-1：顶层 CMake 新增 `NANOSIG_PLATFORM`（`host`|`freertos`，默认 `host`）；`freertos` 分支位于 `WIN32/APPLE/UNIX` 之前，选中时以 `PUBLIC` 注入 `NANOSIG_PLATFORM_FREERTOS=1` 并编入 `platform/freertos/port.c`；选择 `freertos` 时要求 `NANOSIG_FREERTOS_INCLUDE_DIRS`（`NANOSIG_BUILD_FREERTOS_POSIX=ON` 时自动填充，否则必须提供），缺失则 `FATAL_ERROR`；未列出平台维持 `FATAL_ERROR`。
- [ ] AC-2：`nanosig_port.h` 平台判定段先判 `NANOSIG_PLATFORM_FREERTOS`、无 `#else`；`ns_waitable_handle_is_valid`/`NS_WAITABLE_SET`/`NS_WAITABLE_GET` 在 FreeRTOS 下走 handle 分支并排除 `(void *)~0`；**TU 内**分支/union 一致性用 `NS_STATIC_ASSERT`，**跨 TU 传播**由符号名链接探针保证（见 V1）。
- [ ] AC-3：`platform/freertos/port.c` 实现全部 `ns_platform_*`；`platform/` 外无新增平台分支（`rg` 指定命令，port.h 平台判定段为既有例外）。
- [ ] AC-4：`waitset_remove` 按 D3 语义实现；调用方只看到 `NS_OK`/`NS_E_INVAL`；`src/ns_broker.c` 调用点零改动；三后端回归全绿。
- [ ] AC-5：`waitset_add` 按 **token 容量** 校验（D4）；容量/栈/优先级可经宏配置；越界返回 `NS_E_TOO_MANY_HANDLES`；有 pending 时 remove 的返回值与后续 wait 行为有断言。
- [ ] AC-6：`cmake -DNANOSIG_PLATFORM=freertos -DNANOSIG_BUILD_FREERTOS_POSIX=ON ...` 能编入并链接 `platform/freertos/port.c`，且该构建**只**注册 FreeRTOS 契约测试（不注册 host-only 测试）；host 默认构建（`NANOSIG_PLATFORM=host`）行为完全不变。
- [ ] AC-7：FreeRTOS POSIX port（内核 V11.1.0）harness 跑通 waitset add/remove/wait、event signal/drain/deinit-busy、broker 冒烟；**并在文档列明 D5 不覆盖清单**。
- [ ] AC-8：Linux/macOS（及可用时 Windows）现有测试 + `sanitize-all` 回归不受影响。
- [ ] AC-9：`platform/README.md`、`docs/ARCHITECTURE.md`、`docs/plans/共识计划.md` 按 D6 完成修订（四后端 + 治理例外 + 前向路径）。

## Implementation Steps

### Step 1 — P1：`waitset_remove` 契约文档（`platform/README.md`）
- 在 waitset 段落写明：成功 `NS_OK`、未注册 `NS_E_INVAL`；后端负责 token 生命周期，调用方不感知残留 token。
- 不改 `src/ns_broker.c` 调用点。

### Step 2 — P2：平台判定（`include/nanosig/nanosig_port.h`）
- 平台判定段：`#if defined(NANOSIG_PLATFORM_FREERTOS)` 置首，随后 host 三分支，删除 `#else`。
- `ns_waitable_handle_t.handle` 注释扩展（Windows HANDLE / FreeRTOS 队列或信号量成员句柄）；`event_bit` 标注为保留前向。
- `ns_waitable_handle_is_valid`/`NS_WAITABLE_SET`/`NS_WAITABLE_GET` 增加 FreeRTOS（handle）分支。
- `ns_waitable_init` 注释补充 RTOS 的 `(void *)~0` 哨兵。

### Step 3 — P3：`platform/freertos/port.c`
- 原语：`init/shutdown`、`alloc/free`（`pvPortMalloc`/`vPortFree`）、`mutex`（`xSemaphoreCreateMutex`）、`wakeup`（每对象独立 binary semaphore）、`event`（binary semaphore 同时为 waitable 与 signal 目标）、`waitset`（`xQueueCreateSet` + `slots[]`）、`thread`（`xTaskCreate` + 自删；join 用 join 信号量，替换轮询）、`clock`（`xTaskGetTickCount`）。
- `waitset_add`：仅接受 `NS_WAITABLE_EVENT_IN`；成员为空；重复 `NS_E_EXISTS`；按 D4 做 token 容量校验；执行 stale-token flush。
- `waitset_remove`：按 D3 实现（排空本成员 + 摘除 + 容忍 stale token）；返回 `NS_OK`/`NS_E_INVAL`。
- `waitset_wait`：`xQueueSelectFromSet`，仅首个可阻塞；member→waitable 映射；**不 take**；stale token 跳过。
- 可配置常量：`NANOSIG_FREERTOS_WAITSET_CAP`、`NANOSIG_FREERTOS_THREAD_STACK_WORDS`、`NANOSIG_FREERTOS_TASK_PRIORITY`（默认 32 / 1024 / `tskIDLE_PRIORITY+1`）。
- 文件头注释自洽（不得写 task notification）；无下游品牌字样；内部写 union 统一走 `NS_WAITABLE_SET`。

### Step 4 — P4：构建接线（`CMakeLists.txt`、`cmake/`、`CMakePresets.json`）
- 新增 `NANOSIG_PLATFORM` cache 变量（`host`|`freertos`，默认 `host`）；FreeRTOS 分支置于 host 分支之前；`PUBLIC` 注入 `NANOSIG_PLATFORM_FREERTOS=1`；编入 `platform/freertos/port.c`；暴露可配置常量为 cache 变量 → 编译定义。
- 更新 `FATAL_ERROR` 文案为四平台。
- **构建面隔离（D9）**：`NANOSIG_PLATFORM=freertos` 时不注册 host-only 测试；新增 `option(NANOSIG_BUILD_FREERTOS_POSIX ... OFF)` 门控 FreeRTOS POSIX harness 目标（依赖 FreeRTOS-Kernel V11.1.0 + POSIX port）。host 默认构建路径不变。
- **导出平台标记符号（D10）**：库按所选平台导出**不同符号名** `ns_platform_abi_host` / `ns_platform_abi_freertos`，并提供**宏门控的 extern 声明头**（与 `nanosig_port.h` 同一 `#if defined(NANOSIG_PLATFORM_FREERTOS)` 映射），供 harness 按各自宏引用。**禁止**固定同名符号——否则探针永远链接成功、失去检出能力。
- **FreeRTOS SDK 来源**：`NANOSIG_BUILD_FREERTOS_POSIX=ON` 时由 CMake（钉 V11.1.0 的 FreeRTOS-Kernel + POSIX port，FetchContent/子模块）自动填充 `NANOSIG_FREERTOS_INCLUDE_DIRS`；否则该变量必须由使用者提供，缺失即 `FATAL_ERROR`。

### Step 5 — P5：文档与治理（D6）
- `platform/README.md`：三→四后端；新增 FreeRTOS 映射与限制（IN-only、queue-set token 容量约束）；修订"同步推进"条款（非 lockstep 例外 + 理由）；修订 RTOS 前向路径段。
- `docs/ARCHITECTURE.md`：三→四后端；broker 段同步。
- `docs/plans/共识计划.md`：澄清 `NANOSIG_PLATFORM_FREERTOS` 定位与 queue-set 选型。

### Step 6 — P6：host 契约测试（FreeRTOS POSIX port，内核 V11.1.0）
- 新增 host harness（钉 V11.1.0 tag/commit）编译 `platform/freertos/port.c`，产出**独立**的 FreeRTOS 变体库。
- 契约断言：waitset add（含 token 容量越界）/remove（含 pending 时）/wait；event signal/drain/deinit-busy；stale-token 不产生 spurious completion；init/shutdown 无泄漏。
- 声明 D5 不覆盖清单；CMake 可选目标 + CI（linux/macos job，未启用则跳过）。

## Risks and Mitigations
| # | 风险 | 缓解 |
|---|---|---|
| H1 | 平台宏多 TU 不一致 / 分支顺序错误 | D1/D2：PUBLIC 注入 + FREERTOS 分支置首；V1 编译期多 TU 断言 |
| H2 | 后端"remove"误消费他人 token | D3：仅排空被移除成员，禁止无差别 select；测试覆盖 pending 时 remove |
| H3 | 容量只校验成员数 → `configASSERT` 停机 | D4：按 token 容量校验；越界返回错误；测试覆盖越界 |
| H4 | POSIX host 测试假阴/假阳 | D5：显式声明不覆盖清单；硬件项归下游冒烟 |
| H5 | re-add/句柄复用 stale token 假完成 | D3：add 时 stale-token flush；测试覆盖 remove→re-add |
| H6 | 与现有治理条款冲突 | D6：P5 强制修订四处文档 |
| H7 | `add` "成员必须为空"为 FreeRTOS 特有语义 | 在 `platform/README.md` 写明该后端限制；用 `NS_E_INVAL` 表达逻辑错误并在头文件注释澄清 |
| H8 | shutdown 忽略 rc → 静默泄漏 | D7：补 debug 日志 + host harness 泄漏断言 |
| H9 | CMake 平台选择顺序 | D1：freertos 分支置于 host 之前 |
| H11 | join 信号量 vs 自删时序 | 自删任务在 `vTaskDelete(NULL)` 前 signal；host 测试覆盖 create/join |
| H12 | 直写 union vs `NS_WAITABLE_SET` | 统一走宏 |

## Verification Steps
- **V1（H1/AC-2，D10）**：链接期**符号名**探针——库导出平台专属符号名（`ns_platform_abi_host` / `ns_platform_abi_freertos`）；测试 TU 引用与期望平台匹配的符号名；若宏未 PUBLIC 传播，库与消费者选到不同分支，链接因未定义符号失败。
- **V2（AC-1/AC-6，D9）**：`cmake -DNANOSIG_PLATFORM=freertos -DNANOSIG_BUILD_FREERTOS_POSIX=ON -DNANOSIG_FREERTOS_INCLUDE_DIRS=<posix-port/include> -B build-freertos && cmake --build build-freertos -j`（`POSIX=ON` 时该变量可省略，由钉版 FetchContent 自动填充；仅编 `platform/freertos/port.c` + harness，不注册 host-only 测试）。
- **V3（AC-7）**：`cd build-freertos && ctest -R freertos --output-on-failure`（waitset/event/broker 契约 + 泄漏断言）。
- **V4（AC-8）**：`cmake --preset linux-debug-asan && cmake --build build -j && ctest -LE long-stability`；`cmake --build build --target sanitize-all`。
- **V5（AC-3）**：`rg -n "_WIN32|__APPLE__|__linux__|__unix__|FreeRTOS" src include | rg -v "include/nanosig/nanosig_port.h"` → 仅允许既有例外；`rg -n "NANOSIG_PLATFORM_FREERTOS" include platform CMakeLists.txt` 确认无 `#else` 兜底。
- **V6（AC-9）**：`rg -n "三后端|three backend|不包含 RTOS" platform/README.md docs/ARCHITECTURE.md docs/plans/共识计划.md` → 0 命中。

## ADR

- **Decision**：把 FreeRTOS 接入为第四类平台后端；`NANOSIG_PLATFORM`（host|freertos）显式选择、PUBLIC 注入 `NANOSIG_PLATFORM_FREERTOS`；`waitset_remove` 由后端处理 token 生命周期（排空本成员 + 容忍 stale token）；后端常量按 token 容量语义可配置；host 侧以 FreeRTOS POSIX port（V11.1.0）跑契约测试并声明不可覆盖边界。
- **Drivers**：可独立评审合并、最小扰动、host 可验证。
- **Alternatives considered**：B（`#else` 兜底 + broker drain）、B′（显式宏 + broker drain）、C（不做后端）、queue-set vs event-group 子选型 —— 见 Viable Options。
- **Why chosen**：契约与 broker 调用点零改动；显式宏避免未知目标误判；POSIX port 使 host CI 可验证；queue-set 天然表达多 waitable。
- **Consequences**：新增平台面（port.h 分支 + port.c + CMake cache 变量 + host harness）；后端须实现 D3/D4；非 lockstep，硬件冒烟不在本仓库 CI；需修订 4 处治理文档。
- **Follow-ups**：① 多独立 waitable 事件源接入；② macOS wakeup 轻量化；③ Windows waitset completion 容量缺陷；④ `ns_platform_event_wait` 需求评估；⑤ `ns_platform_wakeup_t` caller-owned 化。

## Consensus Log
- **Planner v1**：初稿。
- **Architect**：REVISE（H1–H12；最强反方：后端 purge 名不副实、显式宏可变静默 ABI 错配、POSIX 测试保证不匹配；给出 synthesis：受限成员 + 显式 stale 治理）。
- **Critic**：REVISE（六项评审标准全部不通过；新增 AC-1 vs AC-7 host 选择矛盾；列 9 项必须修改）。
- **Planner v2**：落实全部 must-fix：D1–D8（平台选择机制/分支顺序/PUBLIC 注入、purge 契约、token 容量校验、测试边界、治理修订、shutdown 日志、P6 钉版本），补齐备选评估（B′/event-group），AC 与 Verification 可执行化。
- **Critic 复核（v2）**：10 项中 9 项已解决；阻塞项——`NANOSIG_PLATFORM=freertos` 构建会连带注册 host-only 测试，AC-6/AC-8 不可同时成立；V1 多 TU 探针机制不成立；选项命名不一致。
- **Planner v3**：D9（freertos 构建面隔离 + `NANOSIG_BUILD_FREERTOS_POSIX` 门控）与 D10（链接期多 TU 一致性探针）落实全部阻塞项；统一命名；AC-6/V1/V2 修订。
- **Critic 终审（v3）**：第 1、3 项已解决；第 2 项未闭合——"值编码"链接探针在 C 下不成立且与 AC-2"编译期断言"矛盾；派生缺口——`NANOSIG_BUILD_FREERTOS_POSIX=OFF` 时 `port.c` 无 FreeRTOS 源码可用。
- **Planner v4**：D10 改为**符号名编码**链接探针；D9 补 FreeRTOS SDK 依赖（`NANOSIG_FREERTOS_INCLUDE_DIRS`，缺失 `FATAL_ERROR`）并解耦"编入 port.c"与 harness 选项；AC-1/AC-2/V1 措辞对齐；元数据更新。
- **Critic 确认（v4）**：上轮两阻塞机制已闭合；但发现 Step 4 仍写固定符号名 `ns_platform_chosen_tag`（与 D10 冲突、废掉探针检出），且 V2 缺 `NANOSIG_FREERTOS_INCLUDE_DIRS`（与 AC-1 的 `FATAL_ERROR` 互斥）。
- **Planner v5**：Step 4 改为按平台导出不同符号名 + 宏门控 extern 声明头；新增 FreeRTOS SDK 来源说明（`POSIX=ON` 自动填充）；V2 命令补 `NANOSIG_FREERTOS_INCLUDE_DIRS` 与省略条件；AC-1 同步。

## Changelog (v1 → v2)
- 新增 Resolved Decisions D1–D8，逐条对应 Architect H1–H8 与 Critic must-fix 1–9。
- 修订 AC-1/AC-5/AC-7，消除 AC-1（未知目标报错）与 AC-7（host 跑 FreeRTOS）矛盾（改为显式 `NANOSIG_PLATFORM=freertos`）。
- Viable Options 增补 B′ 与 event-group 子选型对照。
- Risks 表补齐 H1/H6/H8/H9/H11/H12。
- Verification 增加 V1（多 TU 编译期断言）、V2/V3（FreeRTOS preset/ctest）、V6（治理文本）。
- 删除未证实断言口径，改为"contract 不变、broker 调用点零改动"。

## Changelog (v2 → v3)
- D9：`NANOSIG_PLATFORM=freertos` 构建面隔离，新增 `NANOSIG_BUILD_FREERTOS_POSIX`（默认 OFF）；该构建不注册 host-only 测试，host 默认构建不变（消解复核阻塞项 1）。
- D10：V1 由"两 TU 同命令行 include"改为**链接期平台标记符号探针**，真正可检出 PUBLIC 传播失败（消解复核阻塞项 2）。
- 统一 `NANOSIG_BUILD_FREERTOS_POSIX` 命名并写入 Step 4 / AC-6 / V2。
- AC-6 明确"freertos 构建只含 FreeRTOS 契约测试"。

## Changelog (v3 → v4)
- D10 改为**符号名编码**链接探针（原"值编码"在 C 下不可检出跨 TU 错配）；AC-2 区分为"TU 内 static_assert + 跨 TU 符号名探针"。
- D9 补 FreeRTOS SDK 依赖：`NANOSIG_PLATFORM=freertos` 要求 `NANOSIG_FREERTOS_INCLUDE_DIRS`，缺失 `FATAL_ERROR`；"编入 port.c"与 `NANOSIG_BUILD_FREERTOS_POSIX` 解耦。
- AC-1 增补 SDK 依赖要求；V1 改为符号名探针；状态行元数据更新。

## Changelog (v4 → v5)
- Step 4 平台标记符号由固定名 `ns_platform_chosen_tag` 改为按平台导出不同符号名 `ns_platform_abi_host`/`ns_platform_abi_freertos` + 宏门控 extern 声明头（否则探针失效）。
- 新增 FreeRTOS SDK 来源说明：`NANOSIG_BUILD_FREERTOS_POSIX=ON` 时自动填充 `NANOSIG_FREERTOS_INCLUDE_DIRS`，否则必须外部提供。
- V2 命令补充 `NANOSIG_FREERTOS_INCLUDE_DIRS` 与"`POSIX=ON` 可省略"条件；AC-1 同步措辞。
