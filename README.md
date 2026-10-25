# nanosig

C11 的 signal/slot 库，跑在 Linux / macOS / Windows / FreeRTOS / Zephyr 上。

signal 是「发生了某事」的通知，slot 是收到通知后执行的回调。

两者之间不直接调用：发送方只管发一个信号，接收方在自己的事件循环里收，跨线程不用自己写队列、加锁、唤醒。

- **对象归你**：signal、connection（信号和回调的连线）、timer、watcher 都由你声明和释放；loop 例外——`ns_loop_create()` 分配、`ns_loop_destroy()` 释放。
- **发送不阻塞**：emit（发信号）只是把 payload（荷载）拷进目标 loop 的队列就返回，不分配内存。
- **loop（事件循环）不绑线程**：同一个信号可以从任意线程发，处理永远发生在你指定的那个 loop 的线程里。
- **不只管信号**：定时器、平台 fd / 句柄就绪，都能变成信号。

## 接入前要满足的三件事

1. **两个头文件路径**：`include/` 和 `platform/<你的平台>/`。五个后端各有一份同名 `nanosig_platform_port.h`，靠 include 路径选中——选错会在编译期直接报错，不会悄悄用错平台。另外，`include/nanosig/nanosig_version.h.in` 要生成成 `nanosig_version.h`：把里面的 `@PROJECT_VERSION_*@` 换成 `0` / `1` / `0`，版本串换成 `"0.1.0"`。
2. **编译这些文件**：`src/nanosig.c`、`src/ns_timer.c`、`src/ns_broker.c`、`src/ns_formatio.c`、`src/ns_debug.c`、`src/ds/ns_hashtbl.c`、`src/ds/ns_mpsc_record_ring.c`、`src/ds/ns_rbtree.c`、`src/ds/ns_ringbuf.c`，加 `platform/<你的平台>/port.c`。
3. **平台前提**：C11 编译器；Linux / macOS 另需 pthread；Windows 无外部依赖；FreeRTOS 需要 queue set（`configUSE_QUEUE_SETS=1`）；Zephyr 需要 `CONFIG_POLL=y`。

程序入口固定一对：`ns_init()` 一次，`ns_shutdown()` 最后调一次。

## 调试输出怎么接

库自带分级调试输出，不用自己搭日志框架。你要做的只有一件事：告诉它日志往哪里写。

**往哪写**由一个弱符号 `platform_stdout_write` 决定。每个后端在 port.c 里都有默认实现：Linux / macOS / Windows 直接 `fwrite` 到 stdout，Zephyr 走 console，**FreeRTOS 默认丢弃**。所以在 RTOS 上（或想把日志改到串口 / RTT / 文件）就自己强定义它：

```c
/* 自己的源文件里强定义，覆盖 port.c 里的弱默认实现 */
void platform_stdout_write(void *stream, const uint8_t *buf, size_t size)
{
    (void)stream;

    for(size_t i = 0; i < size; i++){
        uart_putc(buf[i]);      /* 换成你的串口 / RTT / ITM 写函数 */
    }
}
```

**两个开关，一个在编译期、一个在运行期**：

- **编译期**决定日志代码是否被编进去。每个模块一个等级，没定义就用 `NS_DBG_MODULE_LEVEL_DEFAULT`（默认 `NS_DBG_SYS`，只留 ERR / WARN / SYS）。给自己的模块开 debug：

```c
#define NS_DBG_MODULE_LEVEL_KEYPAD NS_DBG_DEBUG   /* 放在 #include 之前 */

void keypad_report(uint16_t key, uint8_t type, uint32_t duration_ms)
{
    ns_mdebugln(KEYPAD, "key=%u type=%u held=%ums", key, type, duration_ms);
    ...
}
```

- **运行期**保留代码但临时静音：`ns_dbg_set_level(NS_DBG_WARNING)`，管所有 `ns_dbg_*` 输出。
- 不想按模块分等级，就直接用 `ns_errln` / `ns_warnln` / `ns_infoln` / `ns_debugln` 这类不带模块前缀的宏。

**输出格式**：默认前缀是 `[单调时钟 秒.微秒] [等级] `，由编译期 `NS_CONFIG_DEBUG_FLAGS` 控制（默认 `DEBUG_TAG | MONOTONIC_CLOCK`）。想关掉时间戳就自己定义这个宏。payload 之类的二进制数据用 `ns_debughex(buf, len)` 打。

