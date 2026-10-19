---
review_schema: "nanosig-review-v1"
entries:
  - id: "MPSC-001"
    module: "mpsc-record-ring"
    status: "closed-fixed"
    severity: "unknown"
    type: "bug"
    section: "fixed-history"
    location: "docs/review/mpsc-record-ring-code-review.md#MPSC-001"
  - id: "MPSC-002"
    module: "mpsc-record-ring"
    status: "closed-fixed"
    severity: "unknown"
    type: "bug"
    section: "fixed-history"
    location: "docs/review/mpsc-record-ring-code-review.md#MPSC-002"
  - id: "MPSC-012"
    module: "mpsc-record-ring"
    status: "closed-rejected"
    severity: "high"
    type: "bug"
    section: "closed"
    closed_date: "2026-10-19"
    location: "docs/review/mpsc-record-ring-code-review.md#MPSC-012"
  - id: "MPSC-013"
    module: "mpsc-record-ring"
    status: "open"
    severity: "high"
    type: "bug"
    section: "open"
    location: "docs/review/mpsc-record-ring-code-review.md#MPSC-013"
    locations:
      - "src/ds/ns_mpsc_record_ring.c:51"
      - "src/ds/ns_mpsc_record_ring.c:61"
      - "src/ds/ns_mpsc_record_ring.c:115"
      - "src/ds/ns_mpsc_record_ring.c:326"
      - "src/ds/ns_mpsc_record_ring.c:454"
      - "include/nanosig/nanosig_mpsc_record_ring.h:120"
  - id: "MPSC-014"
    module: "mpsc-record-ring"
    status: "open"
    severity: "medium"
    type: "doc"
    section: "open"
    location: "docs/review/mpsc-record-ring-code-review.md#MPSC-014"
    locations:
      - "docs/MPSC_MEMORY_ORDER.md:35"
      - "docs/MPSC_MEMORY_ORDER.md:36"
      - "docs/MPSC_MEMORY_ORDER.md:107"
      - "src/ds/ns_mpsc_record_ring.c:372"
      - "src/ds/ns_mpsc_record_ring.c:380"
      - "src/ds/ns_mpsc_record_ring.c:383"
  - id: "MPSC-003"
    module: "mpsc-record-ring"
    status: "closed-rejected"
    severity: "medium"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-04"
    location: "docs/review/mpsc-record-ring-code-review.md#MPSC-003"
  - id: "MPSC-005"
    module: "mpsc-record-ring"
    status: "closed-rejected"
    severity: "medium"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-04"
    location: "docs/review/mpsc-record-ring-code-review.md#MPSC-005"
  - id: "MPSC-006"
    module: "mpsc-record-ring"
    status: "closed-rejected"
    severity: "low"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-04"
    location: "docs/review/mpsc-record-ring-code-review.md#MPSC-006"
  - id: "MPSC-007"
    module: "mpsc-record-ring"
    status: "closed-rejected"
    severity: "low"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-04"
    location: "docs/review/mpsc-record-ring-code-review.md#MPSC-007"
  - id: "MPSC-008"
    module: "mpsc-record-ring"
    status: "closed-rejected"
    severity: "low"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-04"
    location: "docs/review/mpsc-record-ring-code-review.md#MPSC-008"
  - id: "MPSC-009"
    module: "mpsc-record-ring"
    status: "closed-rejected"
    severity: "low"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-04"
    location: "docs/review/mpsc-record-ring-code-review.md#MPSC-009"
  - id: "MPSC-010"
    module: "mpsc-record-ring"
    status: "closed-rejected"
    severity: "low"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-04"
    location: "docs/review/mpsc-record-ring-code-review.md#MPSC-010"
  - id: "MPSC-004"
    module: "mpsc-record-ring"
    status: "closed-fixed"
    severity: "unknown"
    type: "unknown"
    section: "fixed-history"
    location: "docs/review/mpsc-record-ring-code-review.md#MPSC-004"
---

# MPSC Record Ring 代码审查报告

**日期**: 2026-07-04
**审查者**: claude-code-review

---

