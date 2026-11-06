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
  - id: "GLOBAL-008"
    module: "global-review"
    status: "closed-fixed"
    severity: "medium"
    type: "doc"
    section: "closed"
    closed_date: "2026-10-23"
    location: "docs/review/global-review.md#GLOBAL-008"
    locations:
      - "include/nanosig/nanosig_port.h:480"
      - "include/nanosig/nanosig_port.h:463"
      - "include/nanosig/nanosig_loop.h:53"
      - "platform/README.md:34"
      - "platform/README.md:58"
      - "platform/README.md:67"
  - id: "GLOBAL-009"
    module: "global-review"
    status: "closed-fixed"
    severity: "medium"
    type: "bug"
    section: "closed"
    closed_date: "2026-11-06"
    location: "docs/review/global-review.md#GLOBAL-009"
    locations:
      - "platform/baremetal/nanosig_platform_port.h:157"
      - "platform/baremetal/nanosig_platform_port.h:173"
      - "test/baremetal/test_cpp_link.cpp:14"
  - id: "GLOBAL-010"
    module: "global-review"
    status: "closed-fixed"
    severity: "low"
    type: "doc"
    section: "closed"
    closed_date: "2026-11-06"
    location: "docs/review/global-review.md#GLOBAL-010"
    locations:
      - "src/ns_broker.c:49"
      - "platform/README.md:78"
  - id: "GLOBAL-011"
    module: "global-review"
    status: "closed-fixed"
    severity: "low"
    type: "doc"
    section: "closed"
    closed_date: "2026-11-06"
    location: "docs/review/global-review.md#GLOBAL-011"
    locations:
      - "docs/MPSC_MEMORY_ORDER.md:46"
      - "docs/MPSC_MEMORY_ORDER.md:60"
      - "docs/ARCHITECTURE.md:197"
      - "src/ds/ns_mpsc_record_ring.c:156"
      - "src/ds/ns_mpsc_record_ring.c:414"
      - "src/ds/ns_mpsc_record_ring.c:421"
      - "src/ds/ns_mpsc_record_ring.c:458"
  - id: "GLOBAL-012"
    module: "global-review"
    status: "open"
    severity: "low"
    type: "cleanup"
    section: "open"
    location: "docs/review/global-review.md#GLOBAL-012"
    locations:
      - "cmake/scripts/audit_runner.sh:46"
      - "cmake/SanitizeAll.cmake:21"
  - id: "GLOBAL-013"
    module: "global-review"
    status: "open"
    severity: "low"
    type: "cleanup"
    section: "open"
    location: "docs/review/global-review.md#GLOBAL-013"
    locations:
      - "cmake/CompileWarnings.cmake:3"
      - "cmake/CompileWarnings.cmake:30"
---

# 全局视角 Review（跨模块问题）

**日期**: 2026-07-05
**审查者**: claude-code-review

---

**2026-11-04 全量审查记录**

- 基线：eaab50eb36f2b38c58a771d256a16a4e9190a4df；开始时工作区干净。本次仅写 review 产物，业务实现与设计源文档保持原样。
- 结论：**REQUEST CHANGES；Architectural Status：BLOCK**。当前确认 10 条打开问题（新增 9 条、既有 1 条复核），CRITICAL 0 / HIGH 4 / MEDIUM 3 / LOW 3。
- 独立证据：review_runtime_ds（code-reviewer）通读 30 个源码/头文件，REQUEST CHANGES；review_platforms（code-reviewer）审查六后端及公共契约，共 18 个文件，REQUEST CHANGES；review_architecture_docs（architect）复核跨模块合同与文档，BLOCK；verify_freertos_contract（researcher）另以固定版本官方实现独立反证，confirmed。
- 多角度交叉：Broker 重复 add 由代码复现与架构合同反查共同确认；MPSC 文档顺序由 runtime/architect 双方确认；FreeRTOS join 由平台 reviewer 与 researcher 双方确认。自定义 code-review quality gates 查询为空，无额外门禁。

