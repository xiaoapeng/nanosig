# slist review 归档 2026-08

<a id="SLIST-005"></a>
### SLIST-005: `ns_slist_for_each` 文档描述与实际遍历起始位置不符

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低
- **类型**: doc

#### 问题描述
docstring 写道"是节点时从该节点开始遍历"，但实际 `pos` 的起始值是 `(head)->next`（即给定节点的下一个节点），给定节点被跳过。

#### review 建议
修改为"是节点时从该节点的 next 开始遍历（跳过该节点本身）"。

#### 作者建议
已采纳 review 建议，将 docstring 中的"从该节点开始遍历"修改为"从该节点之后开始遍历（跳过该节点本身）"。
→ 关闭-已修复

#### 关闭原因
`include/nanosig/nanosig_slist.h:358-359` 文档已修正。

- **关闭日期**: 2026-08-02
- **状态**: 关闭-已修复

#### 定位
include/nanosig/nanosig_slist.h:358-359

---
