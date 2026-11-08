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
    status: "closed-fixed"
    severity: "high"
    type: "bug"
    section: "closed"
    closed_date: "2026-10-19"
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
    status: "closed-fixed"
    severity: "medium"
    type: "doc"
    section: "closed"
    closed_date: "2026-10-19"
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
  - id: "MPSC-015"
    module: "mpsc-record-ring"
    status: "closed-fixed"
    severity: "high"
    type: "bug"
    section: "closed"
    closed_date: "2026-11-08"
    location: "docs/review/mpsc-record-ring-code-review.md#MPSC-015"
    locations:
      - "src/ds/ns_mpsc_record_ring.c:381"
      - "src/ds/ns_mpsc_record_ring.c:395"
      - "src/ds/ns_mpsc_record_ring.c:412"
      - "src/ds/ns_mpsc_record_ring.c:415"
      - "src/ds/ns_mpsc_record_ring.c:418"
      - "src/ds/ns_mpsc_record_ring.c:450"
---

# MPSC Record Ring 代码审查报告

**日期**: 2026-07-04
**审查者**: claude-code-review

---

### 2026-11-07 修复前基线详细复核

范围：src/ds/ns_mpsc_record_ring.c 全文件及公共头契约；调用者与测试仅用于影响和覆盖核对。
以下结论针对修复前基线 c152789；该轮只读复核未修改实现。两个独立审查 lane 均确认同一新问题 MPSC-015；无额外确认 bug。
code-reviewer：REQUEST CHANGES；architect：BLOCK；最终结论：REQUEST CHANGES。
`omx quality-gates list --stage code-review --json` 返回空 gates。

正确性核对：
- meta release/acquire 发布 payload；read_pos release/acquire 保障消费后复用。
- write_pos release/acquire 发布 header 清无效步骤，不代表 payload 已提交。
- record 大小求和防溢出、半环上限、fake record、零长度 one-past-end 指针、32 位 stride 上限均已检查。
- 正常代际内未发现第二个确定错误；完整计数器回绕时预留所有权和容量检查存在 MPSC-015。
- MPSC-012 的异常终止约束、MPSC-005 的初始化策略与其他已拒绝条目保持原仲裁。

优化评估（均未实施或测量收益，先修 MPSC-015）：

| 优先级 | 方案 | 依据与限制 |
|---|---|---|
| 1 | 有界轮询/退避，减少失败 CAS | 基线每次重试重算计划并争用 reserve_pos；测试 1/2/4/8 生产者，记录失败 CAS/成功记录、吞吐、CPU、p99 延迟；不能无界等待特定位置值。 |
| 2 | 核对目标平台 cache line 并配置字段隔离 | 公共头当前按 64 字节间距布局；本机 sysctl hw.cachelinesize=128，现有布局不保证该目标的隔离。需评估内存占用和结构 ABI，先以对齐与实际地址、基准确认收益。 |
| 3 | 将单次调用不变的 record_total_size/padding 计算移出重试 | 先查 Release 汇编；编译器已外提则不改。 |
| 4 | 单独证明推送阶段清 header 的 release 是否可降为 relaxed | write_pos 发布链仍可排序该写；不能顺带改变初始化 helper 的既有仲裁，更不能删 payload 提交与 read_pos 回收的 release/acquire。修复预留协议后重新证明。 |

优化源码定位（修复前基线 c152789）：
src/ds/ns_mpsc_record_ring.c:374
src/ds/ns_mpsc_record_ring.c:390
include/nanosig/nanosig_mpsc_record_ring.h:54
src/ds/ns_mpsc_record_ring.c:216
src/ds/ns_mpsc_record_ring.c:411

验证：Release MPSC 单元测试及 20 秒压力测试 2/2 通过；审查 lane 执行严格 Clang 语法检查和静态分析，无诊断。
确定性 ABA harness 重现成功，见 docs/review/evidence/mpsc-counter-aba.py；使用主机上的 32 位位置算术模拟，非原生 32 位运行。
未运行原生 32 位硬件测试、完整稳定性压力测试、TSAN 或优化基准。普通测试通过不能排除完整计数器周期的暂停交错。

## 修复历史