| 覆盖范围 | 实际审查口径 |
|---|---|
| src、include、platform 的实现/头文件/版本模板，共 44 份 | 两路 code-reviewer 分模块通读；包括六平台、core、timer、broker、全部数据结构、formatio/debug |
| README、platform/README 与 docs，共 80 份基线文档 | 当前主体文档核读；历史 plan/spec/audit/review 按文件身份、批准决策、状态及相关条目核对，未声称所有历史文档逐行精读 |
| 构建证据 | CMakeLists、Presets、CompileWarnings、zephyr 三文件、CI/nightly 正文；ApiCompileChecks 相关构造部分；其他构建脚本只作验证支撑 |
| 排除项 | test/demos 不作为 review 对象，仅读取关联调用与覆盖证据并运行现有测试；tmp 参考代码和隐藏运行时缓存不审查 |

| 验证 | 结果 |
|---|---|
| macOS Release 配置、构建、api-compile-checks | 通过；API 语法检查有一条既有 C++ unused-function 警告 |
| Release CTest，排除 nightly/long-stability | 50/50 通过 |
| MPSC 压测单独设置 NS_MPSC_RECORD_RING_STRESS_DURATION_SEC=20 | 1/1 通过，约 19.43 秒 |
| baremetal-debug | 构建及 6/6 测试通过，包含不支持符号的负向链接门 |
| 独立 ASAN Debug，未构建 demos | 45/45 通过，sanitize-all 四段审计通过 |
| 独立 UBSAN Debug，未构建 demos | 45/45 通过，sanitize-all 四段审计通过 |
| 定向复现 | Broker 注册失效、裸机重入/超大 timer、格式化错误、裸机 C++ 链接失败均实测；debug 并发等级 TSAN 明确报 data race，exit 134 |
| 静态分析 | 9 个业务 C 文件及 macOS/baremetal/FreeRTOS port 分别检查；无新增路径错误，hashtable/rbtree 有 4 个未登记为缺陷的 dead-store 提示 |
| FreeRTOS 16 位 tick join | 固定 V11.1.0 官方源码交叉证明；未硬件实测 |

最初尝试同时启用 ASAN/UBSAN，被本项目配置审计拒绝；随后改为两个单检测器构建，以上通过结果均来自后者。Release 的 sanitize-all 仅说明未启用检测器，未当作 sanitizer 通过证据。

**文档保护与架构观察**：保留单 broker 同步请求的无限等待合同、调用方生命周期职责、裸机能力子集、旧 proposal/audit 快照及全部已拒绝决定。纯指数尾数为 10 的等价表示不新增问题。既有格式化条目仅追加复核，排除公共合同允许的 %g/%G 行为。新增作者建议全部留空；本轮未实施修复。按 2026-10-05 截止日检查，无满 30 天且应归档的关闭条目。

**验证边界**：Linux/Windows/Zephyr 未运行本机平台套件；未运行完整 nightly、长时稳定性或全套 TSAN。现有测试通过不消除定向复现发现的缺陷。审查不能保证不存在其他问题。

**2026-11-06 合同保护复核补记**：CORE-020 保留复现，但依 review 规范的隐含前提分支，改为中优先级 doc，待作者明确 poll 非重入合同；撤回必须增加运行守卫的结论。BROKER-037 的重复 add 不属于重复 init 免责范围，维持原判断。当前 10 条打开问题的分布修订为 CRITICAL 0 / HIGH 3 / MEDIUM 4 / LOW 3；总体 REQUEST CHANGES 与架构 BLOCK 仍由 Broker 等已确认项支撑。上方 2026-11-04 记录作为当时审查快照保留。

**2026-11-06 作者仲裁更新**：作者明确要求 BROKER-037 按文档保护处理；该条已关闭-已拒绝，公开头补齐禁止重复 add 的前提及误用后果，代码现场补 shortcut，运行逻辑未改。该问题不再作为打开缺陷或架构 BLOCK 的依据。综合架构状态调整为 WATCH（保留其他文档观察），总体仍为 REQUEST CHANGES：当前 9 条打开问题，CRITICAL 0 / HIGH 2 / MEDIUM 4 / LOW 3。此前审查和复核结论按日期保留，不覆盖作者仲裁。

