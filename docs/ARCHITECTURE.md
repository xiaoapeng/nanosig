# nanosig 架构

> nanosig v1 / 2026-06-20

nanosig 是一个 C11 线程安全 signal/slot 库，面向 Linux、macOS 和 Windows。本文档描述 v1 的架构设计、数据流和所有权模型。

---

## 1 三层架构

```
┌──────────────────────────────────────────┐
│  公开 API  /  include/nanosig/           │
│  类型安全的宏 + extern 函数声明           │
├──────────────────────────────────────────┤
│  运行时  /  src/                         │
│  signal / slot / loop / timer / broker   │
├──────────────────────────────────────────┤
│  数据结构  /  src/ds/                    │
│  list / slist / ringbuf / hashtable /    │
│  rbtree / MPSC record ring               │
├──────────────────────────────────────────┤
│  平台层  /  platform/                    │
│  port.h 抽象 + linux/macos/windows 后端  │
└──────────────────────────────────────────┘
```

**依赖方向**：平台层 ← 数据结构 ← 运行时 ← 公开 API。下层不依赖上层。

**耦合约束**：

- `src/` 和 `include/nanosig/` 不得包含 OS 头文件或写 `#ifdef _WIN32` 分支。
- 所有 OS 耦合集中在 `nanosig/nanosig_port.h` 的声明和 `platform/*/port.c` 的实现。
- 运行时通过 `ns_platform_*` 接口使用 OS 能力，不直接依赖 pthread / Win32 API。

---

## 2 运行时组件

### 2.1 `ns_loop_t` — 事件循环

每个 event loop 独立拥有：

- 一个 MPSC record ring（用于接收跨线程 emit）
- 一个平台 wakeup（用于线程间通知）
- 一个 `quit_requested` 原子标志

`ns_loop_run(loop)` 在主循环中：
1. 等待 wakeup（阻塞）
2. 调用 `ns_loop_dispatch_pending()` —— 用 `try_acquire` + `release` 零拷贝 drain MPSC ring
3. 检查 `quit_requested`

`ns_loop_quit(loop)` 从任意线程设置 `quit_requested` 并 signal wakeup。

```
    调用方线程                         跨线程 producer
    ┌───────────────┐                ┌──────────────────┐
    │ ns_loop_run() │                │ ns_signal_emit() │
    │  ├ wakeup_wait│  ← wakeup ←───│  └→ try_pushv()  │
    │  ├ dispatch   │  ← drain ─────│   (scatter-gather)│
    │  │  └→ slot() │               └──────────────────┘
    │  ├ check quit │
    │  └─ loop ────→│
    └───────────────┘
```

### 2.2 `ns_signal_t` — signal 对象

signal 拥有一个有头双向链表 `slot_list`，由内部 mutex 保护。

- `ns_signal_emit_raw`：加锁 → 遍历 `slot_list` → 对每个 connection 向对应的 `target_loop->ring` 推入 `ns_slot_call_t` → 解锁 → signal 该 loop 的 wakeup。
- `ns_signal_connect` / `disconnect`：修改 `slot_list`（同样在 mutex 内）。
- emit 路径不分配：所有 payload 在入队时按固定大小 memcpy 到 ring 的预分配存储区。

```
          signal (mutex 保护 slot_list)
    ┌─────────────────────────────────────┐
    │ slot_list ─→ conn1 ─→ conn2 ─→ ... │
    │              │         │            │
    │              ▼         ▼            │
    │         loop_A.ring  loop_B.ring    │
    └─────────────────────────────────────┘
```

### 2.3 `ns_connection_t` — 连接

connection 是调用方**自持存储**。库不分配也不释放它。

```
struct ns_connection {
    ns_signal_t   *signal;       // 所属 signal
    ns_slot_fn     slot_fn;      // 回调函数
    void          *user_data;    // 调用方数据
    ns_loop_t     *target_loop;  // 目标 loop（必须非空）
    ns_list_node_t signal_node;  // signal 的 slot_list 节点
};
```

### 2.4 `ns_timer_t` — 定时器

每个 `ns_timer_t` 内嵌无 payload 的 `ns_signal_t`，到期时触发。

- `ns_timer_init`：初始化 timer + 内嵌 signal（不启动）。
- `ns_timer_start`：注册到全局 `ns_timer_mgr_t` 的 rbtree（按 deadline 排序）。
- `ns_timer_mgr` 是独立模块，通过回调 `ns_timer_notify_fn` 通知 broker 重新计算超时。
- `ns_timer_cancel`：从 rbtree 移除。
- `ns_timer_deinit`：清理内嵌 signal。

