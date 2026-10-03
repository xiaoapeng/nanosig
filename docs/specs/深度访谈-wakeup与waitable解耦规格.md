# Deep Interview Spec: 解耦 wakeup 与 waitable

## Metadata
- Interview ID: di-20261002-wakeup-waitable
- Rounds: 6（含 Round 0 拓扑）
- Final Ambiguity Score: 8%
- Type: brownfield
- Generated: 2026-10-02
- Threshold: 0.2
- Threshold Source: default（settings 中未配置 omc.deepInterview.ambiguityThreshold）
- Initial Context Summarized: no
- Status: PASSED

## Clarity Breakdown
| Dimension | Score | Weight | Weighted |
|-----------|-------|--------|----------|
| Goal Clarity | 0.95 | 0.35 | 0.3325 |
| Constraint Clarity | 0.88 | 0.25 | 0.22 |
| Success Criteria | 0.90 | 0.25 | 0.225 |
| Context Clarity | 0.92 | 0.15 | 0.138 |
| **Total Clarity** | | | **0.9155** |
| **Ambiguity** | | | **~8.5%** |

## Topology

| Component | Status | Description | Coverage / Deferral Note |
|-----------|--------|-------------|--------------------------|
| 平台 API 形态重建 | active | nanosig_port.h 删除 `ns_platform_wakeup_get_waitable`；新增 waitable-first 事件原语 | 验收准则 AC-1/2 |
| 三个平台后端 | active | linux/macos/windows port.c 同步实现新原语、删除桥函数 | 验收准则 AC-3 |
| 核心调用方适配 | active | src/ns_broker.c 改用新原语做 waitset 自唤醒通讯 | 验收准则 AC-4 |
| 测试与文档 | deferred | 两个既有测试删除/改型 + docs 同步 | 用户 Round 5 确认：本轮并入"测试全绿"验收而非独立组件，文档同步随实现走 |

## Goal
将 wakeup 与 waitable 彻底拆成两个独立原语：
1. **wakeup** 退回纯单等原语：只有 create/signal/wait/destroy，不再暴露内部原语（删除 `ns_platform_wakeup_get_waitable`），调用方永远拿不到 wakeup 内部 fd/handle。
2. **新增 `ns_platform_event_t`**（命名用户已确认）—— waitable-first 三件套原语（create/signal/destroy），struct 内嵌 `ns_platform_waitable_t`，调用方直接把 `&ev->waitable` 注册进 waitset。不设 get_waitable 式桥。
3. **broker 迁移**：`src/ns_broker.c` 用 `ns_platform_event_t` 替代原来的 wakeup→waitable 桥做 waitset 自唤醒；op 提交路径 `ns_broker_op_submit` 改为 signal broker->event；`req->wakeup` 单等待路径不变（仍用 wakeup）。

## Constraints
- 新原语为"最简三件套"（create/signal/destroy），**不带单等待**（wait 单等待场景一律归 wakeup；避免与 wakeup 功能重叠，遵守"零容忍仅默认/显式参数不同的内在重复"规则）
- 调用方拥有存储：`ns_platform_event_t` 结构由调用方声明/嵌入，不做 Opaque + alloc 的 wakeup 模式
- 结构内嵌 `ns_platform_waitable_t`；waitable 就是本体的一部分，不存在"事件 → waitable"的第二次转换
- 三平台实现禁止在 `platform/` 之外的文件出现 OS 预处理器分支；包含 `include/nanosig/nanosig_port.h`（该头文件本身是平台抽象边界，允许既有 `NS_PLATFORM_*` 分支，如 `ns_waitable_handle_t` 联合体）
- 平台实现选型（已确认）：Linux = eventfd（sja，同 wakeup）；macos = **pipe**（independent，比 wakeup 的 kq 轻）；Windows = CreateEvent 自动复位事件句柄（s）
- `ns_broker_dispatch_pending_events` 中 `w == &broker->wakeup_waitable` 的跳过逻辑改为新事件的 waitable 地址比较

## Non-Goals
- 不改 `src/nanosig.c` 的 loop 单等待路径（继续使用 `ns_platform_wakeup_wait`）
- 不改 watcher 的 waitable 提供方式（watcher 自己持有 fd/handle）
- 不动 `src/ns_timer.c`（当前不使用 waitset）
- 不在本轮实现"将来多个 waitable 事件源"的具体消费者（仅铺路）
- 不引入新的 demo 目标文档

## Acceptance Criteria
- AC-1: `ns_platform_wakeup_get_waitable` 从 `include/nanosig/nanosig_port.h` 与三个 port.c 中删除
- AC-2: `nanosig_port.h` 新增 `ns_platform_event_t`（内嵌 waitable）+ create/signal/destroy 三函数声明
- AC-3: `platform/{linux,macos,windows}/port.c` 各自实现新原语；`test/unit/test_platform_backend.c` 无编译错误
- AC-4: `src/ns_broker.c` 不再调用已删除的桥函数；broker waitset 自唤醒通讯用新原语
- AC-5: `test/unit/test_platform_backend.c` 中 `test_wakeup_waitable` 删除或改造为新原语的 signal→waitset 触发测试；`test/unit/test_platform_contract_compile.c` 同步
- AC-6: `cmake --build --preset windows-release` + `api-compile-checks` + `ctest` + `sanitize-all` 全通（镜检 Linux 下另跑 linux-release）