**2026-11-06 合同与修复收口**：作者确认 CORE-020 先按非重入调用合同保护；公开头已补 @pre，现场已补 shortcut，该条关闭-已拒绝并永久保留。FREERTOS-009 已在工作区修复（未提交），等待失败后继续重试，收到退出信号才清理资源；修复前回归失败、修复后 FreeRTOS Release / ASAN / UBSAN 各 3 项及 sanitizer 审计通过，裸机 6 项通过。当前 7 条打开问题，CRITICAL 0 / HIGH 1 / MEDIUM 3 / LOW 3；剩余高优先级为 FORMATIO-021，总体仍为 REQUEST CHANGES。此前快照与作者仲裁记录保留。

**2026-11-06 四项修复完成**：按作者要求完成 FORMATIO-031、GLOBAL-009、GLOBAL-010、GLOBAL-011，均关闭-已修复，改动保留在工作区未提交。格式化与裸机 C++ 链接回归均取得修复前失败、修复后通过的证据；macOS Release 全量 53 项、裸机 7 项、ASAN/UBSAN 格式化与日志各 2 项、API 编译检查和两种 sanitize-all 审计通过。普通 MPSC 压测使用 20 秒配置；README 宏参数经手工语法编译验证，MPSC 文档经独立源码复核。当前剩余 2 条打开问题：FORMATIO-021（高）和 DEBUG-001（中）。历史快照、文档保护决定及其审计记录保留。

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

- **GLOBAL-009（2026-11-06）**: 为裸机时钟 setter 补 C linkage，新增 C++ 消费者链接运行回归。修复分支：fix/timer-021-interval-bound（工作区未提交）；验证证据见该条关闭原因。
- **GLOBAL-010（2026-11-06）**: 补齐手工接入必须注入 broker 栈宏的说明与平台默认值链接。修复分支：fix/timer-021-interval-bound（工作区未提交）；验证证据见该条关闭原因。
- **GLOBAL-011（2026-11-06）**: 纠正 MPSC 预留位置、payload 提交及槽位回收的同步说明。修复分支：fix/timer-021-interval-bound（工作区未提交）；验证证据见该条关闭原因。

## 现在打开的问题

<a id="GLOBAL-012"></a>
### GLOBAL-012: 审计脚本手列 API 检查目标，未复用 CMake 聚合目标

- **状态**: 打开
- **严重度**: 🟢 较低（前提：按当前仓库实现与公开契约静态走查；仅维护性重复，未发现由重复本身导致的功能故障）
- **首次发现日期**: 2026-11-06
- **类型**: cleanup

#### 问题描述

audit_runner 再维护一份 API target 列表，并解析 Makefile/build.ninja 决定执行哪些检查。顶层已有按平台配置的 api-compile-checks 目标，脚本重复了构建系统负责的目标选择。

#### review 建议

保留 NANOSIG_BUILD_API_CHECKS=OFF 的跳过分支；启用时直接执行 cmake --build 指定目录 --target api-compile-checks。删除手写列表、生成文件 grep 与目标拼接，不新增探测层。

#### 作者建议

（待作者补充）

#### 可重现的失败场景

新增一个 API 检查并加入顶层 api-compile-checks 后，脚本仍只遍历固定六项，不自动包含新增检查。此处按重复配置的 cleanup 记录，未运行各生成器复现，不声称当前全部检查均遗漏。

#### 影响面

cmake/SanitizeAll.cmake:21 通过自定义目标调用 audit_runner，影响启用 sanitizer 的审计。CMakeLists 为 host、FreeRTOS、裸机分别定义聚合目标，NANOSIG_BUILD_API_CHECKS=OFF 时继续跳过。未改测试/demos 源码，也未执行 sanitizer。

#### 文档仲裁与历史去重

已核对 global 当前与归档，没有同根记录。本项涉及顶层构建定义与审计入口的重复配置，归全局文档。

