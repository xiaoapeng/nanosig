# MPSC Record Ring 代码审查报告

**日期**: 2026-07-04
**审查者**: claude-code-review

---

## 修复历史

### MPSC-001: MSVC meta 位布局不兼容

- **修复 commit**: 当前 diff（`__SIZEOF_SIZE_T__ == 8u` → `SIZE_MAX > UINT32_MAX`）
- **修复说明**: MSVC 不定义 `__SIZEOF_SIZE_T__`，64 位 MSVC 选错 32 位布局。改用标准 `SIZE_MAX` 宏，兼容 GCC/Clang/MSVC。
- **来源**: 深度代码审查 3.1

### MPSC-004: 文档与实现不一致：`part_count == 0`

- **修复 commit**: 当前 diff
- **修复说明**: 头文件 `@param part_count` 从"必须大于 0"改为"允许为 0（推送零大小记录）"；`@retval NS_E_INVAL` 删除 `part_count 为 0` 的表述。文档与实现统一：`part_count == 0` 允许，行为等价于 `ns_mpsc_record_ring_try_push(ring, NULL, 0u)`。

### MPSC-002: 注释语法问题 "perform 3 step"

- **修复 commit**: 当前 diff（注释重写为"下面的顺序是高性能铁律，不能动"）
- **修复说明**: 英语语法问题随注释整体改写解决。
- **来源**: 深度代码审查 7.2

### MPSC-013: 32 位构建下 meta 的 16 位 stride 字段会静默截断大记录

- **修复 commit**: 待合并分支 fix/mpsc-013-stride-mask（PR #21）
- **修复说明**: 在 `capacity_is_valid`（src/ds/ns_mpsc_record_ring.c:115）新增 `((capacity / 2u) <= NS_MPSC_META_STRIDE_MASK)` 硬约束，32 位目标下 capacity/2 超过 0xFFFF 时 init 直接返回 NS_E_INVAL，避免 stride 被静默截断导致记录错位或 stride 解码为 0 使环永久不可用。头文件 docstring（include/nanosig/nanosig_mpsc_record_ring.h:82-84）写明 32 位容量上限。单元测试新增 NS_CAPACITY_65536（应成功）与 NS_CAPACITY_131072（32 位下应拒绝）断言。
- **来源**: mpsc-record-ring-code-review.md MPSC-013

---

## 现在打开的问题

### MPSC-011: `MPSC_MEMORY_ORDER.md` 描述已删除的固定容量 MPSC 队列，未文档化 record ring 的内存序协议

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低
- **类型**: doc

#### 问题描述
`docs/MPSC_MEMORY_ORDER.md` 描述的是已删除的固定容量 MPSC 队列（`nanosig_mpsc.h`），包括 `ns_mpsc_try_push` / `ns_mpsc_try_pop` 等已不存在的函数。当前仓库只有 MPSC record ring，其内存序协议（reserve_pos CAS + valid-bit release + consumer acquire）仅在内联代码注释中描述，无独立文档。

#### review 建议
创建 `docs/MPSC_RECORD_RING_MEMORY_ORDER.md` 或更新 `MPSC_MEMORY_ORDER.md` 添加 record ring 章节，文档化三阶段发布链。

#### 作者建议
已重写 `docs/MPSC_MEMORY_ORDER.md`，删除旧版固定容量 MPSC 队列描述，替换为 MPSC record ring 的三阶段发布链（reserve → write → commit）和完整内存序协议文档。
→ 关闭-已修复

#### 关闭原因
`docs/MPSC_MEMORY_ORDER.md` 已重写为 record ring 内存序协议文档。

- 关闭日期: 2026-08-02
- 状态: 关闭-已修复

#### 定位
docs/MPSC_MEMORY_ORDER.md（全文）

### MPSC-012: 生产者死在 reserve 与 commit 之间会永久卡死整个环

- **状态**: 打开
- **严重度**: 🟠 高
- **类型**: bug

#### 问题描述
生产的"独占权"由 reserve_pos 决定，但空间/layout 判定用的是 write_pos/read_pos：plan_push 只按 write_pos-read_pos 判断 free_capacity，而 CAS 的 expected 是 write_pos（要求 reserve_pos == write_pos 才能抢占）。两者一旦分叉就再也回不来——没有任何代码路径能把 reserve_pos 拉回 write_pos。

