# Implementation Plan: 解耦 wakeup 与 waitable（ns_platform_event 原语）

- 状态: **已实施**（event 重构 + 测试 + 文档同步已完成，当前工作树待提交）
- 输入规格: docs/specs/深度访谈-wakeup与waitable解耦规格.md
- 模式: RALPLAN-DR short（无 --deliberate；不涉及 auth/迁移/生产事故，但属公共 API 变更，以 ADR 收口）
- 日期: 2026-10-02

## Requirements Summary
1. wakeup 退回纯单等原语：删除 `ns_platform_wakeup_get_waitable`（`include/nanosig/nanosig_port.h:319`、三个 `platform/*/port.c`），wakeup 内部 fd/handle 不再可被调用方提取。
2. 新增 waitable-first 事件原语 `ns_platform_event_t`：调用方拥有存储、内嵌 `ns_platform_waitable_t`，init/signal/drain/deinit 四函数（最简边界，不提供阻塞单等待——单等待归 wakeup 独占）。
3. broker 生产代码迁移到新原语，功能等价（op 提交即时唤醒 waitset 线程）。
4. 驱动动机：为将来多个独立 waitable 事件源（timer 服务、用户事件）挂 waitset 铺路，不再绑架 wakeup。

## RALPLAN-DR Summary

### Principles
1. **调用方拥有存储** —— `ns_platform_event_t` 由调用方声明，init/deinit 管生命周期（对齐 `ns_signal_init/deinit`、`ns_loop_init/deinit` 惯例）。
2. **平台隔离** —— OS 细分只出现在 `platform/{linux,macos,windows}/port.c`；`include/nanosig/nanosig_port.h` 仅保留已有的 `NS_PLATFORM_*` 句柄联合体分支（`nanosig_port.h:48-52` 先例）。
3. **零功能重复** —— 阻塞单等待只有 wakeup 一个提供方；事件原语只做"可跨线程 signal 的 waitable"。
4. **显式所有权契约** —— deinit 前必须从 waitset remove，对齐 watcher 既有约定（`include/nanosig/nanosig_port.h:410-411`）。
5. **热路径零分配** —— event signal/drain 不允许分配内存。

### Decision Drivers
1. 未来多事件源挂 waitset 的扩展性（访谈 Round 4 确认动机）。
2. API 概念正交性与领域语义（简洁的 initialize/deinitialize 生命周期）。
3. 与 wakeup 不产生平行重复功能（零容忍重复规则）。

#### 契约正文（三平台统一硬语义，来自 Architect/Critic 评审合入）

1. **布尔语义**：event 是布尔量不是计数器；同轮多次 signal 可合并且合法，调用方不得依赖 signal 次数 == 触发次数。
2. **signal EAGAIN = NS_OK**：macos pipe 满（默认 16KB）时 write 返回 EAGAIN，此时"目标 wait 面已处于 signaled（有未读数据）"，信号目的已达成，三平台统一视为成功返回 NS_OK。非 EAGAIN 的真实写错误才暴露错误码。此契约直接影响 `ns_broker_queue_op` 回滚分支（src/ns_broker.c:230-236 现以 signal 失败回滚入队）——迁移后仅真实错误回滚。
3. **drain reset 时机平台未定义**：linux/macos 读循环到 EAGAIN；windows no-op（auto-reset 在 WFMO 返回时已复位）；契约只保证幂等与"drain 后 wait 不再重复触发"。
4. **signal_handle 同值性非契约**：linux/windows 上与 `waitable.primitive` 同值是实现事实，禁止依赖。
5. **单事件阻塞指引**：在 `ns_platform_event_init` 注释写明"单事件阻塞 = 建 waitset 注册该 waitable 后 wait"；不提供阻塞 wait。

