# nanosig API Design Draft

Status: PD closeout complete. Implementation is intentionally pending.

## Lifecycle

Applications call `ns_init()` before any other nanosig API and call
`ns_shutdown()` during deterministic teardown. This keeps the global timer
thread explicit and TSAN-friendly.

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

## Thread-Bound Loops

Each thread that runs callbacks owns at most one `ns_loop_t`.

```c
ns_loop_config_t cfg = NS_LOOP_CONFIG_DEFAULT();
cfg.queue_capacity = 1024u;
cfg.max_payload_size = 256u;
cfg.debug_name = "worker-B";

ns_loop_t *loop = NULL;
int rc = ns_loop_create(&loop, &cfg);
if(rc != NS_OK) {
    return rc;
}

/* run callbacks on this thread */

(void)ns_loop_destroy(loop);
```

`ns_loop_create` is the per-thread initialization point. A second create in the
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

NS_SIGNAL_DEFINE(app_sample_ready, app_sample_payload_t);

static void on_sample(void *user_data, const app_sample_payload_t *payload);

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

(void)ns_signal_disconnect(connection);
```

No-payload signals use the explicit marker type:

```c
NS_SIGNAL_DEFINE(app_shutdown_requested, ns_no_payload_t);

static void on_shutdown(void *user_data, const ns_no_payload_t *payload);

ns_signal_connect_typed(
    app_shutdown_requested,
    on_shutdown,
    ns_no_payload_t,
    user_data,
    &connection);

ns_signal_emit(app_shutdown_requested, NS_NO_PAYLOAD);

(void)ns_signal_disconnect(connection);
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

Macros that declare, define, initialize, or perform type-only checks stay
uppercase, such as `NS_SIGNAL_DEFINE`, `NS_DEFINE_SLOT`,
`NS_SIGNAL_INITIALIZER`, `NS_SLOT_TYPECHECK`, and `NS_LOOP_CONFIG_DEFAULT`.
Operational function-wrapper macros stay lowercase; `ns_signal_init(signal,
payload_type)` is one of them and directly accepts the payload type.

## Struct-Owned Signals

`NS_SIGNAL_DEFINE(name, payload_type)` is for standalone objects. When a signal
is embedded in a user struct, initialize the member with
`NS_SIGNAL_INITIALIZER(payload_type)`:

```c
typedef struct app_model {
    ns_signal_t changed;
    ns_signal_t stopped;
} app_model_t;

static app_model_t model = {
    .changed = NS_SIGNAL_INITIALIZER(app_sample_payload_t),
    .stopped = NS_SIGNAL_INITIALIZER(ns_no_payload_t)
};
```

For dynamic object lifetimes, use `ns_signal_init`. Signal initialization only
writes metadata; it does not allocate and has no matching deinit call. The
resources associated with slots are owned by connections and are released by
`ns_signal_disconnect`.

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
        return rc;
    }

    return NS_OK;
}
```

`ns_signal_disconnect_all(signal)` is a teardown/escape-hatch helper for
objects that must drop every connection at once. Healthy code should normally
keep the `ns_connection_t *` handles and call `ns_signal_disconnect` explicitly,
because that makes ownership clear. Bulk disconnect still does not cancel
already queued slot calls, so `user_data` lifetime rules remain unchanged.

This is a deliberate PD review point. The implementation plan originally
considered a variadic field-list macro. The struct-payload draft is more
predictable for C11, MSVC, FFI, and documentation because the payload layout is
visible and reusable. If review prefers the variadic field-list form, PD can
still change it before implementation phases start.

## Timers

Timers own an embedded no-payload signal as their first struct member. Creation
does not allocate a timer object; the caller provides storage:

```c
static void on_tick(void *user_data, const ns_no_payload_t *payload);

ns_timer_t timer;
ns_connection_t *connection = NULL;

rc = ns_timer_create(&timer, 100000u, NS_TIMER_ATTR_REPEAT);
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

rc = ns_loop_run(target_loop);

out_connection:
    (void)ns_timer_cancel(&timer);
    (void)ns_signal_disconnect(connection);
out_timer:
    (void)ns_timer_destroy(&timer);
```

`interval_us` is a `uint64_t` microsecond interval. Timer attributes are a
bitmap: bit0 (`NS_TIMER_ATTR_REPEAT`) enables repeat; bit1
(`NS_TIMER_ATTR_RELOAD_FROM_NOW`) changes repeat reload from deadline stepping
to current-time based reload. When bit1 is clear, the next deadline follows
`previous_deadline + interval_us` when that deadline is still in the future,
otherwise it falls back to `now + interval_us`. This mirrors the useful
`eh_timer` behavior while keeping the nanosig timer payload-free.
`ns_timer_cancel` is valid for any timer successfully initialized by
`ns_timer_create`; if the timer is not running, it is a no-op success path.

## Ownership Rules

- One thread owns at most one `ns_loop_t`.
- `ns_loop_create` binds the new loop to the current thread.
- `ns_loop_destroy` must be called by the owning thread and unbinds that thread.
- `ns_signal_emit_raw` is callable from any thread.
- `ns_signal_emit(signal, NS_NO_PAYLOAD)` is callable from any thread for
  `ns_no_payload_t` signals.
- Default `ns_signal_connect_typed` uses the current thread's loop.
- Explicit `ns_signal_connect_typed_to` is a macro-level escape hatch for
  supplied target loops and still calls `ns_signal_connect`.
- Low-level `ns_signal_connect` uses `target_loop == NULL` for current-thread
  loop and non-`NULL` for an explicit target loop; default-vs-explicit behavior
  must not be split into duplicate internal functions.
- `ns_signal_disconnect` is called by the target loop owner thread.
- `ns_signal_disconnect_all` exists for teardown/escape-hatch use, not as the
  normal lifecycle path for healthy programs.
- `ns_signal_t` has no deinit API. Its own state is metadata; slot resources are
  released when connections are disconnected.
- Disconnect does not cancel already queued slot invocations.
- `user_data` must outlive any in-flight emit that can still reach the slot.
- A destroyed loop rejects new queued work with `NS_E_SHUTDOWN`.
- `ns_timer_t.signal` is the first member of `ns_timer_t`; timer callbacks are
  connected to that embedded no-payload signal.