#### 定位
cmake/scripts/audit_runner.sh:46
CMakeLists.txt:456
CMakeLists.txt:477
CMakeLists.txt:507
cmake/SanitizeAll.cmake:21

<a id="GLOBAL-013"></a>
### GLOBAL-013: 未使用的 interface warning 函数重复整套警告配置

- **状态**: 打开
- **严重度**: 🟢 较低（前提：按当前仓库实现与公开契约静态走查；仅维护性重复，未发现由重复本身导致的功能故障）
- **首次发现日期**: 2026-11-06
- **类型**: cleanup

#### 问题描述

nanosig_apply_interface_warnings 无仓库调用者，却复制 private 版本的全部编译器分支和警告选项。维护警告策略时因此多保留了一份未被构建路径使用的实现。

#### review 建议

删除未使用的 nanosig_apply_interface_warnings，保留实际使用的 private 版本。无需为没有调用者的能力新增 scope 参数或公共转发层。

#### 作者建议

（待作者补充）

#### 可重现的失败场景

搜索整个仓库构建代码，interface helper 只出现定义；对比两个函数，除了 INTERFACE/PRIVATE 可见性，警告选项和条件分支相同。无功能失败主张。

#### 影响面

无调用者（内部构建函数）。现有 library/test target 使用 private helper；不改变它们的编译选项。未运行配置或构建。若外部项目直接 include 此内部 cmake 文件并调用未文档化函数，删除会影响该用法。

#### 文档仲裁与历史去重

已核对 global 当前/历史及 cmake 指引，未找到保留该无调用者函数的合同。本项是构建公共配置清理，不涉及源码 API。

#### 定位
cmake/CompileWarnings.cmake:3
cmake/CompileWarnings.cmake:30

---

---

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

<a id="GLOBAL-008"></a>
### GLOBAL-008: 线程栈大小的跨后端公共契约声明与实现不符（三处），且规格与 README 互相矛盾

- **状态**: 关闭-已修复
- **严重度**: 🟡 中
- **类型**: doc
- **关闭原因**: 三项全部消解：(1) 「init 失败后 join 返回 NS_E_INVAL，五个后端一致」已删除并改写为文档化前提（PORT-032）；(2) 「实际分配的栈永远不小于请求值」由 PORT-031 使 POSIX 后端真正向上取整到页粒度而成立；(3) 「超出区间时 ns_loop_create 返回 NS_E_INVAL」的上界问题由 FREERTOS-008 发布真实上界而解决（修复 commit 见该条目）；POSIX/Zephyr 的上界本就是 SIZE_MAX，属已知限制。
- **关闭日期**: 2026-10-23

#### 问题描述

本条只记录**跨后端公共契约表述**的问题（根因在各后端，已分别记为 PORT-031 / PORT-032 / PORT-033 / FREERTOS-008）。

**已消解的两项**（2026-10-23 复核）：

| 原表述 | 现状 |
|---|---|
| "init 失败后 join 返回 `NS_E_INVAL`（五个后端一致）"（include/nanosig/nanosig_port.h） | ✅ 已删除该**反向假承诺**，改为文档化前提"init 失败后不得 join（未定义行为）"；PORT-032 按合同前提保护关闭 |
| "实际分配的栈**永远不小于**请求值（后端按自身粒度向上取整）"（include/nanosig/nanosig_port.h、platform/README.md） | ✅ 已成立：POSIX 后端补上了页粒度向上取整（PORT-031） |

**仍打开的一项**：

"超出后端可接受区间时 `ns_loop_create` 返回 `NS_E_INVAL`"（include/nanosig/nanosig_loop.h）—— 下界在 create 阶段确实被拦；但**上界拦不住**：FreeRTOS 发布 `max = SIZE_MAX`，实际 init 还会因超出 `configSTACK_DEPTH_TYPE` 而拒绝（即 FREERTOS-008），失败点比契约晚一层。POSIX / Zephyr 的上界本身就是 `SIZE_MAX`（平台无法在公共层表达真实上限），属已知限制。本条随 FREERTOS-008 一并关闭。

#### review 建议

