# emit 路径零分配审计（P9.6）

> nanosig v1 / 2026-06-20 审计快照
>
> 审计范围：从 `ns_signal_emit` 宏入口到 MPSC record ring `try_pushv` 成功返回，
> 以及每个 slot 之后调用 `ns_platform_wakeup_signal` 的整条路径，确保任何位置
> 都不调用 `ns_platform_alloc` / `malloc` / `calloc` / `realloc`。

## 摘要

| 指标 | 数值 |
| --- | --- |
| emit 主链路上涉及的函数调用总数 | 8（含 3 个平台后端，实际编译选 1） |
| alloc 命中数 | **0** |
| Critical 级别问题 | 0 |
| Major 级别问题 | 0 |
| Info 级别发现 | 0 |

## 调用链

完整调用链展开（逐级标注 alloc 与否和依据）：

```
ns_signal_emit(signal_name, payload_ptr)
  │  宏: include/nanosig/nanosig_signal.h:196
  │  展开为 ns_signal_emit_raw(&(signal_name), (const void*)(payload_ptr),
  │                              NS_SIGNAL_PAYLOAD_PTR_SIZE(payload_ptr))
  │  _Generic 编译期求值，无运行时 alloc
  │
  └── ns_signal_emit_raw(signal, payload, payload_size)
        │  src/nanosig.c:379
        │  所有变量: node (指针), call_header (栈, 24-32 字节),
        │            parts[2] (栈, 固定 16/32 字节), rc (int)
        │  无 VLA，无堆分配
        │
        ├─ ns_signal_lock(signal)
        │    │  src/nanosig.c:41 → ns_platform_mutex_lock(mutex)
        │    │  [NO ALLOC]
        │    │
        │    ├─ Linux:   pthread_mutex_lock()              platform/linux/port.c:215
        │    │           sys_futex 调用，零分配
        │    ├─ macOS:   pthread_mutex_lock()              platform/macos/port.c:236
        │    │           sys_ulock 调用，零分配
        │    └─ Windows: AcquireSRWLockExclusive()         platform/windows/port.c:170
        │              用户态 SRW lock，零分配
        │
        ├─ ns_list_for_each(node, &signal->slot_list)
        │    │  宏: include/nanosig/nanosig_list.h:216
        │    │  展开: for(node = (head)->next; node != (head); node = node->next)
        │    │  纯指针遍历，零分配
        │    │
        │    └── for each connection:
        │         │
        │         ├─ NS_CONTAINER_OF(node, ns_connection_t, signal_node)
        │         │   宏: include/nanosig/nanosig_types.h:65
        │         │   (type*)((char*)(ptr) - offsetof(type, member))
        │         │   纯指针运算，零分配
        │         │
        │         ├─ parts[0] = { &call_header, sizeof(call_header) }
        │         │  call_header 是栈局部变量（ns_slot_call_t, 16-24 字节）
        │         │  parts[1] = { payload, payload_size }  (payload 来自调用方传入的指针)
        │         │  零分配
        │         │
        │         ├─ ns_mpsc_record_ring_try_pushv(&conn->target_loop->queue, parts, 2)
        │         │    │  src/ds/ns_mpsc_record_ring.c:334
        │         │    │  [NO ALLOC]  关键路径——详细验证见下文
        │         │    │
        │         │    ├─ ns_mpsc_record_ring_is_valid()     [NO ALLOC]
        │         │    │   指针判空 + capacity 检查，算术运算
        │         │    │
        │         │    ├─ 求和 parts[i].size，检查溢出         [NO ALLOC]
        │         │    │   纯算术
        │         │    │
        │         │    ├─ CAS retry loop:
        │         │    │   ├─ atomic_load &write_pos          [NO ALLOC] C11 atomic 指令
        │         │    │   ├─ atomic_load &read_pos           [NO ALLOC] C11 atomic 指令
        │         │    │   ├─ ns_mpsc_record_ring_plan_push() [NO ALLOC] 纯算术
        │         │    │   │    (计算 total_size / align_up / offset / tail_size /
        │         │    │   │     fake_size / publish_size / padding_size, 全部纯算术)
        │         │    │   └─ atomic_compare_exchange_weak     [NO ALLOC] CAS 指令
        │         │    │
        │         │    ├─ fake header: atomic_store (meta, valid=0) [NO ALLOC]
        │         │    │   写入 ring 现有内存，无分配
        │         │    │
        │         │    ├─ mark_uncommitted: atomic_store (meta, 0)  [NO ALLOC]
        │         │    │   写入 ring 现有内存，无分配
        │         │    │
        │         │    ├─ atomic_store &write_pos (release)       [NO ALLOC]
        │         │    │   C11 atomic 指令
        │         │    │
        │         │    ├─ ns_mpsc_record_ring_write_parts()       [NO ALLOC]
        │         │    │   直接 memcpy 从 parts[i].data 到 ring->storage
        │         │    │   无临时缓冲区分段拷贝
        │         │    │   循环: for(i=0; i<part_count; i++) memcpy(dst, data, size)
        │         │    │   每段直接拷贝，dst 指针推进
        │         │    │
        │         │    └─ atomic_store &header->meta (valid=1)    [NO ALLOC]
        │         │       C11 atomic release 指令
        │         │
        │         └─ ns_platform_wakeup_signal(conn->target_loop->wakeup)
        │              │  [NO ALLOC]
        │              │
        │              ├─ Linux:   write(eventfd, ...)          platform/linux/port.c:107
        │              │           syscall，零分配
        │              ├─ macOS:   kevent(kq, EV_SET stack, ..) platform/macos/port.c:147
        │              │           EV_SET 在栈上，kevent syscall，零分配
        │              └─ Windows: SetEvent()                   platform/windows/port.c:98
        │                        kernel32 syscall，零分配
        │
        └─ ns_signal_unlock(signal)
             │  src/nanosig.c:47 → ns_platform_mutex_unlock(mutex)
             │  [NO ALLOC]
             │
             ├─ Linux:   pthread_mutex_unlock()               platform/linux/port.c:223
             ├─ macOS:   pthread_mutex_unlock()               platform/macos/port.c:244
             └─ Windows: ReleaseSRWLockExclusive()            platform/windows/port.c:178
```

