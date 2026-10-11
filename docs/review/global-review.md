# 全局视角 Review（跨模块问题）

**日期**: 2026-07-05
**审查者**: claude-code-review

---

## 修复历史

### 2026-09-21 — sanitize-all 真正化（CMake 多阶段审计入口）

- **GLOBAL-002**: `sanitize-all` 自 P0 起为 echo 占位目标（仅打印
  "no sanitizer/audit targets yet"），从未执行实际审计。CI 在所有 preset
  都显式调用该 target，但实际只是 echo 通过，造成"审计通过"的假象。

  真正化后由 cmake/SanitizeAll.cmake 驱动，按当前 preset 启用的 sanitizer
  创建 `nanosig_audit_asan` / `nanosig_audit_ubsan` / `nanosig_audit_tsan`
  子目标，每个子目标按 A1 api-contract / A2 config / A3 header tags /
  A4 encoding 顺序执行四阶段审计（脚本位于 cmake/scripts/audit_*.{sh,cmake,py}）。
  顶层 `sanitize-all` target 聚合已启用的子目标。CTest 由 CI 单独调用，
  `sanitize-all` 不再覆盖 quick tests。Release preset 下 sanitize-all
  降级为说明性输出。

  副产品：A1 api-compile-check 暴露 rbtree cmp 字段 const-correctness 缺陷，
  已作为 RBTREE-012 修复（详见 rbtree-code-review.md）。
  A3 header audit 暴露 nanosig_rbtree.h 缺 `@thread-safety` 标注，
  已作为 RBTREE-013 打开（详见 rbtree-code-review.md）。

(无其它历史)

---

## 现在打开的问题

### GLOBAL-001: `ns_rbtree_insert` 头文件文档与实现不一致，影响 timer 模块正确性假设

- **状态**: 关闭-已拒绝
- **严重度**: 🟡 中
- **类型**: doc

#### 问题描述
`include/nanosig/nanosig_rbtree.h:108-114` 文档声称："如果插入后该节点成为最左节点，返回值即为该节点；否则返回 NULL"。

实际实现（`src/ds/ns_rbtree.c:342-374`）在成功时**始终返回被插入的节点**（第 374 行 `return node;`），仅在输入无效（NULL tree/node 或已链接节点）时返回 NULL。

`ns_timer_start_locked`（`src/ns_timer.c:125`）的 `insert == NULL` 检查依赖的是**实现语义**（NULL = 错误），而非**文档语义**（NULL = 非最左）。当前代码正确，但如果 rbtree 实现被修改为匹配文档，`start_locked` 会对所有非最紧急的插入返回 `NS_E_CORRUPT`。

此问题同时属于 rbtree 模块（RBTREE-009）和 timer 模块的文档依赖，记入全局 review。

#### review 建议
统一 rbtree 文档与实现语义。推荐将文档改为"成功插入返回该节点；输入无效或节点已链接时返回 NULL"，与实现一致。参见 RBTREE-009。

#### 作者建议
与 RBTREE-009 同源误读。`ns_rbtree_add` 返回值实际为 `leftmost ? node : NULL`（`src/ds/ns_rbtree.c:628`），与文档完全一致。Timer 调用的是 `ns_rbtree_add`，其 leftmost 语义正确，不存在"文档-代码不一致"问题。
→ 关闭-已拒绝

#### 关闭原因
与 RBTREE-009 相同——reviewer 误读代码。`ns_rbtree_add` 实现与文档一致，timer 依赖正确。

- 关闭日期: 2026-07-18
- 状态: 关闭-已拒绝

#### 定位
`include/nanosig/nanosig_rbtree.h:106-114`（文档），`src/ds/ns_rbtree.c:374`（实现），`src/ns_timer.c:125`（依赖方）

---

### GLOBAL-003: `共识计划.md` 仍引用已删除的 `ns_platform_wakeup_get_waitable`

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低
- **类型**: doc
- **关闭原因**: 已更新 docs/plans/共识计划.md:122 的当前实现快照引用为 `ns_platform_event_*`；历史段落（:613/:641/:988）保留不动。
- **关闭日期**: 2026-09-21

#### 问题描述

本次 event 重构移除了平台接口 `ns_platform_wakeup_get_waitable`（由
`ns_platform_event_*` 取代），但 `docs/plans/共识计划.md:122` 在"当前实现快照"
段仍将其列为"平台层新增"接口。该文档被 AGENTS.md 指定为需与当前实现决策保持
同步。其余引用（:613、:641、:988）位于 P5b 设计段与 Iter 记录，属历史快照，
可不改。

#### review 建议

更新 `docs/plans/共识计划.md:122`：把 `ns_platform_wakeup_get_waitable` 替换为
`ns_platform_event_*`（或注明该接口已被 event 原语取代）。历史段落
（:613、:641、:988）保留不动。

#### 作者建议

