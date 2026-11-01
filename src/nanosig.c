/**
 * @file nanosig.c
 * @brief nanosig core lifecycle and loop runtime.
 * @date 2026-05-24
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <string.h>

#include <nanosig/nanosig_port.h>
#include <nanosig/nanosig.h>
#include <nanosig/nanosig_mpsc_record_ring.h>
#include <nanosig/ns_debug.h>
#include "nanosig/internal/ns_broker.h"
#include "nanosig/internal/ns_timer_mgr.h"

#ifdef NANOSIG_BAREMETAL
/* 裸机专属驱动接口；该头只随 platform/baremetal 的 include 路径分发，
   因此非裸机构建下不存在，也就没有"声明了却链接不到"的公开面。 */
#include <nanosig_baremetal_api.h>
#endif

/* Default SYS; overridable at build time (e.g. project CMake
 * target_compile_definitions(nanosig PRIVATE NS_DBG_MODULE_LEVEL_CORE=NS_DBG_DEBUG)). */
#ifndef NS_DBG_MODULE_LEVEL_CORE
#define NS_DBG_MODULE_LEVEL_CORE NS_DBG_SYS
#endif

struct ns_loop {
#ifndef NANOSIG_BAREMETAL
    ns_platform_wakeup_t wakeup;
#endif
    ns_loop_config_t config;
    ns_mpsc_record_ring_t queue;
    atomic_int quit_requested;
    atomic_int running;
#ifndef NANOSIG_BAREMETAL
    ns_platform_thread_t async_thread;
    int                    async_started;
    int                    async_rc;
#endif
    /** 1 = 该 loop 已被创建且尚未销毁。
     *  裸机后端的单例判定依赖它（host 后端不读，但同样维护，保证两个后端取值一致）。 */
    int created;
};

#ifdef NANOSIG_BAREMETAL
/* 裸机后端的内建静态 loop 单例。
 *
 * 存储全部静态分配——signal+timer 主路径因此不依赖任何分配器，这正是
 * 裸机后端不提供 ns_platform_alloc 的前提。
 *
 * ring 存储必须满足 ns_mpsc_record_ring_init 的对齐前置条件
 * （NS_MPSC_RECORD_RING_ALIGNMENT），否则会在运行期被拒绝；裸 uint8_t 数组
 * 在 x86 上会侥幸通过，在其它目标上不一定，故用 _Alignas 显式保证。 */
static ns_loop_t g_ns_bm_loop;
static _Alignas(NS_MPSC_RECORD_RING_ALIGNMENT)
       uint8_t    g_ns_bm_ring_storage[NANOSIG_BAREMETAL_LOOP_QUEUE_BYTES];

NS_STATIC_ASSERT((NANOSIG_BAREMETAL_LOOP_QUEUE_BYTES &
                  (NANOSIG_BAREMETAL_LOOP_QUEUE_BYTES - 1u)) == 0u,
                 "baremetal loop queue bytes must be a power of two");
NS_STATIC_ASSERT(NANOSIG_BAREMETAL_LOOP_QUEUE_BYTES >= (sizeof(size_t) * 3u),
                 "baremetal loop queue bytes must be >= 3*sizeof(size_t)");
#endif

/**
 * @brief 入队的 slot 调用记录头。
 *
 * 紧跟此结构体之后是 payload 字节（payload_size 长度）。
 * 通过 MPSC record ring 的 scatter-gather 写入：第一个 part 是此头，
 * 第二个 part 是 payload 数据（可能为 0 字节）。
 */
typedef struct ns_slot_call {
    ns_slot_fn fn;
    void *user_data;
    size_t payload_size;
} ns_slot_call_t;

/* 兑现 NS_SIGNAL_PAYLOAD_ALIGN_MAX 的两个条件：记录起点按 ring 粒度对齐，且
   payload 相对记录起点的偏移（环头 size_t + 本结构）是该粒度的整数倍。
   任一不成立，ns_signal_init 的编译期断言就是在骗调用方。 */
NS_STATIC_ASSERT(NS_SIGNAL_PAYLOAD_ALIGN_MAX == NS_MPSC_RECORD_RING_ALIGNMENT,
                 "payload alignment limit must equal the record ring alignment");
NS_STATIC_ASSERT(((sizeof(size_t) + sizeof(ns_slot_call_t)) % NS_SIGNAL_PAYLOAD_ALIGN_MAX) == 0u,
                 "payload offset inside a record must be a multiple of the alignment limit");