若某生产者在 CAS 成功之后、store write_pos 之前终止（线程被取消、abort 式退出、或永久得不到调度），reserve_pos 会永久领先 write_pos，产生两个后果：

1. 生产者侧挂死：之后任何线程调用 try_pushv 都在重试循环里无限自旋。每次迭代 plan_push 都会成功（free_capacity 仍按未变的 write_pos 计算），CAS 却永远失败，函数不返回。这直接违反头文件对 try_push/try_pushv 的承诺"非阻塞；空间不足时立即返回 NS_E_QUEUE_FULL，不自旋"。调用方（如 ns_signal_emit 持锁 emit）会连带把持有者一起挂死。
2. 消费者侧永久空转：write_pos 卡在泄漏点不再推进，消费者观察到的 used 恒为 0，永远返回 NS_E_EMPTY（即使后续还有活着的生产者，它们也全部被 1 卡住）。

对称地，若生产者在 store write_pos 之后、写 payload/release-store meta 之前终止，write_pos 已越过该槽位，头部槽位的 valid 仍为 0：消费者在 try_acquire 里通过 used != 0 检查后落到 valid=0 分支，返回 NS_E_EMPTY，此后 read_pos 永远推不过这一槽位，环同样永久不可用。

注：docs/MPSC_MEMORY_ORDER.md 的"关键不变量"承认 reserve_pos >= write_pos（即这种分叉是设计上允许的状态），但头文件与任何文档都没有声明"reserve_pos > write_pos 时环会彻底不可用"，也没有把"生产者线程不得在 reserve 与 commit 之间终止"写成 @pre。

#### review 建议
注意本条与已关闭的 MPSC-003 不是同一问题：MPSC-003 讨论的是 CAS 失败后 read_pos 未刷新（已确认为误报），本条讨论的是重试循环在 reserve_pos != write_pos 时永不终止、环被永久卡死，根因与修复方向都不同。

按代价从低到高，建议至少做前两项：
1. 把该前提显式写进公共头（try_push/try_pushv 的 @pre）："调用 try_pushv 的生产者线程必须在 reserve 与 commit 之间保持存活，不得在函数中途被取消/终止"；同时明确"环是尽力而为的排队设施，线程非正常终止会导致该环不可用"。
2. 把 CAS 重试循环上界化：健康路径下 CAS 只需一次成功（CAS 成功条件本身就是 reserve_pos == write_pos，而在途 owner 的窗口只有 CAS 到 store write_pos 之间的几条指令），因此可设一个较宽的迭代上限（例如 1024 次或 8×已知生产者数），超限返回 NS_E_CORRUPT/NS_E_BUSY。这样把"无限挂死"变成调用方可见的错误，而不是静默卡死。注意这是启发式阈值，不能代替前提文档化。
3. 若要真正容错，需要给"armed 但未提交"的槽位引入可回收机制（例如额外的 publish 序号或超时回收策略），成本高，不建议本轮做。

#### 作者建议
（待作者补充）

#### 可重现的失败场景
形态 A（生产者侧无限自旋，独立 harness 实测复现）：init(capacity=128) 成功后，模拟一个在 CAS 成功后死亡的生产者——只把 reserve_pos 置为 24（write_pos 仍为 0）；随后任意线程调用 try_push(8 字节 payload, size=0xAAAAAAAA...)。预期：立即返回 NS_E_QUEUE_FULL 或错误码；实测：函数在 ns_mpsc_record_ring.c:383 的 CAS 重试循环内不返回，2 秒看门狗线程打印告警后强杀进程。因为 write_pos 恒为 0、reserve_pos 恒为 24，循环条件 `expected = write_pos` 永远不成立，重试必然无限。

形态 B（消费者侧永久 E_EMPTY，实测复现）：init(capacity=128) → 连续 push 若干记录（全成功）→ 模拟某生产者在 store write_pos 之后死亡：把 write_pos 置为 16（越过 write_pos=0 处那条记录的槽位）、该槽位 meta 保持 0 → 反复 try_acquire 均返回 NS_E_EMPTY（NS_E_EMPTY == -8），write_pos=16、read_pos=0、free=32，之后无法再恢复。

#### 定位
src/ds/ns_mpsc_record_ring.c:367
src/ds/ns_mpsc_record_ring.c:379
src/ds/ns_mpsc_record_ring.c:383
src/ds/ns_mpsc_record_ring.c:407
src/ds/ns_mpsc_record_ring.c:452
include/nanosig/nanosig_mpsc_record_ring.h:132
include/nanosig/nanosig_mpsc_record_ring.h:156
docs/MPSC_MEMORY_ORDER.md:107