## Assumptions Exposed & Resolved
| Assumption | Challenge | Resolution |
|------------|-----------|------------|
| "解耦"也可以是 waitset 内建 wakeup / wakeup 提供反向接口 | Round 1 Ontology 三选一 | 用户明确：彻底拆开，两个独立原语 |
| broker 只能继续注册 wakeup | Round 2 三选一 | 用户明确：新增事件原语 |
| 新原语对照 wakeup 且只多 waitable 能力是否重复 | Round 3 指出"零容忍重复"约束 | 用户明确：waitable-first 最简三件套，不带单等待 |
| 9 行桥函数现状稳定，为何拆 | Round 4 Contrarian | 用户明确：为未来多个独立 waitable 事件源（timer/用户事件）挂 waitset 铺路 |
| 原语存储/命名由实现者定 | Round 6 Simplifier + 命名 | 用户明确：`ns_platform_event`，内嵌 waitable |

## Technical Context
### 现状代码事实（来自图检索 + 源码确认）
- 桥函数唯一生产调用方：`src/ns_broker.c:697`（broker init）；测试调用：`test/unit/test_platform_backend.c:130`、`test/unit/test_platform_contract_compile.c:32`
- macos 的 get_waitable 桥有 graph 中 3 个 callers（路径覆盖分析图非权威，以 grep 定位为准）
- wakeup 三平台内部形态：linux `eventfd`（platform/linux/port.c:101）、macos `kqueue EVFILT_USER`（platform/macos/port.c:141）、windows `CreateEventA auto-reset`（platform/windows/port.c:93）
- broker waitset 触发 drain 的自唤醒处理：`src/ns_broker.c:418/537/540`
- 无既有 wakeup ↔ waitable 解耦的 deep-interview / plan 文档（历史 covers: fill-tests / fix-bugs / hashtbl-port）

### 实现方案待定（留给 omc-plan 阶段的 open item）
1. `ns_platform_event_t` 公共 struct 的平台私有字段安放：macos pipe 有读写 fd 两个；signal 要写 write-fd，waitable 的 primitive 是 read-fd。可选思路：内嵌 waitable + 便携内部字段（如 `ns_platform_event_detail` 联合体或固定成员），用 port.h 允许的平台分支；此细节在 omc-plan consensus 中定。
2. macos 实现若用 pipe，则 `ns_waitable_handle_t` 联合体无需变更（read fd 走 `.fd`）

## Ontology (Key Entities)
| Entity | Type | Fields | Relationships |
|--------|------|--------|---------------|
| wakeup | core domain | fd (linux) / kq (macos) / event HANDLE (win) | 被 loop 单等待；被 broker op proxy 单等待 |
| waitable | core domain | primitive(fd/handle)、events、edge_triggered、user_data | 注册进 waitset；由 event 内嵌 |
| ns_platform_event_t | core domain | waitable + 平台私有 signal 通道 | 被 broker 用作 waitset 自唤醒信号；signal 跨线程安全 |
| waitset | core domain | epoll Set | 持有多个 waitable 的指针 |
| broker | core domain | thread/waitset/event/watcher 链 | 由 event 驱动自唤醒；dispatch 到 loop signal |

## Ontology Convergence
| Round | Entity Count | New | Changed | Stable | Stability Ratio |
|-------|-------------|-----|---------|--------|----------------|
| 0-6 | 5 | 3 | 0 | 2 | 40%→80% |

## Interview Transcript
<details>
<summary>Full Q&A (6 rounds)</summary>

### Round 0
**Q:** 拓扑确认：API 形态 / 三平台后端 / 核心调用方 / 测试文档，四个组件对吗？
**A:** 方向不对（理解偏了）
**Ambiguity:** 未评分

### Round 1
**Q:** "解耦"的宾语是什么？拆完后 wakeup 和 waitable 的关系？
**A:** 彻底拆开：两个独立原语，wakeup 只做 signal/wait，waitable 独立由调用方构造
**Ambiguity:** ~55%

### Round 2
**Q:** broker waitset 线程失去 wakeup_waitable 之后靠什么自唤醒？
**A:** 新事件原语（可 signal 且可等待）
**Ambiguity:** ~35%

### Round 3
**Q:** 新原语 API 边界：最简三件套 / 四件套全能 / 吞掉 wakeup？
**A:** waitable-first 最简三件套（create/signal/destroy，无单等待）
**Ambiguity:** ~28%

### Round 4 (Contrarian)
**Q:** 现状桥函数只有 9 行且稳定，拆的真正动机？
**A:** 为未来扩展铺路（多个独立 waitable 事件源挂 waitset）
**Ambiguity:** ~18%

### Round 5
**Q:** "拆完了"的验收标准？
**A:** 测试全绿（wakeup_waitable 测试删改 + broker 集成 + preset 全链 + sanitize-all）
**Ambiguity:** ~12%

### Round 6 (Simplifier + naming)
**Q:** 新原语名字与存储形态？
**A:** `ns_platform_event`，调用方拥有、内嵌 waitable
**Ambiguity:** ~8%（达标）

</details>

---

**状态：已实施（当前工作树，待提交）**
