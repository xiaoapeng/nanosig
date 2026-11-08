# nanosig MPSC Record Ring Memory Order

Status: 当前实现文档（MPSC record ring）。

本文档描述 MPSC record ring（`ns_mpsc_record_ring_t`）的并发设计、内存序协议和发布链。

## 概述

MPSC record ring 是一个使用原子操作协调的变长多生产者单消费者（MPSC）队列。它支持：
- 多个生产者线程并发调用 `ns_mpsc_record_ring_try_pushv` 入队
- 单个消费者线程调用 `ns_mpsc_record_ring_try_acquire` / `ns_mpsc_record_ring_release` 零拷贝出队
- 变长记录，避免固定大小槽位造成的浪费

## 数据结构

```
ring->write_pos    (atomic, 最低位 busy；其余位为已发布的预留进度，不代表 payload 已提交)
ring->read_pos     (atomic, 消费进度)
```

启用 `NS_MPSC_CACHELINE_ALIGNED` 时，写位置、读位置和只读字段按
`NS_MPSC_CACHELINE_SIZE` 间隔布置。本机 CMake 构建自动探测该常量并统一传给使用方；
直接集成或无法探测时回退 64，也可按目标显式覆盖。它只改变字段布局，不改变下述同步协议。
配置和 ABI 要求见 [构建配置](BUILD_CONFIG.md#mpsc-缓存行间距)。

每条记录头部有一个 `ns_mpsc_record_header_t`，其 `meta` 字段的位布局：
- `[31:0]` total_size  — 记录总大小（含头部和填充）
- `[39:32]` padding_size — 填充大小
- `[40]` valid — 记录是否已发布（1=已发布，0=未提交/已消费）
- `[41]` fake — 是否为填充记录（用于对齐）

## 三阶段发布链

### 阶段 1：预留（reserve）

所有位置按 `sizeof(size_t)` 对齐，`write_pos` 最低位用作 busy 标志。生产者先通过 CAS 设置 busy，取得规划与发布位置的独占权，再读取 `read_pos` 并计算本条记录的空间。busy 期间其余位保持原发布边界，消费者仍可读取之前已发布的记录。以下伪代码表示一次尝试；busy 已设置或 CAS 失败时重新尝试：

```c
write_pos = atomic_load(&ring->write_pos, relaxed);
if(write_pos & BUSY) retry;
expected = write_pos;
if(!atomic_compare_exchange_weak(&ring->write_pos, &expected, write_pos | BUSY,
                                acq_rel, relaxed)) retry;
read_pos = atomic_load(&ring->read_pos, acquire);
if(!plan_push(ring, record_size, write_pos, read_pos, &plan)){
    atomic_store(&ring->write_pos, write_pos, release);
    return NS_E_QUEUE_FULL;
}
```

- **relaxed load** 只探测空闲位置；真正取得所有权的是成功的 **acq_rel CAS**。其 acquire 部分与前一个生产者清除 busy 的 release-store 配对，接续已发布的位置及其之前的槽位回收观察。
- **计数器整周回绕**：旧 expected 可能再次匹配空闲位置，但容量计划是在取得 busy 后重新计算的，不沿用旧 `read_pos`。另一个生产者正在跨零预留时，busy 位保持为 1，旧空闲值无法匹配。
- **容量不足**：不修改记录或发布边界，release-store 原位置以清除 busy，返回 `NS_E_QUEUE_FULL`。
- **暂停与存活**：持有 busy 的生产者被暂停时，后续生产者需等待；本协议不保证 lock-free 进度。生产者不得在取得 busy 与 commit 之间终止（见 MPSC-012）。

### 阶段 2：开放后续预留（publish position）

生产者先将真实记录的 meta 标为无效，再通过一次 release-store 步进 `write_pos` 并清除 busy，此时尚未复制 payload：

```c
// 若需跨环尾，先提交不带 payload 的 fake record。
// 将本条真实记录标为未提交。
atomic_store(&header->meta, 0, release);
// 同时向消费者发布预留进度，并允许后续生产者继续规划。
atomic_store(&ring->write_pos, write_pos + publish_size, release);
```

- **release store** 发布的是预留进度及此前的 meta 初始化。观察到 `write_pos` 前进不能证明 payload 可读，因为复制发生在该 store 之后。
- **清除 busy 与位置发布** 是同一次 store；后续生产者取得 busy 后才能接续该位置。payload 复制在清除 busy 之后执行，各生产者仍可并行复制各自记录。

### 阶段 3：提交（commit）

生产者复制 payload，然后用包含 valid 位的 meta 提交记录：

```c
// 写入真实记录头之后的 payload（plain stores）。
ns_mpsc_record_ring_write_parts(ring, record_pos, parts, part_count);
// 编码记录大小、padding 与 valid=1。
meta = ns_mpsc_meta_encode_valid(record_total_size, padding_size);
atomic_store(&header->meta, meta, release);
```

- **release store / acquire load**：消费者读到本次提交的有效 meta 后，才能看到该提交之前写入的完整 payload；记录大小和 padding 同时由 meta 提供。

## 消费者读取

消费者通过 `try_acquire` 读取记录：

```c
// 加载 write_pos 判断是否有已预留空间；若与 read_pos 相等则返回 NS_E_EMPTY。
write_pos = atomic_load(&ring->write_pos, acquire) & ~BUSY;
// 加载记录的 meta 检查 valid 位
meta = atomic_load(&header->meta, acquire);
if(!ns_mpsc_meta_is_valid(meta)) return NS_E_EMPTY;
// 校验并跳过 fake record 后，才可读取真实记录的 payload。
```

- **acquire-load `write_pos`** 用于观察预留进度，不能代替记录提交检查。
- **屏蔽 busy**：消费者的 `try_acquire`、`release` 及 `free_capacity` 都必须先去掉标志位再计算位置差。读到 busy 的 acquire-load 通过前次发布起始的 RMW release sequence 继续同步旧边界；容量不足时的撤销 store 又由取得 busy 的 acquire 串接该同步链。busy 不使旧记录变成不可消费，也不能被计成一个新字节。
- **acquire-load `meta`** 读到 valid=1 的提交值后，与对应的 release-store 建立 payload 的可见性。队首记录尚未提交时返回 `NS_E_EMPTY`，即使后面的记录已提交，也不越过队首消费。

消费完成后调用 `release`：

```c
// 清零 meta 标记为已消费
atomic_store(&header->meta, 0, relaxed);
// 步进 read_pos 释放槽位
atomic_store(&ring->read_pos, read_pos + stride, release);
```

- **relaxed store** 清零 meta：不单独发布回收结果，由随后的 `read_pos` release-store 负责同步。
- **release store** 步进 `read_pos`：在 payload 读取和 meta 清零之后归还空间，生产者 acquire-load 观察到该位置后才能复用。

## 生产者观察满队列

生产者检查队列是否已满：

```c
read_pos = atomic_load(&ring->read_pos, acquire);
```

- **acquire load** 观察消费者 release-store 的 `read_pos`，用于判断可用空间，并保证槽位复用发生在此前的消费和 meta 清零之后。

## 关键不变量

1. `write_pos & ~BUSY` 始终表示已发布的预留边界；busy 为 1 时只有持有者能规划与发布下一段空间。`free_capacity` 忽略 busy 位，只提供并发快照估计；位置差使用无符号运算处理回绕，不能直接按原始值大小判断先后。
2. 每次容量检查都在取得 busy 后进行，一次 release-store 发布新边界并清除 busy；因此各生产者的记录互不重叠，payload 复制可并行。
3. `read_pos` 与 `write_pos` 只确定待检查范围，消费者必须 acquire-load 队首 meta 并确认 valid=1 才能读取 payload。
4. payload 发布依赖 meta 的 release/acquire，槽位复用依赖 read_pos 的 release/acquire；CAS 不是唯一的同步点。

## 内存序总结

| 操作 | 原子变量 | 内存序 | 作用 |
|------|---------|--------|------|
| 加载 write_pos（生产者） | `write_pos` | relaxed | 探测位置与 busy 状态 |
| CAS write_pos | `write_pos` | acq_rel | 设置 busy，取得规划与位置发布的独占权 |
| 存储 write_pos | `write_pos` | release | 发布新边界并清除 busy，或容量不足时原位撤销；不提交 payload |
| 加载 write_pos（消费者） | `write_pos` | acquire | 去掉 busy 后判断待检查范围，不证明 payload 可读 |
| 存储 meta（生产者 valid=0） | header.meta | release | 标记真实记录未提交，发生在 write_pos 发布之前 |
| 清零 meta（消费者） | header.meta | relaxed | 标记已消费，由后续 read_pos 回收操作排序 |
| 存储 meta（valid=1） | header.meta | release | 发布此前写入的 payload 与记录元数据 |
| 加载 meta（消费者） | header.meta | acquire | 确认 valid=1 后读取已提交 payload |
| 加载 read_pos（生产者） | `read_pos` | acquire | 判断可用空间，并同步槽位回收 |
| 存储 read_pos（消费者） | `read_pos` | release | 释放槽位给生产者 |
| 加载 read_pos（消费者） | `read_pos` | relaxed | 内部观察，消费者独占 |

## 与旧版固定容量 MPSC 队列的区别

旧版（已删除）的 `ns_mpsc_try_push` / `ns_mpsc_try_pop` 使用固定大小槽位和序列号（sequence number）实现生产者-消费者同步。当前 record ring 使用 `write_pos` busy CAS 加三阶段发布链，支持变长记录和零拷贝消费。

## 平台边界

所有原子操作通过 `nanosig_atomic.h` 封装，不直接使用平台互斥锁、条件变量或 wakeup 句柄。C11 `stdatomic.h` 提供跨平台内存序语义。

跨线程 event 的 happens-before 由平台 signal/wait 原语提供：Linux eventfd `write(2)`/`read(2)`、macOS pipe `write(2)`/`read(2)`（及 wakeup 的 `kevent`）、Windows `SetEvent`/`WaitForMultipleObjects`。signal 前的写对 wait 返回后的读可见，库内 event 路径不另加 atomic fence。
