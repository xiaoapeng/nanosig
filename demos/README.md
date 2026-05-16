# nanosig API Review Demos

These files are PD-stage API usage demos. They are intentionally not linked as
runnable targets until P7 because the implementation `.c` files do not exist
yet. They are compiled through the root `api-compile-checks` target and CTest
to keep the public API shape from regressing during PD.

- `demo_same_thread.c`
- `demo_cross_thread.c`
- `demo_timer_cross_thread.c`

Use them to review whether the public API feels reasonable before PD sign-off.
The examples keep function-wrapper connect/emit macros lowercase, keep
declaration and type-only macros uppercase, and use C designated initializers so
the call shape stays close to ordinary C APIs. A thread owns at most one loop,
so cross-thread demos sketch producer and consumer thread entry points instead
of creating multiple loops in one function. The demos also cover the current
connect matrix: default current-loop connect, explicit `_to` connect, payload
signals, and no-payload signals through `ns_no_payload_t`. Examples that acquire
more than one nanosig resource use kernel-style `goto` cleanup labels so every
successful init has a matching teardown path. Timer examples use caller-owned
`ns_timer_t` storage and connect to the embedded no-payload `timer.signal`.