## 修复历史

<a id="MPSC-001"></a>
### MPSC-001: MSVC meta 位布局不兼容

- **修复 commit**: 当前 diff（`__SIZEOF_SIZE_T__ == 8u` → `SIZE_MAX > UINT32_MAX`）
- **修复说明**: MSVC 不定义 `__SIZEOF_SIZE_T__`，64 位 MSVC 选错 32 位布局。改用标准 `SIZE_MAX` 宏，兼容 GCC/Clang/MSVC。
- **来源**: 深度代码审查 3.1

<a id="MPSC-002"></a>
### MPSC-002: 注释语法问题 "perform 3 step"

- **修复 commit**: 当前 diff（注释重写为"下面的顺序是高性能铁律，不能动"）
- **修复说明**: 英语语法问题随注释整体改写解决。
- **来源**: 深度代码审查 7.2

<a id="MPSC-004"></a>
### MPSC-004: 文档与实现不一致：`part_count == 0`

- **修复 commit**: 当前 diff
- **修复说明**: 头文件 `@param part_count` 从"必须大于 0"改为"允许为 0（推送零大小记录）"；`@retval NS_E_INVAL` 删除 `part_count 为 0` 的表述。文档与实现统一：`part_count == 0` 允许，行为等价于 `ns_mpsc_record_ring_try_push(ring, NULL, 0u)`。

---

<a id="MPSC-004"></a>

---

---

## 现在打开的问题

<a id="MPSC-013"></a>
### MPSC-013: 32 位构建下 meta 的 16 位 stride 字段会静默截断大记录

- **状态**: 打开
- **严重度**: 🟠 高
- **类型**: bug

#### 问题描述
meta 的位宽随指针宽度切换：SIZE_MAX <= UINT32_MAX（32 位目标）时 stride 只有 16 位（0xFFFF），padding 8 位。但容量校验 capacity_is_valid 只要求"2 的幂、非 0、<= SIZE_MAX/2、>= 3*sizeof(header)"，完全没有约束 capacity/2 能落在 16 位 stride 里；max_record_size() = capacity/2 - align_up(sizeof(header)) 因此在 capacity >= 131072 时会超过 65535。

ns_mpsc_meta_encode 用 `total_size & NS_MPSC_META_STRIDE_MASK` 无检查地截断，于是 32 位目标上：

- total_size 非 65536 倍数时：消费者解出的 stride/payload_size 是错的（实测 capacity=262144、push 100000 字节 → acquire 返回 size=34464），read_pos 按错误的 stride 推进，记录内容与边界全部错位，free_capacity 长期偏小；
- total_size 恰为 65536 的倍数时（例如 capacity=131072 时用 max_record_size() 算出的 65528 字节满记录，total_size=65536）：stride 解码为 0，try_acquire 在 stride 检查处返回 NS_E_INVAL，release 同样返回 NS_E_INVAL，read_pos 永不推进 —— 环永久不可用。

头文件明确承诺 max_record_size 是"保证至少一条最大记录可以写入"的有效负载上限，32 位目标上该承诺不成立。真实触发需要 32 位编译（FreeRTOS 目标 MCXN947/Cortex-M33 是 32 位；CI 的 freertos job 跑的是 POSIX host，64 位，覆盖不到这个分支）。

#### review 建议
在 capacity_is_valid（或 init）里补一条"meta stride 可表示"的硬约束，例如要求 `((capacity / 2u) <= NS_MPSC_META_STRIDE_MASK)`（更精确地：align_up(sizeof(header) + max_record_size) <= NS_MPSC_META_STRIDE_MASK），不满足则 init 返回 NS_E_INVAL；或者在 32 位布局里重新分配位域（如 22 位 stride + 8 位 padding + 2 位 flag，上限 4 MiB），并同步更新 src 顶部的位布局注释。无论选哪种，都应在头文件 max_record_size 的 docstring 中写明 32 位目标的容量/记录上限，避免调用方按 64 位直觉选 NS_CAPACITY_262144 之类。

#### 作者建议
（待作者补充）

