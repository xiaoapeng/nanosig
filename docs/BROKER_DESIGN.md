# nanosig event broker 详细设计

状态：已实现。
更新时间：2026-06-19。

## 概述

event broker 是 nanosig 的事件等待与分发模块。它拥有一个线程和一个
waitset，负责等待事件源（watcher）就绪，就绪时调用 watcher 绑定的 handler。
handler 是 watcher 唯一的就绪出口；broker 不再为 watcher emit 任何 signal。
timer 是内置事件源，通过 timer_mgr 的 notify 回调唤醒 broker，并由 timer_mgr
为 timer 自己的 signal emit（这是有意保留的不对称，见"与 timer 的集成"）。

broker 是全局单例，随 `ns_init()` 初始化，随 `ns_shutdown()` 销毁。

## 架构

```
用户层：
  ns_watcher_init / deinit（handler 必填）
  ns_broker_add(broker, watcher)
  ns_broker_remove(broker, watcher)

broker 层（独立模块）：
  g_broker（全局单例）
  thread + waitset + event
  watcher 链表（注册的 watcher）
  timer_mgr（内置事件源）

timer_mgr 层（独立模块）：
  g_timer_mgr（全局单例）
  rbtree 按 remaining time 排序
  notify 回调唤醒 broker

平台层：
  ns_platform_thread_init / join（调用方拥有 thread 存储）
  ns_platform_waitset
  ns_platform_event_*（caller-owned waitable-first 事件原语）
```

## 依赖关系

```
用户 → ns_watcher_* / ns_broker_*（公开 API）
用户 → ns_timer_*（公开 API）
broker → timer_mgr（生命周期 + 运行时）
broker → waitset（等待事件）
broker → watcher->handler（watcher 就绪时直接回调）
timer_mgr → timer signal emit（timer_mgr 为 timer 触发内嵌 signal）
timer_mgr → notify 回调（通知 broker，不依赖 broker）
loop ← signal_emit（通过 MPSC ring 接收 slot 调用）
```

三层各司其职，单向依赖，没有环。broker 不直接操作 loop 的 wakeup 或
MPSC ring；watcher 就绪时 broker 只调用 handler，若用户需要把事件投递回
loop，由 handler 内自行 emit 用户拥有的 signal（`ns_signal_emit*` 非阻塞，
允许在 broker 线程调用），其余投递通过 signal emit 完成。

## 公开类型

### 事件位

从 `nanosig/nanosig_port.h` 提升到公开头文件：

```c
#define NS_WAITABLE_EVENT_IN   (1u << 0)  /* 可读 / signaled */
#define NS_WAITABLE_EVENT_OUT  (1u << 1)  /* 可写 */
#define NS_WAITABLE_EVENT_ERR  (1u << 2)  /* 错误 */
```

### watcher 对象

```c
typedef void (*ns_watcher_handler_fn)(ns_watcher_t *watcher, uint32_t triggered_events);

typedef struct ns_watcher {
    ns_platform_waitable_t   waitable;     /* 内嵌 waitable，第一个字段 */
    ns_watcher_handler_fn    handler;      /* 就绪回调，必填；同时是生命周期 marker */
} ns_watcher_t;
```

- 调用方自持存储（栈变量、堆分配、嵌入结构体均可）。
- `waitable` 必须是第一个字段。是否已注册由平台层在其 `registered_waitset`
  成员上维护（`add` 成功后写入、`remove` 成功后清除），broker 不自建平行标记。
- `handler` 必填，`ns_watcher_init` 对 NULL handler 返回 `NS_E_INVAL`。handler 是
  watcher 唯一的就绪出口：broker 线程在 waitable 就绪时调用
  `handler(watcher, triggered_events)`，不再为 watcher 内嵌或 emit signal。
- 调用方必须零初始化 `ns_watcher_t`（如 `ns_watcher_t w = {0};`）或仅经
  `ns_watcher_init` 产出；非零初始化的 garbage watcher 传入 `ns_watcher_deinit`
  属未定义输入。

当前实现选择直接内嵌 `ns_platform_waitable_t`，不再额外包一层 opaque
私有结构，也不把 waitable 从 `ns_watcher_t` 中隐藏。公开 watcher API
通过 `ns_watcher_init` 封装常规初始化路径；
调用方通常不需要直接改平台句柄成员，但结构布局保持直接可见。

## 公开 API

### watcher 生命周期

```c
int ns_watcher_init(
    ns_watcher_t *watcher,
    ns_waitable_handle_t handle,
    uint32_t events,
    int edge_triggered,
    ns_watcher_handler_fn handler);

int ns_watcher_deinit(ns_watcher_t *watcher);
```

