# 提案：ns_watcher 回调化

- 状态: **提案**（待作者评审；未批准实施，代码未改动）
- 提出日期: 2026-10-16
- 来源: 与作者的设计脑暴
- 影响面: 公共 API（`include/nanosig/nanosig_broker.h`）、`src/ns_broker.c`、broker 相关测试、设计文档、review 契约条目
- 基线: `fix/zephyr-review-002-007`（PR #9 相关分支）

---

## 1. 动机

当前 `ns_watcher_t` 内嵌一个 `ns_signal_t`，broker 线程在消费 fd 之后会 emit 这个内嵌 signal，
把事件扇出到用户选择的 loop 线程。作者的观察是：

> watcher 触发信号没有必要，只触发消费回调就行；如果用户想要信号，自己做一个信号在消费函数里发送就行。

如果成立，watcher 会退化成一个单职责原语（"就绪 → 调一个回调"），broker 模块可以完全摆脱 signal，
被删掉的正是实现里最别扭的一部分：内嵌 signal 的 init/deinit、每 watcher 一个 mutex、
自动 payload、`pending_consume_handle` 侧信道、以及"返回值 > 0 才 emit"的隐式门控。

本提案把该想法整理成可评审的方案，**不包含代码改动**。

---

## 2. 现状

### 2.1 公共结构

`include/nanosig/nanosig_broker.h:69-80`：

```c
struct ns_watcher {
    ns_signal_t            signal;                  /* 首字段，事件触发时 emit，payload = ns_watcher_event_t */
    ns_platform_waitable_t waitable;                /* 注册到 broker waitset */
    ns_list_node_t         broker_node;             /* broker 内部链表节点 */
    ns_watcher_consume_fn  consume_fn;              /* 可选；NULL 时跳过消费直接 emit */
    void                  *pending_consume_handle;  /* consume_fn 写入、emit 时读出的侧信道 */
};
```

配套还有 `ns_watcher_event_t{triggered_events, consume_handle}`（`nanosig_broker.h:39-42`）、
`ns_watcher_set_consume_handle`（`nanosig_broker.h:177`）与首字段 signal 的静态断言
（`nanosig_broker.h:82`）。

### 2.2 运行链路

`src/ns_broker.c:388-402` 在 broker 线程上：

```
waitset 就绪
  → 若 consume_fn != NULL：调 consume_fn(watcher)
      返回 <= 0 → 直接结束（不 emit）
  → 构造 ns_watcher_event_t{triggered_events, pending_consume_handle}
  → ns_signal_emit_raw(&watcher->signal, &event, sizeof(event))
      → 遍历 slot_list → 每个 connection 的 target_loop MPSC try_push + wakeup
  → 用户在 loop 线程的 slot 内执行
```

即内嵌 signal 承担的是**第二次投递**：把"broker 线程完成消费"扇出到用户选定的 loop 线程。

### 2.3 现有硬约束

`include/nanosig/nanosig_broker.h:52-55` 与 `src/ns_broker.c:18-23` 明令：
**consume_fn 不得调用任何 nanosig API**。

该约束混了两类调用：

- **proxy-op 类**（`ns_broker_add` / `ns_broker_remove` / `ns_shutdown` 等）：入队后阻塞等待
  broker 线程处理，而 broker 此刻正停在 dispatch 内、未回到主循环 → **自死锁**。这是禁令真正的理由。
- **`ns_signal_emit_raw`**：`src/nanosig.c:403-446` 文档明确"本函数是非阻塞的"，只取 signal 自身
  mutex 并向目标 loop 的 MPSC ring `try_pushv` + `wakeup_signal`，**不触碰任何 broker 锁**；
  dispatch 阶段也未持有 `watcher_mutex`。因此从 broker 线程 emit 是安全的。

结论：现有"一刀切"禁令不是技术约束，而是"为简单起见"的保守选择；把它换成白名单是可行的。

---

## 3. 提案（方案 B：consume_fn-only）

### 3.1 目标 API

```c
typedef void (*ns_watcher_consume_fn)(ns_watcher_t *watcher, uint32_t triggered_events);

int ns_watcher_init(ns_watcher_t *watcher, ns_waitable_handle_t handle,
                    uint32_t events, int edge_triggered,
                    ns_watcher_consume_fn consume_fn);   /* consume_fn == NULL → NS_E_INVAL */

int ns_watcher_deinit(ns_watcher_t *watcher);

ns_waitable_handle_t ns_watcher_handle(const ns_watcher_t *watcher);
```