static int ns_signal_lock(ns_signal_t *signal)
{
    if((signal == NULL) || !signal->mutex_initialized) return NS_E_INVAL;
#ifdef NANOSIG_BAREMETAL
    /* 单执行上下文：临界区在构造上无对象，不触碰任何平台 mutex。
       守卫本身保留——未初始化的 signal 在所有后端上行为一致。 */
    return NS_OK;
#else
    return ns_platform_mutex_lock(&signal->mutex);
#endif
}

static int ns_signal_unlock(ns_signal_t *signal)
{
    if((signal == NULL) || !signal->mutex_initialized) return NS_E_INVAL;
#ifdef NANOSIG_BAREMETAL
    return NS_OK;
#else
    return ns_platform_mutex_unlock(&signal->mutex);
#endif
}

static atomic_int g_ns_initialized = 0;

static int ns_is_power_of_two(size_t value)
{
    return (value != 0u) && ((value & (value - 1u)) == 0u);
}

static int ns_loop_config_validate(const ns_loop_config_t *config)
{
    if(config == NULL) return NS_E_INVAL;
    if(config->flags != 0u) return NS_E_INVAL;
    if(!ns_is_power_of_two((size_t)config->queue_byte_capacity)) return NS_E_INVAL;

    /* stack_size == 0 = 后端默认，不受区间约束。 */
    if(config->stack_size == 0u) return NS_OK;

#ifdef NANOSIG_BAREMETAL
    /* 裸机后端没有线程（ns_loop_start 在本后端不存在），非零 stack_size 无意义。
       显式拒绝而不是静默忽略调用方设置的公开字段。 */
    return NS_E_INVAL;
#else
    {
        size_t stack_min = 0u;
        size_t stack_max = 0u;
        int    rc = ns_platform_thread_stack_range(&stack_min, &stack_max);

        if(rc != NS_OK) return rc;
        if(config->stack_size < stack_min) return NS_E_INVAL;
        if(config->stack_size > stack_max) return NS_E_INVAL;

        return NS_OK;
    }
#endif
}

static int ns_runtime_is_initialized(void)
{
    return ns_atomic_load_explicit(&g_ns_initialized, ns_memory_order_acquire) != 0;
}

int ns_init(void)
{
    int rc;

    if(ns_runtime_is_initialized()) return NS_E_EXISTS;

    rc = ns_platform_init();
    if(rc != NS_OK) goto out;

    rc = ns_debug_init();
    if(rc != NS_OK) goto out_platform;

#ifdef NANOSIG_BAREMETAL
    /* 裸机后端没有 broker 线程，而 timer manager 的初始化原本由 broker 承担
       （src/ns_broker.c 的 ns_broker_global_init）。这里必须补上：否则
       ns_timer_mgr_lock() 会返回 NS_E_SHUTDOWN，所有 timer API 全部失效。
       notify 为 NULL 是安全的——裸机的到期派发由 ns_loop_poll 直接驱动。 */
    rc = ns_timer_mgr_global_init(NULL, NULL);
#else
    rc = ns_broker_global_init();
#endif
    if(rc != NS_OK) goto out_debug;

    ns_atomic_store_explicit(&g_ns_initialized, 1, ns_memory_order_release);
    return NS_OK;

out_debug:
    ns_debug_shutdown();
out_platform:
    (void)ns_platform_shutdown();
out:
    return rc;
}

int ns_shutdown(void)
{
    if(!ns_runtime_is_initialized()) return NS_OK;

    ns_atomic_store_explicit(&g_ns_initialized, 0, ns_memory_order_release);

#ifdef NANOSIG_BAREMETAL
    ns_timer_mgr_global_shutdown();
    /* 裸机 loop 是静态单例：shutdown 时归零，避免它带着半创建状态跨越
       init/shutdown 周期存活。 */
    g_ns_bm_loop.created = 0;
#else
    ns_broker_global_shutdown();
#endif
    ns_debug_shutdown();

    return ns_platform_shutdown();
}

int ns_is_initialized(void)
{
    return ns_runtime_is_initialized();
}