### Viable Options
- **A（选定）：新增 `ns_platform_event_t` waitable-first 原语** / / / Pros: 概念正交、caller-owned、为多事件源铺路、broker 改动小（`:697` 一处桥调用换成 init+add）。Cons: 公共 API +3~4 函数、三平台各新增实现。
- **B：waitset 内建自唤醒通道** / / / Pros: 调用方零额外对象。Cons: 把唤醒域耦合进 waitset 概念，未来事件源不经过它的场景不自然；重构面更大。INVALIDATED: 与"waitable 是 caller 实体"的既定架构不一致。
- **C：仅删桥 + broker 有界超时轮询** / / / Pros: 零新 API。Cons: op 处理延迟最多一个 tick，违背"提交通知即时唤醒"的性能契约；`ns_broker.c` 现有 proxy 语义被弱化。INVALIDATED: 用户 Round 2 明确选择新事件原语。
- **D：保留 get_waitable 桥** / / Pros: 零改动。Cons: 绑架 wakeup、概念不正交。INVALIDATED: 访谈 Round 1 明确方向不对。

## Acceptance Criteria（可测）
- AC-1: `ns_platform_wakeup_get_waitable` 在 `include/`、`src/`、`platform/`、`test/` 中零残留（grep 验证为 0 匹配）。
- AC-2: `nanosig_port.h` 新增 `ns_platform_event_t`（内嵌 `ns_platform_waitable_t waitable` + `ns_waitable_handle_t signal_handle`）与 `ns_platform_event_init/signal/drain/deinit` 声明及中文注释；上列契约正文 5 条落入头文件注释。
- AC-3: `platform/{linux,macos,windows}/port.c` 各自实现四函数；`platform/` 之外无新增 OS 预处理器分支。
- AC-4: `src/ns_broker.c` 迁移：`broker->wakeup_waitable` 由 `broker->event.waitable` 取代；`:697` 桥调用改 init；`:418` dispatch 跳过与 `:537-540` drain 调用点；`:751`、`:755`（ns_broker_global_shutdown 内两处 signal）全部完成迁移。
- AC-5: `test/unit/test_platform_backend.c` 的 `test_wakeup_waitable`（`:119-146`）删除，新增 `test_event_backend`（init→waitset add→跨线程 signal→wait 触发 user_data→多次 signal→单次 drain→不再触发→remove→deinit/destroy），并**强测**"未 remove 就 deinit 返回 NS_E_BUSY"；`test_platform_contract_compile.c:32` 同步。
- AC-6: broker 集成测试（broker 相关 integration 套件）行为等价通过；复核 `NS_BROKER_COMPLETION_CAPACITY`（src/ns_broker.c:43）=16 ≤ waitset wait max_completions 容量 64，且 register 总数 ≤ 15（waitset 容量 63 减 event 自身）。
- AC-7: `cmake --build --preset linux-release` + `ctest --preset linux-release` + sanitize 目标全绿（本机），`windows-release` 链路在 CI/可用环境验证；`api-compile-checks` 通过。

## Implementation Steps

### Step 1 — API 声明（include/nanosig/nanosig_port.h）
1. 删除 `ns_platform_wakeup_get_waitable` 声明与注释（`:310-320`）。
2. 在 waitset 段落新增：
```c
typedef struct ns_platform_event {
    ns_platform_waitable_t waitable;     /** 注册进 waitset（events/user_data 由调用方设置） */
    ns_waitable_handle_t   signal_handle; /** signal/drain 的操作句柄（linux=同 fd, macos=pipe 写端 fd, win=event HANDLE） */
} ns_platform_event_t;

int ns_platform_event_init(ns_platform_event_t *event, const char *debug_name);
int ns_platform_event_signal(ns_platform_event_t *event);   /* 跨线程安全，不允许分配内存 */
int ns_platform_event_drain(ns_platform_event_t *event);    /* 非阻塞消费，waitset level-triggered 防 busy-loop */
int ns_platform_event_deinit(ns_platform_event_t *event);   /* 前提：已从 waitset remove */
```
Windows 上 `waitable->primitive.handle` 与 `signal_handle.handle` 为同一 HANDLE；设计上保留两字段为了让 drain/signal 语义不依赖"primitive 即操作句柄"的隐含假设（macos 两者必不同）。