（2026-09-21 作者决定：直接修文档。）仅更新 `共识计划.md:122` 的当前实现快照引用为 `ns_platform_event_*`，历史段落（:613/:641/:988）保留不动。

#### 可重现的失败场景

`grep -rn "ns_platform_wakeup_get_waitable" docs/plans/共识计划.md`
→ 4 命中，其中 :122 与当前平台接口面不一致。

#### 定位
docs/plans/共识计划.md:122

---

### GLOBAL-004: 空 waitset + 无限超时的行为在五个后端间分裂（3 永久阻塞 / 2 报错）

- **状态**: 打开
- **严重度**: 🟡 中
- **类型**: design（跨模块契约缺口）

#### 问题描述

`ns_platform_waitset_wait` 的公开契约只写 `NS_PLATFORM_WAIT_INFINITE_US` = "无限等待"
（include/nanosig/nanosig_port.h:604、platform/README.md:161），**未定义 waitset 为空
（无任何已注册 waitable）时的行为**。五个后端实际分裂：

| 后端 | 空 waitset + INFINITE |
|------|----------------------|
| Linux | 永久阻塞（timerfd 常驻 epoll 但未 arm，`epoll_wait(-1)` 不返回） |
| macOS | 永久阻塞（无限时 `timeout_ptr` 保持 NULL） |
| FreeRTOS | 永久阻塞（`portMAX_DELAY`） |
| Windows | `WaitForMultipleObjects(0,…)` → `WAIT_FAILED`(ERROR_INVALID_PARAMETER) → `NS_E_INVAL` |
| Zephyr | `NS_E_INVAL` |

空 waitset 上不存在任何可唤醒的事件源（Zephyr 的 `k_poll` 用活跃 slot 快照构造
event 数组，阻塞期间另一线程 `waitset_add` 的成员对该次 wait 不可见），因此
"永久阻塞"等价于**静默挂死**：线程永远拿不回控制权，没有错误码也没有日志，在
RTOS 上极难与优先级反转 / 丢中断区分。该缺口由 Zephyr 后端 review 发现
（详见 zephyr-code-review.md 的 ZEPHYR-001），但根因是跨模块契约问题。

**可达性**：公开 API 层完全可达（`create` 后不 `add` 就 `wait`，或 add 后再 remove）；
nanosig 自身不可达——broker 在起线程前就把 `broker->event.waitable` 注册进 waitset
（src/ns_broker.c:695-701），直到 join 之后才摘除（src/ns_broker.c:772）。因此本改动
只影响直接调用平台 API 的调用方，不改变库自身热路径。

#### review 建议

把语义定死并在契约层统一五个后端。三个候选：① 返回专用错误码（如 `NS_E_EMPTY`）；
② 保持永久阻塞（POSIX poll/epoll/kevent 语义）；③ **封顶为最大等待值**后按普通超时
返回。作者选定 ③。

#### 作者建议

（2026-10-11 作者决定）空 waitset + 无限超时统一**封顶为最大等待值**为 **1 秒**，
到期返回 `NS_OK`（`out_count == 0`）；封顶值用公开常量 `NS_PLATFORM_WAIT_EMPTY_MAX_US`
（默认 1000000 µs，可编译期覆盖）。**非空 waitset 仍是真无限等待**（broker 无定时器时
的稳态依赖这一点）；封顶只是等待时间的**上界**，等待期间另一线程注册的 waitable 就绪时
后端可能提前返回该 completion（Windows/Zephyr 用调用时快照，看不到后续注册）。

封顶值由 10 秒调整为 **1 秒**（2026-10-11，作者要求让 CI 跑更快）：该值只影响"误用空
waitset 无限等待"这一路径，1 秒仍是有效防御；10 秒会让 host / FreeRTOS / Zephyr 三处
契约测试每次都实等近 10 秒，1 秒在同样验证契约的前提下缩短测试与 CI 时长。需要更长
封顶可在编译期 `-DNS_PLATFORM_WAIT_EMPTY_MAX_US=<n>` 覆盖。

