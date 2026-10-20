---
review_schema: "nanosig-review-v1"
entries:
  - id: "GLOBAL-001"
    module: "global-review"
    status: "closed-rejected"
    severity: "medium"
    type: "doc"
    section: "closed"
    closed_date: "2026-07-18"
    location: "docs/review/global-review.md#GLOBAL-001"
    locations:
      - "include/nanosig/nanosig_rbtree.h:106-114"
  - id: "GLOBAL-003"
    module: "global-review"
    status: "closed-fixed"
    severity: "low"
    type: "doc"
    section: "closed"
    closed_date: "2026-09-21"
    location: "docs/review/global-review.md#GLOBAL-003"
  - id: "GLOBAL-006"
    module: "global-review"
    status: "closed-fixed"
    severity: "low"
    type: "doc"
    section: "closed"
    closed_date: "2026-10-19"
    location: "docs/review/global-review.md#GLOBAL-006"
    locations:
      - "docs/ARCHITECTURE.md:271"
      - "platform/windows/port.c:256"
  - id: "GLOBAL-007"
    module: "global-review"
    status: "closed-fixed"
    severity: "low"
    type: "design"
    section: "closed"
    closed_date: "2026-10-18"
    location: "docs/review/global-review.md#GLOBAL-007"
    locations:
      - "include/nanosig/nanosig_port.h:67"
      - "include/nanosig/nanosig_port.h:70"
      - "platform/windows/nanosig_platform_port.h:22"
  - id: "GLOBAL-004"
    module: "global-review"
    status: "closed-fixed"
    severity: "unknown"
    type: "unknown"
    section: "fixed-history"
    location: "docs/review/global-review.md#GLOBAL-004"
  - id: "GLOBAL-005"
    module: "global-review"
    status: "closed-fixed"
    severity: "unknown"
    type: "unknown"
    section: "fixed-history"
    location: "docs/review/global-review.md#GLOBAL-005"
---

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

<a id="GLOBAL-004"></a>
<a id="GLOBAL-005"></a>
### 2026-10-11 — 空 waitset 无限等待封顶统一 + max_completions 契约（GLOBAL-004 / GLOBAL-005）

- **GLOBAL-004**: `ns_platform_waitset_wait` 在"空 waitset + `NS_PLATFORM_WAIT_INFINITE_US`"
  上五个后端行为分裂（Linux / macOS / FreeRTOS 永久阻塞，Windows / Zephyr 返回
  `NS_E_INVAL`），且公开 docstring 未规定该输入的行为。作者选定"封顶为最大等待值"：
  新增公开常量 `NS_PLATFORM_WAIT_EMPTY_MAX_US`（默认 1 秒，可编译期覆盖），五个后端在
  该输入上统一封顶后返回 `NS_OK` + `out_count == 0`。封顶是等待时间的**上界**而非固定
  睡眠（等待期间另一线程注册的 waitable 就绪时可能提前返回）；waitable 非空时仍是真
  无限等待（broker 稳态依赖这一点），有限超时不受影响。docstring 与 platform/README.md
  同步补充例外说明；host / FreeRTOS / Zephyr 三处契约测试覆盖。
- **GLOBAL-005**: Windows `waitset_wait` 缺 `max_completions == 0` 早返回，容量为 0 时
  仍会真正等待（有限超时等满 timeout；空 waitset + 无限超时则等满封顶值），与其余四
  后端不一致，且 docstring 未规定 0 的语义。修复为入口补一行早返回，五后端统一立即
  返回 `NS_OK` + `out_count == 0`；docstring 明确该语义，host 契约测试
  `test_waitset_max_completions_zero_no_wait` 覆盖。

- **修复 commit**: `6cf940d`（PR #7，rebase 合并，无 merge 节点）
- **修复日期**: 2026-10-11

(无其它历史)

---

## 现在打开的问题

## 现在关闭的问题

(无)

<a id="GLOBAL-001"></a>
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

- **关闭日期**: 2026-07-18
- **状态**: 关闭-已拒绝

#### 定位
include/nanosig/nanosig_rbtree.h:106-114

<a id="GLOBAL-003"></a>
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

---