- **2026-11-08 — MPSC-015**：修复整周回绕后的过期 CAS 预留，合并位置字段并以 busy 位保护容量复核和发布；加入确定性 ABA 回归、缓存行自动探测与 ABI 配置用例。提交整理分支为 codex/fix-mpsc-aba-cacheline-helpers，作者已明确授权本地提交。MPSC Release/TSAN 各 3/3、缓存行配置 21 项及 Zephyr 模拟配置 7 项通过；压测设置 20 秒。原生 32 位及跨平台远端 CI 留待后续验证。

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

<a id="MPSC-014"></a>
### MPSC-014: MPSC_MEMORY_ORDER.md 的 reserve 阶段伪码与实现不是同一个算法

- **状态**: 关闭-已修复
- **严重度**: 🟡 中
- **类型**: doc
- **关闭原因**: 已在 docs/MPSC_MEMORY_ORDER.md 中将阶段 1 伪码改写为与实现一致的形式（`write_pos = load(write_pos, acquire)`，CAS expected 为 `write_pos`），并补了 `reserve_pos > write_pos` 表示存在在途生产者、不被自动回收的说明。文档与实现现在一致。
- **关闭日期**: 2026-10-19

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

<a id="MPSC-013"></a>
### MPSC-013: 32 位构建下 meta 的 16 位 stride 字段会静默截断大记录

- **状态**: 关闭-已修复
- **严重度**: 🟠 高
- **类型**: bug
- **关闭原因**: src/ds/ns_mpsc_record_ring.c 的 ns_mpsc_record_ring_capacity_is_valid 增加 32 位 stride 硬约束（capacity/2 > NS_MPSC_META_STRIDE_MASK 即拒绝，返回 NS_E_INVAL 而非静默截断）；include/nanosig/nanosig_mpsc_record_ring.h 补 @pre 说明 32 位目标的容量上限；test/unit/test_mpsc_record_ring.c 补单元测试（NS_CAPACITY_65536 成功、NS_CAPACITY_131072 被拒）。commit a7283d3。
- **关闭日期**: 2026-10-19

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

<a id="MPSC-015"></a>
### MPSC-015: 32 位计数器整周回绕使旧 CAS 计划覆盖未消费记录

- **状态**: 关闭-已修复
- **严重度**: 🟠 高（前提：32 位目标，生产者在 CAS 前暂停期间其他线程累计推进 4 GiB；源码时序证明及 64 位主机的 32 位算术模拟实测，未做原生 32 位硬件复现）
- **类型**: bug
- **关闭原因**: 按作者明确要求完成合并 write_pos 的 busy 协议：取得预留占用后再读取 read_pos 并复核容量，同时屏蔽消费者快照的 busy 位；新增确定性满环/在途预留回绕回归。已整理到特性分支 codex/fix-mpsc-aba-cacheline-helpers，由作者本次指令授权本地提交。当前 macOS Release 53/53、ASAN 针对性 12/12；提交前额外 MPSC Release 3/3、TSAN 3/3 通过，压力时长均设置 20 秒；缓存行模块配置 21 项、Zephyr SDK-free 配置/布局 7 项通过。原生 32 位、Linux/Windows 和真实 Zephyr 尚未验证；远端 CI 和 nightly 未触发。本条关闭-已修复，历史吞吐和修复前复现记录保留。
- **关闭日期**: 2026-11-08

#### 问题描述

生产者在容量检查之后、reserve CAS 之前暂停，位置计数器完整回绕后，旧 CAS 可成功并覆盖未消费记录；推送返回 NS_OK，后续消费者返回 NS_E_CORRUPT。CAS 只比较 reserve_pos 数值，不能证明此前的 read_pos/容量计划仍有效。

区别于 MPSC-012：暂停在抢占之前，生产者仍存活，其他生产者正常推进，没有违反 reserve→commit 期间不得异常终止的合同。64 位同类条件需要累计 2^64 字节，现实风险主要在支持的 32 位平台。

#### review 建议

优先修复预留协议：用独立 busy 标志或对齐位置的空闲低位标记预留占用；成功取得占用后重新读取 read_pos 并检查容量，失败时安全撤销预留。标记必须使过期 expected 无法匹配另一个生产者的在途预留。只在现有 CAS 后重读 read_pos 不够：另一个生产者可正持有 ffff8000→0 的预留，此时旧 expected=0 也能成功。单纯增大计数器仅延长触发窗口。实施前需为满队列回绕、在途预留回绕两种时序建立回归，并重新验证发布链。

