#include <nanosig/nanosig.h>

typedef struct matrix_payload {
    int value;
} matrix_payload_t;

typedef struct matrix_owner {
    ns_signal_t payload_signal;
    ns_signal_t no_payload_signal;
} matrix_owner_t;

NS_DEFINE_SLOT(matrix_payload_slot_fn, matrix_payload_t);
NS_DEFINE_SLOT(matrix_no_payload_slot_fn, ns_no_payload_t);

NS_SIGNAL_DEFINE(matrix_payload_signal, matrix_payload_t);
NS_SIGNAL_DEFINE(matrix_no_payload_signal, ns_no_payload_t);

enum {
    matrix_no_payload_type_has_zero_payload = 1 / (NS_SIGNAL_PAYLOAD_SIZE(ns_no_payload_t) == 0u),
    matrix_no_payload_ptr_has_zero_payload = 1 / (NS_SIGNAL_PAYLOAD_PTR_SIZE(NS_NO_PAYLOAD) == 0u)
};

static void matrix_payload_slot(void *user_data, const matrix_payload_t *payload)
{
    int *out_value = (int *)user_data;
    *out_value = payload->value;
}

static void matrix_no_payload_slot(void *user_data, const ns_no_payload_t *payload)
{
    (void)payload;

    int *out_value = (int *)user_data;
    *out_value = 1;
}

static void matrix_raw_slot(void *user_data, const void *payload)
{
    (void)user_data;
    (void)payload;
}

int ns_pd_macro_matrix_compile_only(ns_loop_t *loop)
{
    matrix_payload_slot_fn typed_slot = matrix_payload_slot;
    matrix_no_payload_slot_fn typed_no_payload_slot = matrix_no_payload_slot;
    matrix_payload_t payload = {
        .value = 7
    };
    matrix_owner_t owner = {
        .payload_signal = NS_SIGNAL_INITIALIZER(matrix_payload_t),
        .no_payload_signal = NS_SIGNAL_INITIALIZER(ns_no_payload_t)
    };
    matrix_owner_t dynamic_owner;
    ns_timer_t timer = NS_TIMER_INITIALIZER(
        100000u,
        NS_TIMER_ATTR_REPEAT | NS_TIMER_ATTR_RELOAD_FROM_NOW);
    int observed = 0;
    ns_connection_t connection;

    NS_SLOT_TYPECHECK(matrix_payload_slot, matrix_payload_t);
    NS_SLOT_TYPECHECK(typed_slot, matrix_payload_t);
    NS_SLOT_TYPECHECK(matrix_no_payload_slot, ns_no_payload_t);
    NS_SLOT_TYPECHECK(typed_no_payload_slot, ns_no_payload_t);

    (void)ns_signal_init(&dynamic_owner.payload_signal, matrix_payload_t);
    (void)ns_signal_init(&dynamic_owner.no_payload_signal, ns_no_payload_t);
    (void)owner;
    (void)dynamic_owner;
    (void)ns_timer_create(&timer, 100000u, NS_TIMER_ATTR_REPEAT);
    (void)ns_signal_connect_typed_to(timer.signal, matrix_no_payload_slot, ns_no_payload_t, loop, &observed, &connection);

    (void)ns_signal_connect_typed(
        matrix_payload_signal,
        matrix_payload_slot,
        matrix_payload_t,
        &observed,
        &connection);
    (void)ns_signal_connect_typed_to(
        matrix_payload_signal,
        typed_slot,
        matrix_payload_t,
        loop,
        &observed,
        &connection);

    (void)ns_signal_connect(
        &matrix_payload_signal,
        matrix_raw_slot,
        NULL,
        &observed,
        &connection);
    (void)ns_signal_connect(
        &matrix_payload_signal,
        matrix_raw_slot,
        loop,
        &observed,
        &connection);

    (void)ns_signal_connect_typed(
        matrix_no_payload_signal,
        matrix_no_payload_slot,
        ns_no_payload_t,
        &observed,
        &connection);
    (void)ns_signal_connect_typed_to(
        matrix_no_payload_signal,
        typed_no_payload_slot,
        ns_no_payload_t,
        loop,
        &observed,
        &connection);

    (void)ns_signal_connect(
        &matrix_no_payload_signal,
        matrix_raw_slot,
        NULL,
        &observed,
        &connection);
    (void)ns_signal_connect(
        &matrix_no_payload_signal,
        matrix_raw_slot,
        loop,
        &observed,
        &connection);

    (void)ns_signal_emit(matrix_payload_signal, &payload);
    (void)ns_signal_emit(matrix_no_payload_signal, NS_NO_PAYLOAD);
    (void)ns_signal_disconnect_all(&matrix_payload_signal);
    (void)ns_signal_disconnect_all(&matrix_no_payload_signal);
    (void)ns_signal_disconnect_all(&timer.signal);
    (void)ns_timer_destroy(&timer);

    return observed;
}

#if 0
static void matrix_wrong_payload_slot(void *user_data, const unsigned *payload);
static void matrix_wrong_no_payload_slot(void *user_data, const unsigned *payload);

void ns_pd_macro_matrix_negative_examples(void)
{
    ns_signal_connect_typed(matrix_payload_signal, matrix_wrong_payload_slot, matrix_payload_t, 0, 0);
    ns_signal_connect_typed(matrix_no_payload_signal, matrix_wrong_no_payload_slot, ns_no_payload_t, 0, 0);
}
#endif
