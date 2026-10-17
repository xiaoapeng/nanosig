# Implementation Plan: ns_watcher 回调化（方案 B / callback-only）

- 状态: **已实施**（本地分支 `feat/ns-watcher-callback-only`，off origin/master 单提交；macOS release/asan/ubsan 本地门禁已过；Linux/Windows/FreeRTOS/Zephyr 运行时与 ci.yml 门禁待 CI 验证）
- 输入规格: .opencode/specs/deep-interview-ns-watcher-callback-only.md
- 来源: 深度访谈（方案 B）+ docs/proposals/ns_watcher回调化提案.md
- 模式: RALPLAN-DR short
- 基线: `fix/zephyr-review-002-007`（含 PR #9）
- 日期: 2026-10-16

---

## Requirements Summary

把 `ns_watcher` 从「内嵌 signal + 可选 consume_fn」的双出口模型，改造为**回调唯一的就绪原语**。仓库侧交付：

1. 删除 `ns_watcher_t.signal`、`ns_watcher_event_t`、`ns_watcher_set_consume_handle` / `pending_consume_handle`；`ns_watcher_consume_fn` 改名/改签名为 `ns_watcher_handler_fn`。
2. broker 不再对 watcher `ns_signal_emit_raw`；dispatch 退化为 `watcher->handler(watcher, triggered_events)`。
3. handler 必填、返回 `void`；生命周期 marker 改为 `handler != NULL`（并保留次要结构校验，见 D3）。
4. 线程 API 边界由「不得调用任何 nanosig API」改为**黑名单式**（只禁会等待 broker 线程的 API），以文档为唯一约束。
5. 两套投递范式（回调即终点 / 回调即入口）以 demo + 文档 + 契约测试交付，非库 API。
6. 迁移单测/集成测/zephyr/freertos 用例、文档与 review 契约条目加注。
7. `ns_timer_t` **保持原样**（继续内嵌 signal 并由 timer_mgr emit）；文档显式声明该有意不对称。
8. 单 PR 一次到位，特性分支 + rebase 合并。

## Principles

1. **回调唯一出口** —— 消费点只有 handler 一处，取消「NULL 则 broker 直接 emit」路径。
2. **库不新增 API** —— 不提供 fd→loop 一段式便利、不引入可选 sink（不回退方案 C）。
3. **契约文档化，不加运行时守卫** —— 线程边界与 drain 义务用 `@warning`/文档约束。
4. **有意不对称但显式声明** —— watcher 用回调、timer 用 signal，文档必须写清。
5. **回归安全** —— 三 host 平台 + FreeRTOS/Zephyr CI 全量回归；`sanitize-all` 不受影响。
6. **caller-owned 惯例** —— 上下文用 `NS_CONTAINER_OF` 反推，不新增结构字段。

## Decision Drivers

1. 消除现状双出口歧义与「在槽里读 fd 喂爆 MPSC ring」的反模式。
2. 让 broker 对 watcher 单向、无 signal 依赖。
3. 破坏性变更可独立评审、可独立合并。

## Viable Options

- **A（选定 = 提案方案 B，handler-only）**：删除内嵌 signal，handler 为唯一下游出口；回 loop 由用户自建 signal。
- **C（可选 sink 指针）**：**驳回**——库 signal payload 装不下 fd 数据，"便利"是幻觉；保留 emit 路径收益小。
- **D（最小改进）**：**驳回为最终形态**——未消灭双出口歧义与喂爆风险。
- **E（反转 I/O）**：**INVALIDATED**——阻塞 read 会卡 loop。

## Resolved Decisions

