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

## 现在关闭的问题

(无)