int ns_loop_create(ns_loop_t **out_loop, const ns_loop_config_t *config)
{
    ns_loop_t *loop;
    ns_loop_config_t local_config;
    int rc;

    if(out_loop != NULL) *out_loop = NULL;

    if(!ns_runtime_is_initialized()) return NS_E_SHUTDOWN;

#ifdef NANOSIG_BAREMETAL
    /* 裸机：单一静态单例，禁止创建第二个。
       单例判定必须先于配置校验——否则「二次 create 传入不同容量」会返回
       NS_E_INVAL 而不是 NS_E_EXISTS，调用方无法区分"已存在"和"参数错"。 */
    if(g_ns_bm_loop.created) return NS_E_EXISTS;

    local_config = (config != NULL) ? *config : NS_LOOP_CONFIG_DEFAULT();
    rc = ns_loop_config_validate(&local_config);
    if(rc != NS_OK) return rc;

    /* 容量由编译期常量决定，不支持按 loop 定制；超出即拒绝，不静默忽略。 */
    if((size_t)local_config.queue_byte_capacity > (size_t)NANOSIG_BAREMETAL_LOOP_QUEUE_BYTES){
        return NS_E_INVAL;
    }

    /* 记入 config 的必须是**实际**容量：环恒为编译期常量大小。若原样保留调用方
       请求的较小值，loop->config 就会对一个并不存在的容量撒谎。 */
    local_config.queue_byte_capacity = (ns_capacity_t)NANOSIG_BAREMETAL_LOOP_QUEUE_BYTES;

    loop = &g_ns_bm_loop;
    loop->config = local_config;
    ns_atomic_init(&loop->quit_requested, 0);
    ns_atomic_init(&loop->running, 0);

    rc = ns_mpsc_record_ring_init(
        &loop->queue,
        g_ns_bm_ring_storage,
        local_config.queue_byte_capacity);
    if(rc != NS_OK) return rc;

    loop->created = 1;
    if(out_loop != NULL) *out_loop = loop;
    return NS_OK;
#else
    {
        size_t struct_size;
        size_t total_size;

        local_config = (config != NULL) ? *config : NS_LOOP_CONFIG_DEFAULT();
        rc = ns_loop_config_validate(&local_config);
        if(rc != NS_OK) return rc;

        struct_size = ns_align_up(sizeof(*loop), NS_MPSC_RECORD_RING_ALIGNMENT);
        total_size = struct_size + (size_t)local_config.queue_byte_capacity;
        loop = (ns_loop_t *)ns_platform_alloc(total_size);
        if(loop == NULL) return NS_E_NOMEM;

        loop->config = local_config;
        ns_atomic_init(&loop->quit_requested, 0);
        ns_atomic_init(&loop->running, 0);

        loop->async_started = 0;
        loop->async_rc = NS_OK;
        loop->created = 1;

        rc = ns_mpsc_record_ring_init(
            &loop->queue,
            ((uint8_t *)loop) + struct_size,
            local_config.queue_byte_capacity);
        if(rc != NS_OK) goto out_free;

        rc = ns_platform_wakeup_init(&loop->wakeup, local_config.debug_name);
        if(rc != NS_OK) goto out_free;

        if(out_loop != NULL) *out_loop = loop;
        return NS_OK;

out_free:
        ns_platform_free(loop);
        return rc;
    }
#endif
}

int ns_loop_destroy(ns_loop_t *loop)
{
    if(loop == NULL) return NS_E_INVAL;

#ifdef NANOSIG_BAREMETAL
    /* 与 host 后端**同语义**：不因 ring 中还有未派发记录而拒绝销毁。
       裸机没有 free 的风险，静态存储永远有效，故直接丢弃未派发记录并
       归还单例名额。可诊断性由 ns_loop_stats()（后续项）承担，而不是
       用 host 没有的错误码制造跨后端语义分叉。 */
    if(loop != &g_ns_bm_loop) return NS_E_INVAL;
    if(!loop->created) return NS_E_INVAL;
    if(ns_atomic_load_explicit(&loop->running, ns_memory_order_acquire) != 0) return NS_E_INVAL;

    loop->created = 0;
    return NS_OK;
#else
    {
        int rc;

        if(loop->async_started) return NS_E_BUSY;
        if(ns_atomic_load_explicit(&loop->running, ns_memory_order_acquire) != 0) return NS_E_INVAL;

        rc = ns_platform_wakeup_deinit(&loop->wakeup);
        if(rc != NS_OK) {
            ns_merrln(CORE, "wakeup_deinit failed: %d, freeing loop anyway", rc);
        }

        ns_platform_free(loop);
        return rc;
    }
#endif
}

