# nanosig event broker 详细设计

状态：已实现。
更新时间：2026-06-19。

## 概述

event broker 是 nanosig 的事件等待与分发模块。它拥有一个线程和一个
waitset，负责等待事件源（watcher）就绪，就绪时 emit 绑定的 signal。
timer 是内置事件源，通过 timer_mgr 的 notify 回调唤醒 broker。

broker 是全局单例，随 `ns_init()` 初始化，随 `ns_shutdown()` 销毁。

## 架构

```
用户层：
  ns_watcher_init_fd / init_handle / deinit
  ns_signal_connect(&watcher->signal, slot, ...)
  ns_broker_add(broker, watcher)
  ns_broker_remove(broker, watcher)

broker 层（独立模块）：
  g_broker（全局单例）
  thread + waitset + wakeup
  watcher 链表（注册的 watcher）
  timer_mgr（内置事件源）

timer_mgr 层（独立模块）：
  g_timer_mgr（全局单例）
  rbtree 按 remaining time 排序
  notify 回调唤醒 broker

平台层：
  ns_platform_thread_create / join
  ns_platform_waitset
  ns_platform_wakeup_get_waitable
```

## 依赖关系

```
用户 → ns_watcher_* / ns_broker_*（公开 API）
用户 → ns_timer_*（公开 API）
broker → timer_mgr（生命周期 + 运行时）
broker → waitset（等待事件）
broker → signal_emit（触发 watcher/timer 的 signal）
timer_mgr → notify 回调（通知 broker，不依赖 broker）
loop ← signal_emit（通过 MPSC ring 接收 slot 调用）
```

三层各司其职，单向依赖，没有环。broker 不直接操作 loop 的 wakeup 或
MPSC ring；所有投递通过 signal emit 完成。

## 公开类型

### 事件位

从 `platform/port.h` 提升到公开头文件：

```c
#define NS_WAITABLE_EVENT_IN   (1u << 0)  /* 可读 / signaled */
#define NS_WAITABLE_EVENT_OUT  (1u << 1)  /* 可写 */
#define NS_WAITABLE_EVENT_ERR  (1u << 2)  /* 错误 */
```

### watcher 事件 payload

```c
typedef struct ns_watcher_event {
    uint32_t triggered_events;  /* NS_WAITABLE_EVENT_* 组合 */
} ns_watcher_event_t;
```

watcher signal 的 payload 类型。slot 收到此结构体指针，可判断触发了
哪些事件。

### watcher 对象

```c
typedef struct ns_watcher {
    ns_signal_t              signal;       /* 第一个字段，事件触发时 emit */
    ns_platform_waitable_t   waitable;     /* 内嵌 waitable */
    ns_list_node_t           broker_node;  /* broker 内部链表节点 */
} ns_watcher_t;
```

- 调用方自持存储（栈变量、堆分配、嵌入结构体均可）。
- `signal` 必须是第一个字段，与 `ns_timer_t` 保持一致。
- `waitable` 由 `ns_watcher_init_*` 填充。
- `broker_node` 由 broker 内部使用，调用方不得访问。

当前实现选择直接内嵌 `ns_platform_waitable_t`，不再额外包一层 opaque
私有结构，也不把 waitable 从 `ns_watcher_t` 中隐藏。公开 watcher API
通过 `ns_watcher_init_fd` / `ns_watcher_init_handle` 封装常规初始化路径；
调用方通常不需要直接改平台句柄成员，但结构布局保持直接可见。

## 公开 API

### watcher 生命周期

```c
int ns_watcher_init_fd(
    ns_watcher_t *watcher,
    int fd,
    uint32_t events,
    int edge_triggered);

int ns_watcher_init_handle(
    ns_watcher_t *watcher,
    void *handle,
    uint32_t events,
    int edge_triggered);

int ns_watcher_deinit(ns_watcher_t *watcher);
```

- `init_fd`：Linux/macOS 文件描述符。内部调 `ns_signal_init_raw`（payload_size
  = `sizeof(ns_watcher_event_t)`），填充 `watcher->waitable`（fd、events、
  edge_triggered），初始化 `broker_node`。
- `init_handle`：Windows HANDLE。同上，填充 `handle` 字段。
- 两个 init 函数都是正常公开初始化入口，不引入 `NS_E_UNSUPPORTED` 或按
  平台隐藏符号。参数有效时应完成 signal、waitable 和 broker_node 初始化；
  调用方负责在当前平台传入能被对应 waitset 后端等待的 fd 或 HANDLE。