### 3.2 数据结构

```c
struct ns_watcher {
    ns_platform_waitable_t waitable;      /* 注册到 broker waitset */
    ns_list_node_t         broker_node;   /* broker 内部链表节点 */
    ns_watcher_consume_fn  consume_fn;    /* 必填；同时作为"已初始化"marker */
};
```

删除项：

- `ns_watcher_t.signal`（内嵌 signal）与首字段 signal 的 `NS_STATIC_ASSERT`
- `ns_watcher_event_t{triggered_events, consume_handle}`
- `ns_watcher_t.pending_consume_handle` 与 `ns_watcher_set_consume_handle`
- `nanosig_broker.h` 对 `nanosig_signal.h` 的依赖

### 3.3 运行时

`ns_broker_dispatch_watcher` 退化为：

```c
watcher->consume_fn(watcher, triggered_events);
```

`src/ns_broker.c` 不再调用 `ns_signal_emit_raw`，broker 模块变为 signal-free；
`ns_watcher_init` / `ns_watcher_deinit` 不再 `ns_signal_init_raw` / `ns_signal_deinit_raw`
（`src/ns_broker.c:145`、`src/ns_broker.c:181`），每个 watcher 少一次 mutex 分配与释放。

### 3.4 consume_fn 契约（以文档为唯一约束）

回调在 **broker 线程**执行，必须非阻塞（一个慢回调会拖死全部 watcher）。

**白名单**（broker 线程可调、非阻塞）：

- `ns_signal_emit*`（`src/nanosig.c:403-446`，非阻塞）

**黑名单**（会等待 broker 线程 → 自死锁）：

- `ns_broker_add` / `ns_broker_remove`
- `ns_shutdown`
- `ns_watcher_deinit` / `ns_timer_deinit` / `ns_loop_deinit` 等任何 teardown

**灰区**（需在实现前逐一论证，暂不入白名单）：

- `ns_timer_start` / `ns_timer_cancel` / `ns_timer_restart`：notify 路径非阻塞，但会持有 timer mgr 锁；
  建议单独论证后再决定。

另外必须写入头文件 `@warning`：

- 回调在 broker 线程执行、必须非阻塞；
- 电平触发的 waitable 必须在回调内读净数据，否则每轮空转重复触发。

---

## 4. 作者已确认的取舍（2026-10-16）

1. **consume_fn 的 API 限制用文档约束**，不引入运行时守卫。
2. **接受与 timer 的不对称**：`ns_timer_t` 继续内嵌 signal 并由 timer_mgr 自动 emit；
   只有 watcher 改为回调式。本提案不改 timer。
3. user 若需要"回 loop 线程"，自行创建 signal + loop + connect，并在 consume_fn 内 emit。

---

## 5. 待定子决策（含倾向）

| # | 子决策 | 倾向 | 说明 |
|---|--------|------|------|
| D1 | consume_fn 返回值 | `void` | 无 emit 需要门控，返回 int 会暗示 broker 会处理 |
| D2 | consume_fn 是否必填 | 必填（NULL → `NS_E_INVAL`） | B 下 NULL 的 watcher 无意义；且 `consume_fn != NULL` 可作为 initialized marker，无需新增 magic/state 字段 |
| D3 | 用户上下文携带方式 | `NS_CONTAINER_OF` 嵌入 | 与仓库 caller-owned 惯例一致；如需更省事可加 `void *user_data` 字段（二选一） |
| D4 | 命名 | 改为"处理器"语义 | `consume_fn` 现已是就绪处理器，可改名（如 `ns_watcher_handler_fn` / `handler`）；反正是破坏性变更 |

D2 的 marker 取向与 PORT-017 / P9 的既有决定（"文档保护、不加 state 字段"）一致，故倾向不加新字段。

---

## 6. 被否决 / 备选方案

| | watcher 内有什么 | broker 线程跑什么 | 用户要回 loop 线程 | 库复杂度 | 破坏面 |
|---|---|---|---|---|---|
| A. 现状 | 内嵌 signal + consume_fn | consume_fn | 直接 connect 内嵌 signal | 中 | — |
| **B. 本提案** | 只有 consume_fn | consume_fn（含业务逻辑） | 自建 signal/loop，在回调里 emit | 低 | 大 |
| C. 可选 sink 指针 | consume_fn + `ns_signal_t *sink`（用户自持，可 NULL） | sink 非空 → broker emit；为空 → 纯回调 | connect 自己的 signal 到 sink | 低 | 中 |
| D. 最小改进 | 同 A，但 `triggered_events` 作为参数传给 consume_fn | 同 A | 同 A | 中 | 小 |
| E. 反转 I/O | waitable + signal（删 consume_fn） | 只 emit，不消费 | 直接 connect | 低 | 大（read 移到 loop 线程，阻塞 read 会卡 loop） |