static NS_NO_SANITIZE_FUNCTION void
ns_loop_dispatch_pending(ns_loop_t *loop)
{
    void *record = NULL;
    size_t record_size = 0u;
    int rc;

    for(;;){
        rc = ns_mpsc_record_ring_try_acquire(&loop->queue, &record, &record_size);
        if(rc != NS_OK) break;

        if(record_size >= sizeof(ns_slot_call_t)){
            ns_slot_call_t *call = (ns_slot_call_t *)record;
            const void *payload = (call->payload_size > 0u)
                ? ((const uint8_t *)record + sizeof(ns_slot_call_t))
                : NULL;
            call->fn(call->user_data, payload);
        }

        (void)ns_mpsc_record_ring_release(&loop->queue, record);
    }
}

#ifndef NANOSIG_BAREMETAL
static int ns_loop_run_impl(ns_loop_t *loop)
{
    ns_platform_wait_result_t wait_result = NS_PLATFORM_WAIT_TIMEOUT;
    int expected_running = 0;
    int rc;

    if(loop == NULL) return NS_E_INVAL;
    if(!ns_runtime_is_initialized()) return NS_E_SHUTDOWN;

    if(!ns_atomic_compare_exchange_strong(&loop->running, &expected_running, 1)) return NS_E_EXISTS;

    for(;;){
        ns_loop_dispatch_pending(loop);

        if(ns_atomic_load_explicit(&loop->quit_requested, ns_memory_order_acquire) != 0) break;

        rc = ns_platform_wakeup_wait(&loop->wakeup, NS_PLATFORM_WAIT_INFINITE_US, &wait_result);
        if(rc != NS_OK) goto out;

        (void)wait_result;
    }

    /**
     * 重置 quit_requested，使 loop 可被重新运行。
     *
     * @note 重置与并发 ns_loop_quit() 之间存在极小竞态窗口：
     *       如果在 break 之后、reset 之前有另一线程调用了
     *       ns_loop_quit()（设置 quit_requested = 1），
     *       该请求会被覆盖，下一次 ns_loop_run 将正常启动而非立即退出。
     *       这属于已知设计约束：loop 退出后准备重新运行，单线程驱动
     *       loop 的使用模式不受影响。
     */
    ns_atomic_store_explicit(&loop->quit_requested, 0, ns_memory_order_release);
    rc = NS_OK;

out:
    ns_atomic_store_explicit(&loop->running, 0, ns_memory_order_release);
    return rc;
}

int ns_loop_run(ns_loop_t *loop)
{
    if(loop == NULL) return NS_E_INVAL;
    if(!ns_runtime_is_initialized()) return NS_E_SHUTDOWN;
    if(loop->async_started) return NS_E_BUSY;

    return ns_loop_run_impl(loop);
}

int ns_loop_quit(ns_loop_t *loop)
{
    if(loop == NULL) return NS_E_INVAL;
    if(!ns_runtime_is_initialized()) return NS_E_SHUTDOWN;

    ns_atomic_store_explicit(&loop->quit_requested, 1, ns_memory_order_release);
    return ns_platform_wakeup_signal(&loop->wakeup);
}

/* ------------------------------------------------------------------ */
/*  async loop （后台线程）                                              */
/* ------------------------------------------------------------------ */

static void ns_loop_async_entry(void *arg)
{
    ns_loop_t *loop = (ns_loop_t *)arg;

    loop->async_rc = ns_loop_run_impl(loop);
}

int ns_loop_start(ns_loop_t *loop)
{
    int rc;

    if(loop == NULL) return NS_E_INVAL;
    if(!ns_runtime_is_initialized()) return NS_E_SHUTDOWN;
    if(loop->async_started) return NS_E_BUSY;

    loop->async_rc = NS_OK;

    rc = ns_platform_thread_init(
        &loop->async_thread,
        ns_loop_async_entry,
        loop,
        loop->config.debug_name ? loop->config.debug_name : "ns-loop",
        loop->config.stack_size);
    if(rc != NS_OK) return rc;

    loop->async_started = 1;
    return NS_OK;
}

