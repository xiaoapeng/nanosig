# nanosig API Design Draft

Status: PD API closeout complete. P1 loop platform backends, P2 public
data structures, P3 variable-size MPSC record ring, P4 loop management,
P5 signal/slot runtime, and P6 phase-1 timer manager have landed. Broker-driven
timer integration remains a later phase.

## Lifecycle

Applications call `ns_init()` before any other nanosig API and call
`ns_shutdown()` during deterministic teardown. This keeps the platform layer,
loop manager, signal mutexes, timer manager, and future `ns_event_broker_t`
lifecycle explicit and TSAN-friendly. It does not imply a standalone global
timer thread; the current timer manager is driven through `next_timeout` /
`fire_expired`, and later broker integration will call those hooks.

```c
int rc = ns_init();
if(rc != NS_OK) {
    return rc;
}

/* create loops, connect slots, emit signals, run timers */

out_shutdown:
    (void)ns_shutdown();
    return rc;
```

## Status Codes

`ns_status_t` and `NS_*` status constants live in
`include/nanosig/nanosig_status.h`. The umbrella `nanosig.h`, `nanosig_ds.h`,
and implementation-backed data-structure headers include it so users can
compare results against `NS_OK`, `NS_E_INVAL`, and the other public codes
without relying on a private include order.

## Atomic Helpers

The C11 atomic wrapper is public as `include/nanosig/nanosig_atomic.h`. It keeps
the `ns_memory_order_t` names and `ns_atomic_*` operation wrappers available to
users that build lock-free or low-level structures next to nanosig.

## Type Helpers

Common eventhub-style compiler and type helper macros are exposed with nanosig
names in `include/nanosig/nanosig_types.h`. This includes container lookup,
array sizing, static assertions, alignment helpers, branch hints, selected
compiler attributes, and bit-scan helpers. The public surface does not preserve
`eh_*` names.

## Public Data Structures

P2 plus P3 expose the generic low-level structures under `include/nanosig/` so
applications can embed and use them directly:

- `nanosig_list.h`
- `nanosig_slist.h`
- `nanosig_ringbuf.h`
- `nanosig_hashtable.h`
- `nanosig_rbtree.h`
- `nanosig_mpsc.h`
- `nanosig_mpsc_record_ring.h`
- `nanosig_ds.h`
- `nanosig_types.h`
- `nanosig_status.h`

Public-header declaration rule:

- helpers that are intended to stay header-only use `static inline`;
- all other public function declarations in `include/nanosig/*.h` must be
  written with an explicit `extern`;
- implementation-backed public data-structure `.c` files live under `src/ds/`.

The implementation-backed structures live in `src/ds/`. Their fields are
visible for C embedding and static storage, but callers should use the public
functions/macros instead of mutating links, cursors, or tree internals directly.
The fixed-capacity MPSC queue (`nanosig_mpsc.h` / `ns_mpsc.c`) and the
variable-size MPSC record ring (`nanosig_mpsc_record_ring.h` /
`ns_mpsc_record_ring.c`) are both public and follow the same split.

## Thread-Bound Loops

Each thread that runs callbacks owns at most one `ns_loop_t`.

```c
ns_loop_config_t cfg = NS_LOOP_CONFIG_DEFAULT();
cfg.queue_capacity = NS_CAPACITY_1024;
cfg.max_payload_size = 256u;
cfg.debug_name = "worker-B";

ns_loop_t *loop = NULL;
int rc = ns_loop_init(&loop, &cfg);
if(rc != NS_OK) {
    return rc;
}

/* run callbacks on this thread */

(void)ns_loop_deinit();
```

`ns_loop_init` is the per-thread initialization point. A second create in the
same thread fails with `NS_E_EXISTS`. There is no separate `ns_thread_init`.

The type remains named `ns_loop_t`. The object is still the event loop; thread
binding is a lifecycle constraint, not a reason to expose a new public
thread-loop type. `ns_loop_current` returns the loop bound to the current thread
and returns `NS_E_NO_LOOP` if none exists.

## Signals And Slots

The low-level contract is stable and intentionally simple:

- `ns_signal_t` owns a fixed `payload_size`.
- Default connect APIs bind the slot to the current thread's loop.
- Explicit `_to` connect APIs bind the slot to a supplied target loop.
- Payload signals call slots with `(void *user_data, const payload_type *payload)`.
- No-payload signals use the marker payload type `ns_no_payload_t`; typed slots
  still use `(void *user_data, const ns_no_payload_t *payload)` and ignore the
  payload pointer. `ns_no_payload_t` is not a payload object: applications do
  not instantiate it or pass an object pointer for emit.
- `ns_signal_emit_raw` copies the payload into the target loop queue.
- `ns_signal_emit_raw` accepts `payload = NULL` and `payload_size = 0` for
  no-payload signals.
