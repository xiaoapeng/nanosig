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
ring->write_pos    (atomic, 发布进度)
ring->read_pos     (atomic, 消费进度)
```

每条记录头部有一个 `ns_mpsc_record_header_t`，其 `meta` 字段的位布局：
- `[31:0]` total_size  — 记录总大小（含头部和填充）
- `[39:32]` padding_size — 填充大小
- `[40]` valid — 记录是否已发布（1=已发布，0=未提交/已消费）
- `[41]` fake — 是否为填充记录（用于对齐）

## 三阶段发布链

### 阶段 1：预留（reserve）

生产者通过 CAS 竞争 `reserve_pos`：

```c
reserve_pos = atomic_load(&ring->reserve_pos, relaxed);
// CAS(reserve_pos, reserve_pos + publish_size)
atomic_compare_exchange_weak(&ring->reserve_pos, &reserve_pos, reserve_pos + publish_size,
                             acq_rel, relaxed);
```

- **relaxed load**：允许读取陈旧值，CAS 失败时重试即可
- **acq_rel CAS**：成功时获取该槽位的独占所有权，同时释放上一轮操作的内存序。CAS 失败不产生任何内存序副作用

### 阶段 2：写入（write）

生产者将记录数据写入预留的槽位，然后步进 `write_pos`：

```c
// 写入记录数据（plain stores）
memcpy(record_header, ...);
// 步进 write_pos，指示其他生产者可以越过此位置继续预留
atomic_store(&ring->write_pos, reserve_pos + publish_size, release);
```

- **release store** 保证：`memcpy` 数据写入在 `write_pos` 发布之前对消费者可见。后续生产者通过 `write_pos` acquire-load 观察到该 store 时，也能看到之前的 memcpy 数据

### 阶段 3：提交（commit）

生产者设置记录的 valid 位，完成最终发布：

```c
// 设置 valid=1 位
meta |= (1u << NS_MPSC_META_VALID_SHIFT);
atomic_store(&header->meta, meta, release);
```

- **release store** 保证：完整的记录数据（含 total_size/padding_size）在 valid=1 之前对消费者可见

## 消费者读取

消费者通过 `try_acquire` 读取记录：

```c
// 加载 write_pos 获取当前写入进度
write_pos = atomic_load(&ring->write_pos, acquire);
// 加载记录的 meta 检查 valid 位
meta = atomic_load(&header->meta, acquire);
```

- **acquire load** 保证：读取 `write_pos` 时同步到生产者 release-store 之前的所有数据
- **acquire load** 保证：读取 `meta` 时同步到生产者 release-store 的 valid=1 之前的所有数据写入

消费完成后调用 `release`：

```c
// 清零 meta 标记为已消费
atomic_store(&header->meta, 0, relaxed);
// 步进 read_pos 释放槽位
atomic_store(&ring->read_pos, read_pos + stride, release);
```

- **relaxed store** 清零 meta：消费者独占，无需同步
- **release store** 步进 `read_pos`：允许生产者读取 `read_pos` 以判断队列是否已满

## 生产者观察满队列

生产者检查队列是否已满：

```c
read_pos = atomic_load(&ring->read_pos, acquire);
```

- **acquire load** 保证：看到消费者 release-store 的 `read_pos` 更新，从而正确判断可用空间

## 关键不变量

1. `reserve_pos` ≥ `write_pos` ≥ `read_pos`（预留进度 ≥ 写入进度 ≥ 消费进度）
2. 生产者通过 `reserve_pos` 竞争独占槽位，CAS 是唯一的同步点
3. 消费者通过 `read_pos` 和 `write_pos` 确定可消费范围
4. valid=1 是消费者的唯一可见性标记——仅在 release-store 后消费者才能看到完整记录

## 内存序总结

| 操作 | 原子变量 | 内存序 | 作用 |
|------|---------|--------|------|
| 加载 reserve_pos | `reserve_pos` | relaxed | 探测起始位置，失败重试 |
| CAS reserve_pos | `reserve_pos` | acq_rel | 竞争槽位所有权 |
| 加载 write_pos（生产者） | `write_pos` | acquire | 观察其他生产者写入进度 |
| 存储 write_pos | `write_pos` | release | 发布写入完成 |
| 加载 meta（消费者） | header.meta | acquire | 观察 valid=1 及记录元数据 |
| 存储 meta（valid=1） | header.meta | release | 提交记录到消费者 |
| 加载 read_pos（生产者） | `read_pos` | acquire | 判断队列是否已满 |
| 存储 read_pos（消费者） | `read_pos` | release | 释放槽位给生产者 |
| 加载 read_pos（消费者） | `read_pos` | relaxed | 内部观察，消费者独占 |

## 与旧版固定容量 MPSC 队列的区别

旧版（已删除）的 `ns_mpsc_try_push` / `ns_mpsc_try_pop` 使用固定大小槽位和序列号（sequence number）实现生产者-消费者同步。当前 record ring 使用 `reserve_pos` CAS 加三阶段发布链，支持变长记录和零拷贝消费。

## 平台边界

所有原子操作通过 `nanosig_atomic.h` 封装，不直接使用平台互斥锁、条件变量或 wakeup 句柄。C11 `stdatomic.h` 提供跨平台内存序语义。