### 2.5 `ns_watcher_t` — 事件监视器

watcher 将平台事件（fd/handle readiness）转换为 signal emit。

```
ns_watcher_t
├─ signal（内嵌 ns_signal_t，payload 为 ns_watcher_event_t）
├─ waitable（ns_platform_waitable_t，可注册到 waitset）
└─ broker_node（broker 内部链表节点）
```

### 2.6 `ns_event_broker_t` — 全局事件调度器

broker 是全局单例，随 `ns_init()` 创建，随 `ns_shutdown()` 销毁。

拥有：

- 1 个 broker 线程
- 1 个 waitset（所有 watcher 的 waitable 注册到这里）
- 1 个 `ns_timer_mgr_t`
- 1 个 wakeup（注册到自己的 waitset，用于 timer 通知唤醒）

主循环：

```
1.  timeout = timer_mgr_next_deadline()
2.  waitset_wait(timeout)
3.  process completions → ns_signal_emit_raw(watcher->signal, ...)
4.  timer_mgr_fire_expired()
5.  goto 1
```

broker 与 loop **完全解耦**：broker 不直接操作 loop 的 wakeup 或 MPSC ring。所有事件通过 signal emit 投递。

```
     event arrival
          │
          ▼
   ┌──────────────┐
   │ broker 线程   │
   │              │
   │ watcher→emit │ ──┐
   │ timer→fire   │ ──┤  signal → MPSC ring → loop dispatch → slot
   └──────────────┘   └─────────────────────────────────────────┘
```

---

## 3 跨线程数据流

### emit → dispatch 全链路

```
调用方线程                         接收者线程 (ns_loop_run)
┌─────────────────┐              ┌────────────────────────┐
│ ns_signal_emit   │              │ wakeup_wait            │
│  ├ lock mutex   │              │   ↑ (wakeup signal)    │
│  ├ for each conn│              │ dispatch_pending       │
│  │  try_pushv   │── ring ─────→│   ├ try_acquire        │
│  │  (memcpy)    │              │   ├ call slot_fn       │
│  │  store_release│              │   └ release            │
│  ├ unlock       │              │ check quit             │
│  │ wakeup_signal│── wakeup ───→│ loop                   │
│  └───────────────┘              └────────────────────────┘
```

### 内存序同步链

| 步骤 | producer 侧 | memory order | consumer 侧 |
|------|-------------|-------------|-------------|
| 1 | 写 payload 到 ring slot | relaxed | — |
| 2 | store `write_pos` | **release** | — |
| 3 | signal wakeup | release | — |
| 4 | — | — | **acquire** | wakeup wait 返回 |
| 5 | — | — | **acquire** | load `write_pos` |
| 6 | — | — | 读 payload | 可见（happens-before 链保证） |
| 7 | — | — | store `read_pos` | **release** |

### quit → stop 同步

- `ns_loop_quit`：`store_release(quit_requested, 1)` → `wakeup_signal()`。
- `ns_loop_run` 主循环：`wakeup_wait()` → `load_acquire(quit_requested)` → exit。
- 类似模式用于 broker 的 `quit_requested`。

---

## 4 线程安全模型

| API 类别 | 安全等级 | 保护机制 |
|----------|---------|----------|
| `ns_signal_emit_raw` | MPM-safe | mutex（slot_list）+ lock-free MPSC（push） |
| `ns_signal_connect` | MPM-safe | mutex |
| `ns_signal_disconnect` | MPM-safe | mutex |
| `ns_timer_start/cancel/restart` | MPM-safe | timer_mgr mutex + rbtree |
| `ns_broker_add/remove` | MPM-safe | broker internal lock |
| `ns_loop_run` | 单线程 | 调用方线程绑定 |
| `ns_loop_quit` | MPM-safe | atomic store release |
| `ns_loop_init/destroy` | 需序列化 | 调用方保证 |
| `ns_signal_init_raw/deinit_raw` | 需序列化 | 调用方保证 |
| `ns_timer_init/destroy` | 需序列化 | 调用方保证 |
| `ns_watcher_init_*/deinit` | 需序列化 | 调用方保证 |
| `ns_init/ns_shutdown` | 需序列化 | 调用方保证 |
| DS 函数（list/rbtree/hashtable/ringbuf）| 单线程 | 外部同步 |
| `ns_mpsc_record_ring_try_pushv` | MPM-safe | fetch_add relaxed（slot 分配） |
| `ns_mpsc_record_ring_try_acquire` | 单线程 | 仅 consumer 调用 |

> 注：`ns_signal_init_raw` 初始化完成后，connect / disconnect / emit 才可多线程并发。