#### 可重现的失败场景
把 32 位布局分支强制选中（在 64 位主机上把 src/ds/ns_mpsc_record_ring.c:43 的 `#if SIZE_MAX > UINT32_MAX` 改为 `#if 0` 后单独编译）：
1. init(capacity=131072) → max_record_size()=65528 → try_push(payload, 65528) 返回 NS_OK → try_acquire 连续返回 NS_E_INVAL（== -3，stride 解码为 0）；此时 write_pos=65536、read_pos=0、free=65536，环永久不可用（release 也无法推进 read_pos）。
2. init(capacity=262144) → try_push(payload, 100000) 返回 NS_OK → try_acquire 返回 NS_OK 但 out_size=34464（真实 100000），调用方按 34464 解释 payload 得到错误数据；随后 release 只释放 34464，free_capacity 停在 196608 而非 262144。
（两个用例均为独立 harness 实测输出。）

#### 定位
src/ds/ns_mpsc_record_ring.c:51
src/ds/ns_mpsc_record_ring.c:61
src/ds/ns_mpsc_record_ring.c:115
src/ds/ns_mpsc_record_ring.c:326
src/ds/ns_mpsc_record_ring.c:454
include/nanosig/nanosig_mpsc_record_ring.h:120

---

<a id="MPSC-014"></a>
### MPSC-014: MPSC_MEMORY_ORDER.md 的 reserve 阶段伪码与实现不是同一个算法

- **状态**: 打开
- **严重度**: 🟡 中
- **类型**: doc

#### 问题描述
docs/MPSC_MEMORY_ORDER.md 的"阶段 1：预留"把抢占写成基于 reserve_pos 自身：

    reserve_pos = atomic_load(&ring->reserve_pos, relaxed);
    CAS(reserve_pos, reserve_pos + publish_size)

实现（src/ds/ns_mpsc_record_ring.c:372-391）加载的是 write_pos（acquire），CAS 的 expected 是 write_pos，publish_size 与记录位置也全部由基于 write_pos 的 plan_push 算出。两者语义不同：文档版本按"自己读到的 reserve_pos"继续推进，在 reserve_pos > write_pos（存在在途生产者）时会让两个生产者写到重叠区域；实现版本要求 reserve_pos == write_pos 才能抢占，即"独占整环写入"。文档 line 107 又把 reserve_pos >= write_pos 列为关键不变量，与文档 line 35-38 的伪码互相矛盾（按文档伪码，CAS 成功后两者应始终相等）。

按文档伪码去"修正"实现会重新引入重叠写缺陷；这也是 MPSC-012 很难从文档侧发现的原因（文档没有描述 reserve_pos 领先且不会被回收的状态）。

#### review 建议
把文档 line 34-38 的伪码改成与实现一致：`write_pos = load(write_pos, acquire); CAS(reserve_pos, expected=write_pos, desired=write_pos+publish_size, acq_rel, relaxed)`，并补一句为什么必须基于 write_pos（抢占语义是"拿到整环的独占写入权"，而非"接在自己读到的值后面"）。同时把"reserve_pos > write_pos 表示存在在途生产者；该状态不被自动回收"写进不变量一节，作为 MPSC-012 文档化前提的落点。

#### 作者建议
（待作者补充）

#### 可重现的失败场景
无需运行。按文档 line 36-38 重写 try_pushv 的 CAS（expected 改用 reserve_pos 的 relaxed 读值）：两个生产者 A/B 同时读到 reserve_pos=W（此时 write_pos=W），各自 CAS 成功后分别从 W 与 W+size_A 开始写自己的记录，而两者都认为自己拥有 [W, W+size_x) 的写入权 —— 后写者覆盖先写者的头部与 payload，消费者会读到字段互相矛盾的记录。现有并发测试只能偶发暴露（写入窗口极短）。

#### 定位
docs/MPSC_MEMORY_ORDER.md:35
docs/MPSC_MEMORY_ORDER.md:36
docs/MPSC_MEMORY_ORDER.md:107
src/ds/ns_mpsc_record_ring.c:372
src/ds/ns_mpsc_record_ring.c:380
src/ds/ns_mpsc_record_ring.c:383

