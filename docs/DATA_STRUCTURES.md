# nanosig Data Structure Design Draft

Status: PD closeout complete. Implementation is intentionally pending.

## Publicly Visible Shapes

Only opaque handles and configuration structs are visible to users:

- `ns_loop_t`
- `ns_signal_t`
- `ns_connection_t`
- `ns_timer_t`
- `ns_loop_config_t`

## Internal Structures

The following structures are implementation-owned and will be introduced in
later phases:

- intrusive doubly linked list
- intrusive singly linked list
- byte ring buffer
- string-key hashtable
- rbtree keyed by timer deadline
- fixed-capacity Vyukov-style MPSC queue

## Loop Shape

Each loop owns:

- fixed-capacity MPSC queue
- platform wakeup handle
- owner thread id
- registration node in the internal loop manager
- shutdown flag
- maximum payload size copied into queue slots

## Loop Manager Shape

The internal loop manager owns the one-loop-per-thread invariant:

- fast current-thread lookup slot backed by platform TLS when available
- registry from platform thread id to `ns_loop_t *`
- lock protecting the registry
- create-time duplicate check returning `NS_E_EXISTS`
- current-thread lookup failure returning `NS_E_NO_LOOP`

The registry is still required even when a platform has thread-local user data,
because Linux/POSIX, macOS, and Win32 TLS APIs do not provide a portable
foreign-thread "get user pointer by thread handle" operation. Platforms that do
support task-handle user pointers can implement the registry lookup as a thin
backend optimization later, but the core contract remains manager-shaped.

## Signal Shape

Each signal owns:

- fixed payload size
- `payload_size == 0` when the public payload type is `ns_no_payload_t`
- no-payload signals enqueue 0 payload bytes; `ns_no_payload_t` is a type
  marker for slot signatures, not a copied payload object
- slot list
- optional slot capacity hint
- debug name

Signals do not have a public deinit step. Signal metadata is initialized
statically with `NS_SIGNAL_DEFINE` / `NS_SIGNAL_INITIALIZER` or dynamically with
`ns_signal_init`; connection-owned nodes are released by `ns_signal_disconnect`
or the teardown escape hatch `ns_signal_disconnect_all`.

Each connection owns:

- signal pointer
- target loop pointer
- slot function pointer (`ns_slot_fn`)
- user data pointer
- list node for the signal slot list

## Timer Shape

The global timer service owns:

- dedicated timer thread
- wakeup handle
- rbtree ordered by deadline
- registry lock

Each timer owns:

- embedded `ns_signal_t signal` as its first field
- interval in microseconds (`uint64_t`)
- next deadline in microseconds
- attr bitmap: bit0 repeat, bit1 reload from current time
- running/cancellation state; `ns_timer_cancel` is valid after
  `ns_timer_create` and is a no-op when the timer is not running

## Allocation Boundary

Allocation is allowed during `ns_loop_create`, `ns_signal_connect`, and
`ns_timer_create`. Allocation is not allowed during `ns_signal_emit_raw` or
the future typed `ns_signal_emit` wrapper.