- **D1 公共结构定稿**：`ns_watcher_t{ waitable, broker_node, handler }`；删除内嵌 `signal` 首字段与 `NS_STATIC_ASSERT(offsetof(signal)==0)`（include/nanosig/nanosig_broker.h:82）。核实：`ns_watcher_t`/`ns_timer_t` 无跨类型布局兼容依赖，无 generic helper 依赖首字段 signal（唯一使用者是 broker 自身 dispatch）。
- **D2 签名定稿**：`int ns_watcher_init(ns_watcher_t*, ns_waitable_handle_t, uint32_t events, int edge_triggered, ns_watcher_handler_fn)`；`typedef void (*ns_watcher_handler_fn)(ns_watcher_t*, uint32_t triggered_events)`；保留 `ns_watcher_deinit` / `ns_watcher_handle` / `ns_broker_add` / `ns_broker_remove`。
- **D3 生命周期 marker**：判定 = `handler != NULL` **且** `waitable.events` 合法 **且** `broker_node` 已初始化（保留次要结构校验，避免 garbage/非零初始化 watcher 被误判为已初始化）。
- **D13 init 校验顺序与重复 init 策略（与 D3 配套）**：`ns_watcher_init` 顺序固定为
  1. `watcher == NULL` → `NS_E_INVAL`
  2. `handler == NULL` → `NS_E_INVAL`
  3. 句柄/事件非法 → `NS_E_INVAL`
  4. runtime 未就绪 → `NS_E_SHUTDOWN`
  5. `reset_empty` 后填充
  **不检测重复初始化**：caller-owned 对象统一不设「已初始化 → `NS_E_EXISTS`」判定
  （与 `ns_signal_init_raw` 一致，亦符合既有审计 F1/F2「重复 init 泄漏按文档免责」）；
  重复 init（未先 deinit）属调用方错误、未定义行为，仅在公开头 `@warning` 说明。
  `ns_watcher_is_initialized` 仅用于 `deinit` 未初始化校验与 `add/remove` 合法性检查，不再用于 init。
  影响：test/unit/test_broker_lifecycle.c:123 现期望「合法句柄 + NULL handler + 未 `ns_init` → `NS_E_SHUTDOWN`」；按此顺序改为在 handler 检查处返回 `NS_E_INVAL`，该测试需同步更新（或用非 NULL handler 表达 NS_E_SHUTDOWN）。
- **D14 deinit 完整重置**：`ns_watcher_deinit` 成功后必须重置 `waitable` + `broker_node` + 清零 `handler`（复用 `reset_empty`），否则 `ns_watcher_handle` 在已 deinit 的 watcher 上返回陈旧句柄而非无效哨兵。未初始化 → `NS_E_INVAL`。
- **D4 线程边界（黑名单式）**：`nanosig_broker.h` 的 `@warning` 由「不得调用任何 nanosig API」改为「必须非阻塞；不得调用任何会**等待 broker 线程**的 nanosig API（`ns_broker_add`/`ns_broker_remove`/`ns_shutdown` 及 teardown）」。`ns_signal_emit*` 明确允许（非阻塞，src/nanosig.c:408-446）。
- **D5 dispatch 退化**：`ns_broker_dispatch_watcher` 改为仅 `watcher->handler(watcher, triggered_events);`（src/ns_broker.c:388-402）。文件头 `@section 生命周期`（:18-23）同步改写。
- **D6 上下文携带**：`NS_CONTAINER_OF` 嵌入，不新增 `user_data` 字段。
- **D7 交付物**：新增两个 demo（情况一「就绪即处理」/情况二「读净 + emit 自建 signal 回 loop」）+ 情况二跨线程投递契约测试 + 文档更新。
  - **可移植 waitable（修复 demos 无 OS 分支约束）**：demo 用公开的 `ns_platform_event_t`（`ns_platform_event_init` + `ns_platform_event_signal`，注册其 `waitable`；include/nanosig/nanosig_port.h:403-438）作为事件源，另一线程 signal；**不得**在 demos/ 内写 OS 预处理器分支（AGENTS.md 禁止事项）。