- `init`：统一初始化入口，接受 `ns_waitable_handle_t`（平台内核原语的透明别名，
  由构建注入的平台头提供：POSIX 是 fd，Windows 是 HANDLE，RTOS 是对象指针）和必填
  `handler`。内部直接把句柄赋给 `watcher->waitable.primitive`（events、
  edge_triggered 同步设置），并保存 `handler`。
- `init` 校验顺序固定：`watcher == NULL` → `NS_E_INVAL`；`handler == NULL` →
  `NS_E_INVAL`；句柄/事件非法 → `NS_E_INVAL`；runtime 未就绪 → `NS_E_SHUTDOWN`。
- `init` **不做重复初始化判定**（统一策略，同 `ns_signal_init_raw`）：重复 init
  未先 deinit 属调用方错误（未定义行为），仅在公开头 `@warning` 说明，不返回
  `NS_E_EXISTS`。
- `init` 是正常公开初始化入口，不引入 `NS_E_UNSUPPORTED` 或按平台隐藏符号。
  参数有效时应完成 waitable 初始化；调用方负责在当前
  平台传入能被对应 waitset 后端等待的 fd 或 HANDLE。
- `deinit`：完整重置 `waitable` 并清零 `handler`。调用前必须已
  `ns_broker_remove`。未初始化的 watcher 返回 `NS_E_INVAL`。
- `events` 是 `NS_WAITABLE_EVENT_IN/OUT/ERR` 的位组合。
- `edge_triggered`：1 = 边沿触发（Linux `EPOLLET` / macOS `EV_CLEAR`），0 = 电平触发。

### handler 契约

handler 在 broker 线程执行，必须非阻塞。它**不得调用任何会等待 broker 线程的
nanosig API**（如 `ns_broker_add`、`ns_broker_remove`、`ns_shutdown` 及 broker
teardown），否则 broker 线程将**永久阻塞**（这些调用没有超时，不是可恢复的
错误返回）；`ns_signal_emit*` 属于非阻塞操作，**允许**调用。
电平触发的 watcher 必须在 handler 内把事件源读净，否则每轮 `waitset_wait`
都会重复触发。

### broker 接口

```c
ns_event_broker_t *ns_broker(void);

int ns_broker_add(ns_watcher_t *watcher);

int ns_broker_remove(ns_watcher_t *watcher);
```

- `ns_broker()`：返回全局 broker 指针（仅供内部和测试使用）。`ns_init` 前返回 `NULL`。
  公开 API 用户通常不需要此函数；`add` / `remove` 自动访问全局 broker。
- `add`：注册 watcher 到 broker 的 waitset。同一 watcher 重复 add 返回
  `NS_E_EXISTS`（由平台 waitset add 的 `registered_waitset` 判定）。内部设置
  `watcher->waitable.user_data = watcher`。
- `remove`：从 waitset 注销 watcher，平台 waitset remove 清除 waitable
  注册状态；未注册时返回 `NS_E_INVAL`。已入队的 slot 调用不撤回。
- `add` / `remove` 是**同步会合**：请求经内部 op 队列交给 broker 线程执行，
  完成后才返回。**没有等待上界**——handler 阻塞或平台等待原语持续失败时调用
  不会返回。这两个函数可由多线程并发调用；内部经 op 队列串行化到 broker 线程。

## 内部结构

### broker 结构体

```c
struct ns_event_broker {
    ns_platform_thread_t    *thread;
    ns_platform_waitset_t   *waitset;
    ns_platform_event_t      event;          /* broker 自唤醒事件源（waitable 注册到 waitset） */
    atomic_int               quit_requested; /* loop 退出信号 */
    ns_platform_mutex_t     *op_lock;        /* 串行化 add/remove 入队 */
    ns_list_node_t           op_queue_head;  /* 待处理 op 队列 */
    atomic_int               shutdown_started; /* ns_shutdown 中置位，早返回新请求 */
};
```

