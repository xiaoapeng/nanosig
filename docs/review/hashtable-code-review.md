# Hashtable 代码审查报告

**日期**: 2026-07-04
**审查者**: claude-code-review

---

## 修复历史

### HASHTABLE-002: `ns_hashtable_node_init` 接受 `key=NULL`

- **修复日期**: 2026-07-04
- **修复 commit**: working tree (待提交)
- **修复方式**: `ns_hashtable_node_init` 入口添加 `if(key == NULL) return;` 静默返回，与 `node == NULL` 处理风格一致。
- **影响文件**: `src/ds/ns_hashtable.c`

### HASHTABLE-003: `ns_hash_string` 中 cursor 在 NULL 检查前赋值

- **修复日期**: 2026-07-04
- **修复 commit**: working tree (待提交)
- **修复方式**: 将 `if(key == NULL) return 0u;` 移到 cursor 赋值之前。
- **影响文件**: `src/ds/ns_hashtable.c`

### HASHTABLE-004: `--table->size` 在 size_t 下可能下溢

- **修复日期**: 2026-07-04
- **修复 commit**: working tree (待提交)
- **修复方式**: `--table->size` 之前添加 `assert(table->size > 0u);` 防御性断言。
- **影响文件**: `src/ds/ns_hashtable.c`

### HASHTABLE-006: `ns_hashtable_clear` 使用非惯用的预先 pop 循环模式

- **修复日期**: 2026-07-04
- **修复 commit**: working tree (待提交)
- **修复方式**: 改为 do-while 模式 `do { node = pop_front(); } while(node != NULL);`。
- **影响文件**: `src/ds/ns_hashtable.c`

### HASHTABLE-007: `.c` 文件违反 IWYU 原则

- **修复日期**: 2026-07-04
- **修复 commit**: working tree (待提交)
- **修复方式**: 显式 include `<assert.h>`、`<stddef.h>`、`<stdint.h>`。
- **影响文件**: `src/ds/ns_hashtable.c`

### HASHTABLE-008: 公共头未声明线程不安全约束

- **修复日期**: 2026-07-04
- **修复 commit**: working tree (待提交)
- **修复方式**: 在 `nanosig_hashtable.h` 文件顶部添加 `@warning` 文档说明线程不安全约束。
- **影响文件**: `include/nanosig/nanosig_hashtable.h`

---

## 现在打开的问题（2026-07-05 review）

### HT-010: `ns_hashtable_remove` 中 `--table->size` 前缺失防御性 assert（HASHTABLE-004 回归）

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低
- **类型**: bug

#### 问题描述
`docs/review/hashtable-code-review.md` 的修复历史中记录 HASHTABLE-004 已于 2026-07-04 修复（添加 `assert(table->size > 0u);`），但当前代码中 `ns_hashtable_remove` 函数（第 135-138 行）在 `--table->size` 之前没有该 assert。修复要么从未实际应用，要么在后续 commit 中被回退。

#### review 建议
在 `src/ds/ns_hashtable.c:137` 的 `--table->size` 之前添加 `assert(table->size > 0u);`。

#### 作者建议
正常调用 API 时调用方保证 remove 前 size > 0，不需要防御性 assert。与 HASHTABLE-004 同理。
→ 关闭-已拒绝

#### 关闭原因
正常调用 API 时调用方保证 remove 前 size > 0，不需要防御性 assert。

- 关闭日期: 2026-08-02
- 状态: 关闭-已拒绝

#### 定位
src/ds/ns_hashtable.c:137

## 现在打开的问题

（无）

---

## 现在关闭的问题

### HASHTABLE-009: `ns_hashtable_node_init` 在 `key==NULL` 时留 struct 未初始化 (MEDIUM) `bug`

- **状态**: 关闭-已修复
- **严重度**: 🟡 中
- **关闭原因**: 头文件 `ns_hashtable_node_init` 补 `@note` 文档："当 key 为 NULL 时，本函数不初始化任何字段，直接返回。调用者有责任在后续操作前检查 key 是否为 NULL。"
- **关闭日期**: 2026-07-04

### HASHTABLE-001: `ns_hashtable_insert` 使用缓存的 `node->hash` 而非从 key 重算

- **状态**: 关闭-已拒绝
- **严重度**: 🟡 中

#### 定位
`src/ds/ns_hashtable.c:73`

#### 问题描述
`ns_hashtable_insert` 直接使用 `node->hash` 进行分桶和重复检查，而非从 key 重新计算。如果调用者手动初始化节点（绕过 `ns_hashtable_node_init`），或在 `ns_hashtable_node_init` 后修改 `node->key` 而不更新 `node->hash`，则：

1. 新节点被分桶到 `node->hash % bucket_count`（错误的桶）
2. 重复检查在新桶中搜索，但旧节点在正确桶中，导致重复漏报
3. 表中有两个相同 key 的节点位于不同的桶，`ns_hashtable_find` 只能查到一个

#### review 建议
在 `ns_hashtable_insert` 中重新计算 hash：

```c
index = (size_t)ns_hash_string(node->key) % table->bucket_count;
```

并在 entry 比较中重新计算 hash，或直接使用 key 字符串比较作为权威（hash 仅用于分桶加速）。

#### 作者建议
文档说明，使用者不能这么做

#### 关闭原因
设计权衡：使用缓存的 hash 避免二次哈希计算以提升插入性能（尤其是在哈希计算开销大的场景）。调用方应始终通过 `ns_hashtable_node_init` 初始化节点，且不得在初始化后修改 key 而不更新 hash。

#### 关闭日期
2026-07-04

---

### HASHTABLE-005: `ns_hashtable_find` 返回非 const 指针穿过 const 表

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低

#### 定位
`include/nanosig/nanosig_hashtable.h:90`

#### 问题描述
```c
extern ns_hashtable_node_t *ns_hashtable_find(const ns_hashtable_t *table, const char *key);
```
返回的非 const 指针别名于 const 表的内容。拥有 const 表引用的调用者可绕过 const 契约写入 `entry->value`。在 C 标准下技术上良构（const 在包装层而非 pointee），但削弱了 const-correctness。

#### review 建议
提供 `const ns_hashtable_node_t *` 变体，或在文档中说明此设计意图。

#### 作者建议
文档中说明，使用者不能这么做

#### 关闭原因
设计权衡：保持 API 简洁，不为 const 提供重载变体。调用方通过 `const ns_hashtable_t *` 传入后获得的节点指针，应被视为只读，修改 `value` 违反 const 契约。

#### 关闭日期
2026-07-04