- Public payload-size macros special-case `ns_no_payload_t` and `NS_NO_PAYLOAD`
  to 0, so no-payload emit copies 0 bytes.
- The emit path must not allocate.

The PD macro surface currently uses explicit payload structs:

```c
typedef struct app_sample_payload {
    int value;
    const char *label;
} app_sample_payload_t;

ns_signal_t app_sample_ready;

static void on_sample(void *user_data, const app_sample_payload_t *payload);

ns_connection_t connection;

rc = ns_signal_init(&app_sample_ready, app_sample_payload_t);
if(rc != NS_OK) {
    return rc;
}

ns_signal_connect_typed(
    app_sample_ready,
    on_sample,
    app_sample_payload_t,
    user_data,
    &connection);

app_sample_payload_t payload = {
    .value = 42,
    .label = "adc0"
};
ns_signal_emit(app_sample_ready, &payload);

(void)ns_signal_disconnect(&connection);
(void)ns_signal_deinit(app_sample_ready);
```

No-payload signals use the explicit marker type:

```c
ns_signal_t app_shutdown_requested;

static void on_shutdown(void *user_data, const ns_no_payload_t *payload);

ns_connection_t shutdown_conn;

rc = ns_signal_init(&app_shutdown_requested, ns_no_payload_t);
if(rc != NS_OK) {
    return rc;
}

ns_signal_connect_typed(
    app_shutdown_requested,
    on_shutdown,
    ns_no_payload_t,
    user_data,
    &shutdown_conn);

ns_signal_emit(app_shutdown_requested, NS_NO_PAYLOAD);

(void)ns_signal_disconnect(&shutdown_conn);
(void)ns_signal_deinit(app_shutdown_requested);
```

The public connect surface keeps loop targeting explicit without adding a
second no-payload function family:

| Shape | Current-thread loop | Explicit target loop |
|---|---|---|
| Payload, typed slot | `ns_signal_connect_typed(sig, slot, payload_type, user_data, out)` | `ns_signal_connect_typed_to(sig, slot, payload_type, loop, user_data, out)` |
| Payload, raw slot | `ns_signal_connect(&sig, slot, NULL, user_data, out)` | `ns_signal_connect(&sig, slot, loop, user_data, out)` |
| No payload, typed slot | `ns_signal_connect_typed(sig, slot, ns_no_payload_t, user_data, out)` | `ns_signal_connect_typed_to(sig, slot, ns_no_payload_t, loop, user_data, out)` |
| No payload, raw slot | `ns_signal_connect(&sig, slot, NULL, user_data, out)` | `ns_signal_connect(&sig, slot, loop, user_data, out)` |

`ns_signal_connect_typed` and `ns_signal_connect_typed_to` perform C11 `_Generic`
compile-time checks that the slot has signature
`void (*)(void *, const payload_type *)`. When `payload_type` is
`ns_no_payload_t`, the signal's `payload_size` is 0 and emit uses
`NS_NO_PAYLOAD`. The raw variants are deliberately untyped escape hatches for
bindings and adapters that already operate on `ns_slot_fn`.

The typed connect macros are lowercase because they behave like function
wrappers and ultimately call `ns_signal_connect`. Public signal/slot operation
names use the `ns_signal_*` prefix consistently with `ns_signal_disconnect`.
For explicit cross-thread or binding-layer use, call the `_to` variants or the
low-level `ns_signal_connect(..., target_loop, ...)`.

Macros that declare symbols, compute payload metadata, or perform type-only
checks stay uppercase, such as `NS_SIGNAL_DECLARE`, `NS_DEFINE_SLOT`,
`NS_SIGNAL_PAYLOAD_SIZE`, `NS_SIGNAL_PAYLOAD_PTR_SIZE`, `NS_SLOT_TYPECHECK`,
and `NS_LOOP_CONFIG_DEFAULT`.
Operational function-wrapper macros stay lowercase; `ns_signal_init(signal,
payload_type)` is one of them and directly accepts the payload type.

## Struct-Owned Signals

`ns_signal_t` storage may be static, automatic, heap-allocated, or embedded in a
user struct, but it must be explicitly initialized before use. The current API
does not provide an aggregate static initializer for signal objects:

```c
typedef struct app_model {
    ns_signal_t changed;
    ns_signal_t stopped;
} app_model_t;

static app_model_t model;
```

Use `ns_signal_init` during the owning object's initialization. A successful
init creates an internal mutex, so every successfully initialized signal must be
deinitialized after its connections have been disconnected.