按此决定已实现（工作区，未提交）：include/nanosig/nanosig_port.h、五个
platform/*/port.c 的 `waitset_wait`、platform/README.md，以及 host / Zephyr / FreeRTOS
三处契约测试。

#### 可重现的失败场景

```c
ns_platform_waitset_t *ws = NULL;
ns_platform_waitset_completion_t c[4];
size_t n = 0u;

ns_platform_waitset_create(&ws);            /* 不 add 任何 waitable */
ns_platform_waitset_wait(ws, NS_PLATFORM_WAIT_INFINITE_US, c, 4u, &n);
```

修复前：Linux / macOS / FreeRTOS 不返回（挂死）；Windows / Zephyr 返回 `NS_E_INVAL`。
修复后（统一）：等待时间被封顶到 `NS_PLATFORM_WAIT_EMPTY_MAX_US`（上界，非固定睡眠），
返回 `NS_OK`；通常 `n == 0`，若等待期间另一线程注册的 waitable 就绪则可能提前返回该
completion（Windows/Zephyr 用调用时快照，看不到后续注册）。

#### 定位

include/nanosig/nanosig_port.h:245
platform/linux/port.c:531
platform/macos/port.c:603
platform/windows/port.c:432
platform/freertos/port.c:628
platform/zephyr/port.c:493

---

### GLOBAL-005: Windows 后端缺 `max_completions == 0` 早返回，容量为 0 时仍会真正等待

- **状态**: 打开
- **严重度**: 🟢 较低
- **类型**: design（跨后端行为不一致 + 契约缺口）

#### 问题描述

四个后端（Linux / macOS / FreeRTOS / Zephyr）在 `ns_platform_waitset_wait` 入口都有
`if(max_completions == 0u) return NS_OK;` 早返回；**Windows 没有**——它的入口只判
`waitset->count == 0u && timeout_us == 0u`（platform/windows/port.c:450）。于是同一个
退化输入在各后端行为不同：

| 输入 | Linux / macOS / FreeRTOS / Zephyr | Windows |
|------|----------------------------------|---------|
| `max_completions == 0` + 非空 waitset + 有限超时 | 立即 `NS_OK`、`out_count == 0` | 修复前：真等满 timeout 后 `NS_OK`；修复后：立即 `NS_OK` |
| `max_completions == 0` + 空 waitset + 无限超时 | 立即 `NS_OK` | 修复前：`WaitForMultipleObjects(0,…)` → 立即 `NS_E_INVAL`；修复后：立即 `NS_OK` |

`max_completions == 0` 时调用方拿不到任何 completion（写入被
`*out_count < max_completions` 挡住），所以 Windows 的等待纯属浪费，且与其余四后端
不一致。`ns_platform_waitset_wait` 的 docstring 只写了 `max_completions` 是"输出数组
容量"，**没有规定 0 的语义**，因此这是契约缺口而非 Windows 单方面的偏差。

本条由 GLOBAL-004 的同一改动顺带暴露：GLOBAL-004 把"空 waitset + 无限超时"从 Windows 的
立即报错改为"等满封顶值返回"，一度使 Windows 在该退化输入上从"立即返回"变成"最多阻塞
1 秒"。按作者建议补上早返回后，Windows 恢复为立即返回，五个后端在该退化输入上完全一致。

#### review 建议

推荐给 Windows 入口补上与其他四后端一致的早返回：

```c
*out_count = 0u;
if(max_completions == 0u) return NS_OK;   /* 与 Linux/macOS/FreeRTOS/Zephyr 对齐 */
if(waitset->count == 0u && timeout_us == 0u) return NS_OK;
```

这样 GLOBAL-004 的封顶语义在退化输入下也完全一致，改动量一行。替代方案是在
docstring 写明"`max_completions == 0` 时立即返回 `NS_OK`、不等待"并让 Windows 遵循
（等价于改代码）；若选择保留 Windows 现状，则必须承认同一输入在后端间不一致，与
"统一空 waitset 语义"的目标相冲突。

#### 作者建议

（2026-10-11 作者决定）**修**：给 Windows 入口补上与其他四后端一致的早返回
`if(max_completions == 0u) return NS_OK;`，并在 `ns_platform_waitset_wait` 的
docstring 明确 `max_completions == 0` 的语义（立即返回 `NS_OK` + `out_count == 0`、
不做等待），把原先的隐含约定升级为文档化契约。测试由 host 契约测试
（`test_waitset_max_completions_zero_no_wait`）覆盖——该二进制在 Linux / macOS /
Windows 三种 host 构建下都运行，正好覆盖被修的 Windows 后端；FreeRTOS / Zephyr
本就合规，未新增用例。

按此决定已实现（工作区，未提交）：platform/windows/port.c、
include/nanosig/nanosig_port.h、test/unit/test_platform_backend.c。提交后本条移入
"修复历史"。

#### 可重现的失败场景

```c
ns_platform_waitset_t *ws = NULL;
ns_platform_waitset_completion_t c[1];
size_t n = 99u;

ns_platform_waitset_create(&ws);
/* 先注册一个（不触发信号的）waitable，使 waitset 非空 */
ns_platform_waitset_add(ws, &ev.waitable);
ns_platform_waitset_wait(ws, 500000u, c, 0u, &n);   /* max_completions == 0 */
```

Linux / macOS / FreeRTOS / Zephyr：立即返回 `NS_OK`、`n == 0`（耗时 < 1 ms）。
修复前 Windows：等满 500 ms 后返回 `NS_OK`、`n == 0`；修复后五个后端都是立即返回。

#### 定位

platform/windows/port.c:449
platform/linux/port.c:546
platform/macos/port.c:620
platform/freertos/port.c:641
platform/zephyr/port.c:509

---

## 现在关闭的问题

(无)