#### 作者建议

2026-11-07 作者先要求：“继续吧，你先修一版我看看”；随后明确修订：“你把reserve_pos 合并成 write_pos 试试，write_pos直观一点”。依照作者要求合并字段，保留特性分支工作区供审阅，不提交。

合并字段版试修（codex/fix-mpsc-counter-aba，待作者审阅；后续缓存行适配见末尾）：
- 删除 reserve_pos；只保留 write_pos、read_pos。write_pos 的最低位表示 busy，其余位是已发布的预留边界。
- CAS 设置 busy 后才 acquire-load read_pos 并检查容量。失败时 release-store 原位置；成功时先将记录 meta 标为无效，一次 release-store 新 write_pos 同时发布位置与清除 busy，再复制 payload、release-store 有效 meta。
- free_capacity、try_acquire、release 都先屏蔽 busy 再计算位置差。busy 期间旧记录仍可消费，未提交记录仍返回 NS_E_EMPTY，真正超容量的状态仍返回 NS_E_CORRUPT。
- 相比第一版，成功推送减少一次原子 store，并删除一个原子字段及其 padding；固定 64 字节间距时，本机 64 位桌面配置 sizeof(ns_mpsc_record_ring_t) 从 208 降到 144 字节。公共函数签名不变，但结构体 ABI 已变化，使用方需重新编译。吞吐对比见下方；不能仅按原子操作数量判断性能。
- 已有回绕回归在重构前通过；合并后继续覆盖满队列整周回绕、跨零在途预留、fake tail 跨 native size_t 零点，并增加 busy 下的消费/释放/容量/损坏检查。测试只拦截自己的源码副本，不向生产代码加 hook，不窄化位置算术。
- 合并版 macOS Release 全部 54 项、ASAN+UBSAN 快速集 52 项、TSAN 的 MPSC 单测/ABA 回归/20 秒压力共 3 项均通过。ASAN、单独 UBSAN、TSAN 审计及 API 契约检查通过。独立 MPSC 协议复核和 Clang 静态分析无阻塞发现。
- 第一版额外 TSAN integration_layer1/2 的 broker/wakeup 告警已在未修改的 c152789 基线复现；本轮只重新验证 MPSC TSAN 范围，未扩展修复相关模块。基线日志保留于 build/mpsc-tsan-baseline/baseline-ctest.log。
- 原 Python 复现脚本同时固定历史源码与公共头至 c152789，继续保留失败证据；当前实现由 CTest 回归验证。尚未进行原生 32 位运行。

吞吐验证（作者本轮明确要求对比，2026-11-07）：
- Apple M4 / 128 字节缓存行；相同 Release -O3，无 sanitizer；64 字节 payload、64 KiB 环，结构体起点按 128 对齐。1/2/4/8 生产者 + 1 消费者，各预热 0.25 秒后测 2 秒，重复 3 次，随机顺序串行运行；检查序号、全部 payload 与最终入出队总数。
- 下面为百万条/秒的中位数，修复前 master 是 c152789；第一版快照是三位置 busy 协议，合并版为当时固定 64 字节间距的两位置协议。

| 生产者 | master | 第一版 | 合并版 | 合并版较 master | 合并版较第一版 |
|---|---:|---:|---:|---:|---:|
| 1 | 16.722 | 16.328 | 19.742 | +18.1% | +20.9% |
| 2 | 10.573 | 9.972 | 9.433 | -10.8% | -5.4% |
| 4 | 3.546 | 2.148 | 3.097 | -12.7% | +44.2% |
| 8 | 2.438 | 1.625 | 2.005 | -17.8% | +23.4% |

