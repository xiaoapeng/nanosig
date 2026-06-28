# Thread Loop Binding

Status: Updated 2026-06-18. Loop model changed from implicit thread binding
to explicit parameter passing.

## Decision

Loop 不绑定线程。调用方通过显式传参管理 loop 生命周期。

`ns_loop_init` 创建 loop，不绑定当前线程。调用方负责决定哪个线程
run 哪个 loop。`ns_loop_run(ns_loop_t *loop)` 和
`ns_loop_quit(ns_loop_t *loop)` 都接受显式 loop 参数。

早期版本的 `ns_loop_current`、`ns_loop_is_owner`、内部 loop manager
注册表和平台 TLS 已全部移除。

## Connect

`ns_signal_connect` 的 `target_loop` 必须非空。loop 不绑定线程，不存在
隐式当前线程 loop：

```c
ns_signal_connect(&signal, slot_fn, target_loop, user_data, &connection);
```

typed connect 同样显式指定目标 loop：

```c
ns_signal_connect_typed(signal_name, slot_fn, payload_type, target_loop, user_data, &connection);
```

## Connect Matrix

| Payload | Slot check | Public macro | Low-level function |
|---|---|---|---|
| yes | compile-time typed | `ns_signal_connect_typed` | `ns_signal_connect` |
| yes | raw `ns_slot_fn` | `ns_signal_connect(...)` | `ns_signal_connect` |
| `ns_no_payload_t` | compile-time typed | `ns_signal_connect_typed(..., ns_no_payload_t, ...)` | `ns_signal_connect` |
| `ns_no_payload_t` | raw `ns_slot_fn` | `ns_signal_connect(...)` | `ns_signal_connect` |

At the low level, `ns_signal_connect(signal, slot_fn, target_loop, user_data, connection)`
is the single connect function. `target_loop` must be non-NULL.

## Rationale

早期设计使用 TLS 绑定 loop 到线程，但这增加了平台复杂度（需要 TLS API）
且限制了灵活性（一个线程只能有一个 loop）。改为显式传参后：

- 平台层不再需要 TLS 接口，简化了 `nanosig/nanosig_port.h`。
- 调用方可以自由决定 loop 和线程的映射关系。
- broker 等内部组件也可以更灵活地使用 loop。