- `deinit`：释放 signal 内部资源，重置 waitable。调用前必须已
  `ns_broker_remove`。
- `deinit` 只对成功初始化的 watcher 返回成功。`ns_watcher_init_*`
  失败时会把 watcher 重置为空未初始化状态；随后调用 `deinit` 返回
  `NS_E_INVAL`，不静默伪装成成功清理。
- `events` 是 `NS_WAITABLE_EVENT_IN/OUT/ERR` 的位组合。
- `edge_triggered`：1 = 边沿触发（Linux `EPOLLET` / macOS `EV_CLEAR`），0 = 电平触发。

### broker 接口

```c
ns_event_broker_t *ns_broker(void);

int ns_broker_add(ns_watcher_t *watcher);

int ns_broker_remove(ns_watcher_t *watcher);
```

- `ns_broker()`：返回全局 broker 指针（仅供内部和测试使用）。`ns_init` 前返回 `NULL`。
  公开 API 用户通常不需要此函数；`add` / `remove` 自动访问全局 broker。
- `add`：注册 watcher 到 broker 的 waitset。同一 watcher 重复 add 返回
  `NS_E_EXISTS`。内部设置 `watcher->waitable.user_data = watcher`，平台
  waitset add 维护 `watcher->waitable.registered_waitset`。
- `remove`：从 waitset 注销 watcher，平台 waitset remove 清除 waitable
  注册状态。已入队的 slot 调用不撤回。

## 内部结构

### broker 结构体

```c
struct ns_event_broker {
    ns_platform_thread_t    *thread;
    ns_platform_waitset_t   *waitset;
    ns_platform_wakeup_t    *wakeup;          /* broker 自己的 wakeup */
    ns_platform_waitable_t   wakeup_waitable; /* 注册到 waitset */
    ns_list_node_t           watcher_head;    /* 已注册 watcher 链表 */
    ns_platform_mutex_t     *watcher_mutex;   /* 保护链表 */
    atomic_int               quit_requested;
};
```

### 全局单例

```c
static ns_event_broker_t *g_broker = NULL;
```

`ns_init` 时创建并赋值，`ns_shutdown` 时销毁并置 `NULL`。

## broker 主循环

```c
static void broker_run(void *arg)
{
    ns_event_broker_t *b = (ns_event_broker_t *)arg;
    ns_platform_waitset_completion_t completions[16];
    size_t count;
    size_t i;

    while(atomic_load_explicit(&b->quit_requested, ns_memory_order_acquire) == 0){
        /* 1. 取 timer deadline */
        ns_platform_time_us_t timeout = NS_PLATFORM_WAIT_INFINITE_US;
        (void)ns_timer_mgr_next_timeout(&timeout);

        /* 2. 等待事件 */
        (void)ns_platform_waitset_wait(
            b->waitset, timeout, completions, 16, &count);

        /* 3. 处理 watcher 事件 */
        for(i = 0; i < count; i++){
            ns_watcher_t *w =
                (ns_watcher_t *)completions[i].waitable->user_data;
            if(w != NULL){
                ns_watcher_event_t event;
                event.triggered_events = completions[i].triggered_events;
                (void)ns_signal_emit_raw(
                    &w->signal, &event, sizeof(event));
            }
        }

        /* 4. fire 过期 timer */
        (void)ns_timer_mgr_fire_expired();
    }
}
```

### 事件处理顺序

1. watcher 事件先于 timer fire。
2. 同一批 watcher 事件按 completion 数组顺序处理。
3. 单个 emit 失败不影响后续 watcher 或 timer fire。

### wakeup 机制

broker 的 waitset 里始终注册一个 wakeup waitable。以下场景触发
wakeup：

- `ns_timer_start` / `ns_timer_cancel` / `ns_timer_restart` 通过
  timer_mgr 的 notify 回调调 `ns_broker_notify()`，内部
  `wakeup_signal(broker->wakeup)`。
- `ns_broker_destroy` 时 wakeup signal 唤醒 broker 线程退出。

wakeup 返回后 broker 重新计算 timeout 并继续循环。

## add / remove 流程

### ns_broker_add

```
1. mutex_lock(broker->watcher_mutex)
2. 检查 watcher->broker_node 是否已链接（已链接 → NS_E_EXISTS）
3. watcher->waitable.user_data = watcher
4. waitset_add(broker->waitset, &watcher->waitable)（成功后设置 registered_waitset）
5. list_push_back(&broker->watcher_head, &watcher->broker_node)
6. mutex_unlock
```

### ns_broker_remove