- 布局对照只在临时副本把两处 56 字节 padding 改为 120 字节，使 write/read 间距从 64 变成 128。与同轮 64 字节间距合并版相比，1/2/4/8 生产者中位数变化为 -22.0%, +21.4%, +46.0%, +30.1%；因此布局是本机吞吐差异的重要因素，但增加 padding 也有单生产者代价，未据此修改工作区实现。
- 结论：合并版多数并发度优于第一版，2 生产者例外；相对修复前 master，多生产者仍下降约 11%–18%。不能声称无性能下降。短测仅代表本机、固定负载、固定对齐，未绑核；需结合实际 payload/队列容量及目标机器再判断。
- 可复跑脚本 build/mpsc-pos-compare/compare.py；主测试与布局对照原始记录分别在 results.json、layout-results.json，同目录保存 summary.json、environment.json、manifest.json 及各源码/头快照。


2026-11-08 作者确认采用构建时自动适配缓存行间距：
- 新增 NS_MPSC_CACHELINE_SIZE，CMake 本机构建通过 platform/cache_line_probe.c 探测，显式值优先；跨目标或探测失败时状态提示并回退 64，允许覆盖。库、应用和 API 检查通过 PUBLIC 数值定义共享同一 ABI。
- 本机 M4 自动得到 128；实际编译布局 write=0、read=128、storage=256，sizeof=272。固定 64 时的旧性能记录仍保留为历史实验；其脚本改为读取已保存的快照，不会把新工作区混进旧比较。
- 宏约束为不小于 16 的十进制 2 的幂；padding 按 sizeof(atomic_size_t) 计算，不增加运行时查询、分配或指针间接访问。宏改变后库与调用方都须重编译。
- Zephyr 独立入口按显式值、有效 CONFIG_DCACHE_LINE_SIZE、/cpus/cpu@0 的 d-cache-line-size、64 的优先级解析，不探测构建宿主。
- 自动 128 下 Release 54/54、ASAN+UBSAN 快速集 52/52、MPSC TSAN 3/3 通过；配置/API/编码/头契约审计通过。默认及显式 64/128 的库/消费者/C/C++ ABI 回归、非法值拒绝、交叉目标保护检查通过；Zephyr SDK-free 配置/布局夹具通过，不等同真实 SDK/backend 测试。
- 已独立核对构建与探针边界，无阻塞发现；Linux、Windows 探针和真实 Zephyr 尚未在对应平台运行。更完整的配置及回退说明见 docs/BUILD_CONFIG.md。改动继续保留工作区，未提交。

2026-11-08 作者进一步明确要求：“把这次工作区修改整理一下，新建特性分支提交，先别推送和开 PR。”本次将已验证的修复整理到 codex/fix-mpsc-aba-cacheline-helpers，允许本地提交；覆盖前述仅保留工作区、不提交的临时安排。推送和 PR 仍未获当前执行授权。

#### 可重现的失败场景

32 位模型：capacity=65536，stride=32768，W=R=reserve=0。A 算出计划后暂停于 CAS 前。B 推送 131072 条，消费者释放前 131070 条；剩余两条填满队列，W=reserve=0，R=0xffff0000。A 恢复执行旧 CAS 0→32768，覆盖 offset=0 的未消费记录。

运行 python3 docs/review/evidence/mpsc-counter-aba.py。实测：Before stale CAS reserve=0 write=0 read=4294901760 free=0；stale push=0；未消费字节从 0x42 变成 0x41；acquire=-9，used=98304 > capacity=65536。

证据边界：macOS 64 位主机，仅在临时源副本将位置更新及差值截为 uint32_t，并注入确定性调度 hook；保留原生 8 字节 header，payload=32760。原生 32 位对应 header=4、payload=32764，stride 相同。未进行原生 32 位硬件复现。

#### 定位

src/ds/ns_mpsc_record_ring.c:381
src/ds/ns_mpsc_record_ring.c:395
src/ds/ns_mpsc_record_ring.c:412
src/ds/ns_mpsc_record_ring.c:415
src/ds/ns_mpsc_record_ring.c:418
src/ds/ns_mpsc_record_ring.c:450

#### 影响面

直接生产调用：src/nanosig.c:673 的 ns_signal_emit_raw，以及本模块 try_push 封装；直接消费调用：src/nanosig.c:335 的 ns_loop_dispatch_pending。影响所有基于该队列的跨线程 signal 投递。现有单元/压力测试覆盖普通绕环、零长度、大小上限、并发 payload 校验，但未覆盖暂停跨越完整计数器周期的旧计划。

---

---
