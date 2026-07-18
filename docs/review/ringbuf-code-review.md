# Ringbuf 代码审查报告

**日期**: 2026-07-04
**审查者**: claude-code-review

---

## 修复历史

### RINGBUF-001: `ns_ringbuf_fix` 有符号整数溢出 UB

- **修复 commit**: 当前 diff
- **修复说明**: `size` 类型从 `int32_t` 改为 `uint32_t`；`ns_ringbuf_fix` 宏改为 `size * 2u`；
  `ns_ringbuf_size`/`ns_ringbuf_free_size` 改用纯 `uint32_t` 比较（`ns_ringbuf_data_size` 辅助函数）。
  初始化增加 `size > UINT32_MAX / 3` 上界检查。

### RINGBUF-002/003: size 上限约束改为 UINT32_MAX/3 消除中间加法溢出

- **修复 commit**: 当前 diff
- **修复说明**: `ns_ringbuf_init` 的 size 上界从 `UINT32_MAX/2` 收紧为 `UINT32_MAX/3`。
  在此上限下 `w + inc < 3*size ≤ UINT32_MAX`，天然消除所有中间 uint32_t 加法溢出。
  同时修复 RINGBUF-003（`w/r + offset` 溢出）的同一根因。

### RINGBUF-004: 文档说 len 是 int32_t 但代码已是 uint32_t

- **修复 commit**: 当前 diff
- **修复说明**: 头文件 `@note` 更新为"写入/读取函数的 `len` 参数为 uint32_t，单次操作上限由 `size` 决定"。
  size 上限同步更新为 `UINT32_MAX/3`。

### RINGBUF-005: ns_ringbuf_data_size 注释含内部开发笔记

- **修复 commit**: 当前 diff
- **修复说明**: 重写注释为可观测行为描述。删除"无需有符号溢出技巧"内部比较行，更新 size 上限为 `UINT32_MAX/3`。

### RINGBUF-005b: 单读单写无锁缺内存屏障

- **修复 commit**: 当前 diff
- **修复说明**: 在 `ns_ringbuf_write` / `write_skip` / `read` / `read_skip` / `clear` 发布 w/r 之前各加一处 `ns_memory_order_release_barrier()`，方向与 `eh_ringbuf.c` 对齐——保证 w/r 之前的所有数据 memcpy 与旁路写对读者线程可见，并约束 loadstore / storestore 重排。`draft_write` / `peek` / `peek_copy` 不修改 w/r，沿用 eventhub 不加屏障的处理。`reset` / 查值函数同样不加（eventhub 同样不加）。

### RINGBUF-006: `ns_ringbuf_clear` 的非原子访问

- **修复 commit**: 当前 diff
- **修复说明**: 文档标注 "**必须由读者线程调用**"，写者线程调用会破坏单读单写安全假设。

### RINGBUF-007: peek 的 *len 是 int32_t* 而其余是 uint32_t

- **修复 commit**: 当前 diff
- **修复说明**: `ns_ringbuf_peek` 的 `len` 参数改为 `uint32_t*`，消除内部 4 处强制类型转换。

### RINGBUF-008: draft_write 返回 -1 与其他函数不一致

- **修复 commit**: 当前 diff
- **修复说明**: 改用新增状态码 `NS_E_RANGE` 作为 offset 越界返回值。同时 `nanosig_status.h` 新增 `NS_E_PARAM`、`NS_E_RANGE`，`ringbuf.h` 文档同步更新。

### RINGBUF-009: (int32_t)wl 返回值强制转换依赖 size ≤ INT32_MAX

- **修复 commit**: 当前 diff
- **修复说明**: 在 `src/ds/ns_ringbuf.c` 顶端加 `_Static_assert((UINT32_MAX / 3u) <= (uint32_t)INT32_MAX, ...)`，把"(int32_t)wl 不截断"这一不变量固化在编译期。任何放宽 size 上限到 INT32_MAX 以上的企图都会触发编译错误。同步在 `include/nanosig/nanosig_ringbuf.h` 的结构体 @note 写明第三条："由于 UINT32_MAX/3 < INT32_MAX，int32_t 返回值不会因 size 截断为负"。

### RINGBUF-010: w/r 冗余重载

- **修复 commit**: 当前 diff
- **修复说明**: 在 `ns_ringbuf_write` / `write_skip` / `read` / `read_skip` / `draft_write` / `peek` / `peek_copy` 七个函数内统一把 `ringbuf->w` 和 `ringbuf->r` 预加载到局部变量，再把这些值（而非 `ringbuf->*` 字段本身）传给 `ns_ringbuf_data_size`、`% size`、读指针算术等后续运算。消除"调用 free/size 内部重复加载 + 自己再独立 % size"的双重内存读取。查值函数 `ns_ringbuf_size` / `free_size` 不动。

