# nanosig API Review Demos

These files are API usage demos. Each one is built as a runnable executable
(`nanosig_demo_*`) and registered in ctest with its own timeout, so they cover
both the public call shape and the runtime behavior end to end.

- `demo_same_thread.c`
- `demo_cross_thread.c`
- `demo_timer_cross_thread.c`
- `demo_watcher_ready.c`
- `demo_watcher_loop.c`

Use them to review whether the public API feels reasonable. The examples keep
function-wrapper connect/emit/init macros lowercase, keep declaration,
payload-metadata, and type-only macros uppercase, and use C designated initializers so
the call shape stays close to ordinary C APIs. Cross-thread demos sketch
producer and consumer thread entry points. The demos also cover the connect
matrix: explicit loop connect, payload signals, and no-payload signals through
`ns_no_payload_t`. Examples that acquire
more than one nanosig resource use kernel-style `goto` cleanup labels so every
successful init has a matching teardown path. Timer examples use caller-owned
`ns_timer_t` storage and connect to the embedded no-payload `timer.signal`.

`demo_watcher_ready.c` and `demo_watcher_loop.c` show the two `ns_watcher`
callback patterns: the handler either finishes the work in place ("就绪即处理") or
drains the event source and emits a user-owned signal back to a loop. Both get the
waitable from the public `ns_platform_event_t` primitive, so they carry no
OS-specific preprocessor branches.