- **D8 迁移面**（单 PR；**逐文件**说明，非"改签名"一句）：
  - 代码：include/nanosig/nanosig_broker.h、src/ns_broker.c。（`include/nanosig/internal/ns_broker.h` 仅含 `ns_broker()`/global init/shutdown，**无 watcher 声明，不在改动面**——原 Step 2 撤销。）
  - 单测：test_broker_consume_fn.c（→ handler 语义，建议更名 test_broker_handler.c）、test_broker_lifecycle.c、test_broker_event.c、test_broker_multi.c、test_broker_concurrent_remove.c（CMakeLists.txt:224 注册）、test_broker_contract_compile.c。
  - 集成测（**均为 NULL + `&watcher.signal` 的旧路径，需重写为必填 handler**）：scenario_watchdog.c:69-72、scenario_multi_route.c:122-124、scenario_lifecycle_chaos.c:114-116、scenario_lifecycle_marathon.c:67-68、scenario_reentrant_race.c:98-99、scenario_random_chaos.c:101-102、test_hive.c:91-96。
  - 单测中同类 NULL+signal 路径：test_broker_event.c:149,153（slot:281）、test_broker_lifecycle.c:123,147（slot:69-77）、test_broker_multi.c:184,192、test_broker_contract_compile.c:8,22-24、test_broker_concurrent_remove.c:38。
  - 平台用例：test/zephyr/src/main.c（`broker_consume`）、test/freertos/test_freertos_broker.c（`broker_consume`）。
  - 改写范式：`ns_watcher_init(..., NULL)` + `ns_signal_connect(&w.signal,...)` → 必填 handler；「回 loop」类在 handler 内构建/emit 用户自建 signal（与 D7 情况二一致）。
- **D9 符号消失的保证方式（修订：不设机械探针）**：C 语言没有「正向断言某符号不存在」的写法（无 `static_assert(!exists(T))`），唯一**精确**手段是「故意编不过的 TU」（ctest `WILL_FAIL`）。但该方案代价明确：需要往 `test/` 引入一棵**设计性失败**的 TU，且 `WILL_FAIL` 对失败**原因**不敏感（因无关原因失败也算通过），因此还必须配一棵**正向对照** TU 来证明探针通道健康。
  - 经决策**不设机械化负向探针**：符号消失由 **code review + 一次性 V2 grep 验证**保证。理由：重新引入已删除的公共符号是需要主动为之、且必然经过公共头 review 的动作，不属于手滑型失误；`include/nanosig/` 的单一权威头文件正是 review 的天然检查点。
  - 由此**不新增** ctest-only compile probe helper、**不新增**负向/正向对照 TU、**不触碰** `api-compile-checks` 聚合目标。
  - 明确**放弃**对 `ns_watcher_t.signal` 的机械钉死：文本匹配无法把它与合法的 `ns_timer_t.signal` 区分开，该字段删除同样交由 review 保证。