---

## 5 所有权模型

| 类型 | 存储所有者 | 分配方式 | 销毁方式 |
|------|-----------|---------|---------|
| `ns_loop_t` | 调用方 | `ns_loop_init`（`ns_platform_alloc`） | `ns_loop_deinit` |
| `ns_signal_t` | 调用方 | 静态/栈/堆；`ns_signal_init` 只分配 mutex | `ns_signal_deinit` |
| `ns_connection_t` | 调用方 | 调用方完全拥有存储 | 调用方自由处置 |
| `ns_timer_t` | 调用方 | 静态/栈/堆；`ns_timer_init` 只 init + mutex | `ns_timer_deinit` |
| `ns_watcher_t` | 调用方 | 静态/栈/堆；`ns_watcher_init` 只 init + mutex | `ns_watcher_deinit` |
| `ns_event_broker_t` | 库 | `ns_init` 时 `ns_platform_alloc` | `ns_shutdown` 时释放 |
| MPSC record ring | `ns_loop_t` 内联 | 内嵌在 `ns_loop_init` 的分配块中 | `ns_loop_deinit` 隐式 |

**emit 路径零分配**：从 `ns_signal_emit_raw` 到 MPSC `try_pushv` 返回的整条调用链上，不调用任何 `ns_platform_alloc` 或标准库 `malloc` / `calloc` / `realloc`。

---

## 6 公开 API 头文件索引

| 头文件 | 主要类型 | 用途 |
|--------|---------|------|
| `nanosig.h` | （聚合） | 包含所有公开头 |
| `nanosig_loop.h` | `ns_loop_t`, `ns_loop_config_t` | loop 生命周期与管理 |
| `nanosig_signal.h` | `ns_signal_t`, `ns_connection_t` | signal/slot |
| `nanosig_timer.h` | `ns_timer_t` | 定时器 |
| `nanosig_broker.h` | `ns_event_broker_t`, `ns_watcher_t` | 事件 broker |
| `nanosig_port.h` | `ns_platform_waitable_t` | waitable 类型 |
| `nanosig_status.h` | `NS_OK`, `NS_E_*` | 状态码 |
| `nanosig_types.h` | 通用宏 | `NS_CONTAINER_OF`, `NS_STATIC_ASSERT` |
| `nanosig_atomic.h` | 原子操作 | `ns_atomic_load`, `ns_atomic_store` |
| `nanosig_safety.h` | — | v1 占位 |
| `nanosig_list.h` | `ns_list_node_t` | 双向链表 |
| `nanosig_slist.h` | `ns_slist_node_t` | 单向链表 |
| `nanosig_ringbuf.h` | `ns_ringbuf_t` | 环形缓冲区 |
| `nanosig_hashtable.h` | `ns_hashtable_t` | 哈希表 |
| `nanosig_rbtree.h` | `ns_rbtree_t` | 红黑树 |
| `nanosig_mpsc_record_ring.h` | `ns_mpsc_record_ring_t` | MPSC record ring |

---

## 7 平台层快速参考

详见 `platform/README.md`。三后端对比：

| 能力 | Linux | macOS | Windows |
|------|-------|-------|---------|
| mutex | pthread_mutex_t | pthread_mutex_t | SRWLOCK |
| 线程 | pthread_create / join | pthread_create / join | CreateThread / WaitForSingleObject |
| wakeup | eventfd / pipe | kqueue EVFILT_USER | auto-reset event |
| 单调时间 | clock_gettime(CLOCK_MONOTONIC) | clock_gettime(CLOCK_MONOTONIC) | QueryPerformanceCounter |
| waitset | epoll_create1 / epoll_ctl / epoll_wait | kqueue / kevent | WaitForMultipleObjects |
| 边沿触发 | EPOLLET | EV_CLEAR | 不支持 |
| waitset 容量 | 无硬上限 | 无硬上限 | 64 handle |
| timeout 后端 | timerfd | kevent timespec | WaitableTimer |

---

## 8 v1 交付范围

- **P0–P9 完成**（2026-06-20）。涵盖：脚手架、公开 API 收口、三后端 loop 平台、公开数据结构、MPSC record ring、loop 管理、signal/slot、waitset 契约、timer + broker、验收 demo、bench、审计。
- **不包含**：ISR 安全（`*_from_isr` API）、RTOS 后端、多线程 broker（v1 为 1 线程 + 1 waitset）。
- **质量基线**：macOS Release 零警告，ctest 16 个运行时测试 + 6 个 compile check，3 个 bench 已归档，ASAN/TSAN/UBSAN 配置就绪但需 Linux 环境验证。
