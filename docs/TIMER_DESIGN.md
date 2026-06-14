# nanosig timer 详细设计

状态：phase 1 已实现并由单元测试直接覆盖；broker 接入待后续阶段。
更新时间：2026-06-14。

## 概述

timer 是 nanosig 的定时器模块，基于 rbtree 按 deadline 排序，到期时通过内嵌
`ns_signal_t` 触发 connected slot。timer_manager 是独立模块，通过回调函数指针
与 broker 解耦。

参考实现：`tmp/eventhub_os/src/eh_timer.c`。

## 架构

```
用户层：
  ns_timer_create / start / cancel / restart / destroy
  ns_signal_connect(&timer->signal, slot, ...)

timer_manager 层（独立模块）：
  g_timer_mgr（全局单例）
  rbtree 按 remaining time 排序
  mutex 保护
  notify 回调通知 broker

core / broker 层：
  ns_timer_mgr_global_init(notify, ctx)  ← ns_init 当前已调用
  ns_timer_mgr_next_timeout(&timeout)    ← 当前由单测直接调用，后续 broker 调用
  ns_timer_mgr_fire_expired()            ← 当前由单测直接调用，后续 broker 调用

平台层：
  ns_platform_clock_monotonic_us()
  ns_platform_mutex
```

## 依赖关系

```
broker → timer_mgr（生命周期 + 运行时）
timer_mgr → notify 回调（通知 broker，不依赖 broker）
用户 → ns_timer_*（间接使用 g_timer_mgr）
```

timer_mgr 不知道 broker 的存在，通过回调解耦。

## timer_mgr 实例设计

```c
typedef void (*ns_timer_notify_fn)(void *ctx);

struct ns_timer_mgr {
    ns_platform_mutex_t   *mutex;        /* 保护 rbtree */
    ns_rbtree_t            tree;         /* 按 remaining time 排序 */
    ns_platform_time_us_t  now;          /* 操作前刷新，操作内不变 */
    ns_timer_notify_fn     notify;       /* broker 注册的回调 */
    void                  *notify_ctx;   /* 回调上下文 */
};
```

### now 缓存

`mgr->now` 是全局时间缓存，每次进入 mutex 保护区域后第一件事刷新：

```c
mgr->now = ns_platform_clock_monotonic_us();
```

后续所有 deadline 比较和 remaining time 计算都用 `mgr->now`。
好处：一次操作内时间一致，不会因为中间系统时间跳动导致判断矛盾。

## 公开 API

### 用户接口

```c
int ns_timer_create(ns_timer_t *timer, ns_time_us_t interval_us, uint32_t attr);
int ns_timer_start(ns_timer_t *timer);
int ns_timer_cancel(ns_timer_t *timer);
int ns_timer_restart(ns_timer_t *timer);
int ns_timer_destroy(ns_timer_t *timer);
```

用户通过 `ns_signal_connect(&timer->signal, ...)` 连接 slot。`ns_timer_create`
初始化内嵌 no-payload signal，`ns_timer_destroy` 负责释放该 signal 的内部资源。

### broker 内部接口

```c
int  ns_timer_mgr_global_init(ns_timer_notify_fn notify, void *ctx);
void ns_timer_mgr_global_shutdown(void);

int  ns_timer_mgr_next_timeout(ns_platform_time_us_t *out_timeout_us);
int  ns_timer_mgr_fire_expired(void);
```

- `global_init`：创建 mutex、初始化 rbtree、注册回调。当前由 `ns_init()` 调用；后续 broker 接入时传入 notify 回调。
- `global_shutdown`：销毁 mutex、清空 rbtree。当前由 `ns_shutdown()` 调用。
- `next_timeout`：返回最近 timer 的 remaining time。无 timer 返回 `NS_E_NO_TIMER`。
- `fire_expired`：内部取 `now`，fire 所有 `remaining ≤ 0` 的 timer，repeat timer 重新插入。

## ns_timer_t 结构

```c
typedef struct ns_timer {
    ns_signal_t      signal;       /* 第一个字段，到期时触发 */
    ns_time_us_t     interval_us;  /* 触发间隔 */
    ns_time_us_t     expire_us;    /* 绝对到期时间（内部维护） */
    uint32_t         attr;         /* NS_TIMER_ATTR_* */
    ns_rbtree_node_t rb_node;      /* rbtree 节点（新增） */
} ns_timer_t;
```

`rb_node` 用于挂在 timer_mgr 的 rbtree 上。timer 不在树中时（未启动或已取消），
`rb_node` 应为初始化状态。

## rbtree 排序

comparator 用**相对时间**（remaining time），不用绝对 deadline：

```c
static int timer_cmp(ns_rbtree_node_t *a, ns_rbtree_node_t *b, void *ctx)
{
    ns_timer_mgr_t *mgr = (ns_timer_mgr_t *)ctx;
    ns_timer_t *ta = NS_CONTAINER_OF(a, ns_timer_t, rb_node);
    ns_timer_t *tb = NS_CONTAINER_OF(b, ns_timer_t, rb_node);
    int64_t ra = (int64_t)(ta->expire_us - mgr->now);
    int64_t rb = (int64_t)(tb->expire_us - mgr->now);
    return (ra < rb) ? -1 : (ra > rb) ? 1 : 0;
}
```

**为什么用相对时间：** unsigned 减法天然处理时钟绕回（wraparound）。
`(uint64_t)(expire - now)` 在时钟绕回时仍算出正确距离（模 2^64）。
直接比绝对值在绕回时会误判优先级。