三个坑：

1. 这个写入函数里**禁止**再调 `ns_debugln` 等任何调试宏——调试层是持锁写完一整条输出的，锁非递归，会自死锁。
2. 模块级宏（`ns_m*`）要求 `NS_DBG_MODULE_LEVEL_<模块>` 已定义。忘了定义不会报错，但等级会被算成 30，等于永远输出。
3. `ns_debug_init` / `ns_debug_shutdown` 由 `ns_init` / `ns_shutdown` 自动调用，不用手动管。

## 程序骨架：主函数长什么样

不管业务多复杂，形态都是这一个：平台给你一个主线程，你在主线程上初始化 nanosig、建一个主 loop、初始化各业务模块，然后让这个 loop 一直跑到有人请求退出，最后逆序释放。下面用到的 keypad_init / app_start_scan 就是后文例子的函数。

```c
int main(void)
{
    ns_loop_t *loop = NULL;
    int rc;

    if(ns_init() != NS_OK) return 1;                  /* 1. 全局初始化，进程内一次 */

    rc = ns_loop_create(&loop, NULL);                 /* 2. 建主 loop，NULL = 默认配置 */
    if(rc != NS_OK) goto out_shutdown;

    if((rc = keypad_init()) != NS_OK) goto out_loop;         /* 3. 业务模块初始化 */
    if((rc = app_start_scan(loop)) != NS_OK) goto out_keypad; /*    要 loop 的模块显式传 */

    rc = ns_loop_run(loop);                           /* 4. 跑到底，直到有人请求退出 */

    app_stop_scan();                                  /* 5. 逆序释放 */
out_keypad:
    keypad_deinit();
out_loop:
    ns_loop_destroy(loop);
out_shutdown:
    ns_shutdown();
    return rc == NS_OK ? 0 : 1;
}
```

- **RTOS 上必须先有一个主线程**：`ns_init()`、`ns_loop_create()` / `ns_loop_run()`、`ns_timer_*`、`ns_signal_*` 都不能在中断上下文里调。裸机启动或 RTOS 启动代码里要自己建一个任务（main task），把上面整段骨架搬进去跑；这个任务的栈要能容纳 slot 回调，因为 loop 就在它上面跑。
- **中断里什么都别调，`ns_signal_emit()` 也不行**。原因很具体：
  - emit 内部要拿 signal 的互斥锁，中断里不能拿 mutex；
  - 队列满时它返回 `NS_E_QUEUE_FULL`，要调用方稍后重试，中断里没地方重试；
  - 最后它还要唤醒目标 loop，而 FreeRTOS 后端用的是普通 `xSemaphoreGive`，不是 `...FromISR`。

  所以库不提供任何 ISR 版本接口，也没有 ISR 契约。
- **中断里的正确做法**：ISR 只 give 一个二值或计数信号量（`xSemaphoreGiveFromISR`），把这个信号量注册成 watcher（见后面「监控平台原语」一节）。链路是：ISR → 信号量 → broker 线程（库内部等定时器和平台事件的后台线程）→ 你的 handler 里 `ns_signal_emit()` → 目标 loop → slot。
- **谁让 loop 停下来**：通常是某个 slot 里调 `ns_loop_quit(loop)`——它是异步的，可以从 slot 或其它线程调；但它内部也要唤醒 loop，同样不能在中断里调。
- **不要在 slot 里调 `ns_loop_stop()`**：那个要 join 后台线程，在 loop 自己的线程里调用等于让自己等自己。
- **平台没有主线程怎么办**：`ns_loop_create()` 之后改用 `ns_loop_start()` 让 loop 在后台线程跑，主线程去做别的；关掉时用 `ns_loop_stop()`。
- 需要 loop 的模块，显式把 `loop` 作为参数传进去；不需要的模块（比如只发信号的按键模块）不传。

## 五个概念

| 概念 | 是什么 |
|------|--------|
| signal | 一条"发生了某事"的通道。对象由你声明，初始化时绑一个 payload 类型。 |
| slot | 处理函数，签名 `void (*)(void *user_data, const payload_type *payload)`。 |
| connection | signal 和 slot 的连线。存储归你，断开之前不能释放。 |
| loop | 事件循环。signal 必须连到某个 loop，slot 在它的线程上执行。 |
| emit | 发一条信号：把 payload 拷进目标 loop 的队列，立即返回。 |

