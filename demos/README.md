# nanosig API Review Demos

These files are API usage demos. They are still compiled through the root
`api-compile-checks` target instead of being linked as runnable P7 demos, so
they can keep the public call shape stable while broker/demo runtime work is
still pending.

- `demo_same_thread.c`
- `demo_cross_thread.c`
- `demo_timer_cross_thread.c`

Use them to review whether the public API feels reasonable. The examples keep
function-wrapper connect/emit/init macros lowercase, keep declaration,
payload-metadata, and type-only macros uppercase, and use C designated initializers so
the call shape stays close to ordinary C APIs. A thread owns at most one loop,
so cross-thread demos sketch producer and consumer thread entry points instead
of creating multiple loops in one function. The demos also cover the current
connect matrix: default current-loop connect, explicit `_to` connect, payload
signals, and no-payload signals through `ns_no_payload_t`. Examples that acquire
more than one nanosig resource use kernel-style `goto` cleanup labels so every
successful init has a matching teardown path. Timer examples use caller-owned
`ns_timer_t` storage and connect to the embedded no-payload `timer.signal`.