int ns_loop_stop(ns_loop_t *loop)
{
    int rc;

    if(loop == NULL) return NS_E_INVAL;
    if(!loop->async_started) return NS_E_INVAL;

    (void)ns_loop_quit(loop);
    rc = ns_platform_thread_join(&loop->async_thread);
    /* join 失败（如 loop 自身线程内调用导致 EDEADLK）时 worker 仍存活，必须保留
     * async_started，否则 start/deinit 的 NS_E_BUSY 守卫失效。 */
    if(rc == NS_OK) loop->async_started = 0;

    return rc != NS_OK ? rc : loop->async_rc;
}
#endif /* !NANOSIG_BAREMETAL */

#ifdef NANOSIG_BAREMETAL
/* ------------------------------------------------------------------ */
/*  裸机派发驱动                                                        */
/* ------------------------------------------------------------------ */

int ns_loop_poll(ns_loop_t *loop, uint32_t max_wait_us)
{
    int      rc = NS_OK;
    int      round;
    int      max_round;
    uint64_t next_us = 0u;

    /* 库从不睡眠；max_wait_us 只用于给内部补轮次数定上界：
       0 = 只跑最小轮次（1 轮），非 0 = 最多 NANOSIG_BAREMETAL_POLL_MAX_ROUNDS 轮。 */
    max_round = (max_wait_us == 0u) ? 1 : NANOSIG_BAREMETAL_POLL_MAX_ROUNDS;

    if(loop == NULL) return NS_E_INVAL;
    if(!ns_runtime_is_initialized()) return NS_E_SHUTDOWN;
    if(loop != &g_ns_bm_loop) return NS_E_INVAL;
    if(!loop->created) return NS_E_INVAL;

    /**
     * 一轮 = 派发到期 timer（其 slot 调用被推入 ring）→ drain ring 并调用 slot。
     *
     * 顺序不可交换：ns_timer_mgr_fire_expired() 经 ns_signal_emit_raw 入队，
     * 若先 drain 后 fire，timer 的回调就要等到下一次 poll 才执行，
     * "一次 poll 内到期即派发"的契约随之失效。
     *
     * 轮数上界防止 slot 重新装载 timer 造成无界重入；耗尽仍有到期项时
     * 返回 NS_OK 而非报错——调用方在自己的主循环里会再次进入。
     */
    for(round = 0; round < max_round; round++){
        int fired = ns_timer_mgr_fire_expired();

        if((fired != NS_OK) && (rc == NS_OK)) rc = fired;

        ns_loop_dispatch_pending(loop);

        /* 收敛判据：没有"立即到期"的 timer 就停。被 slot 重新装载且已到期的
           timer 会让 next_us == 0，从而再补一轮。 */
        if((ns_timer_mgr_next_timeout(&next_us) != NS_OK) || (next_us > 0u)) break;
    }

    return rc;
}

int ns_loop_next_timeout(ns_loop_t *loop, ns_platform_time_us_t *out_us)
{
    int rc;

    if((loop == NULL) || (out_us == NULL)) return NS_E_INVAL;
    if(!ns_runtime_is_initialized()) return NS_E_SHUTDOWN;
    if(loop != &g_ns_bm_loop) return NS_E_INVAL;
    if(!loop->created) return NS_E_INVAL;

    rc = ns_timer_mgr_next_timeout(out_us);
    if(rc == NS_E_NO_TIMER){
        /* ns_timer_mgr_next_timeout 在无 timer 时不写 *out_us；
           这里补齐哨兵值，否则调用方会读到一个未初始化的睡眠时长。 */
        *out_us = NS_PLATFORM_WAIT_INFINITE_US;
        return NS_OK;
    }

    return rc;
}
#endif /* NANOSIG_BAREMETAL */

/* ------------------------------------------------------------------ */
/*  signal / slot runtime                                              */
/* ------------------------------------------------------------------ */

int ns_signal_init_raw(ns_signal_t *signal, size_t payload_size, size_t slot_capacity, const char *debug_name)
{
    if(signal == NULL) return NS_E_INVAL;

    signal->payload_size = payload_size;
    signal->slot_capacity = slot_capacity;
    signal->debug_name = debug_name;
    ns_list_init(&signal->slot_list);
    signal->mutex_initialized = 0;

#ifdef NANOSIG_BAREMETAL
    /* 裸机没有 mutex 可初始化，但 mutex_initialized 仍必须置 1：
       ns_signal_lock() 与 ns_timer_validate_created() 都用它判定"对象已初始化"。
       丢掉这一步会让所有 signal/timer API 统一返回 NS_E_INVAL。 */
    signal->mutex_initialized = 1;
    return NS_OK;
#else
    {
        int rc = ns_platform_mutex_init(&signal->mutex, debug_name ? debug_name : "ns-signal");
        if(rc != NS_OK) return rc;

        signal->mutex_initialized = 1;
        return NS_OK;
    }
#endif
}