```
线程 A                            线程 B 的 loop
ns_signal_emit(sig, &ev)  ──▶  [队列]  ──▶  派发  ──▶  slot(user_data, ev)
        立即返回                                      slot 跑在 loop 所在线程
```

**最容易踩的一条**：emit 之后 slot 不会当场执行。必须有 loop 在跑（`ns_loop_run()` 或 `ns_loop_start()`）才会派发。

## 用信号：一个按键模块

模块对外只提供一个信号；谁想处理按键就 connect 它，模块不知道订阅方是谁。

### 对外契约（keypad.h）

```c
#ifndef KEYPAD_H
#define KEYPAD_H

#include <stdint.h>

#include <nanosig/nanosig.h>

/* 按键事件类型 */
enum keypad_event_type {
    KEYPAD_DOWN = 0,
    KEYPAD_UP,
    KEYPAD_LONG
};

/* 信号荷载：哪个键、什么事件、按了多久 */
typedef struct keypad_event {
    uint16_t key_id;
    uint8_t  type;          /* enum keypad_event_type */
    uint32_t duration_ms;
} keypad_event_t;

/* 模块对外的唯一信号；订阅方在自己的 loop 上 connect 它 */
NS_SIGNAL_DECLARE(g_keypad_event);

/* 订阅方可用来声明自己的处理函数类型；签名不一致会在 connect 处编译报错 */
NS_SLOT_FN_TYPEDEF(keypad_slot_fn, keypad_event_t);

int  keypad_init(void);      /* 只准备自己的信号，不关心谁来处理 */
void keypad_deinit(void);

#endif /* KEYPAD_H */
```

### 只管什么时候发（keypad.c）

模块内部只需要一个上报函数加上 init / deinit。怎么发现按键（轮询、驱动回调，或者中断经 broker，见后文「监控平台原语」）、怎么去抖、长按怎么判定，都是模块自己的事，这里全部省略。

```c
#define NS_DBG_MODULE_LEVEL_KEYPAD NS_DBG_DEBUG   /* 本模块的编译期日志等级 */

#include <stddef.h>

#include "keypad.h"

ns_signal_t g_keypad_event;

/* 检测到一次按键动作时调用；检测逻辑省略 */
static void keypad_report(uint16_t key, uint8_t type, uint32_t duration_ms)
{
    keypad_event_t ev = {
        .key_id      = key,
        .type        = type,
        .duration_ms = duration_ms
    };

    ns_mdebugln(KEYPAD, "key=%u type=%u held=%ums", key, type, duration_ms);
    (void)ns_signal_emit(g_keypad_event, &ev);   /* 只入队，立即返回 */
}

...

int keypad_init(void)
{
    return ns_signal_init(&g_keypad_event, keypad_event_t);
}

void keypad_deinit(void)
{
    (void)ns_signal_deinit(&g_keypad_event);   /* 订阅方的连接由订阅方自己断开 */
}
```

### 订阅方：一行 connect

```c
static void on_key(void *user_data, const keypad_event_t *ev)   /* 签名与 keypad_slot_fn 一致 */
{
    app_ctx_t *app = user_data;

    if(ev->type == KEYPAD_DOWN){
        app_handle_key(app, ev->key_id);
    }
}

ns_connection_t key_conn;

/* slot 只会在 my_loop 的线程里执行 */
ns_signal_connect_typed(g_keypad_event, on_key, keypad_event_t, my_loop, app, &key_conn);
...
ns_signal_disconnect(&key_conn);   /* 先断开，模块才能安全 deinit */
```

### 荷载规则（payload）

- **按字节拷贝**：emit 时整块复制进目标 loop 的队列，slot 拿到的是副本。emit 之后改原变量，不影响已入队的事件。
- **有大小上限**：目标 loop 默认队列 1024 字节，单条 emit 的 payload 实际约 480 字节以内，超了返回 `NS_E_INVAL`，重试无用。更大的数据请传指针（指向的对象要活到 slot 执行完）。
- **对齐不超过 `sizeof(size_t)`**：超了是编译期报错，改传指针即可。
- **要更大容量**：调 loop 配置的 `queue_byte_capacity`（须为 2 的幂），上限随之变大。
- **无 payload 信号**：payload 类型写 `ns_no_payload_t`，emit 时传 `NS_NO_PAYLOAD`，不拷贝任何字节。

