# FreeRTOS Backend 代码审查报告

**日期**: 2026-10-11
**审查者**: opencode-review
**范围**: platform/freertos/port.c、test/freertos/*、FreeRTOS 构建接线（CMakeLists.txt、cmake/FreeRTOSPosix.cmake、CMakePresets.json）与 include/nanosig/nanosig_port.h 中 FreeRTOS 相关分支。
**验证**: macos 上 `cmake --build --preset freertos-release` 零警告、`ctest --preset freertos-release -LE nightly` 2/2 通过。

---

## 修复历史

### FREERTOS-001: `ns_platform_clock_monotonic_us` 在 32 位 tick 目标上不再单调

- **修复**: 平台层改用回绕扩展的 64 位 tick 累加器（无符号回绕差值 `now - last` 累加 +
  `taskENTER_CRITICAL`/`taskEXIT_CRITICAL` 保护），消除回绕瞬间的时间倒流。
- **文档**: 在 `include/nanosig/nanosig_port.h` 的 `ns_platform_clock_monotonic_us` docstring、
  `platform/README.md` FreeRTOS 小节与 `port.c` 注释中明确"FreeRTOS tick 回绕不能太快"
  的采样前提：相邻两次采样之间必须最多发生一次回绕（采样间隔小于一个 `TickType_t` 回绕
  周期），否则会少计整周期。典型 32 位 tick @1kHz 回绕周期约 49.7 天。
- **分支**: fix/freertos-clock-monotonic-wrap
- **日期**: 2026-10-11

### FREERTOS-002: `ns_platform_waitset_remove` 对"数据队列"成员用 `xQueueReset` 静默丢弃未处理负载

- **修复**: 采用文档化（review 建议 ①）：明确 FreeRTOS 后端 remove 会排空被移除成员、
  对**数据队列**成员丢弃未取走的消息，并标注为向 `xQueueRemoveFromSet`“成员必须为空”
  妥协的**已知取舍**；其它后端 remove 不消费成员数据。
- **文档**: include/nanosig/nanosig_port.h（`ns_platform_waitset_remove` @note）、
  platform/README.md（中立契约 + FreeRTOS 小节）、
  docs/plans/子计划-FreeRTOS平台后端接入.md（D3/AC-4/Step 3）、
  docs/specs/深度访谈-FreeRTOS平台后端接入规格.md（Constraints/AC）。
- **测试**: test/freertos/test_freertos_waitset.c 新增 `test_remove_clears_data_queue`。
- **工作区**（未提交）: fix/freertos-clock-monotonic-wrap
- **日期**: 2026-10-11

### FREERTOS-003: `edge_triggered` 在 FreeRTOS 后端被忽略但未文档化

- **澄清（修正 review 原文）**: FreeRTOS queue set 是“每次 send/give 记一次就绪”的
  **token/边沿语义**，不是原文所说的“按电平重报”。实测：入队 3 条未取消息，一次
  `waitset_wait` 得 1、紧接着再 wait 得 0，成员数据仍在。故该后端的风险是**漏报**
  （按电平假设的调用方会漏掉一次就绪里未处理完的剩余部分），不是重复上报。
- **作者决策**: 不改运行时行为——强制 `edge_triggered == 1`、拒绝其它值会给可移植调用方
  增加负担（同一段代码必须显式改参数才能在 FreeRTOS 上跑），不可接受；改为**只在文档中
  说明** FreeRTOS 忽略 `edge_triggered` 及其真实 token 语义。
- **文档**: include/nanosig/nanosig_port.h（`ns_platform_waitset_add` @note）、
  platform/README.md（FreeRTOS 小节）、platform/freertos/port.c（文件头注释）。
- **工作区**（未提交）: fix/freertos-clock-monotonic-wrap
- **日期**: 2026-10-11

### FREERTOS-004: `ns_platform_waitset_add` 中 `i == NANOSIG_FREERTOS_WAITSET_CAP` 分支不可达

- **作者决策**: 删除该死分支（review 建议的第一种：直接删除防线）。
- **修复**: platform/freertos/port.c 删除 `if(i == CAP){ xQueueRemoveFromSet; return
  NS_E_TOO_MANY_HANDLES; }` 回滚线，并在循环前加注释声明不变量：入口已排除
  `count >= CAP`，且 `count` 与 `slots[]` 在 add/remove 中成对维护，故必有空槽（`i < CAP`）。
- **工作区**（未提交）: fix/freertos-clock-monotonic-wrap
- **日期**: 2026-10-11

### FREERTOS-005: FreeRTOS 契约测试覆盖缺口（非空成员 add、drain 幂等、max_completions 截断）

- **作者决策**: 4 项全做（①②③ 补测试；④ 先实测 sanitizer，可用则接入、不可用则文档化）。
- **修复**:
  - ① test/freertos/test_freertos_waitset.c 新增 `test_add_rejects_nonempty_member`：
    先 signal 使成员非空 → `add` 断言 `NS_E_INVAL` 且 `registered_waitset == NULL`；drain 后 add 成功。
  - ② 新增 `test_drain_idempotent`：连续两次 drain 均 `NS_OK`，其间与之后 `wait` 均 count==0。
  - ③ 新增 `test_wait_truncates_completions`：两成员就绪、`max_completions=1` → 首次 count==1，
    再次 wait 取到另一个成员（剩余 token 留待下次）。
  - ④ 实测：ASAN / UBSAN 在 POSIX harness 可用（macOS 通过）；TSAN 挂死（无任何输出）。
    新增 `freertos-debug-asan` / `freertos-debug-ubsan` presets（configure/build/test）；
    platform/README.md「host POSIX harness 覆盖面」补 sanitizer 覆盖说明，并把 TSAN 列入不覆盖清单。
- **验证**: `ctest --preset freertos-release` 2/2；`freertos-debug-asan` 2/2；`freertos-debug-ubsan` 2/2（macOS）。
- **工作区**（未提交）: fix/freertos-clock-monotonic-wrap
- **日期**: 2026-10-11

### FREERTOS-006: `api-compile-checks` 未注册到 FreeRTOS 构建，平台契约编译检查未覆盖 FreeRTOS 分支

- **澄清（修正 review 原文）**: `test/unit/test_platform_contract_compile.c` 实际**未**引用
  `ns_waitable_handle_is_valid` / `NS_WAITABLE_SET` / `NS_WAITABLE_GET`（引用数 0）；它用的是
  `ns_waitable_init`、`NS_PLATFORM_WAIT_INFINITE_US` 的 `#error` 检查与 `ns_platform_waitable_t`
  字段契约。且这三个宏的 FreeRTOS 分支本已在编（`NS_WAITABLE_SET` 见 src/ns_broker.c:168，
  另两个见 test/freertos/*.c）。真实缺口是：专用 `-fsyntax-only` 契约目标没在
  `NANOSIG_PLATFORM_FREERTOS` 定义下运行。
- **作者决策**: ①+②。① 让 `api-compile-checks` 在 freertos 构建下真实存在；② 在 host 上用
  平台宏矩阵覆盖各平台编译期分支（不依赖目标工具链）。
- **修复**:
  - ① CMakeLists.txt：新增 `NANOSIG_PLATFORM STREQUAL "freertos"` 分支注册 `api-compile-checks`
    （平台契约 + `DEFINES -DNANOSIG_PLATFORM_FREERTOS=1`）；该检查同时注册为 ctest 用例，故
    `ctest --preset freertos-* -LE nightly` 会自动执行（CI 无需额外步骤）。
  - ② CMakeLists.txt（host 分支）：平台契约新增 `_windows` / `_zephyr` / `_freertos` 三个宏矩阵
    变体，一并挂到 host 的 `api-compile-checks`。
  - cmake/ApiCompileChecks.cmake：`nanosig_add_api_compile_check` 增加可选 `DEFINES <define>...`
    （`cmake_parse_arguments`），并抽出 `_nanosig_api_c_syntax_flags` / `_nanosig_add_syntax_target`
    复用；原行为与 MSVC/GCC 标志集保持不变。
- **验证**: 干净 host 构建 9/9 api 检查通过（含 3 个宏矩阵变体）；`cmake --build --preset
  freertos-release --target api-compile-checks` 成功；`ctest --preset freertos-release -LE nightly`
  3/3（waitset / broker / 平台契约）。
- **工作区**（未提交）: fix/freertos-clock-monotonic-wrap
- **日期**: 2026-10-11

---

## 现在打开的问题

（暂无。）

## 现在关闭的问题

（暂无。）
