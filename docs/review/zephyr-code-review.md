# Zephyr Backend 代码审查报告

**日期**: 2026-10-11
**审查者**: opencode-review
**范围**: platform/zephyr/port.c、test/zephyr/*、zephyr/{module.yml,CMakeLists.txt,Kconfig}、include/nanosig/nanosig_port.h 中的 Zephyr 分支，以及相关文档（platform/README.md、docs/ARCHITECTURE.md 第 7 节、docs/plans/子计划-Zephyr平台后端接入.md）。
**规范**: docs/review/AGENTS.md
**验证**: 本机未运行 Zephyr native_sim（需 west 工作区），结论基于源码 ↔ 公开契约（nanosig_port.h / platform/README.md）↔ 已批准计划（子计划-Zephyr平台后端接入.md / 深度访谈规格）三方比对；运行证据引用 CI `zephyr` job（`ci.yml:72-106`，最近一次 pull_request run 38120360058 为 success）。
**基线**: master 111b5a3

---

## 修复历史

### 2026-10-11 — ZEPHYR-001 闭环：空 waitset + 无限超时统一为有界封顶

- **ZEPHYR-001**: 空 waitset + `NS_PLATFORM_WAIT_INFINITE_US` 的语义未定义（五个后端
  3 永久阻塞 / 2 报错），Zephyr 侧走 `k_sleep` 且真无限等待等价于静默挂死。按作者决定
  统一为"封顶为最大等待值"：`platform/zephyr/port.c` 在 `n == 0` 分支把无限超时封顶到
  `NS_PLATFORM_WAIT_EMPTY_MAX_US`（默认 1 秒，可编译期覆盖），随后 `k_sleep` 返回
  `NS_OK` + `out_count == 0`；`test/zephyr/src/main.c` 新增
  `test_wait_empty_infinite_is_capped` 覆盖。

- **修复 commit**: `6cf940d`（PR #7，rebase 合并，无 merge 节点）
- **修复日期**: 2026-10-11

### 2026-10-11 — ZEPHYR-002…007 闭环（无效句柄加固 + 覆盖缺口 + cleanup）

作者指示"全部修复"，以下六条一并闭环。

- **ZEPHYR-002（bug，真缺陷）**: `waitset_add` 只判 `handle == NULL`，`ns_waitable_init`
  写入的 `(void *)~0` 哨兵可进入 waitset，随后被交给 `k_poll` 解引用。修复为统一使用
  `ns_waitable_handle_is_valid`：`waitset_add` 用 `NS_WAITABLE_GET(waitable)` 判定；一并
  加固 `event_signal`（`signal_handle` 侧）与 `event_drain` / `event_deinit`（`waitable`
  侧），无效句柄统一返回 `NS_E_INVAL`（deinit 为安全 no-op），与 Linux/macOS 一致。
  测试：`test_add_remove_errors` 补哨兵被拒断言，新增
  `test_event_invalid_handle_rejected`。
- **ZEPHYR-003（test）**: 新增跨线程确定性用例
  `test_event_cross_thread_signal_drain_in_poller` 与 `test_wakeup_cross_thread_signal_wait`：
  两个 `k_sem`（ready/done）显式握手替代睡眠轮询，poller 线程消费并 `event_drain`；断言
  跨线程 signal 被观测、`k_poll_signal_reset` 只发生在 poller 线程。
- **ZEPHYR-004（test）**: 新增 `test_wait_truncates_completions`：3 个就绪 +
  `max_completions == 2` 截断为 2、下一轮上报剩余 1、`max_completions == 0` 立即返回、
  全部 drain 后为 0。
- **ZEPHYR-005（test）**: `test_wait_level_triggered_and_timeout` 补时长断言：有限超时断言
  `elapsed >= timeout`；新增亚 tick（1us）用例断言向上取整到 ≥ 1 tick（floor 退化出的
  忙等会失败）。
- **ZEPHYR-006（test）**: `test_remove_does_not_reset` 补"remove 后不 re-add → 不再出现在
  completions"断言（AC-5/AC-7），保留原有 re-add 立即触发断言。
- **ZEPHYR-007（cleanup）**: 删除 `waitset_add` 中不可达的 `i == CAP` 返回分支，改以注释
  声明"count 与 slots[] 成对维护、必有空槽"不变量（与 FreeRTOS-004 同型处理）。

跨后端：同一根因（哨兵未排除）也存在于 FreeRTOS 后端，已作为 FREERTOS-007 一并加固。
文档：`include/nanosig/nanosig_port.h` 的 `ns_platform_waitset_add` docstring 补"无效句柄
返回 NS_E_INVAL"；`platform/zephyr/port.c` 与 `platform/freertos/port.c` 文件头补对应语义。

- **分支**: fix/zephyr-review-002-007
- **修复 commit**: `220dd1d`（代码与测试）
- **修复日期**: 2026-10-11

---

## 现在打开的问题

（暂无。）

---

## 现在关闭的问题

（暂无。）
