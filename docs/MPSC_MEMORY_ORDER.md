# nanosig MPSC Record Ring Memory Order

Status: 当前实现文档（MPSC record ring）。

本文档描述 MPSC record ring（`ns_mpsc_record_ring_t`）的并发设计、内存序协议和发布链。

## 概述

MPSC record ring 是一个变长多生产者单消费者（MPSC）无锁队列。它支持：
- 多个生产者线程并发调用 `ns_mpsc_record_ring_try_pushv` 入队
- 单个消费者线程调用 `ns_mpsc_record_ring_try_acquire` / `ns_mpsc_record_ring_release` 零拷贝出队
- 变长记录，避免固定大小槽位造成的浪费

## 数据结构

```
ring->reserve_pos  (atomic, 生产者竞争)
ring->write_pos    (atomic, 已开放给后续生产者的预留进度，不代表 payload 已提交)
ring->read_pos     (atomic, 消费进度)
```

每条记录头部有一个 `ns_mpsc_record_header_t`，其 `meta` 字段的位布局：
- `[31:0]` total_size  — 记录总大小（含头部和填充）
- `[39:32]` padding_size — 填充大小
- `[40]` valid — 记录是否已发布（1=已发布，0=未提交/已消费）
- `[41]` fake — 是否为填充记录（用于对齐）

## 三阶段发布链

### 阶段 1：预留（reserve）

生产者以 `write_pos` 为 expected，对 `reserve_pos` 做 CAS，获取本条记录的独占写入权。以下伪代码表示一次尝试；失败后重新加载位置、检查容量并重算预留计划：

```c
write_pos = atomic_load(&ring->write_pos, acquire);
// 只有 reserve_pos == write_pos 时，才能预留下一段空间。
expected = write_pos;
atomic_compare_exchange_weak(&ring->reserve_pos, &expected, write_pos + publish_size,
                             acq_rel, relaxed);
```

- **acquire load**：读取当前 `write_pos` 作为 CAS 的 expected 值，只有 `reserve_pos == write_pos` 时才能抢占。
- **acq_rel CAS**：成功时将 `reserve_pos` 推进到 `write_pos + publish_size`，获得本条记录的独占空间。失败只更新临时变量 `expected`；下一次尝试重新读取 `write_pos`，不沿用失败返回的 expected。
- **`reserve_pos` 在逻辑上领先 `write_pos`**：表示本轮预留尚未开放给后续生产者。二者相等也不代表所有 payload 已提交，先前生产者可能仍在复制数据。生产者不得在 reserve 与 commit 之间终止（见 MPSC-012）。

### 阶段 2：开放后续预留（publish position）

生产者先将真实记录的 meta 标为无效，再步进 `write_pos`，此时尚未复制 payload：

```c
// 若需跨环尾，先提交不带 payload 的 fake record。
// 将本条真实记录标为未提交。
atomic_store(&header->meta, 0, release);
// 允许后续生产者继续预留，各生产者可并行复制自己的 payload。
atomic_store(&ring->write_pos, reserve_pos + publish_size, release);
```

- **release store** 发布的是预留进度及此前的 meta 初始化。观察到 `write_pos` 前进不能证明 payload 可读，因为复制发生在该 store 之后。

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
write_pos = atomic_load(&ring->write_pos, acquire);
// 加载记录的 meta 检查 valid 位
meta = atomic_load(&header->meta, acquire);
if(!ns_mpsc_meta_is_valid(meta)) return NS_E_EMPTY;
// 校验并跳过 fake record 后，才可读取真实记录的 payload。
```

- **acquire-load `write_pos`** 用于观察预留进度，不能代替记录提交检查。
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

1. 逻辑进度依次为预留 `reserve_pos`、开放后续预留 `write_pos`、回收 `read_pos`；实现用无符号位置差处理计数器回绕，不能直接按原始值大小判断先后。
2. `reserve_pos` CAS 分配互不重叠的空间；推进 `write_pos` 后，生产者仍可在本条记录内复制数据。
3. `read_pos` 与 `write_pos` 只确定待检查范围，消费者必须 acquire-load 队首 meta 并确认 valid=1 才能读取 payload。
4. payload 发布依赖 meta 的 release/acquire，槽位复用依赖 read_pos 的 release/acquire；CAS 不是唯一的同步点。

## 内存序总结

| 操作 | 原子变量 | 内存序 | 作用 |
|------|---------|--------|------|
| 加载 write_pos（生产者） | `write_pos` | acquire | 作为 CAS expected 值，探测可继续预留的位置 |
| CAS reserve_pos | `reserve_pos` | acq_rel | 竞争槽位所有权，推进 reserve_pos |
| 存储 write_pos | `write_pos` | release | 开放后续预留，不提交 payload |
| 加载 write_pos（消费者） | `write_pos` | acquire | 判断待检查范围，不证明 payload 可读 |
| 存储 meta（生产者 valid=0） | header.meta | release | 标记真实记录未提交，发生在 write_pos 发布之前 |
| 清零 meta（消费者） | header.meta | relaxed | 标记已消费，由后续 read_pos 回收操作排序 |
| 存储 meta（valid=1） | header.meta | release | 发布此前写入的 payload 与记录元数据 |
| 加载 meta（消费者） | header.meta | acquire | 确认 valid=1 后读取已提交 payload |
| 加载 read_pos（生产者） | `read_pos` | acquire | 判断可用空间，并同步槽位回收 |
| 存储 read_pos（消费者） | `read_pos` | release | 释放槽位给生产者 |
| 加载 read_pos（消费者） | `read_pos` | relaxed | 内部观察，消费者独占 |

## 与旧版固定容量 MPSC 队列的区别

旧版（已删除）的 `ns_mpsc_try_push` / `ns_mpsc_try_pop` 使用固定大小槽位和序列号（sequence number）实现生产者-消费者同步。当前 record ring 使用 `reserve_pos` CAS 加三阶段发布链，支持变长记录和零拷贝消费。

## 平台边界

所有原子操作通过 `nanosig_atomic.h` 封装，不直接使用平台互斥锁、条件变量或 wakeup 句柄。C11 `stdatomic.h` 提供跨平台内存序语义。

跨线程 event 的 happens-before 由平台 signal/wait 原语提供：Linux eventfd `write(2)`/`read(2)`、macOS pipe `write(2)`/`read(2)`（及 wakeup 的 `kevent`）、Windows `SetEvent`/`WaitForMultipleObjects`。signal 前的写对 wait 返回后的读可见，库内 event 路径不另加 atomic fence。