int ns_signal_deinit_raw(ns_signal_t *signal)
{
    if(signal == NULL) return NS_E_INVAL;
    if(!signal->mutex_initialized) return NS_OK;

#ifdef NANOSIG_BAREMETAL
    signal->mutex_initialized = 0;
    return NS_OK;
#else
    {
        int rc = ns_platform_mutex_deinit(&signal->mutex);

        signal->mutex_initialized = 0;
        return rc;
    }
#endif
}

int ns_signal_connect(
    ns_signal_t *signal,
    ns_slot_fn slot_fn,
    ns_loop_t *target_loop,
    void *user_data,
    ns_connection_t *connection)
{
    int rc;

    if((signal == NULL) || (slot_fn == NULL) || (target_loop == NULL) || (connection == NULL)) return NS_E_INVAL;

    connection->signal = signal;
    connection->slot_fn = slot_fn;
    connection->user_data = user_data;
    connection->target_loop = target_loop;
    ns_list_init(&connection->signal_node);

    rc = ns_signal_lock(signal);
    if(rc != NS_OK) return rc;

    ns_list_push_back(&signal->slot_list, &connection->signal_node);

    rc = ns_signal_unlock(signal);
    return rc;
}

int ns_signal_disconnect(ns_connection_t *connection)
{
    int rc;

    if(connection == NULL) return NS_E_INVAL;

    rc = ns_signal_lock(connection->signal);
    if(rc != NS_OK) return rc;

    ns_list_remove_init(&connection->signal_node);

    rc = ns_signal_unlock(connection->signal);
    return rc;
}

int ns_signal_disconnect_all(ns_signal_t *signal)
{
    ns_list_node_t *node;
    int rc;

    if(signal == NULL) return NS_E_INVAL;

    rc = ns_signal_lock(signal);
    if(rc != NS_OK) return rc;

    while(!ns_list_empty(&signal->slot_list)){
        node = ns_list_pop_front(&signal->slot_list);
        ns_list_init(node);
    }

    rc = ns_signal_unlock(signal);
    return rc;
}

/**
 * @brief 向 signal 的所有 slot 发送消息。
 *
 * 遍历 slot 列表，依次向每个目标 loop 的 MPSC ring 推送记录。
 *
 * @note 本函数是非阻塞的。如果第 N 个连接的推送失败（目标 loop 队列满），
 *       函数立即返回 NS_E_QUEUE_FULL，但前 N-1 个连接已成功入队且无法回滚。
 *       在广播场景下这意味着状态更新可能只传播到了部分接收者。
 *       如需原子广播语义，应在调用前确认所有目标队列有空闲容量。
 */
int ns_signal_emit_raw(ns_signal_t *signal, const void *payload, size_t payload_size)
{
    ns_list_node_t *node;
    ns_slot_call_t call_header;
    ns_mpsc_record_part_t parts[2];
    int rc;

    if(signal == NULL) return NS_E_INVAL;
    if(signal->payload_size != payload_size) return NS_E_INVAL;
    if((payload_size > 0u) && (payload == NULL)) return NS_E_INVAL;

    rc = ns_signal_lock(signal);
    if(rc != NS_OK) return rc;

    parts[1].data = payload;
    parts[1].size = payload_size;

    ns_list_for_each(node, &signal->slot_list){
        ns_connection_t *conn = NS_CONTAINER_OF(node, ns_connection_t, signal_node);

        call_header.fn = conn->slot_fn;
        call_header.user_data = conn->user_data;
        call_header.payload_size = payload_size;

        parts[0].data = &call_header;
        parts[0].size = sizeof(call_header);

        rc = ns_mpsc_record_ring_try_pushv(&conn->target_loop->queue, parts, 2);
        if(rc != NS_OK){
            (void)ns_signal_unlock(signal);
            return rc;
        }

#ifndef NANOSIG_BAREMETAL
        (void)ns_platform_wakeup_signal(&conn->target_loop->wakeup);
#endif
    }

    rc = ns_signal_unlock(signal);
    return rc;
}