### Step 2 — 平台后端（platform/*/port.c）
- linux: init= eventfd(EFD_CLOEXEC|EFD_NONBLOCK)，`waitable.primitive.fd = signal_handle.fd = efd`；signal= write(efd,1)；drain= read 循环（复用/参考 `ns_linux_wakeup_drain` `platform/linux/port.c:43` 的 EAGAIN/EINTR 处理，但操作 event 而非 wakeup——独立实现避免签名式重复）；deinit= close。
- macos: init= pipe(fds)，先 `ns_waitable_init(&event->waitable)`（哨兵）再覆盖 fd；`waitable.primitive.fd = r`、`signal_handle.fd = w`；两个 fd 都走 `ns_macos_set_cloexec`（platform/macos/port.c:41 先例）；signal= write(w)，EAGAIN→NS_OK（契约 2），非 EAGAIN 错误上抛；drain= read(r) 循环（EAGAIN/EINTR 语义）；deinit= 判 `fd >= 0` 后 close 读写两个 fd（防连调）。比 wakeup 的 EVFILT_USER kqueue 轻（访谈确认选型），代价是 pipe 有 16KB 缓冲上限（见 Risks）。
- windows: init= CreateEventA(auto-reset)，两 handle 同值；signal= SetEvent；drain= no-op（auto-reset 在 wait 返回时已复位，win 后端直接 return NS_OK）；deinit= CloseHandle。
- 三后端 `ns_platform_wakeup_get_waitable`（linux `:186`-194、macos `:215`-223、windows `:151`-159）删除。

### Step 3 — broker 迁移（src/ns_broker.c）
1. struct ns_event_broker（`:66-78`）：删除 `wakeup`/`wakeup_waitable` 两字段，换成 `ns_platform_event_t event;`（proxy 字段保留）。
2. init（`:683-705`）：`ns_platform_event_init(&broker->event, "nanosig-broker")` → 设 `events=NS_WAITABLE_EVENT_IN`、`user_data=NULL` → `waitset_add(&broker->event.waitable)`；失败路径 `out_wakeup_waitable` 改名 `out_event`。
3. op 提交唤醒（`:209-210`、`:233`）：`ns_platform_event_signal(&broker->event)` 替代 `wakeup_signal(broker->wakeup)`；同步复核 `ns_broker_queue_op` 回滚分支（`:230-236`）——新契约下 EAGAIN 归为成功，仅真实写错误才回滚入队。
4. dispatch 跳过（`:418`）与 completion 识别+drain（`:537-540`，原为 `wakeup_wait(0)` 语义）：`w == &broker->event.waitable` → `ns_platform_event_drain(&broker->event)`。
5. **shutdown 路径（CRIT-1）**：`ns_broker_global_shutdown` 内 `:751`、`:755` 两处 `ns_platform_wakeup_signal(broker->wakeup)` 同样迁移为 `ns_platform_event_signal(&broker->event)`。
6. destroy（`:727-733`、`:766-772`）：先 `waitset_remove(&broker->event.waitable)` 再 `ns_platform_event_deinit`；broker 自身不再持 wakeup（proxy per-request wakeup `req->wakeup` 保持不变）。

### Step 4 — 测试
- `test/unit/test_platform_backend.c`：删 `test_wakeup_waitable`（`:119-146`）及注册点（`:885`）；新增 `test_event_backend`（同文件风格 EXPECT_OK/expect_true）：init → waitset create → add → 子线程或主线程 signal → wait 触发 user_data 校验 → 多次 signal → 单次 drain → wait 不再触发（电平触发防 busy-loop）→ remove → deinit/destroy；**强测**"未 remove 就 deinit 返回 NS_E_BUSY"、以及"signal 后 drain 幂等"。
- `test/unit/test_platform_contract_compile.c:31-40`：改用新原语做编译契约检查。
- 集成/回归：broker 套件 + demo（demo_timer_cross_thread）不需改代码，跑通即可。

### Step 5 — 文档与收尾
- `docs/BROKER_DESIGN.md:37/154-155/213+`、`docs/ARCHITECTURE.md:129` 中 wakeup_waitable 描述改为 ns_platform_event；wakeup 段落注明"仅单等待"。
- `include/nanosig/nanosig_port.h` 模块头注释（`:6`"waitable 事件描述符"段落）核对表述一致性。
- 运行验证命令见 AC-7。