### 调用链函数数统计

| 层次 | 函数名 | 类型 | 是否 alloc | 依据 |
| --- | --- | --- | --- | --- |
| 0 | `ns_signal_emit` | 宏 | N | `_Generic` + 参数转发，编译期求值 |
| 1 | `ns_signal_emit_raw` | 函数 | N | 栈变量 + 指针，无堆分配 |
| 2 | `ns_signal_lock` | static inline | N | 转发到平台 mutex lock |
| 3 | `ns_platform_mutex_lock` | 平台函数 | N | pthread / SRWLOCK：纯 syscall，零分配 |
| 2 | `ns_list_for_each` | 宏 | N | 纯指针遍历 |
| 3 | `NS_CONTAINER_OF` | 宏 | N | 纯指针运算 |
| 3 | `ns_mpsc_record_ring_try_pushv` | 函数 | N | 纯算术 + C11 atomic + memcpy |
| 4 | `ns_mpsc_record_ring_write_parts` | static 函数 | N | 直接 memcpy 到 ring storage |
| 3 | `ns_platform_wakeup_signal` | 平台函数 | N | write/kevent/SetEvent syscall |
| 2 | `ns_signal_unlock` | static inline | N | 转发到平台 mutex unlock |
| 3 | `ns_platform_mutex_unlock` | 平台函数 | N | pthread / SRWLOCK：纯 syscall，零分配 |

## alloc 命中清单

| file:line | 调用 | 是否在 emit 路径 | 严重度 | 备注 |
| --- | --- | --- | --- | --- |
| *(无)* | | | | **emit 路径上未发现任何 alloc 调用** |

### 全仓 alloc 调用对照（确认发射路径无意外调用）

以下列出了项目所有 `ns_platform_alloc` / `malloc` / `calloc` / `realloc` 出现的位置，
逐一确认都不在 emit 路径上：

| file:line | 调用 | 所在函数 | 是否在 emit 路径 | 备注 |
| --- | --- | --- | --- | --- |
| platform/linux/port.c:63 | `return malloc(size)` | `ns_platform_alloc` | **否** | 公共分配函数，emit 不调用 |
| platform/macos/port.c:106 | `return malloc(size)` | `ns_platform_alloc` | **否** | 同上 |
| platform/windows/port.c:55 | `return malloc(size)` | `ns_platform_alloc` | **否** | 同上 |
| platform/linux/port.c:80 | `ns_platform_alloc` | `ns_platform_wakeup_create` | **否** | 仅 loop 创建时调用 |
| platform/linux/port.c:188 | `ns_platform_alloc` | `ns_platform_mutex_create` | **否** | 仅 signal init/connect 时调用 |
| platform/linux/port.c:262 | `ns_platform_alloc` | `ns_platform_thread_create` | **否** | 仅 loop start 时调用 |
| platform/linux/port.c:311 | `ns_platform_alloc` | `ns_platform_waitset_create` | **否** | 仅 broker/loop 初始化时调用 |
| platform/macos/port.c:123 | `ns_platform_alloc` | `ns_platform_wakeup_create` | **否** | 同上 |
| platform/macos/port.c:209 | `ns_platform_alloc` | `ns_platform_mutex_create` | **否** | 同上 |
| platform/macos/port.c:283 | `ns_platform_alloc` | `ns_platform_thread_create` | **否** | 同上 |
| platform/macos/port.c:327 | `ns_platform_alloc` | `ns_platform_waitset_create` | **否** | 同上 |
| platform/windows/port.c:72 | `ns_platform_alloc` | `ns_platform_wakeup_create` | **否** | 同上 |
| platform/windows/port.c:150 | `ns_platform_alloc` | `ns_platform_mutex_create` | **否** | 同上 |
| platform/windows/port.c:221 | `ns_platform_alloc` | `ns_platform_thread_create` | **否** | 同上 |
| platform/windows/port.c:272 | `ns_platform_alloc` | `ns_platform_waitset_create` | **否** | 同上 |
| src/nanosig.c:132 | `ns_platform_alloc` | `ns_loop_init` | **否** | 仅 loop 创建时调用 |
| src/ns_broker.c:291 | `ns_platform_alloc` | `ns_event_broker_create` | **否** | 仅 broker 创建时调用 |