`mgr->now` 在每次 mutex 操作前刷新，comparator 用它算 remaining time。

## ns_timer_start 流程

```
ns_timer_start(timer):
  mutex_lock(mgr->mutex)
  mgr->now = clock_monotonic()
  timer->expire_us = mgr->now + timer->interval_us
  rb_insert(&timer->rb_node, &mgr->tree)  // comparator 自动排到正确位置
  was_first = (rb_first == &timer->rb_node)  // 是否变成最紧急的
  mutex_unlock(mgr->mutex)
  if was_first:
      mgr->notify(mgr->notify_ctx)  // 通知 broker 重新算 timeout
```

## ns_timer_cancel 流程

```
ns_timer_cancel(timer):
  mutex_lock(mgr->mutex)
  if timer->rb_node 不在树中:
      mutex_unlock()
      return OK
  rb_remove(&timer->rb_node, &mgr->tree)
  rb_node_init(&timer->rb_node)
  was_first = ...  // 被删的是否是最紧急的
  mutex_unlock(mgr->mutex)
  if was_first:
      mgr->notify(mgr->notify_ctx)  // 通知 broker
```

## fire_expired 流程

```
ns_timer_mgr_fire_expired():
  mutex_lock(mgr->mutex)
  mgr->now = clock_monotonic()

  while rbtree 非空:
      first = rb_first()
      remaining = first->expire_us - mgr->now  // signed 比较
      if remaining > 0: break  // 没有过期的了

      rb_remove(first)
      rb_node_init(first)

      // fire：emit 内嵌 no-payload signal
      ns_signal_emit_raw(&first->signal, NS_NO_PAYLOAD, 0)

      // repeat timer 重新插入
      if first->attr & NS_TIMER_ATTR_REPEAT:
          if first->attr & NS_TIMER_ATTR_RELOAD_FROM_NOW:
              base = mgr->now
          else:
              // 从上次 deadline 步进
              next = first->expire_us + first->interval_us
              if (int64_t)(next - mgr->now) <= 0:
                  base = mgr->now  // 落后太多，用 now
              else:
                  base = first->expire_us  // 从上次 deadline 步进
          first->expire_us = base + first->interval_us
          rb_insert(first)

  mutex_unlock(mgr->mutex)
```

## next_timeout 流程

```
ns_timer_mgr_next_timeout(out):
  mutex_lock(mgr->mutex)
  mgr->now = clock_monotonic()

  if rbtree 为空:
      mutex_unlock()
      return NS_E_NO_TIMER

  first = rb_first()
  remaining = first->expire_us - mgr->now
  if remaining <= 0:
      *out = 0  // 已过期，非阻塞
  else:
      *out = (ns_platform_time_us_t)remaining

  mutex_unlock()
  return NS_OK
```

## repeat 语义

`ns_timer_t.attr` 定义：

```c
NS_TIMER_ATTR_ONESHOT           = 0       /* 单次触发 */
NS_TIMER_ATTR_REPEAT            = 1 << 0  /* 自动重复 */
NS_TIMER_ATTR_RELOAD_FROM_NOW   = 1 << 1  /* repeat 时以当前时间为基准 */
```

repeat 行为：
- `RELOAD_FROM_NOW` 设置：`next_expire = now + interval`
- `RELOAD_FROM_NOW` 未设置：`next_expire = previous_expire + interval`
  - 如果 `next_expire` 已落后于 `now`，退化为 `now + interval`

## 与 broker 的集成

### ns_init 时

```
ns_init()
  → platform_init()
  → ns_timer_mgr_global_init(broker_wakeup_callback, broker_ctx)
  → broker_thread_start()
```

### broker 主循环

```
broker_run:
  1. rc = ns_timer_mgr_next_timeout(&timeout)
     if rc == NS_E_NO_TIMER → timeout = INFINITE
  2. ns_platform_waitset_wait(waitset, timeout, ...)
  3. process completions（外部事件）
  4. drain MPSC queue
  5. ns_timer_mgr_fire_expired()
  6. loop
```

### 跨线程 timer 操作

```
线程 B: ns_timer_start(timer)
  → timer_mgr_add → notify(broker_ctx)
  → broker wakeup signal → waitset_wait 返回
  → broker 重新算 timeout → 重新 wait
```

## 与 eh_timer 的差异

| 方面 | eh_timer | nanosig timer_mgr |
|------|----------|-------------------|
| 全局状态 | `static` 全局变量 | `ns_timer_mgr_t` 结构体 + `global_init/shutdown` |
| 时间缓存 | `static eh_clock_t timer_now` | `mgr->now`（结构体成员） |
| rbtree 排序 | comparator 用相对时间 | 同（用相对时间，防 wraparound） |
| 无 timer 时 | 返回 60s 上限 | 返回 `NS_E_NO_TIMER` |
| 变化通知 | 返回 `FIRST_TIMER_UPDATE`，调用方调 `eh_idle_break` | 内部调 `notify(notify_ctx)` 回调 |
| 临界区 | `eh_enter_critical`（可能关中断） | `ns_platform_mutex`（用户态） |
| fire 机制 | `eh_event_notify` | `ns_signal_emit_raw` |
| 模块注册 | `eh_interior_module_export` 自注册 | broker 显式调 `global_init` |
| running 判断 | `!eh_rb_node_is_empty(&rb_node)` | `ns_timer_t` 内部状态 |
| 解耦方式 | 调用方负责通知 loop | 回调函数指针（timer_mgr 不依赖 broker） |
| 最大等待上限 | 60 秒 | 无上限（v1 不做 RTOS 兜底） |