```
1. mutex_lock(broker->watcher_mutex)
2. 检查 watcher->broker_node 是否已链接（未链接 → NS_E_INVAL）
3. waitset_remove(broker->waitset, &watcher->waitable)（成功后清除 registered_waitset）
4. list_remove_init(&watcher->broker_node)
5. mutex_unlock
```

### 线程安全

- `add` / `remove` 通过 `watcher_mutex` 保护链表操作。
- `waitset_add` / `waitset_remove` 在 mutex 内调用，保证与 broker
  线程的 `waitset_wait` 不并发操作同一 waitset。
- 用户线程调 `add` / `remove`，broker 线程调 `waitset_wait` +
  `fire_expired`。两者通过 mutex 隔离。

## 生命周期协议

### ns_init

```
ns_init()
  → ns_platform_init()
  → ns_broker_init()
    → ns_platform_alloc(broker)
    → ns_platform_wakeup_create(&broker->wakeup, "broker")
    → ns_platform_waitset_create(&broker->waitset)
    → broker->wakeup_waitable = ns_platform_wakeup_get_waitable(broker->wakeup)
    → broker->wakeup_waitable.events = NS_WAITABLE_EVENT_IN
    → ns_platform_waitset_add(broker->waitset, &broker->wakeup_waitable)
    → ns_platform_mutex_create(&broker->watcher_mutex)
    → ns_list_init(&broker->watcher_head)
    → ns_timer_mgr_global_init(ns_broker_notify, broker)
    → atomic_init(&broker->quit_requested, 0)
    → ns_platform_thread_create(&broker->thread, broker_run, broker)
    → g_broker = broker
  → atomic_store(g_ns_initialized, 1)
```

`g_broker` 在 broker 对象完整初始化且 broker 线程创建成功后发布。timer
manager 的 notify 回调使用非空 `ctx` 指向 broker，不依赖 `g_broker`
提前可见。若线程创建或更早步骤失败，失败路径必须回收已创建资源，且不得
发布 `g_broker`。

### ns_shutdown

```
ns_shutdown()
  → 调用方已按前置条件销毁 loop/timer 并注销 watcher
  → atomic_store(g_ns_initialized, 0)
  → ns_broker_destroy()
    → atomic_store(broker->quit_requested, 1)
    → ns_platform_wakeup_signal(broker->wakeup)
    → ns_platform_thread_join(broker->thread)
    → g_broker = NULL
    → ns_timer_mgr_global_shutdown()
    → mutex_lock(broker->watcher_mutex)
    → 遍历 watcher_list：waitset_remove + clear user_data + list_remove_init
    → mutex_unlock
    → waitset_remove(wakeup_waitable)
    → waitset_destroy
    → wakeup_destroy
    → mutex_destroy
    → ns_platform_free(broker)
  → ns_platform_shutdown()
```

### 用户职责

- `ns_watcher_init` 必须在 `ns_broker_add` 之前调用。
- `ns_broker_remove` 必须在 `ns_watcher_deinit` 之前调用。
- `ns_broker_remove` 应在 `ns_shutdown` 之前调用。若仍有 watcher 残留，
  `ns_shutdown` 的 broker 销毁路径必须至少把这些 watcher 从 waitset
  注销、清除 waitable 注册状态并把 `broker_node` 重新初始化。
- `ns_timer_destroy` 必须在 `ns_shutdown` 之前调用。
- `ns_loop_destroy` 必须在 `ns_shutdown` 之前调用（现有逻辑）。

### 竞态：remove 时已入队的 emit

`ns_broker_remove` 从 waitset 注销 watcher 后，不再有新的 completion
指向该 watcher。但已通过 `ns_signal_emit_raw` 入队的 slot 调用不会被
撤回。这与 `ns_signal_disconnect` 语义一致。

用户通过 `ns_signal_connect` 绑定 slot 时的 `user_data` 生命周期必须
长于任何 in-flight emit。

## 错误处理

- broker 线程内 `waitset_wait` 或 `fire_expired` 返回错误时 continue，
  不终止 broker 线程。单次错误不中断服务。
- `ns_signal_emit_raw` 返回错误（如 MPSC 满）时 continue，不影响后续
  watcher 或 timer。

## 平台层新增接口

### ns_platform_thread_create / join

```c
typedef struct ns_platform_thread ns_platform_thread_t;
typedef void (*ns_platform_thread_fn)(void *arg);

int ns_platform_thread_create(
    ns_platform_thread_t **out_thread,
    ns_platform_thread_fn entry,
    void *arg,
    const char *debug_name);

int ns_platform_thread_join(ns_platform_thread_t *thread);
```