1. 契约表述要与实现对齐，**三选一并明确写下来**：
   - (a) 修后端，使实现满足现有表述（POSIX 向上取整；状态重置前移；构建期校验 broker 旋钮）—— 保留契约的强表述；
   - (b) 弱化契约：把"五个后端一致""永远不小于请求值""`ns_loop_create` 就返回 `NS_E_INVAL`"改成与实现相符的措辞；
   - (c) 混合：下界与"init 失败后 join 返回错误"保留强契约（可强制），上界改为"尽力在 `ns_loop_create` 阶段校验，实际以 `ns_loop_start` 为准"。

   倾向 (c)：下界与失败语义是**可强制**的，值得写成硬契约；上界依赖各后端实现上限（POSIX 受 RLIMIT_STACK / 平台实现限制、Zephyr 受堆大小限制、FreeRTOS 受深度类型宽度限制），本就无法在公共层统一表达。

2. 修正 `platform/README.md:58` 的 FreeRTOS `min` 规则表述，使其与 `platform/freertos/port.c:363-367` 一致（建议直接引用 `min = (configMINIMAL_STACK_SIZE - 1) * sizeof(StackType_t) + 1` 这个式子）。

3. 修正 `platform/README.md:67` 与规格文档的推荐值（见 PORT-033）。

4. 规格文档 `docs/specs/深度访谈-线程栈大小参数规格.md` 的 AC-6/AC-7/AC-11 与 D-3 表也需要按上述结论复核。

#### 作者建议

同意全部消解，无异议。三项分别由 PORT-032（删除假承诺并文档化前提）、PORT-031（POSIX 真正向上取整）、FREERTOS-008（发布真实上界）解决。POSIX/Zephyr 上界为 SIZE_MAX 属已知限制，接受。

#### 可重现的失败场景

逐条对照：

- `include/nanosig/nanosig_port.h:480` 的"五个后端一致" ↔ PORT-032 的四后端反例。
- `include/nanosig/nanosig_port.h:463` 与 `platform/README.md:34` 的"向上取整" ↔ macOS 实测 `setstacksize(100000) -> EINVAL`。
- `include/nanosig/nanosig_loop.h:53` 的"`ns_loop_create` 返回 `NS_E_INVAL`" ↔ `ns_loop_create(stack_size=100000)` 返回 `NS_OK`。
- `platform/README.md:58` ↔ `platform/freertos/port.c:363-367` 的 8184/8185 例子。
- `platform/README.md:67` ↔ PORT-033 的 `ns_init()` 失败实测。

#### 定位
include/nanosig/nanosig_port.h:480
include/nanosig/nanosig_port.h:463
include/nanosig/nanosig_loop.h:53
platform/README.md:34
platform/README.md:58
platform/README.md:67

---

<a id="GLOBAL-009"></a>
### GLOBAL-009: 裸机时钟注入函数缺少 C linkage，C++ 消费者链接失败

- **状态**: 关闭-已修复
- **严重度**: 🟡 中（前提：裸机库按 C11 编译、调用方按 C++11 编译；macOS arm64 Clang 链接实测）
- **首次发现日期**: 2026-11-04
- **类型**: bug
- **关闭原因**: 工作区修复（未提交；当前分支 fix/timer-021-interval-bound）：platform/baremetal/nanosig_platform_port.h:157 仅为时钟 setter 声明增加 C++ extern C 守卫，保留 SDK include 边界。新增 C++ 消费者实际链接 C 库并校验两次注入时钟读值。旧头链接失败、修复后通过；裸机 7 项 CTest 和 API 编译检查通过。
- **关闭日期**: 2026-11-06

#### 问题描述

C++ 应用无法链接裸机初始化必需的 ns_baremetal_set_now_us。公共平台头在 extern C 之前包含裸机类型头，而该 setter 声明没有自己的 C linkage 守卫；C++ 调用方与 C 库对象生成不同符号。

#### review 建议

仅为 ns_baremetal_set_now_us 的声明补 C++ extern C 守卫，保持平台 SDK include 位于公共 extern C 外。补 C 库对象与 C++ 消费者的实际链接验收。