<a id="GLOBAL-006"></a>
### GLOBAL-006: `docs/ARCHITECTURE.md` 平台表格仍写 `CreateThread`（实现是 `_beginthreadex`）

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低
- **类型**: doc
- **来源**: PORT-021（platform 模块）；同一措辞跨 `docs/` 与 `platform/README.md`，
- **关闭原因**: 已在 `docs/ARCHITECTURE.md:270` 改为 `_beginthreadex / WaitForSingleObject`，与实现和 PLATFORM-013 修复结论一致。
- **关闭日期**: 2026-10-19
  按 §4.2 记入全局文档。

#### 问题描述
`docs/ARCHITECTURE.md:271` 的能力表把 Windows 线程写成 `CreateThread / WaitForSingleObject`，
而 `platform/windows/port.c:256` 自 PLATFORM-013 起使用 `_beginthreadex`。
`platform/README.md:102` 与 `docs/agents/platform/AGENTS.md:67` 有同样措辞（那两处
已记入 PORT-021，本条只跟踪跨模块的 `docs/ARCHITECTURE.md`）。

#### review 建议
把表格单元格改为 `_beginthreadex / WaitForSingleObject`，与实现和 PLATFORM-013
的修复结论一致。

#### 作者建议
（待作者补充）

#### 可重现的失败场景
按表格假设 `CreateThread` 语义（如句柄可继承、`GetExitCodeThread` 行为、
CRT 初始化差异）编写集成代码 → 与实际 `_beginthreadex` 持有的运行时状态不符。

#### 定位
docs/ARCHITECTURE.md:271
platform/windows/port.c:256

---

<a id="GLOBAL-007"></a>
### GLOBAL-007: `nanosig_port.h` 在 `extern "C"` 块内 include 平台 SDK 头

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低
- **类型**: design
- **置信度**: 低（本机无 MSVC/clang-cl 环境；Windows CI 走 MinGW，覆盖不到）

#### 问题描述
`include/nanosig/nanosig_port.h:35-37` 打开 `extern "C"`，`:68` 才
`#include NANOSIG_PLATFORM_HEADER`。macOS/Linux 的 `<pthread.h>` 自带 C++ 守卫，
无影响；Windows 的 `nanosig_platform_port.h` 直接 `#include <windows.h>`，
C++ 消费者会把整份 Win32 声明包进 `extern "C"`，可能与 C++ 重载/模板声明冲突。

#### review 建议
把 `#include NANOSIG_PLATFORM_HEADER` 移到 `extern "C"` 之前（平台头自身已带
`extern "C"` 与否不影响类型别名）。修改后需在 MSVC/clang-cl 上补一次编译验证。

#### 作者建议
按 review 建议修：把 `#include NANOSIG_PLATFORM_HEADER` 连同它前面的
`ns_platform_thread_fn` 前置声明与 `struct ns_platform_waitable;` 一起移到
`extern "C"` 之前。本机（macOS）事后跑过 macos-release 全量 ctest 与
api-compile-checks 的 CPP 兼容检查（Clang），MSVC 上仍未验证。→ 关闭-已修复

#### 关闭原因
include/nanosig/nanosig_port.h 现按 include 顺序 + 前置声明 + 平台后端头 +
`extern "C"` 排列：平台 SDK 头（Windows 的 `<windows.h>`）不再被 C 链接包围。
macos-release 52/52、api-compile-checks（含 cpp_compat）通过。

- **关闭日期**: 2026-10-18
- **状态**: 关闭-已修复
- **关闭原因**: 代码布局已调整：`#include NANOSIG_PLATFORM_HEADER`（第 68 行）已置于 `extern "C" {`（第 71 行）之前，平台 SDK 头不再被 C 链接包围。不属 C++ 兼容性缺陷。
- **关闭日期**: 2026-10-19

#### 可重现的失败场景
Windows + MSVC：一个 C++ TU 只 `#include <nanosig/nanosig.h>`（间接包含
`nanosig_port.h`）即可能因 `<windows.h>` 的 C++ 声明被 C 链接包围而报错。
本机未验证，需 Windows CI 或本地 MSVC 复核。

#### 定位
include/nanosig/nanosig_port.h:67
include/nanosig/nanosig_port.h:70
platform/windows/nanosig_platform_port.h:22

---

---
