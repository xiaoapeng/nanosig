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

ns_signal_t matrix_payload_signal;
ns_signal_t matrix_no_payload_signal;

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
    matrix_owner_t owner = {0};
    matrix_owner_t dynamic_owner;
    ns_timer_t timer;
    int observed = 0;
    ns_connection_t connections[9];

    NS_SLOT_TYPECHECK(matrix_payload_slot, matrix_payload_t);
    NS_SLOT_TYPECHECK(typed_slot, matrix_payload_t);
    NS_SLOT_TYPECHECK(matrix_no_payload_slot, ns_no_payload_t);
    NS_SLOT_TYPECHECK(typed_no_payload_slot, ns_no_payload_t);

    (void)ns_signal_init(&matrix_payload_signal, matrix_payload_t);
    (void)ns_signal_init(&matrix_no_payload_signal, ns_no_payload_t);
    (void)ns_signal_init(&dynamic_owner.payload_signal, matrix_payload_t);
    (void)ns_signal_init(&dynamic_owner.no_payload_signal, ns_no_payload_t);
    (void)owner;
    (void)dynamic_owner;
    (void)ns_timer_create(&timer, 100000u, NS_TIMER_ATTR_REPEAT);
    (void)ns_signal_connect_typed(timer.signal, matrix_no_payload_slot, ns_no_payload_t, loop, &observed, &connections[0]);

    (void)ns_signal_connect_typed(
        matrix_payload_signal,
        matrix_payload_slot,
        matrix_payload_t,
        loop,
        &observed,
        &connections[1]);
    (void)ns_signal_connect_typed(
        matrix_payload_signal,
        typed_slot,
        matrix_payload_t,
        loop,
        &observed,
        &connections[2]);

    (void)ns_signal_connect(
        &matrix_payload_signal,
        matrix_raw_slot,
        loop,
        &observed,
        &connections[3]);
    (void)ns_signal_connect(
        &matrix_payload_signal,
        matrix_raw_slot,
        loop,
        &observed,
        &connections[4]);

    (void)ns_signal_connect_typed(
        matrix_no_payload_signal,
        matrix_no_payload_slot,
        ns_no_payload_t,
        loop,
        &observed,
        &connections[5]);
    (void)ns_signal_connect_typed(
        matrix_no_payload_signal,
        typed_no_payload_slot,
        ns_no_payload_t,
        loop,
        &observed,
        &connections[6]);

    (void)ns_signal_connect(
        &matrix_no_payload_signal,
        matrix_raw_slot,
        loop,
        &observed,
        &connections[7]);
    (void)ns_signal_connect(
        &matrix_no_payload_signal,
        matrix_raw_slot,
        loop,
        &observed,
        &connections[8]);

    (void)ns_signal_emit(matrix_payload_signal, &payload);
    (void)ns_signal_emit(matrix_no_payload_signal, NS_NO_PAYLOAD);
    (void)ns_signal_disconnect_all(&matrix_payload_signal);
    (void)ns_signal_disconnect_all(&matrix_no_payload_signal);
    (void)ns_signal_disconnect_all(&timer.signal);
    (void)ns_signal_deinit(matrix_no_payload_signal);
    (void)ns_signal_deinit(matrix_payload_signal);
    (void)ns_signal_deinit(dynamic_owner.no_payload_signal);
    (void)ns_signal_deinit(dynamic_owner.payload_signal);
    (void)ns_timer_destroy(&timer);

    return observed;
}

#if 0
static void matrix_wrong_payload_slot(void *user_data, const unsigned *payload);
static void matrix_wrong_no_payload_slot(void *user_data, const unsigned *payload);

void ns_pd_macro_matrix_negative_examples(ns_loop_t *loop)
{
    ns_signal_connect_typed(matrix_payload_signal, matrix_wrong_payload_slot, matrix_payload_t, loop, 0, 0);
    ns_signal_connect_typed(matrix_no_payload_signal, matrix_wrong_no_payload_slot, ns_no_payload_t, loop, 0, 0);
}
#endif