---


## 现在关闭的问题

### MPSC-014: MPSC_MEMORY_ORDER.md 的 reserve 阶段伪码与实现不是同一个算法

- **状态**: 关闭-已修复
- **严重度**: 🟡 中
- **类型**: doc

#### 问题描述
docs/MPSC_MEMORY_ORDER.md 的"阶段 1：预留"伪码按 `reserve_pos` 自身 CAS，与实现（基于 `write_pos` acquire-load，CAS expected 为 `write_pos`）不一致。

#### 关闭原因
已在 docs/MPSC_MEMORY_ORDER.md 中将阶段 1 伪码改写为与实现一致的形式（`write_pos = load(write_pos, acquire)`，CAS expected 为 `write_pos`），并补了 `reserve_pos > write_pos` 表示存在在途生产者、不被自动回收的说明。文档与实现现在一致。

- 关闭日期: 2026-10-19
- 状态: 关闭-已修复

---
### MPSC-003: `try_pushv` CAS 重试循环未刷新 `read_pos`

- **状态**: 关闭-已拒绝
- **严重度**: 🟡 中

#### 关闭原因
误报。代码第 365 行的 `read_pos = ns_atomic_load_explicit(...)` 实际位于 `for(;;)` 循环体内（与 `write_pos` 并列加载），CAS 失败后下一次迭代会重新读取。不存在"陈旧 read_pos"的问题。

#### 关闭日期
2026-07-04

---

### MPSC-005: init 循环用原子 release-store 逐槽清零

- **状态**: 关闭-已拒绝
- **严重度**: 🟡 中

#### 关闭原因
设计意图。`mark_uncommitted` 的原子 release-store 是文档化的防御性初始化策略，保证未提交 header 的 `valid=0` 在多线程下即时可见，杜绝消费者读取残留数据。改为 `memset` 在调用方已 zero-init 的 buffer 上等价，但失去"任意输入 buffer 都被正确初始化"的保证。

#### 关闭日期
2026-07-04

---

### MPSC-006: `release()` 上界检查使用 `>` 而非 `>=`

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低

#### 关闭原因
设计意图。`record_addr == storage_end` 表示"缓冲区末尾之后一个地址"，类似 C 数组的 one-past-end iterator 语义，永不解引用但作为合法哨兵存在。改为 `>=` 会拒绝合法边界地址。

#### 关闭日期
2026-07-04

---

### MPSC-007: `ns_capacity_t` 枚举限制容量范围

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低

#### 关闭原因
设计约束。`ns_capacity_t` 是枚举类型，受 C 标准约束须在 `int` 范围内，已在头文件文档中明确说明。改为 `size_t` 会破坏 API 二进制兼容性，且 1 GiB 上限已远超实际使用场景。

#### 关闭日期
2026-07-04

---

### MPSC-008: `plan_push` 从 `const` 参数强制转换 `ring`

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低

#### 关闭原因
内部静态辅助函数。`plan_push` 是仅在本文件内部使用的辅助函数，`const` 仅作为设计意图提示（不修改 ring 的逻辑字段），需要调用 `header_at` 时 cast 是不可避免的（非 const 重载会破坏 API 对称性）。

#### 关闭日期
2026-07-04

---

### MPSC-009: 注释使用非正式指令语气

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低

#### 关闭原因
风格偏好，非缺陷。热路径代码注释有意保持简洁以避免阅读者注意力的分散。如需详细解释，应放在头文件中的函数文档而非内联注释。

#### 关闭日期
2026-07-04

---

### MPSC-010: `free_capacity` 通过双重强制转换丢弃 `const`

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低

#### 关闭原因
C 语言限制。C 原子变量不可声明为 const 但物理上可修改。`free_capacity` 的 const cast 是读取原子值的必要操作，逻辑上仍为"观察而非修改"。分离读写路径会显著增加代码量而无实际收益。

#### 关闭日期
2026-07-04

### MPSC-004: 文档与实现不一致：`part_count == 0`

- **状态**: 关闭-已修复
- **严重度**: 🟡 中
- **关闭原因**: 头文件文档统一：`@param part_count` 改为"允许为 0（推送零大小记录）"，`@retval NS_E_INVAL` 删除 `part_count 为 0` 的表述。
- **关闭日期**: 2026-07-04