## 满队 / 边界行为

### `try_pushv` 满队返回时是否分配？

**不分配。**

```c
// src/ds/ns_mpsc_record_ring.c:366-367
if(!ns_mpsc_record_ring_plan_push(ring, record_size, write_pos, read_pos, &plan)){
    return NS_E_QUEUE_FULL;
}
```

`ns_mpsc_record_ring_plan_push`（第 182-225 行）只做算术运算和指针计算，不写任何外部状态、不分配内存。
满队时立即返回 `NS_E_QUEUE_FULL`，不触发任何回退分配、不展开任何后备路径。

### 跨段拷贝（payload 拆成 header + body 两段写）是否分配中间 buffer？

**不分配。**

`ns_mpsc_record_ring_write_parts`（第 152-167 行）逐段循环拷贝：

```c
static void ns_mpsc_record_ring_write_parts(
    ns_mpsc_record_ring_t *ring, size_t record_pos,
    const ns_mpsc_record_part_t *parts, size_t part_count)
{
    uint8_t *dst = ring->storage + ns_mpsc_record_ring_offset(ring,
                    record_pos + sizeof(ns_mpsc_record_header_t));
    size_t i;
    for(i = 0u; i < part_count; ++i){
        if(parts[i].size != 0u){
            memcpy(dst, parts[i].data, parts[i].size);
            dst += parts[i].size;
        }
    }
}
```

- 不申请临时 buffer，不拼接后再拷
- 直接从 `parts[i].data`（即 call_header 地址和 payload 地址）memcpy 到 ring storage
- dst 指针在每次拷贝后推进，保证两段在 ring 中连续存储
- 前置条件 `plan_push` 已通过 fake record 保证了真实记录整体连续

## 严重度等级

- **Critical** —— emit 路径发现 alloc（违反 v1 核心承诺）。
- **Major** —— emit 路径可能间接 alloc（如 lock 实现偶发 alloc）。
- **Info** —— 命名 / 风格。

## 关键发现

### 发现 1：emit 路径零分配契约成立

整条路径从 `ns_signal_emit` 宏展开到 `ns_mpsc_record_ring_try_pushv` -> `write_parts` -> `ns_platform_wakeup_signal`，
经过逐函数代码走读和三平台后端验证，**未发现任何 `ns_platform_alloc` / `malloc` / `calloc` / `realloc` 调用**。

### 发现 2：port.h 注释明确要求 wakeup_signal 零分配

`platform/port.h` 第 118 行注释：
> `ns_platform_wakeup_signal` — 本函数可从跨线程 emit 或控制路径调用，**不允许分配内存**。

该约束在所有三个平台后端都被正确遵守。

### 发现 3：scatter-gather 写无中间缓冲区

MPSC try_pushv 的 write_parts 不申请临时 buffer，直接逐段 memcpy 到 ring storage，
且通过 fake record 保证目标范围在环中连续，无需处理绕回分段拷贝。

### 发现 4：所有分配均发生在初始化/创建路径

全面扫描表明，项目中所有 `ns_platform_alloc` 调用仅出现在以下路径：

| 路径 | 分配内容 |
| --- | --- |
| `ns_loop_init` | loop 结构体（含 queue storage） |
| `ns_platform_mutex_create` | mutex 句柄 |
| `ns_platform_wakeup_create` | wakeup 句柄（eventfd / kq 包装） |
| `ns_platform_thread_create` | thread 句柄 |
| `ns_platform_waitset_create` | waitset 句柄 |
| `ns_event_broker_create` | broker 结构体 |

所有这些都在 emit 路径之外，属于一次性初始化开销。

## 结论

**v1 emit 零分配契约成立。** 以下结论基于逐函数代码走读和三平台后端（Linux x86_64 / macOS arm64 / Windows x64）验证：

1. emit 路径上**零次** `ns_platform_alloc` / `malloc` / `calloc` / `realloc` 调用
2. 满队返回 `NS_E_QUEUE_FULL` 路径**不触发任何后备分配**
3. scatter-gather 双段拷贝**不使用中间 buffer**，直接 memcpy 到 MPSC ring storage
4. 所有平台的 mutex lock/unlock 和 wakeup_signal 后端实现**均不分配内存**
5. 全部 17 处 `ns_platform_alloc` 出现位置均在初始化/创建路径，emit 路径之外

### 建议

- 无。契约被严格遵守，emit 路径保持零分配。
- 后续新增平台后端（如 RTOS、Zephyr）时，Code Review 应重点关注新的
  `ns_platform_mutex_lock` 和 `ns_platform_wakeup_signal` 实现是否引入隐藏分配。
