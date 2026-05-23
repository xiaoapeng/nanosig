# Thread Loop Binding

Status: PD closeout complete design constraint; P1b loop-only platform
contract now provides TLS current-thread lookup without public platform
thread-id helpers.

## Decision

Each thread may bind at most one `ns_loop_t`. `ns_loop_create` creates and binds
the current thread's loop; a second create on the same thread returns
`NS_E_EXISTS`. `ns_loop_current` returns the loop bound to the current thread and
returns `NS_E_NO_LOOP` when no loop is bound.

The public type remains `ns_loop_t`. The object is still an event loop; the
thread relationship is a lifecycle invariant.

## Default Connect

Default connect APIs target the current thread's loop:

```c
ns_signal_connect_typed(signal_name, slot_fn, payload_type, user_data, &connection);
ns_signal_connect(&signal, slot_fn, NULL, user_data, &connection);
```

Explicit target-loop variants are retained for cross-thread setup, tests, and
FFI/binding layers:

```c
ns_signal_connect_typed_to(signal_name, slot_fn, payload_type, target_loop, user_data, &connection);
ns_signal_connect(&signal, slot_fn, target_loop, user_data, &connection);
```

## Connect Matrix

The API keeps loop targeting explicit and represents no-payload signals with
the marker type `ns_no_payload_t` instead of a separate `0` function family:

| Payload | Slot check | Loop target | Public macro | Low-level function |
|---|---|---|---|---|
| yes | compile-time typed | current thread | `ns_signal_connect_typed` | `ns_signal_connect` |
| yes | raw `ns_slot_fn` | current thread | `ns_signal_connect(..., NULL, ...)` | `ns_signal_connect` |
| yes | compile-time typed | explicit `ns_loop_t *` | `ns_signal_connect_typed_to` | `ns_signal_connect` |
| yes | raw `ns_slot_fn` | explicit `ns_loop_t *` | `ns_signal_connect(..., loop, ...)` | `ns_signal_connect` |
| `ns_no_payload_t` | compile-time typed | current thread | `ns_signal_connect_typed(..., ns_no_payload_t, ...)` | `ns_signal_connect` |
| `ns_no_payload_t` | raw `ns_slot_fn` | current thread | `ns_signal_connect(..., NULL, ...)` | `ns_signal_connect` |
| `ns_no_payload_t` | compile-time typed | explicit `ns_loop_t *` | `ns_signal_connect_typed_to(..., ns_no_payload_t, ...)` | `ns_signal_connect` |
| `ns_no_payload_t` | raw `ns_slot_fn` | explicit `ns_loop_t *` | `ns_signal_connect(..., loop, ...)` | `ns_signal_connect` |

The typed rows are the default recommendation. The raw rows exist for
FFI/binding layers or adapters that need to erase the payload type deliberately.

At the low level, `ns_signal_connect(..., target_loop, ...)` is the single
connect function. `target_loop == NULL` means "use current thread loop"; a
non-`NULL` value means "use this explicit loop". Splitting these two cases into
separate implementation functions is not allowed.

## Platform Finding

Do not depend solely on OS thread user pointers for loop lookup.

- POSIX/macOS pthread-specific data is key-based thread-local storage for the
  current thread.
- Win32 TLS/FLS is likewise current-thread local by index.
- Some RTOSes expose per-task local storage through task handles, but this is
  not portable across Linux, macOS, Windows, and RTOS targets.

## Implementation Shape

Use these pieces:

- platform TLS for the fast current-thread lookup used by default connect APIs
  and `ns_loop_current`;
- create-time duplicate checks can be satisfied first through the current
  thread TLS slot;
- any future cross-thread diagnostics or "lookup by thread handle" support must
  be a core-owned registry design added when the feature exists, not a hidden
  dependency on platform thread-id helpers.

P1a/P1b `platform/port.h` deliberately defines only the TLS key operations
needed for the current-thread fast path:

- `ns_platform_tls_key_create`
- `ns_platform_tls_key_destroy`
- `ns_platform_tls_get`
- `ns_platform_tls_set`

The core above `platform/` must not include platform headers or use OS-specific
thread APIs directly.