## 用定时器：给按键加一个扫描节拍

定时器只做一件事：到期时 emit 它内嵌的 `timer.signal`。所以"用定时器"就是把它连到某个 loop，然后在那个 loop 里干活。这里仍然用按键模块举例：每 20ms 采一次键值，采到事件就 `keypad_report()`。

```c
static ns_timer_t      g_scan_timer;
static ns_connection_t g_scan_conn;

/* 到期回调：跑在 connect 时指定的那个 loop 的线程里 */
static void on_scan(void *user_data, const ns_no_payload_t *payload)
{
    (void)user_data;
    (void)payload;

    ...                    /* 采样、去抖、判定长按：省略 */
}

int app_start_scan(ns_loop_t *loop)
{
    int rc;

    /* 20ms 周期；默认是单次，REPEAT 让它自动重复 */
    if((rc = ns_timer_init(&g_scan_timer, 20000u, NS_TIMER_ATTR_REPEAT)) != NS_OK) return rc;

    /* timer.signal 固定无 payload，连到承接回调的 loop */
    if((rc = ns_signal_connect_typed(g_scan_timer.signal, on_scan, ns_no_payload_t,
                                     loop, NULL, &g_scan_conn)) != NS_OK) goto out_timer;

    if((rc = ns_timer_start(&g_scan_timer)) != NS_OK) goto out_conn;
    return NS_OK;

out_conn:  (void)ns_signal_disconnect(&g_scan_conn);
out_timer: (void)ns_timer_deinit(&g_scan_timer);
    return rc;
}

void app_stop_scan(void)
{
    (void)ns_timer_cancel(&g_scan_timer);
    (void)ns_signal_disconnect(&g_scan_conn);
    (void)ns_timer_deinit(&g_scan_timer);
}
```

定时器要点：

- 间隔单位是微秒（`uint64_t`）。
- timer 只发无 payload 事件，到期效果就是 emit 它内嵌的 `timer.signal`；所以 timer 也需要一个 loop 承接回调。
- 这就是 `keypad_init()` 不接收 loop 的原因：模块只负责发信号，扫描节拍属于应用侧，连到哪个 loop 由启动扫描的地方决定。
- 三个行为位：默认单次；`NS_TIMER_ATTR_REPEAT` 自动重复；`NS_TIMER_ATTR_RELOAD_FROM_NOW` 每次以当前时间重排（否则按上一次 deadline 步进）。
- 没启动的 timer 也可以安全 `ns_timer_cancel()`。
- 到期回调在 **connect 时指定的那个 loop 的线程**执行，不是定时器线程。

## 监控平台原语：ns_watcher_t

要把平台自己的 fd / 句柄 / 信号量接进来，就注册一个 watcher——一条「盯住这个句柄，等它就绪」的登记。它就绪时，broker 线程会回调你传进去的 handler。

```c
typedef struct uart_ctx {
    ns_platform_event_t ev;        /* 演示用跨平台事件；换自己的句柄见下表 */
    ns_watcher_t        watcher;
    ns_signal_t         rx_ready;  /* 想回到自己 loop 处理就发信号 */
} uart_ctx_t;

static void on_ready(ns_watcher_t *watcher, uint32_t events)
{
    uart_ctx_t *ctx = NS_CONTAINER_OF(watcher, uart_ctx_t, watcher);

    (void)events;
    (void)ns_platform_event_drain(&ctx->ev);              /* 电平触发必须读净 */
    (void)ns_signal_emit(ctx->rx_ready, NS_NO_PAYLOAD);   /* 非阻塞，允许在此调用 */
}

int uart_watch_start(uart_ctx_t *ctx)
{
    int rc;

    if((rc = ns_signal_init(&ctx->rx_ready, ns_no_payload_t)) != NS_OK) return rc;
    if((rc = ns_platform_event_init(&ctx->ev, "uart")) != NS_OK) goto out_signal;

    /* 三步：取句柄 → 初始化 watcher → 注册进 broker */
    if((rc = ns_watcher_init(&ctx->watcher, ctx->ev.waitable.primitive,
                             NS_WAITABLE_EVENT_IN, 1, on_ready)) != NS_OK) goto out_event;
    if((rc = ns_broker_add(&ctx->watcher)) != NS_OK) goto out_watcher;
    return NS_OK;

out_watcher: (void)ns_watcher_deinit(&ctx->watcher);
out_event:   (void)ns_platform_event_deinit(&ctx->ev);
out_signal:  (void)ns_signal_deinit(&ctx->rx_ready);
    return rc;
}
```

