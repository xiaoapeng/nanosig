# hashtable review 归档 2026-07

<a id="HASHTABLE-009"></a>
### HASHTABLE-009: `ns_hashtable_node_init` 在 `key==NULL` 时留 struct 未初始化 (MEDIUM) `bug`

- **状态**: 关闭-已修复
- **严重度**: 🟡 中
- **关闭原因**: 头文件 `ns_hashtable_node_init` 补 `@note` 文档："当 key 为 NULL 时，本函数不初始化任何字段，直接返回。调用者有责任在后续操作前检查 key 是否为 NULL。"
- **关闭日期**: 2026-07-04

---