- Windows：`CreateThread` + wrapper 函数适配 `LPTHREAD_START_ROUTINE`。
- Linux/macOS：`pthread_create`。
- `join` 后释放线程句柄和内部结构体。

### ns_platform_wakeup_get_waitable

```c
ns_platform_waitable_t ns_platform_wakeup_get_waitable(
    ns_platform_wakeup_t *wakeup);
```

- Windows：`w->handle = wakeup->event`。
- Linux：`w->fd = wakeup->fd`。
- macOS：`w->fd = wakeup->kq`（kqueue fd，内部注册 `EVFILT_USER`）。
- `events` 和 `user_data` 由调用方设置。

## 与 timer 的集成

timer_mgr 的 notify 回调 = `ns_broker_notify`。timer start / cancel /
restart 时内部调 `notify(ctx)` → `ns_broker_notify()` →
`wakeup_signal(broker->wakeup)`。

`ns_broker_notify` 是零分配、可跨线程调用的函数：

```c
void ns_broker_notify(void *ctx)
{
    ns_event_broker_t *broker = ctx;

    if(broker != NULL){
        (void)ns_platform_wakeup_signal(broker->wakeup);
    }
}
```

timer_mgr 不知道 broker 的存在，只知道一个 `void *ctx` 和一个回调
函数。broker 通过 `global_init(notify, ctx)` 注册自己。

## 与 loop 的关系

broker 和 loop **完全解耦**：

- broker 不持有 loop 引用。
- broker 不直接操作 loop 的 wakeup 或 MPSC ring。
- broker 通过 `ns_signal_emit_raw` 投递事件，emit 内部处理 loop 投递。
- loop 不知道 broker 的存在。

broker 线程 emit watcher/timer signal → emit 遍历 slot_list → 每个
connection 的 target_loop 的 MPSC ring 写入 → wakeup target_loop →
用户线程 `ns_loop_run` 从 wakeup_wait 返回 → dispatch_pending →
slot 回调执行。

## 架构总览

```
┌─────────────────────────────────────────────────────┐
│                ns_event_broker_t (全局单例)           │
│                                                      │
│  ┌──────────────┐  ┌──────────────┐                  │
│  │  thread       │  │  waitset      │                  │
│  └──────────────┘  └──────────────┘                  │
│                                                      │
│  ┌──────────────────────────────────────┐            │
│  │  watcher_list (双向链表)              │            │
│  │  每个 watcher 内嵌 waitable + signal │            │
│  └──────────────────────────────────────┘            │
│                                                      │
│  ┌──────────────────────────────────────┐            │
│  │  ns_timer_mgr_t (rbtree)  [内置]     │            │
│  │  next_timeout() → μs                 │            │
│  │  fire_expired() → emit timer signal  │            │
│  │  start/cancel/restart → notify       │            │
│  └──────────────────────────────────────┘            │
│                                                      │
│  broker_run 主循环：                                  │
│    1. timeout = timer_mgr_next_timeout()             │
│    2. waitset_wait(timeout)                          │
│    3. 遍历 completions → emit watcher signal         │
│    4. timer_mgr_fire_expired()                       │
│    5. loop                                           │
│                                                      │
│  notify 回调链：                                      │
│    timer start/cancel/restart                        │
│    → timer_mgr notify(ctx)                           │
│    → ns_broker_notify()                              │
│    → wakeup_signal(broker->wakeup)                   │
│    → waitset_wait 返回 → 重新计算 timeout             │
└─────────────────────────────────────────────────────┘

┌──────────────────────────────────────┐
│           ns_loop_t                   │
│  MPSC queue + wakeup                 │
│                                      │
│  ns_loop_run:                        │
│    wakeup_wait() → 等 signal 唤醒    │
│    dispatch_pending()                │
│    loop                              │
└──────────────────────────────────────┘

用户使用模式：
  watcher_init → signal_connect → broker_add
  事件到达 → broker emit → loop dispatch → slot 执行
  broker_remove → signal_disconnect → watcher_deinit
```

## 扩展路径

- v2：N 线程 + N waitset。1 个负责 timeout（timer waitset），其余等待
  无限（IO waitset）。add 时按策略分配到某个 waitset。
- v2：RTOS event group 支持。`watcher->waitable.event_bit` 预留。
- v2：watcher 支持 payload（传递更多事件元数据）。
- v2：broker 线程错误上报（连续错误时通知用户）。