句柄从哪来：

| 后端 | 传给 `ns_watcher_init` 的 handle | 典型来源 |
|------|--------------------------------|----------|
| Linux / macOS | `int` fd | pipe、socket、eventfd；nanosig 自己的 `ns_platform_event_t` |
| Windows | `HANDLE` | `CreateEvent`；socket 需先 `WSAEventSelect` 得到 event |
| FreeRTOS | 队列集可用对象句柄 | `xSemaphoreCreateBinary`、`xSemaphoreCreateCounting`、`xQueueCreate`（ISR 里 give 这些对象即可唤起 broker） |
| Zephyr | `struct k_poll_signal *` | `k_poll_signal_init` 出来的对象（仅此一类） |

红线：

1. handler 跑在 broker 线程，**必须非阻塞**。卡住它等于卡住所有 watcher 和 timer。
2. handler 里**禁止**调 `ns_broker_add` / `ns_broker_remove` / `ns_shutdown`：它们要等 broker 线程自己，没有超时，会永久死锁。`ns_signal_emit` 是安全的。
3. 注销顺序 `ns_broker_remove` → `ns_watcher_deinit`。电平触发的必须先把事件源读净，否则每轮都重复触发。

## 各后端支持一览

| | Linux | macOS | Windows | FreeRTOS | Zephyr |
|---|---|---|---|---|---|
| 等待句柄 | `int`（fd） | `int`（fd） | `HANDLE` | `void *`（queue-set 对象） | `void *`（`k_poll_signal *`） |
| wakeup | eventfd | kqueue fd | Event HANDLE | 二值信号量 `SemaphoreHandle_t` | `struct k_sem`（上限 1） |
| mutex | `pthread_mutex_t` | `pthread_mutex_t` | `SRWLOCK` | 互斥信号量 `SemaphoreHandle_t` | `struct k_mutex` |
| 线程 | `pthread_t` | `pthread_t` | 线程 HANDLE | `TaskHandle_t` + join 信号量 | port 分配的不透明对象 |
| waitset 底层 | epoll | kqueue | WaitForMultipleObjects | queue set | `k_poll` |
| event 原语 | eventfd | pipe（读写两个 fd） | Event HANDLE | 二值信号量 | `k_poll_signal` |
| 支持的事件位 | IN / OUT / ERR | IN / OUT / ERR | 仅 IN | 仅 IN | 仅 IN |
| 单次等待上报上限 | 64 | 64 | 63（预留 1 个内部 timer） | 32（Σ成员+max ≤ CAP，可配） | 32（可配） |
| 等待精度 | 微秒（timerfd） | 微秒（kevent） | 毫秒 | 系统 tick | 系统 tick |
| 电平 / 边沿 | 两种都支持 | 两种都支持 | 忽略 edge 参数 | 忽略 | 忽略（`k_poll_signal` 电平保持到 reset） |

已知取舍：

- 对端正常关闭只置 IN、不置 ERR，别用 ERR 判断连接断开。
- FreeRTOS 加入 queue set 前，成员必须为空；`waitset_remove` 会排空数据队列，未读消息会丢。
- Zephyr 只认 `k_poll_signal`，不支持 `k_sem` / FIFO / pipe / socket。

## 释放顺序与错误速查

按这个顺序拆，否则会拿到 use-after-free 或泄漏平台句柄：

```
ns_timer_cancel → ns_signal_disconnect → ns_timer_deinit / ns_signal_deinit
                → ns_loop_destroy → ns_shutdown
ns_broker_remove → ns_watcher_deinit      （watcher 先于 ns_shutdown）
```

| 现象 | 原因 | 怎么办 |
|------|------|--------|
| slot 一直不执行 | emit 只是入队，没有 loop 在跑 | 确认目标 loop 已 `ns_loop_run` 或 `ns_loop_start` |
| `NS_E_INVAL` | payload 超过单条上限、大小与 init 不一致，或队列容量不是 2 的幂 | 传指针或调大容量 |
| `NS_E_QUEUE_FULL` | 目标 loop 队列满 | 退避重试，或加大容量 / 拆多个 loop |
| `NS_E_BUSY` | 同一 loop 已在后台跑又调 `ns_loop_run`，或重复 `ns_loop_start` | 二选一 |
| `NS_E_EXISTS` | 同一 loop 被两个线程同时 run，或 watcher 重复注册 | 检查调用点 |
| deinit 崩溃 / 句柄泄漏 | 没 disconnect 就 deinit；或 `ns_shutdown` 前没移除 watcher | 按上面顺序拆 |