**为何选 B 而非 C**：B 让 broker 完全摆脱 signal、模块单向依赖最干净，且删掉的都是最别扭的机制；
C 保留 emit 路径与 payload，只搬移 signal 存储，收益较小。若后续确认"fd 就绪 → 直接投递到 loop"
是主用法（不愿让用户手写 signal+loop+connect），可回退到 C。

---

## 7. 迁移清单

- 代码
  - `include/nanosig/nanosig_broker.h`：结构体、类型、docstring、`@warning`
  - `include/nanosig/internal/ns_broker.h`：内部声明
  - `src/ns_broker.c`：init / deinit / is_initialized / dispatch 及其注释
- 测试
  - `test/unit/test_broker_lifecycle.c`、`test_broker_event.c`、`test_broker_multi.c`、
    `test_broker_concurrent_remove.c`、`test_broker_consume_fn.c`、`test_broker_contract_compile.c`
  - `test/integration/scenario_*.c`、`test/integration/test_hive.c`
  - `test/zephyr/src/main.c`、`test/freertos/test_freertos_broker.c`
- 文档
  - `docs/BROKER_DESIGN.md`（结构体 / 主循环 / API / 依赖关系）
  - `docs/ARCHITECTURE.md:109-150`
  - `docs/plans/共识计划.md` 的 broker 段
- demo
  - `demos/demo_timer_cross_thread.c` 走 timer 路径，**不受影响**
- review 文档（契约实质变更，需加注而非重开）
  - `docs/review/broker-code-review.md`：BROKER-025 及头文件 `@warning` 中
    "consume_fn 禁止调用任何 nanosig API"的合同在本提案下被有意修订，需在对应条目加注说明。

预计代码为净减行数；主要工作量在测试改造与文档同步。

---

## 8. 风险与护栏

| 风险 | 后果 | 护栏 |
|------|------|------|
| 白名单被误用（回调内调 `ns_broker_add/remove`） | 自死锁、静默挂死，极难排查 | 头文件 `@warning` + 文件头 `@section` + consume_fn 示例旁直书禁令 |
| 用户把重活留在回调 | 单 broker 线程吞吐被拖垮 | 文档明确"回调只做读 + emit，重活放 slot" |
| 电平触发未读净 | 每轮空转重复触发 | 文档 + 契约测试覆盖 drain |
| 破坏性 API 变更 | 下游编译失败 | 特性分支 + PR + 10 项 CI + nightly，rebase 合并 |

---

## 9. 验证策略

沿用仓库门禁：

- 快速 CI：`ci.yml` 10 个 job（linux asan/ubsan/release、macos asan/ubsan/release、windows、
  freertos ubuntu/macos、zephyr）全绿
- 手动 nightly：`gh workflow run nightly.yml --ref <branch>`（TSAN）
- 合并：`gh pr merge --rebase`，保持 master 线性

新增/调整的验收点（草案）：

- AC-1：`ns_watcher_init` 对 `consume_fn == NULL` 返回 `NS_E_INVAL`
- AC-2：就绪时回调被调用一次，且 `triggered_events` 与 waitset completion 一致
- AC-3：回调内 `ns_signal_emit` 可将事件投递到用户 loop 的 slot（跨线程）
- AC-4：`ns_watcher_deinit` 在未初始化时返回 `NS_E_INVAL`
- AC-5：`nanosig_broker.h` 不再依赖 `nanosig_signal.h`
- AC-6：电平触发 + 回调内读净 → 不再重复触发（覆盖既有 consume_fn 语义）

---

## 10. 开放问题

1. D1–D4 待定稿（见 §5）。
2. broker 线程上可调的 API 白名单最终范围，尤其 timer 三类是否入选（§3.4 灰区）。
3. 是否需要提供"fd → loop"的一段式便利（若需要即回退方案 C）。
4. review 文档 BROKER-025 相关合同的加注措辞。

---

## 11. 变更记录

- 2026-10-16：初稿（提案），整理自作者脑暴，未含代码改动。
- 2026-10-16：从 `docs/plans/` 移入 `docs/proposals/`，标题由"子计划"改为"提案"。
