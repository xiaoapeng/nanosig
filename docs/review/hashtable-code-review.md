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

### HASHTBL-101: `ns_hashtbl_for_each_with_string_safe` 前缀匹配假阳性

- **修复日期**: 2026-08-09
- **修复 commit**: working tree (待提交)
- **修复方式**: 遍历宏过滤条件改为精确匹配——`key_len == strlen(string) && memcmp(...)`，长度 O(1) 短路 + memcmp 块比较，与 find_with_string / for_each_with_key_safe 语义统一。
- **影响文件**: `include/nanosig/nanosig_hashtbl.h`、`test/unit/test_ds_hashtable.c`

### HASHTBL-102: 字符串工厂/查找无 NULL 键守卫

- **修复日期**: 2026-08-09
- **修复 commit**: working tree (待提交)
- **修复方式**: 两个字符串入口加 NULL 守卫——工厂返回 NULL、find_with_string 返回 NS_E_INVAL，与 ns_hash_string 对称。
- **影响文件**: `src/ds/ns_hashtbl.c`、`include/nanosig/nanosig_hashtbl.h`、`test/unit/test_ds_hashtable.c`

### HASHTBL-103: 极小 load_factor 时 threshold==0 导致每次 insert 都 resize

- **修复日期**: 2026-08-09
- **修复 commit**: working tree (待提交)
- **修复方式**: create 中 threshold 钳制下限到 1，消除每次 insert 都 resize 的病态（摊还 O(1)）。
- **影响文件**: `src/ds/ns_hashtbl.c`、`test/unit/test_ds_hashtable.c`


## 现在打开的问题

（无）

---

## 现在关闭的问题

> 注：2026-08-08 哈希表按 eventhub_os `eh_hashtbl` 重新移植，API 从 `ns_hashtable_*`（字符串键指针式、调用方拥有存储）改为 `ns_hashtbl_*`（二进制键 KV 内联、库管理内存、自动扩容 + 渐进式重建）。旧 API 条目见修复历史与下方关闭区。

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
### HASHTBL-103: 极小 load_factor 时 `threshold == 0` 导致每次 insert 都 resize `perf`

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低
- **类型**: perf

#### 问题描述
`0 < load_factor < 1/16` 时，`(unsigned)(16 * load_factor)` 截断为 0，`count + 1 >= 0` 恒真，每次 insert 都触发扩容（O(n) per insert，表无界增长）。不产生数据丢失，但性能病态。上游 `eh_hashtbl` 行为相同。`create` 的 NaN/上限钳制守卫未覆盖此下限。

#### review 建议
将 `threshold` 下限钳制到 `>= 1`（如 `MAX(threshold, 1u)`）。

#### 作者建议
（2026-08-09 作者修复：`ns_hashtbl_create` 中 threshold 计算后钳制下限到 1——`if(threshold == 0) threshold = 1;`，消除 0 < load_factor < 1/16 时 threshold==0 → 每次 insert 都 resize（resize 内 `threshold <<= 1` 恒 0 → 表无界增长）的病态。回归用例 test_tiny_load_factor_bounded：create(0.01f) 插入 100 节点后断言 mask < 4096；反向验证：无钳制时该用例直接 OOM 被杀（exit 137），证明病态严重。）

#### 定位
src/ds/ns_hashtbl.c:366

---


### HASHTBL-102: 字符串工厂/查找无 NULL 键守卫 `bug`

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低
- **类型**: bug

#### 问题描述
`ns_hashtbl_node_new_with_string_refresh` 和 `ns_hashtbl_find_with_string` 将键直接传入 `fnv1a_str`，`key == NULL` 时会解引用崩溃。上游 `eh_hashtbl` 行为相同（仅 `ns_hash_string` 在移植时补了 NULL 守卫）。未文档化的输入 UB，与 C API 惯例一致。

#### review 建议
为对称性给这两个函数加 `if(key == NULL)` 守卫，或在头文件文档化非 NULL 前置条件。

#### 作者建议
（2026-08-09 作者修复：`ns_hashtbl_node_new_with_string_refresh` 入口加 `if(key == NULL) return NULL;`；`ns_hashtbl_find_with_string` 入口加 `if(key_str == NULL) return NS_E_INVAL;`，与 ns_hash_string 的 NULL 守卫对称。头文件 @param/@return 已同步文档化 NULL 行为。回归断言（test_find_with_string_null_out 内追加）：工厂 NULL 键返回 NULL、查找 NULL 键返回 NS_E_INVAL 且不崩溃。）

#### 定位
src/ds/ns_hashtbl.c:178
src/ds/ns_hashtbl.c:333

---


### HASHTBL-101: `ns_hashtbl_for_each_with_string_safe` 前缀匹配假阳性 `bug`

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低
- **类型**: bug

#### 问题描述
`ns_hashtbl_for_each_with_string_safe` 的过滤条件是 `strncmp(node_key, string, node_key_len)`——只比较节点的键长度。搜索较长字符串时，会匹配键是其严格前缀的节点（如搜索 "abcd" 会遍历到键为 "abc" 的节点）。专用查找 `ns_hashtbl_find_with_string` 用 `memcmp` 按搜索串长度比较，无此问题；仅遍历宏存在此行为。

#### review 建议
在头文件文档化前缀匹配语义，或改为比较 `ns_hashtbl_node_key_len(node) == strlen(string) && memcmp(...)` 做精确匹配。

#### 作者建议
（2026-08-09 作者修复：遍历宏过滤条件改为精确匹配——`ns_hashtbl_node_key_len(node) == strlen(string) && memcmp(...) == 0`，长度 O(1) 短路在前、memcmp 块比较在后，与 ns_hashtbl_find_with_string / for_each_with_key_safe 语义一致，消除前缀假阳性且比较更快。头文件 @brief 已更新为"精确匹配，不做前缀匹配"。回归用例：键 "aa" 与搜索串 "aaz" 的 FNV-1a 哈希在各掩码下均同桶，修复前遍历 "aaz" 误出 "aa"（已反向验证用例可抓 bug）。）

#### 定位
include/nanosig/nanosig_hashtbl.h:284（宏定义）
src/ds/ns_hashtbl.c:342（find_with_string，无此问题，对比参考）

---


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