---

## 现在关闭的问题

<a id="MPSC-003"></a>
---
<a id="MPSC-012"></a>
### MPSC-012: 生产者死在 reserve 与 commit 之间会永久卡死整个环

- **状态**: 关闭-已拒绝
- **严重度**: 🟠 高
- **类型**: bug
- **关闭原因**: 合同前提保护：生产者线程必须在 `reserve` 与 `commit` 之间保持存活，属调用方文档化前提。环卡死仅在调用方违反该前提时发生（线程取消/abort/永久不调度），不属库缺陷。已在 `include/nanosig/nanosig_mpsc_record_ring.h` 的 `try_push/try_pushv` docstring 中补写 `@pre` 前置条件。
- **关闭日期**: 2026-10-19
<a id="MPSC-003"></a>
### MPSC-003: `try_pushv` CAS 重试循环未刷新 `read_pos`

- **状态**: 关闭-已拒绝
- **严重度**: 🟡 中

#### 关闭原因
误报。代码第 365 行的 `read_pos = ns_atomic_load_explicit(...)` 实际位于 `for(;;)` 循环体内（与 `write_pos` 并列加载），CAS 失败后下一次迭代会重新读取。不存在"陈旧 read_pos"的问题。

#### 关闭日期
2026-07-04

---

<a id="MPSC-005"></a>
### MPSC-005: init 循环用原子 release-store 逐槽清零

- **状态**: 关闭-已拒绝
- **严重度**: 🟡 中

#### 关闭原因
设计意图。`mark_uncommitted` 的原子 release-store 是文档化的防御性初始化策略，保证未提交 header 的 `valid=0` 在多线程下即时可见，杜绝消费者读取残留数据。改为 `memset` 在调用方已 zero-init 的 buffer 上等价，但失去"任意输入 buffer 都被正确初始化"的保证。

#### 关闭日期
2026-07-04

---

<a id="MPSC-006"></a>
### MPSC-006: `release()` 上界检查使用 `>` 而非 `>=`

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低

#### 关闭原因
设计意图。`record_addr == storage_end` 表示"缓冲区末尾之后一个地址"，类似 C 数组的 one-past-end iterator 语义，永不解引用但作为合法哨兵存在。改为 `>=` 会拒绝合法边界地址。

#### 关闭日期
2026-07-04

---

<a id="MPSC-007"></a>
### MPSC-007: `ns_capacity_t` 枚举限制容量范围

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低

#### 关闭原因
设计约束。`ns_capacity_t` 是枚举类型，受 C 标准约束须在 `int` 范围内，已在头文件文档中明确说明。改为 `size_t` 会破坏 API 二进制兼容性，且 1 GiB 上限已远超实际使用场景。

#### 关闭日期
2026-07-04

---

<a id="MPSC-008"></a>
### MPSC-008: `plan_push` 从 `const` 参数强制转换 `ring`

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低

#### 关闭原因
内部静态辅助函数。`plan_push` 是仅在本文件内部使用的辅助函数，`const` 仅作为设计意图提示（不修改 ring 的逻辑字段），需要调用 `header_at` 时 cast 是不可避免的（非 const 重载会破坏 API 对称性）。

#### 关闭日期
2026-07-04

---

<a id="MPSC-009"></a>
### MPSC-009: 注释使用非正式指令语气

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低

#### 关闭原因
风格偏好，非缺陷。热路径代码注释有意保持简洁以避免阅读者注意力的分散。如需详细解释，应放在头文件中的函数文档而非内联注释。

#### 关闭日期
2026-07-04

---

<a id="MPSC-010"></a>
### MPSC-010: `free_capacity` 通过双重强制转换丢弃 `const`

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低

#### 关闭原因
C 语言限制。C 原子变量不可声明为 const 但物理上可修改。`free_capacity` 的 const cast 是读取原子值的必要操作，逻辑上仍为"观察而非修改"。分离读写路径会显著增加代码量而无实际收益。

#### 关闭日期
2026-07-04