### RINGBUF-014: NULL→0 契约变更破坏 backward compatibility

- **修复 commit**: 当前 diff
- **修复说明**: `ns_ringbuf_size` / `ns_ringbuf_free_size` / `ns_ringbuf_total_size` 三个查询函数从 `uint32_t` 统一为 `int32_t` 返回。NULL 输入时返回 -1，正常时返回非负字节数。头文件 `@note` 与函数 `@return` 同步更新，文档化 NULL 行为；`_Static_assert` 注释扩展到覆盖 size 族返回值。

### RINGBUF-017: `ns_ringbuf_data_size` 过度耦合

- **修复 commit**: 当前 diff
- **修复说明**: `ns_ringbuf_data_size` 签名从 `(const ns_ringbuf_t *ringbuf, uint32_t w, uint32_t r)` 改为 `(uint32_t size, uint32_t w, uint32_t r)`，消除与 ringbuf 结构体的耦合；返回值从 `uint32_t` 改为 `int32_t`（与 nanosig_status.h 风格一致）。9 处调用点全部同步更新，在 `uint32_t` 上下文中使用显式 cast 消除 -Wsign-conversion 警告。

---

## 现在打开的问题

### RINGBUF-023: `ns_ringbuf_peek` 不拒绝 `*len == 0`，与其他函数不一致

- **状态**: 打开
- **严重度**: 🟢 较低
- **类型**: design

#### 问题描述
`ns_ringbuf_peek` 当调用方传入 `*len = 0` 时，函数不拒绝，而是返回内部缓冲区指针和可用连续字节数。这与 `ns_ringbuf_read`/`ns_ringbuf_peek_copy`/`ns_ringbuf_write` 的 `len == 0 → return 0` guard 不一致。头文件文档 `@param len [in] 需要读取的字节数` 暗示 `*len = 0` 应返回 NULL。

#### review 建议
在 `ns_ringbuf_peek` 中 `rl = *len;` 之后增加 `if(rl == 0u) return NULL;` guard。若"探测可用连续字节数"是有意设计，应在头文件中显式文档化。

#### 作者建议
（待作者补充）

#### 可重现的失败场景
```c
uint32_t want = 0;
const uint8_t *p = ns_ringbuf_peek(&rb, 0, buf, &want);
// 期望: p == NULL, want 不变
// 实际: p != NULL (指向内部缓冲区), want = 5
```

#### 定位
src/ds/ns_ringbuf.c:223-224

---

### RINGBUF-024: `peek`/`peek_copy` 绕回路径及 `draft_write` 非零 offset 路径无测试覆盖

- **状态**: 打开
- **严重度**: 🟡 中
- **类型**: test

#### 问题描述
以下关键路径完全未测试：
1. `ns_ringbuf_peek` 绕回路径（`rl > read_size_first_max` 分支）
2. `ns_ringbuf_peek_copy` 绕回路径
3. `ns_ringbuf_draft_write` 非零 offset
4. `ns_ringbuf_peek` 数据不足返回 NULL 路径

#### review 建议
在 `test_ds_ringbuf.c` 中增加绕回测试、数据不足测试、draft_write 非零 offset 测试。

#### 作者建议
（待作者补充）

#### 定位
test/unit/test_ds_ringbuf.c（缺失测试用例）
src/ds/ns_ringbuf.c:220-238
src/ds/ns_ringbuf.c:260-268
src/ds/ns_ringbuf.c:128

---

## 现在关闭的问题

### RINGBUF-011: SPSC 内存序——release fence + plain store 不构成发布语义 (HIGH)

- **状态**: 关闭-已拒绝
- **严重度**: 🟠 高
- **定位**: `src/ds/ns_ringbuf.c:105-106, 154-155, 186-187, 203-204, 278-279`

#### 作者建议
1. 我对 fence 的理解：release fence 约束所有后续 store，不分 plain/atomic。C11 禁止 fence 后任意 store 重排到 fence 前。所以 writer 侧 `data → fence → w store` 的顺序是安全的。
2. reader 侧不需要 acquire：读到旧 w 只意味着 reader 处理旧数据，不会访问未完成的内存。读到新 w 时 fence 已保证数据可见。
3. ARM 上 aligned uint32_t load/store 是 single-copy atomic，不会撕裂。
4. C11 标准层的数据竞争"UB"在这个 SPSC 模式下实际行为可预测。

→ reviewer 无反驳，确认不是 bug。

#### 关闭原因
作者仲裁：SPSC 中 release fence + plain store 模式正确。reader 侧读到旧 w 不影响正确性（只意味着落后），读到新 w 时 fence 已保证数据可见。ARM 上 aligned uint32_t 无撕裂。关闭-已拒绝。

- **关闭日期**: 2026-07-04