```c
int app_model_init(app_model_t *model)
{
    int rc;

    rc = ns_signal_init(&model->changed, app_sample_payload_t);
    if(rc != NS_OK) {
        return rc;
    }

    rc = ns_signal_init(&model->stopped, ns_no_payload_t);
    if(rc != NS_OK) {
        goto out_changed;
    }

    return NS_OK;

out_changed:
    (void)ns_signal_deinit(model->changed);
    return rc;
}

void app_model_deinit(app_model_t *model)
{
    /* Disconnect tracked connections before deinitializing these signals. */
    (void)ns_signal_deinit(model->stopped);
    (void)ns_signal_deinit(model->changed);
}
```

`ns_signal_deinit(signal)` releases the internal mutex created by
`ns_signal_init_raw` / `ns_signal_init`. Calling deinit is a serialized lifetime
operation; it must not race with connect, disconnect, emit, or another init /
deinit on the same signal.

`ns_signal_disconnect_all(signal)` is a teardown/escape-hatch helper for
objects that must drop every connection at once. Healthy code should normally
keep the `ns_connection_t` handles and call `ns_signal_disconnect` explicitly,
because that makes ownership clear. Neither `ns_signal_disconnect` nor
`ns_signal_disconnect_all` frees memory; callers own `ns_connection_t` storage.
Bulk disconnect still does not cancel already queued slot calls, so
`user_data` lifetime rules remain unchanged.

The PD review considered a variadic field-list macro, but the accepted API uses
explicit payload structs. That shape is more predictable for C11, MSVC, FFI, and
documentation because the payload layout is visible and reusable.

## Timers

Timers own an embedded no-payload signal as their first struct member. Creation
does not allocate a timer object; the caller provides storage:

```c
static void on_tick(void *user_data, const ns_no_payload_t *payload);

ns_timer_t timer;
ns_connection_t connection;

rc = ns_timer_init(&timer, 100000u, NS_TIMER_ATTR_REPEAT);
if(rc != NS_OK) {
    goto out_loop;
}

rc = ns_signal_connect_typed_to(
    timer.signal,
    on_tick,
    ns_no_payload_t,
    target_loop,
    user_data,
    &connection);
if(rc != NS_OK) {
    goto out_timer;
}

rc = ns_timer_start(&timer);
if(rc != NS_OK) {
    goto out_connection;
}

rc = ns_loop_run();

out_connection:
    (void)ns_timer_cancel(&timer);
    (void)ns_signal_disconnect(&connection);
out_timer:
    (void)ns_timer_deinit(&timer);
```

`interval_us` is a `uint64_t` microsecond interval. Timer attributes are a
bitmap: bit0 (`NS_TIMER_ATTR_REPEAT`) enables repeat; bit1
(`NS_TIMER_ATTR_RELOAD_FROM_NOW`) changes repeat reload from deadline stepping
to current-time based reload. When bit1 is clear, the next deadline follows
`previous_deadline + interval_us` when that deadline is still in the future,
otherwise it falls back to `now + interval_us`. This mirrors the useful
`eh_timer` behavior while keeping the nanosig timer payload-free.
`ns_timer_cancel` is valid for any timer successfully initialized by
`ns_timer_init`; if the timer is not running, it is a no-op success path.

## Ownership Rules

- One thread owns at most one `ns_loop_t`.
- `ns_loop_init` binds the new loop to the current thread.
- `ns_loop_deinit` must be called by the owning thread and unbinds that thread. It takes no parameter and reads the loop from TLS.
- `ns_signal_emit_raw` is callable from any thread.
- `ns_signal_emit(signal, NS_NO_PAYLOAD)` is callable from any thread for
  `ns_no_payload_t` signals.
- `ns_signal_connect` is callable from any thread when the signal was
  initialized via `ns_signal_init_raw` (which creates an internal mutex).
- `ns_signal_disconnect` is callable from any thread under the same condition.
- `ns_signal_disconnect_all` is callable from any thread under the same
  condition. It exists for teardown/escape-hatch use, not as the normal
  lifecycle path for healthy programs.
- Default `ns_signal_connect_typed` uses the current thread's loop.
- Explicit `ns_signal_connect_typed_to` is a macro-level escape hatch for
  supplied target loops and still calls `ns_signal_connect`.
- Low-level `ns_signal_connect` uses `target_loop == NULL` for current-thread
  loop and non-`NULL` for an explicit target loop; default-vs-explicit behavior
  must not be split into duplicate internal functions.
- `ns_signal_deinit` releases internal resources (mutex) allocated by
  `ns_signal_init_raw` / `ns_signal_init`. `ns_signal_disconnect` removes the
  connection from the signal's slot list but does not free memory. Callers own
  `ns_connection_t` storage.
- Disconnect does not cancel already queued slot invocations.
- `user_data` must outlive any in-flight emit that can still reach the slot.
- A destroyed loop rejects new queued work with `NS_E_SHUTDOWN`.
- `ns_timer_t.signal` is the first member of `ns_timer_t`; timer callbacks are
  connected to that embedded no-payload signal.