- **D10 timer 保持原样 + 措辞收紧**：不改 `ns_timer_t` 与 `src/ns_timer.c`；正文必须写「broker 线程仍会为 **timer** emit signal，只是不再为 watcher emit」，**不写**"broker signal-free"。**docs/proposals/* 为历史草案，保持原文不改**；其 §3.3 措辞由本计划 D10 取代（不做回改）。
- **D11 治理文档同步（修正行号 + 范围）**：需修订
  - docs/BROKER_DESIGN.md:9,47,67-85,190-193,229-232,408,425,438,461-463（结构体/主循环/API/依赖/示例）
  - docs/ARCHITECTURE.md:111,136,141,150-152,209（:**243 是头文件索引行，无需改**）
  - docs/plans/共识计划.md `### watcher`（:216-244，含 :229-233 已不存在的 `ns_watcher_init(int fd)` / `(void* handle)` 双入口、:238「自动调 ns_signal_init_raw」、:239「释放 signal 内部资源」）与 `### event broker`（:246-266）
  - **显式声明 out-of-scope（历史快照，不随本次改）**：docs/audit/*（api_consistency_audit.md、behavior_contract_audit.md）、docs/specs/深度访谈-信号槽定时器代理测试规格.md。
  - docs/review/nanosig-core-code-review.md 中引用已移除符号 / 旧行号处**加注**（见 D12）。
- **D12 review 契约加注**：按 `docs/review/AGENTS.md`「加注而非重开」处理 `docs/review/broker-code-review.md` 的 `BROKER-004/012`（合同修订为黑名单式）、`BROKER-016`（非阻塞约束保留）、`BROKER-025`（措辞）；不新增重开的运行时守卫条目。另对 `docs/review/nanosig-core-code-review.md` 中引用 `test_broker_consume_fn.c` 的陈旧行**加注**「相关符号已随 watcher 回调化移除」，不改原结论。
- **D15 零初始化前提（评审确认）**：`ns_watcher_t` 必须由调用方零初始化或仅经 `ns_watcher_init` 产出；非零初始化的 garbage watcher 传入 `deinit` 属未定义输入（与仓库既有 caller-owned 契约一致），并在头文件 `@pre` 写明。此前提 + D3 次要校验共同守住 AC-4。

## Acceptance Criteria

- [x] **AC-1** `ns_watcher_init` 对 `handler == NULL` 返回 `NS_E_INVAL`
- [x] **AC-2** 就绪时 handler 被调用一次，且 `triggered_events` 与 waitset completion 一致
- [x] **AC-3** handler 内 `ns_signal_emit` 可把事件跨线程投递到用户 loop 的 slot
- [x] **AC-4** 未初始化（含 `{0}`）时 `ns_watcher_deinit` 返回 `NS_E_INVAL`
- [x] **AC-5** `nanosig_broker.h` 不再 `#include <nanosig/nanosig_signal.h>`
- [x] **AC-6** 电平触发 + handler 内读净 → 不再重复触发
- [x] **AC-7（已修订：取消）** 原定「二次 `ns_watcher_init` 返回 `NS_E_EXISTS`」经决策取消：caller-owned 对象统一不做重复 init 判定（D13 修订）；改为公开头 `@warning` 声明重复 init 属未定义行为，测试断言二次 init **不**返回 `NS_E_EXISTS`。
- [x] **AC-8** `ns_watcher_t` 无 `signal` 字段（结构断言）
- [x] **AC-9（已修订：取消机械探针）** 符号消失由 code review + V2 一次性 grep 保证（D9 修订）：`ns_watcher_event_t`/`ns_watcher_set_consume_handle`/`ns_watcher_consume_fn`/`pending_consume_handle` 在 `src`/`include`/`test`/`demos` 中 0 命中（`docs/` 设计文档除外）。
- [x] **AC-10** 全部现有 broker 单测/集成测（含 D8 逐文件清单）在改名后全绿
- [ ] **AC-11** 门禁：ci.yml 全部必需检查（矩阵展开：linux asan/ubsan/release、macos asan/ubsan/release、windows、zephyr、freertos ubuntu/macos）全绿 + 手动 nightly（TSAN）通过，`gh pr merge --rebase`
- [x] **AC-12** 治理文档同步（D11）：BROKER_DESIGN / ARCHITECTURE / 共识计划 watcher+broker 段与实现一致，且改后正文无"broker signal-free"表述

## Implementation Steps

### Step 1 — 公共头（include/nanosig/nanosig_broker.h）
- 删 `ns_watcher_event_t`（:39-42）与 `ns_watcher_consume_fn` typedef（:61）；新增 `ns_watcher_handler_fn`。
- `struct ns_watcher`（:69-80）改为 `{ waitable, broker_node, handler }`；删 :82 signal 首字段静态断言。
- 删 `#include <nanosig/nanosig_signal.h>`。
- 改写 `ns_watcher_init`（:105-107）docstring、`@pre`（零初始化/D15）、`@warning`（D4）；`ns_watcher_deinit`（:120）注明完整重置（D14）。
- 删 `ns_watcher_set_consume_handle`（:161-177）；保留 `ns_watcher_handle`。

### Step 2 — 运行时（src/ns_broker.c）
- `ns_watcher_reset_empty`（:110-121）：删除 signal 清零，改为重置 waitable + broker_node + `handler = NULL`。
- `ns_watcher_is_initialized`（:124-129）：改为 `handler != NULL && events 合法 && broker_node 已初始化`（D3）。
- `ns_watcher_init` / `ns_watcher_init_common`（:132-152 / :154-171）：删 `ns_signal_init_raw`；按 D13 顺序插入校验与二次 init 检查（在 reset 之前）。
- `ns_watcher_deinit`（:173-186）：删 `ns_signal_deinit_raw`；按 D14 完整重置。
- `ns_broker_dispatch_watcher`（:388-402）：退化为 `watcher->handler(watcher, triggered_events);`。
- 文件头 `@section 生命周期`（:18-23）：改写为 handler + 黑名单式契约。

### Step 3 — 测试迁移（D8 逐文件）
- 按 D8 清单改写：单测（含 test_broker_concurrent_remove.c）、集成测（NULL+signal → 必填 handler）、zephyr/freertos 用例。
- test_broker_contract_compile.c：删已移除符号引用，迁到新签名 + AC-1/4/7 断言。
- 更新 test_broker_lifecycle.c:123 断言顺序（D13）。

### Step 4 — 新增交付物（D7）
- demos/ 情况一、情况二（用 `ns_platform_event_t` 造 waitable；CMake 按 CMakeLists.txt:277-283 模式注册）。
- 情况二跨线程投递契约测试（AC-3）。

### Step 5 — 文档（D11）
- docs/BROKER_DESIGN.md、docs/ARCHITECTURE.md、docs/plans/共识计划.md（按 D11 行号）。

### Step 6 — review 加注（D12）
- docs/review/broker-code-review.md：BROKER-004/012/016/025 加注合同修订，不重开。

### Step 7 — 门禁
- 特性分支 → PR → ci.yml 全绿 + nightly(TSAN) → `gh pr merge --rebase`。

## Risks and Mitigations

| # | 风险 | 缓解 |
|---|------|------|
| H1 | handler 内误调 `ns_broker_add/remove` → 自死锁、静默挂死 | D4 `@warning` + 文件头 `@section` + 示例旁禁令 |
| H2 | 电平触发未读净 → 每轮空转重复触发 | 文档 + AC-6 drain 契约测试 |
| H3 | 破坏性变更导致下游编译失败 | 单 PR + 全量 CI；显式标注破坏性 |
| H4 | handler 内 emit 遇 MPSC ring 满静默丢事件 | 沿用既有语义（现状 dispatch 亦 `(void)` 丢弃，src/ns_broker.c:401）；文档提示 |
| H5 | ~~二次 init 检查死路径~~（策略已改） | 决策取消重复 init 判定（D13 修订）：统一文档免责，测试断言不返回 NS_E_EXISTS |
| H6 | 文档写"broker signal-free"误导 | D10 措辞收紧 + AC-12 |
| H7 | review 契约加注漏项 | D12 明确清单 |
| H8 | FreeRTOS/Zephyr 用例迁移遗漏 | D8 逐文件清单 + 对应 CI job |
| H9 | ~~负向探针假通过 / 误挂聚合目标~~（策略已改） | 决策不设机械探针（D9 修订）：符号消失交由 code review + 一次性 grep |
| H10 | watcher-api 校验减弱（garbage watcher） | D3 次要校验 + D15 零初始化前提 + AC-4 |
| H11 | demo 需真实 waitable 但禁止 OS 分支 | D7：改用 `ns_platform_event_t` |

## Verification Steps

- **V1（AC-1..10）**：`cmake --preset macos-release && cmake --build --preset macos-release && ctest --preset macos-release`（另跑 linux/windows）；`cmake --build --preset macos-release --target api-compile-checks`；`cmake --build <build> --target sanitize-all`。
- **V2（AC-5/AC-9）**：`rg -n "ns_watcher_event_t|ns_watcher_consume_fn|ns_watcher_set_consume_handle|pending_consume_handle" src include test demos` → **0 命中**（`docs/` 设计文档除外）；`ctest -R "broker|demo_watcher"` 全绿。
- **V3（AC-3）**：情况二跨线程投递契约测试。
- **V4（AC-6）**：电平触发 drain 测试，断言不重复触发。
- **V5（AC-11）**：`gh workflow run nightly.yml --ref <branch>`；`gh pr merge --rebase`。
- **V6（AC-12）**：`rg -n "signal-free|broker 模块变为" docs/BROKER_DESIGN.md docs/ARCHITECTURE.md docs/plans/共识计划.md include/nanosig/nanosig_broker.h` → 0 命中（**docs/proposals/* 为历史草案，排除**）；共识计划 watcher 段签名与头文件一致。

## ADR

- **Decision**：watcher 改为回调唯一的就绪原语（方案 B）；删除内嵌 signal 与侧信道；handler 必填/void/改名；线程边界黑名单式；两套范式以 demo+文档+契约测试交付；timer 保持 signal 不动、文档显式声明不对称；单 PR + rebase 合并。
- **Drivers**：消除双出口歧义与喂爆反模式、broker 对 watcher 单向无 signal 依赖、可独立评审合并。
- **Alternatives considered**：C（可选 sink）、D（最小改进）、E（反转 I/O）。
- **Why chosen**：库 signal 不携带 fd 数据；删掉的正是最别扭的机制（内嵌 signal init/deinit、侧信道、隐式门控）。
- **Consequences**：破坏性公共 API 变更；下游需改；回 loop 需用户自建 signal；与 timer 有意不对称（文档化）；符号消失不设机械探针、交由 code review 保证；`ns_watcher_init` **不检测重复初始化**（未定义行为，文档化；与 `ns_signal_init_raw` 统一）。
- **Follow-ups**：① 是否后续对齐 timer（本次明确不做）；② handler 内 emit 失败/drop 语义是否显式化；③ 是否需要 fan-out（多订阅者）。

## Consensus Log

- **Planner v1**：初稿（依据深度访谈 spec 与提案，含 D1–D12、AC-1..12、V1–V6）。
- **Architect/Critic 复核（v1）**：15 项必须修改。Blocker：① AC-7 二次 init 检测被 `reset_empty` 置于死路径，计划仅改名 marker 无法修复；② 大量 NULL+`&watcher.signal` 用例在删 signal 后静默破裂，D8 只写"改签名"未覆盖。Major：D9 负向探针误挂聚合目标且会假通过；D8 漏 test_broker_concurrent_remove.c；D11 行号错误且漏 audit/docs；D7 demo 无便携 waitable（OS 分支禁令）；Step 2 为空操作；AC-4 守卫减弱。Minor：init 校验顺序、deinit 未重置 waitable、CI 数量口径、V6 grep 自相矛盾、行号范围。
- **Planner v2**：全部落实 —— D3/D13（marker + 校验顺序 + 前置二次 init）、D8（逐文件 + concurrent_remove）、D9（ctest-only WILL_FAIL + 类型级硬错误 + 正向对照）、D7（`ns_platform_event_t` 便携 waitable）、D11（真实行号 + out-of-scope 声明）、D14（deinit 完整重置）、D15（零初始化前提）、AC-7 标注新修复、AC-11 口径对齐、V2/V6 修订、撤销空 Step。
- **开放项收敛（作者裁决）**：① 非零初始化 watcher = 未定义输入，零初始化为硬前提（D15）；② nanosig-core-code-review.md 陈旧引用**加注**（D12）；③ AC-11 门禁按 ci.yml 实际矩阵展开。
- **Planner v3（定稿）**：落实三项裁决；D12 扩到 core review 加注；D15 标注确认；状态置定稿待批准。开放问题清零。

## Open Items

（无。v3 已收敛全部开放项。）

## Changelog (v2 → v3)

- D15 经作者确认：零初始化是硬前提。
- D12 扩展：docs/review/nanosig-core-code-review.md 陈旧引用加注。
- AC-11 确认为按 ci.yml 矩阵展开口径。
- 状态：v3 定稿，待批准实施。

## Changelog (v3 → 实施)

- 在分支 `feat/ns-watcher-callback-only`（off origin/master，单提交）落地 D1–D15。
- 事实核对：origin/master 与当时工作树的 src/ns_broker.c、include/nanosig/nanosig_broker.h、test_broker_consume_fn.c 三者一致（PR #9 为平台层修复，未触碰 broker），故计划行号在 master 上成立。
- 本地已验证：macos-release 构建 + ctest 全绿、api-compile-checks、sanitize-all 审计、V2/V6 grep；timer 未改动。（负向/正向编译探针后经决策取消，见 v5。）
- `test_broker_consume_fn.c`（13 用例）由 `test_broker_handler.c`（6 用例）+ 新增 `test_broker_delivery.c` 替代：被删用例均为已消失语义（返回值 emit 门控、consume_handle 侧信道、set_consume_handle），剩余语义一一映射。
- 仍未验证（交由 CI）：Linux/Windows/Zephyr/FreeRTOS 编译与运行时、nightly(TSAN)；AC-11 门禁未勾。
- 说明：`docs/plans/共识计划.md` 内的 `### watcher` / `### event broker` 段已同步；`docs/proposals/*` 保持历史原文。

## Changelog (v3 → v4：取消重复 init 判定)

- 决策：caller-owned 对象统一**不做**「已初始化 → `NS_E_EXISTS`」判定（与 `ns_signal_init_raw` 一致，符合既有审计 F1/F2「重复 init 泄漏按文档免责」结论）。
- 影响：`ns_watcher_init` 删除重复 init 前置检查；D13 修订校验顺序；AC-7 取消并改为断言「二次 init 不返回 `NS_E_EXISTS`」；H5 作废；公开头 `@warning` 改为声明重复 init 属未定义行为；test/unit/test_broker_lifecycle.c 断言同步。
- 未改动：`ns_broker_add` 重复注册仍返回 `NS_E_EXISTS`（注册幂等，非对象 init）；全局单例（`ns_init` / `ns_broker_global_init` / `ns_timer_mgr_global_init` / `ns_debug_init`）保留「已注册」错误，等同内核 `register_*` 风格。
- 范围外遗留：`ns_timer_init` / `ns_signal_init_raw` 的重复 init 资源泄漏不在本次处理（同样按文档免责策略）。

## Changelog (v3 → v5：取消负向编译探针)

- 决策：**不为「符号消失」设置机械化探针**。C 无「正向断言某符号不存在」的写法，唯一精确手段是「故意编不过的 TU（`WILL_FAIL`）+ 正向对照」，代价是往 `test/` 引入一棵设计性失败的 TU，并让校验强度依赖「正向对照是否健康」。权衡后改为 **code review + 一次性 V2 grep**：重新引入已删除的公共符号是主动为之、必然过公共头 review 的动作。
- 影响：删除 `test/unit/test_broker_removed_symbols_negative.c` 与 `test/unit/test_broker_contract_positive.c`；回退 cmake/ApiCompileChecks.cmake 新增的 `_nanosig_add_compile_probe` helper；移除 CMakeLists.txt 的两处探针注册；D9 重写、AC-9 修订、H9 作废、V2 去掉 ctest 断言、ADR consequences 同步。ctest 用例数 57 → 55。
- 明确放弃：`ns_watcher_t.signal` 的机械钉死（文本匹配会误伤合法的 `ns_timer_t.signal`），同样交由 review。
- 保留不动：平台 ABI 链接探针（`NS_PLATFORM_ABI_PROBE`）与本次无关，仍由 harness TU 引用，用于检出后端宏跨 TU 错配。

## Changelog (v1 → v2)

- 新增 D13（init 校验顺序，含二次 init 前置于 reset）、D14（deinit 完整重置）、D15（零初始化前提）；D3 补次要结构校验。
- D8 改为逐文件迁移清单，纳入 test_broker_concurrent_remove.c 与所有 NULL+signal 集成/单测；撤销原 Step 2（内部头无 watcher 声明）。
- D9 重写为 ctest-only WILL_FAIL 探针 + 类型级硬错误 + 正向对照，排除于 `api-compile-checks` 聚合目标。
- D7 指定 `ns_platform_event_t` 作为 demo 的便携 waitable，规避 demos/ 内 OS 分支禁令。
- D11 修正文档行号并显式声明 docs/audit/*、docs/specs/*、历史 review 为 out-of-scope；D10 明确 docs/proposals/* 保持原文。
- AC-7 标注为「新修复」，新增 AC 与 V2/V6 修订，AC-11 与 AGENTS.md 口径对齐。

## Changelog (v1)

- 初稿：由 `.opencode/specs/deep-interview-ns-watcher-callback-only.md` 细化而来，锁定 D1–D12 与 AC/Verification。