所有字段仅 broker 线程访问：user 线程通过 op 队列交接 add/remove，不直接触碰
waitset。此前用于保护 watcher 链表的 `watcher_mutex` 与 `watcher_head` 已删除
——「已注册」状态由平台层的 `registered_waitset` 唯一表达。

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
            if((w != NULL) && (w->handler != NULL)){
                w->handler(w, completions[i].triggered_events);
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

### 自唤醒机制

broker 的 waitset 里始终注册一个 `ns_platform_event_t` 的 waitable
（wakeup 仅保留阻塞单等待职责，不再暴露内部 waitable）。以下场景触发：

- `ns_timer_start` / `ns_timer_cancel` / `ns_timer_restart` 通过
  timer_mgr 的 notify 回调调 `ns_broker_notify()`，内部
  `ns_platform_event_signal(&broker->event)`。
- `ns_broker_destroy` 时 event signal 唤醒 broker 线程退出。

event 触发后 broker drain 该事件、重新计算 timeout 并继续循环。

## add / remove 流程

### ns_broker_add

```
1. 构造 op_request（op = ADD，自建 wakeup）并入 op_queue
2. event_signal 唤醒 broker 线程；signal 失败则在 op_lock 内回滚出队并返回错误
3. 用户线程阻塞等待 req.wakeup（无超时）
4. broker 线程 drain op_queue：
   watcher->waitable.user_data = watcher
   waitset_add(broker->waitset, &watcher->waitable)（已注册 → NS_E_EXISTS）
5. broker 写 req.rc 并 signal req.wakeup；用户线程读取后返回
```

### ns_broker_remove

```
1. 构造 op_request（op = REMOVE）并入 op_queue
2. event_signal 唤醒 broker 线程
3. 用户线程阻塞等待 req.wakeup（无超时）
4. broker 线程 drain op_queue：
   waitset_remove(broker->waitset, &watcher->waitable)（未注册 → NS_E_INVAL）
   成功后置 NULL 化本轮对应 completion.waitable，并清 user_data
5. broker 写 req.rc 并 signal req.wakeup
```

### 线程安全

- `add` / `remove` 只操作 op 队列（受 `op_lock` 保护）；实际的 waitset 操作
  全部在 broker 线程串行执行，与 `waitset_wait` 天然不并发。
- `op_lock` 同时保证 signal 失败时能原子回滚已入队的 req。
- 契约要求调用方在 `ns_shutdown()` 前完成 teardown；库不再兜底清理残留
  watcher 或残留 op（见「生命周期协议 / 用户职责」）。

## 生命周期协议

### ns_init

```
ns_init()
  → ns_platform_init()
  → ns_broker_init()
    → ns_platform_alloc(broker)
    → ns_platform_waitset_init(&broker->waitset)
    → ns_platform_event_init(&broker->event, "nanosig-broker")
    → broker->event.waitable.events = NS_WAITABLE_EVENT_IN
    → ns_platform_waitset_add(&broker->waitset, &broker->event.waitable)
    → ns_platform_mutex_init(&broker->op_lock)
    → ns_list_init(&broker->op_queue_head)
    → ns_timer_mgr_global_init(ns_broker_notify, broker)
    → atomic_init(&broker->quit_requested, 0) / atomic_init(&broker->shutdown_started, 0)
    → ns_platform_thread_init(&broker->thread, broker_run, broker)
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
  → 调用方已按前置条件销毁 loop/timer 并注销全部 watcher
  → atomic_store(g_ns_initialized, 0)
  → ns_broker_destroy()
    → atomic_store(broker->shutdown_started, 1)   /* 新 add/remove 直接返回 NS_E_SHUTDOWN */
    → atomic_store(broker->quit_requested, 1)
    → ns_platform_event_signal(&broker->event)
    → ns_platform_thread_join(&broker->thread)
    → 若 op_queue 非空：记一条警告（不救援、不 signal；不持 op_lock）
    → ns_timer_mgr_global_shutdown()
    → waitset_remove(event.waitable)
    → waitset_destroy（waitset 非空时返回 NS_E_EXISTS 并记录错误）
    → ns_platform_event_deinit(&broker->event)
    → mutex_destroy(op_lock)
    → g_broker = NULL
    → ns_platform_free(broker)
  → ns_platform_shutdown()
```

### 用户职责

- `ns_watcher_init` 必须在 `ns_broker_add` 之前调用。
- `ns_broker_remove` 必须在 `ns_watcher_deinit` 之前调用。
- `ns_broker_remove` 必须在 `ns_shutdown` 之前调用。**库不兜底**：残留 watcher
  不会被自动注销，`waitset_destroy` 因非空返回 `NS_E_EXISTS`，底层句柄泄漏。
- `ns_timer_deinit` 必须在 `ns_shutdown` 之前调用。
- `ns_loop_deinit` 必须在 `ns_shutdown` 之前调用（现有逻辑）。

### 竞态：remove 时已入队的 dispatch

`ns_broker_remove` 从 waitset 注销 watcher 后，不再有新的 completion
指向该 watcher。但已经开始的 handler dispatch 不会被撤回。这与
`ns_signal_disconnect` 语义一致。

handler 在其内部 emit 用户 signal 时，该 emit 已经指向用户自己的 slot；
用户通过 `ns_signal_connect` 绑定 slot 时的 `user_data` 生命周期必须长于
任何 in-flight emit。

## 错误处理

- broker 线程内 `waitset_wait` 或 `fire_expired` 返回错误时 continue，
  不终止 broker 线程。单次错误不中断服务。
- watcher handler 内部的 emit 失败（如 MPSC 满）由 handler 自身决定如何处理；
  broker 不介入。timer 的 `ns_signal_emit_raw` 返回错误时继续，不影响后续
  watcher 或 timer。

## 平台层新增接口

### ns_platform_thread_init / join

平台类型由构建注入的平台头提供（caller-owned 存储，不再是 `struct` 前向声明）：

```c
typedef void (*ns_platform_thread_fn)(void *arg);

/* ns_platform_thread_t 是平台头的透明别名（pthread_t / HANDLE / 任务句柄 + 内嵌栈） */
int ns_platform_thread_init(
    ns_platform_thread_t *thread,
    ns_platform_thread_fn entry,
    void *arg,
    const char *debug_name);

int ns_platform_thread_join(ns_platform_thread_t *thread);
```

- Windows：`_beginthreadex` + wrapper 函数适配 `LPTHREAD_START_ROUTINE`。
- Linux/macOS：`pthread_create`。
- `init` 在调用方提供的存储上初始化，不分配 thread 对象；`join` 只回收线程自身
  的平台资源（如 Windows 的线程句柄），不释放调用方存储。

### ns_platform_event_*

```c
typedef struct ns_platform_event {
    ns_platform_waitable_t waitable;      /* 等待/消费侧：注册进 waitset，drain 读取此侧 */
    ns_waitable_handle_t   signal_handle; /* 触发侧：signal 写入此句柄 */
} ns_platform_event_t;

int ns_platform_event_init(ns_platform_event_t *event, const char *debug_name);
int ns_platform_event_signal(ns_platform_event_t *event);
int ns_platform_event_drain(ns_platform_event_t *event);
int ns_platform_event_deinit(ns_platform_event_t *event);
```

caller-owned waitable-first 事件原语，供 broker 自唤醒和未来多事件源接入。
`events` 和 `user_data` 由调用方设置。`waitable` 为等待/消费侧（drain 读此侧），
`signal_handle` 为触发侧（signal 写此句柄）；调用方只经 API 操作，不得依赖二者
是否同值。wakeup 仅保留阻塞单等待职责。

- Windows：`CreateEventA` auto-reset，两 handle 同值；drain 为 no-op。
- Linux：`eventfd`，waitable fd 与 signal_handle fd 同值。
- macOS：`pipe`，waitable fd = 读端、signal_handle fd = 写端；pipe 满时
  signal 视为成功（EAGAIN = 已 signaled）。
- `deinit` 前必须已 `waitset_remove`，否则返回 `NS_E_BUSY`。

## 与 timer 的集成

timer_mgr 的 notify 回调 = `ns_broker_notify`。timer start / cancel /
restart 时内部调 `notify(ctx)` → `ns_broker_notify()` →
`ns_platform_event_signal(&broker->event)`。

`ns_broker_notify` 是零分配、可跨线程调用的函数：

```c
void ns_broker_notify(void *ctx)
{
    ns_event_broker_t *broker = ctx;

    if((broker != NULL) && ns_waitable_handle_is_valid(broker->event.signal_handle)){
        (void)ns_platform_event_signal(&broker->event);
    }
}
```

timer_mgr 不知道 broker 的存在，只知道一个 `void *ctx` 和一个回调
函数。broker 通过 `global_init(notify, ctx)` 注册自己。

## 与 loop 的关系

broker 和 loop **完全解耦**：

- broker 不持有 loop 引用。
- broker 不直接操作 loop 的 wakeup 或 MPSC ring。
- watcher 就绪时 broker 只调用 handler；若用户要把事件投递回 loop，在 handler 内
  emit 用户拥有的 signal，emit 内部处理 loop 投递。
- loop 不知道 broker 的存在。

用户 handler 内 emit signal → emit 遍历 slot_list → 每个
connection 的 target_loop 的 MPSC ring 写入 → wakeup target_loop →
用户线程 `ns_loop_run` 从 wakeup_wait 返回 → dispatch_pending →
slot 回调执行。timer 同理：timer 由 timer_mgr emit 内嵌 signal 走同一投递链。

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
│  │  每个 watcher 内嵌 waitable + handler│            │
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
│    3. 遍历 completions → 调用 watcher handler         │
│    4. timer_mgr_fire_expired()                       │
│    5. loop                                           │
│                                                      │
│  notify 回调链：                                      │
│    timer start/cancel/restart                        │
│    → timer_mgr notify(ctx)                           │
│    → ns_broker_notify()                              │
│    → event_signal(&broker->event)                    │
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
  watcher_init(handler) → broker_add
  事件到达 → broker 调用 handler →（可选）handler emit 自建 signal → loop dispatch
  broker_remove → watcher_deinit
```

## 扩展路径

- v2：N 线程 + N waitset。1 个负责 timeout（timer waitset），其余等待
  无限（IO waitset）。add 时按策略分配到某个 waitset。
- v2：watcher 支持 payload（传递更多事件元数据）。
- v2：broker 线程错误上报（连续错误时通知用户）。