## Risks and Mitigations
| 风险 | 缓解 |
|------|------|
| macos pipe drain 短读/部分读导致 busy-loop 或漏唤醒 | drain 用 EAGAIN/EINTR 循环语义（复用 wakeup_drain 的错误处理模式）；专门单测 signal 多次→单次 drain→wait 不再触发 |
| Windows auto-reset 事件在 WaitForMultipleObjects 返回即复位，drain 属 no-op —— 若 broker drain 统一调用会"空操作"不一致 | 契约写明"drain 幂等、after-wait 复位随平台"；Drain 在 win 后端直接 return NS_OK |
| **Windows waitset completion 容量溢出丢信号（既有缺陷，CRIT-3 合入）**：max_completions 耗尽后 WFSO(0) 扫描停止，剩余 signaled auto-reset 事件被消费且无 completion 记录（platform/windows/port.c:447 附近）；迁移后 drain= no-op 使其不可自恢复 | AC-6 复核容量关系（broker 16 ≤ 64）；test_event_backend 覆盖多事件并发触发场景；登记为 waitset 后端独立缺陷（非本轮修复，另开 issue/review 条目） |
| 用户未 remove 就 deinit → waitset 内悬挂指针（UAF） | **NS_E_BUSY 防御（R18-2）**：deinit 检查 `event->waitable.registered_waitset != NULL` → 返回 NS_E_BUSY（状态码 include/nanosig/nanosig_status.h:33，字段先例 nanosig_port.h:81）；不采用"先探测 remove"的 TOCTOU 式缓解 |
| 事件原语与 wakeup 在 linux/windows 底层同类 OS 对象 | 边界已划清（单等待归 wakeup）；port.h 注释相互引用避免语义漂移；不抽公共内部 helper（签名不同、对象不同） |
| happens-before 保证变化 | eventfd/pipe 的 write-read 与 wakeup 现有机制同类（`src/ns_broker.c:13` 注释沿用；必要处补 MemoryOrder 文档一行） |

## Verification Steps
1. `grep -rn "get_waitable" include src platform test demos` → 0 匹配。
2. `grep -rnE "wakeup_waitable|broker->wakeup" src/` → 0 匹配（proxy 的 `req->wakeup` 除外）。
3. `cmake --build --preset linux-release && ctest --preset linux-release`。
4. `cmake --build <build> --target api-compile-checks` 和 sanitize 目标。
5. （可用环境）`cmake --build --preset windows-release` 全链。

## ADR

- **Decision**: 删除 `ns_platform_wakeup_get_waitable`，新增 caller-owned waitable-first 原语 `ns_platform_event_t`（init/signal/drain/deinit），broker 自唤醒迁移到该原语；wakeup 保留为唯一阻塞单等待原语。
- **Drivers**: 见 Decision Drivers（多事件源扩展性 > 概念正交 > 无重复）。
- **Alternatives considered**: waitset 内建唤醒（B）、删桥+轮询（C）、维持现状桥（D）——
 驳回理由见 Options。
- **Why chosen**: 概念正交且不绑架 wakeup；存储模型对齐项目"调用方拥有存储"哲学；三平台实现面小（各 <30 行）；为 timer/用户事件等未来 waitset 消费者提供一等公民入口。
- **Consequences**: 公共 API 面净变化 +4 函数 +1 类型 −1 函数；broker 字段重排；测试改型；文档同步。macos wakeup（kqueue）不变，留待后续优化（非本轮）。`ns_platform_wakeup_t` 维持 opaque + 平台分配，与新原语的 caller-owned 模型并存，属已知技术债，登记为后续议题而非本轮扩大范围。
- **Follow-ups**: ① 未来 timer 服务/用户事件源接入 waitset 时直接复用 `ns_platform_event_t`；② macos wakeup 迁移到更轻通道的评估（独立议题）；③ `docs/MPSC_MEMORY_ORDER.md` 补 event happens-before 说明一行；④ 评估 `ns_platform_event_wait`（单事件阻塞）是否真有需求——本轮回执为暂缓，需求出现时再以独立函数落地；⑤ 评估将 `ns_platform_wakeup_t` 改为 caller-owned 以消除双存储模型；⑥ Windows waitset completion 容量溢出丢信号（既有缺陷）另开 review/issue。

## Consensus Log
- Architect review: **REVISE** → R18-1~R18-6 已全部合入
- Critic review: **REVISE（轻量）** → CRIT-1~CRIT-3 已合入；计划引用漂移与乱码已更正
- 修订后状态: **APPROVED**（二轮评审意见全部合入，无原则级违反）
- 计划状态: **已实施**（当前工作树，待提交）