### RINGBUF-017: `ns_ringbuf_data_size` 过度耦合 (LOW) `cleanup`

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低
- **定位**: `src/ds/ns_ringbuf.c:56-61`
- **关闭原因**: 签名改为 `(uint32_t size, uint32_t w, uint32_t r)`，返回值改为 `int32_t`，消除结构体耦合。详见修复历史。
- **关闭日期**: 2026-07-04

### RINGBUF-018: `draft_write` 缺少 barrier 注释 (LOW) `cleanup`

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低
- **定位**: `src/ds/ns_ringbuf.c:132-137`
- **关闭原因**: 注释风格问题。review agent 不再提同类 cleanup。作者确认不修。
- **关闭日期**: 2026-07-04

### RINGBUF-019: `_Static_assert` 耦合两个不变量 (LOW) `design`

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低
- **定位**: `src/ds/ns_ringbuf.c:23-24`
- **关闭原因**: 设计层面偏好，当前实现无正确性问题。review agent 不再提同类建议。作者确认不修。
- **关闭日期**: 2026-07-04

### RINGBUF-020: memcpy wrap/nowrap 分支重复 5 次 (LOW) `cleanup`

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低
- **定位**: `src/ds/ns_ringbuf.c:97-102, 132-137, 178-183, 230-238, 264-269`
- **关闭原因**: 代码组织优化，不影响功能。review agent 不再提同类 cleanup。作者确认不修。
- **关闭日期**: 2026-07-04

### RINGBUF-021: `ns_ringbuf_size`/`free_size` 应 inline (LOW) `cleanup`

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低
- **定位**: `src/ds/ns_ringbuf.c:63-76`
- **关闭原因**: 性能微优化，不影响正确性。review agent 不再提同类 cleanup。作者确认不修。
- **关闭日期**: 2026-07-04

### RINGBUF-022: header @note 溢出实现细节 (LOW) `doc`

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低
- **定位**: `include/nanosig/nanosig_ringbuf.h:24-33`
- **关闭原因**: 文档风格偏好，当前 doc 不影响使用者理解。review agent 不再提同类 doc 问题。作者确认不修。
- **关闭日期**: 2026-07-04

### RINGBUF-002: `ns_ringbuf_fix` 宏加法溢出（size > 1.43 GiB）

- **状态**: 关闭-已修复
- **严重度**: 🔴 关键
- **关闭原因**: init 已增加 `size ≤ UINT32_MAX/3` 上限检查，`w + wl < 3*size ≤ UINT32_MAX` 不再溢出。
- **关闭日期**: 2026-07-04

### RINGBUF-003: 读写位置 + offset 溢出（peek/peek_copy/draft_write）

- **状态**: 关闭-已修复
- **严重度**: 🔴 关键
- **关闭原因**: 同 RINGBUF-002 根因，被 init 的 `size ≤ UINT32_MAX/3` 上限检查封住。
- **关闭日期**: 2026-07-04

### RINGBUF-004: 文档说 len 是 int32_t 但代码已是 uint32_t

- **状态**: 关闭-已修复
- **严重度**: 🟡 中
- **关闭原因**: 头文件 `@note` 更新，size 上限同步更新为 `UINT32_MAX/3`。
- **关闭日期**: 2026-07-04

### RINGBUF-006: `ns_ringbuf_clear` 的非原子访问

- **状态**: 关闭-已修复
- **严重度**: 🟡 中
- **关闭原因**: 文档标注 reader-thread-only。
- **关闭日期**: 2026-07-04

### RINGBUF-007: peek 的 *len 是 int32_t* 而其余是 uint32_t

- **状态**: 关闭-已修复
- **严重度**: 🟡 中
- **关闭原因**: `peek` 的 `*len` 改为 `uint32_t*`。
- **关闭日期**: 2026-07-04

### RINGBUF-008: draft_write 返回 -1 与其他函数不一致

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低
- **关闭原因**: 改用 `NS_E_RANGE` 替代硬编码 -1，属于 nanosig 状态码体系。
- **关闭日期**: 2026-07-04

### RINGBUF-009: (int32_t)wl 返回值强制转换依赖 size ≤ INT32_MAX

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低
- **关闭原因**: 编译期静态断言 + 头文件 @note 双重保证。详见修复历史。
- **关闭日期**: 2026-07-04

### RINGBUF-010: w/r 冗余重载

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低
- **关闭原因**: 七个内部读/写函数全部预加载 w/r 到局部变量，消除双重内存读取。
- **关闭日期**: 2026-07-04

### RINGBUF-014: NULL→0 契约变更破坏 backward compatibility

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低
- **关闭原因**: `ns_ringbuf_size`/`free_size`/`total_size` 改为 `int32_t` 返回，NULL 时返回 -1；头文件文档同步标注 NULL 行为。
- **关闭日期**: 2026-07-04