#### 作者建议

作者于 2026-11-06 明确要求修复列表第 4 项：为裸机时钟 setter 声明补 C linkage，保持平台 SDK include 边界不变，补 C 库与 C++ 消费者的实际链接回归；改动留在工作区，不提交。

#### 可重现的失败场景

创建 review.cpp：
```cpp
#include <nanosig_baremetal_api.h>
static uint64_t now_us() { return 0; }
int main() { ns_baremetal_set_now_us(now_us); return ns_platform_init(); }
```
执行：
```sh
cc -std=c11 -Iinclude -Iplatform/baremetal -c platform/baremetal/port.c -o /tmp/review-port.o
c++ -std=c++11 -Iinclude -Iplatform/baremetal review.cpp /tmp/review-port.o -o /tmp/review-cpp
```
第二条命令 exit 1：Undefined symbols: ns_baremetal_set_now_us(unsigned long long (*)())；链接器同时提示找到 C 符号且声明可能缺 extern C。

#### 影响面

所有 C++ 裸机消费者；仓库内唯一调用是 test/baremetal/main.c 的 C 测试，无生产内部调用。声明由 nanosig_baremetal_api.h → nanosig_port.h → 裸机平台头引入，实现为 platform/baremetal/port.c 的 C 函数。现有语法检查没有调用 setter 后链接，因此未暴露问题。

#### 文档仲裁与历史去重

裸机子计划明确要求 C++ 兼容，未文档化仅支持 C。GLOBAL-007 保护的是 SDK include 不应包在 C linkage 内；本项只修函数声明，根因不同。已核对 global/history，无重复。

#### 定位

platform/baremetal/nanosig_platform_port.h:157
platform/baremetal/nanosig_platform_port.h:173
test/baremetal/test_cpp_link.cpp:14
CMakeLists.txt:370


---

---

<a id="GLOBAL-010"></a>
### GLOBAL-010: README 手工接入步骤遗漏 broker 必需编译定义

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低（前提：非裸机、绕过 nanosig CMake target 手工集成；macOS Clang 语法检查实测）
- **首次发现日期**: 2026-11-04
- **类型**: doc
- **关闭原因**: 工作区修复（未提交；当前分支 fix/timer-021-interval-bound）：README.md:19 补充 NANOSIG_BROKER_STACK_SIZE 必需定义、字节单位、0 默认值及平台栈文档链接，说明 CMake/Zephyr 自动注入和裸机例外。macOS Clang -std=c11 -Werror -fsyntax-only 实测：缺宏按预期失败，增加 -DNANOSIG_BROKER_STACK_SIZE=0 后通过；保留源码编译守卫。
- **关闭日期**: 2026-11-06

#### 问题描述

按照 README 列出的三项接入步骤手工编译 broker，会因缺少 NANOSIG_BROKER_STACK_SIZE 而失败。源码有意要求构建系统注入该宏；README 没有列出这个必要条件。

#### review 建议

在 README 接入步骤列出必需的 NANOSIG_BROKER_STACK_SIZE 编译定义，并链接 platform/README.md 的平台默认值、RTOS 栈大小说明。保留缺宏即编译失败的守卫。

#### 作者建议

作者于 2026-11-06 明确要求修复列表第 5 项：在 README 手工接入说明补 NANOSIG_BROKER_STACK_SIZE 必需定义及平台默认值链接，保留缺宏编译守卫；改动留在工作区，不提交。

#### 可重现的失败场景

已有生成版本头时执行：
```sh
clang -std=c11 -fsyntax-only -Iinclude -Ibuild/include -Iplatform/macos src/ns_broker.c
```
结果：src/ns_broker.c 的缺宏 #error 及未声明宏错误。相同命令增加 -DNANOSIG_BROKER_STACK_SIZE=0 后通过。

#### 影响面

所有手工编译且包含 broker 的消费者；当前 CMake target 和 Zephyr module 会注入该宏，裸机构建排除 broker，均不受此文档遗漏影响。下级证据为 CMakeLists.txt 的 target_compile_definitions 与 src/ns_broker.c 的编译守卫；API 编译检查不替代手工构建说明。