## API 一览

| 分类 | 接口 | 说明 |
|------|------|------|
| 全局 | `ns_init` / `ns_shutdown` / `ns_is_initialized` | 进程级初始化与收尾，各调一次 |
| signal | `ns_signal_init(sig, payload_type)` / `ns_signal_deinit` | 初始化 / 释放，payload 类型在编译期定死 |
| signal | `NS_SIGNAL_DECLARE(name)` | 在头文件里声明对外信号 |
| signal | `NS_SLOT_FN_TYPEDEF(name, payload_type)` | 定义一个绑好 payload 类型的 slot 函数指针类型 |
| signal | `ns_signal_connect_typed(sig, fn, payload_type, loop, user_data, conn)` | 连接并做编译期签名检查 |
| signal | `ns_signal_disconnect` / `ns_signal_disconnect_all` | 断开单个 / 全部连接 |
| signal | `ns_signal_emit(sig, &payload)` 或 `NS_NO_PAYLOAD` | 发信号，只入队；不可在中断里调 |
| loop | `ns_loop_create` / `ns_loop_destroy` | 创建 / 销毁，`NULL` 配置用默认值 |
| loop | `ns_loop_run` / `ns_loop_quit` | 自己跑 / 请求退出（任意线程可调） |
| loop | `ns_loop_start` / `ns_loop_stop` | 后台线程跑 / 停止并 join（不可在 loop 自身线程调） |
| timer | `ns_timer_init(timer, interval_us, attr)` / `start` / `cancel` / `restart` / `deinit` | 微秒间隔；`NS_TIMER_ATTR_REPEAT`、`NS_TIMER_ATTR_RELOAD_FROM_NOW` |
| watcher | `ns_watcher_init` / `ns_broker_add` / `ns_broker_remove` / `ns_watcher_deinit` | 把平台原语接进 broker |
| 平台 | `ns_platform_wakeup_*` / `_event_*` / `_mutex_*` / `_thread_*` / `_waitset_*` / `ns_platform_clock_monotonic_us` | 平台原语直通；存储都由你提供 |
| 错误码 | `nanosig_status.h` | `NS_OK` 与 `NS_E_*`，失败一律负数 |

## 顺带给你的数据结构

库内部用的容器也开放出来，同一个工程不用再引第二套链表或环形缓冲。

| 结构 | 用来做什么 | 存储谁管 | 线程安全 |
|---|---|---|---|
| `ns_list` 双向链表 | 侵入式挂链，节点是结构体里的一个钩子字段 | 你（节点内嵌在自己的结构体里） | 否 |
| `ns_slist` 单向链表 | 同上，省一个指针 | 你 | 否 |
| `ns_rbtree` 红黑树 | 有序插入与查找（定时器就靠它排 deadline） | 你 | 否 |
| `ns_ringbuf` 字节环 | 单读单写、无锁的字节流 | 你提供缓冲区 | 单读 + 单写无锁 |
| `ns_hashtbl` 哈希表 | key → 节点，负载因子可调 | **库**（`ns_hashtbl_create` / `ns_hashtbl_destroy`） | 否 |

侵入式容器的用法：节点不用单独分配，塞进你自己的结构体就行。

```c
typedef struct my_item {
    int            id;
    ns_list_node_t node;    /* 挂链表用的钩子 */
} my_item_t;
```

四条要注意的：

- 除了 ringbuf 的单读单写那一对操作，这些结构**都不是线程安全的**，跨线程访问自己加锁。
- ringbuf 只保证「一个读者 + 一个写者」无锁；多一个读者就会坏。
- hashtbl 是唯一由库分配内存的：`create` / `destroy` 成对用；`destroy` 之后不能再对任何节点调 `ns_hashtbl_node_delete`，会双重释放。
- 所有 `*_init` 都不幂等：init 一次、deinit 一次。

## 许可

MIT，见 `LICENSE`。
