# list review 归档 2026-07

<a id="LIST-004"></a>
### LIST-004: 缺少 `ns_list_entry_safe`（NULL 安全版反查宏）

- **状态**: 关闭-已修复
- **严重度**: 🟡 中
- **类型**: design

#### 问题描述
`nanosig_slist.h` 提供了 `ns_slist_entry_safe(ptr, type, member)` 宏（NULL 安全），但 `nanosig_list.h` 没有对应的 `ns_list_entry_safe` 宏。虽然 list 的遍历宏使用 `head` 哨兵而非 NULL 终止，但用户在 `ns_list_front`/`ns_list_back` 等返回值处理时可能需要 NULL 安全的 entry 反查。slist 已有此模式，list 侧缺失造成 API 对称性不一致。

#### review 建议
添加 `ns_list_entry_safe` 宏以保持与 slist 的 API 对称性：
```c
#define ns_list_entry_safe(ptr, type, member) \
    NS_CONTAINER_OF_SAFE((ptr), type, member)
```

#### 作者建议
已添加 `ns_list_entry_safe` 到 `include/nanosig/nanosig_list.h:214-216`，与 `ns_list_entry` 相邻，与 slist 的 `ns_slist_entry_safe` 语义一致。→ 关闭-已修复

#### 关闭原因
已在 `include/nanosig/nanosig_list.h:214-216` 添加。

- **关闭日期**: 2026-07-18
- **状态**: 关闭-已修复

#### 定位
include/nanosig/nanosig_list.h:214-216

---