#### 文档仲裁与历史去重

线程栈规格明确要求缺定义即编译失败，源码守卫受保护。PORT-033 讨论已传入非零栈值的验证，GLOBAL-008 讨论栈范围说明，均不是接入步骤遗漏，未重复旧 finding。

#### 定位

README.md:19
src/ns_broker.c:49
CMakeLists.txt:210
platform/README.md:78


---

---

<a id="GLOBAL-011"></a>
### GLOBAL-011: 当前 MPSC 同步文档误把预留位置发布当作 payload 提交

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低（前提：维护者依据当前内存序文档理解实现；源码走查确认，未发现对应运行时同步缺陷）
- **首次发现日期**: 2026-11-04
- **类型**: doc
- **关闭原因**: 工作区修复（未提交；当前分支 fix/timer-021-interval-bound）：docs/MPSC_MEMORY_ORDER.md:46 和 docs/ARCHITECTURE.md:197 按当前实现明确先开放 write_pos、再复制 payload、最后 release-store 有效 meta；消费者 acquire-load 有效 meta 才读取 payload，槽位复用由 read_pos release/acquire 保证。同步修正 CAS 所有权与总结表，并区分生产者 meta 清零 release、消费者清零 relaxed。已由独立 agent 对照源码复核并修正其指出的内存序笔误；未修改 MPSC 运行逻辑，保留历史审计快照。
- **关闭日期**: 2026-11-06

#### 问题描述

文档给出了错误的 payload 可见性证明，维护者据此调整原子操作会误删真正的同步。MPSC_MEMORY_ORDER 写成先 memcpy 再 release-store write_pos，并称消费者 acquire write_pos 后即可看到 payload；ARCHITECTURE 的同步表也漏掉记录 meta。实际顺序是先 write_pos，再复制 payload，最后 release-store 有效 meta；消费者 acquire meta 且确认有效位后才能消费。

#### review 建议

仅修改两份当前说明：write_pos 表示预留进度；记录 meta 的 release/acquire 才完成 payload 提交与消费同步。保留当前实现和历史审计快照。

#### 作者建议

作者于 2026-11-06 明确要求修复列表第 6 项：按当前实现修正 MPSC_MEMORY_ORDER 与 ARCHITECTURE 的 write_pos / meta 同步说明，保留现有运行逻辑和历史审计快照；改动留在工作区，不提交。

#### 可重现的失败场景

对照生产者 src/ds/ns_mpsc_record_ring.c:413、416、419 与消费者 :455、469。令生产者完成 write_pos 发布后、memcpy 前有限暂停；消费者能看到 write_pos 前进，却因 meta 仍无效而返回 NS_E_EMPTY。因此 acquire write_pos 本身不能证明尚未复制的 payload 可见。

#### 影响面

直接影响 MPSC 维护者及 core emit/dispatch 的并发证明；源码消费者为 ns_mpsc_record_ring_try_acquire，其公开 copy-out 包装及 ns_loop_dispatch_pending 依赖该协议。现有压力测试验证实现行为，不能验证文档同步证明的措辞。

#### 文档仲裁与历史去重

MPSC-014 已修的是阶段一 CAS expected 值；本项是后续 payload 提交顺序，根因不同。MPSC-011 是旧固定槽文档替换，MPSC-012 保护生产者不能在 reserve/commit 中永久退出；这里使用有限暂停，不挑战该前提。docs/audit/memory_ordering_audit.md 按历史快照保留。架构与 runtime 两路均确认本项。

#### 定位

docs/MPSC_MEMORY_ORDER.md:46
docs/MPSC_MEMORY_ORDER.md:60
docs/ARCHITECTURE.md:197
src/ds/ns_mpsc_record_ring.c:156
src/ds/ns_mpsc_record_ring.c:414
src/ds/ns_mpsc_record_ring.c:421
src/ds/ns_mpsc_record_ring.c:458

---

---
