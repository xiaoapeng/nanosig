# nanosig Data Structure Design Draft

Status: P2 public data structures are implemented and covered by unit tests.
P3 internal fixed-capacity MPSC queue is implemented for later loop runtime
integration.

## Publicly Visible Shapes

Core runtime handles remain opaque or API-owned:

- `ns_loop_t`
- `ns_signal_t`
- `ns_connection_t`
- `ns_timer_t`
- `ns_loop_config_t`

Generic data structures are public because they are useful outside nanosig's
runtime and can be embedded by users directly.

## Public Data Structures

The following generic structures are public and available to users through
`include/nanosig/`:

- intrusive doubly linked list: `include/nanosig/nanosig_list.h`
- intrusive singly linked list: `include/nanosig/nanosig_slist.h`
- byte ring buffer: `include/nanosig/nanosig_ringbuf.h` / `src/ds/ns_ringbuf.c`
- string-key hashtable: `include/nanosig/nanosig_hashtable.h` / `src/ds/ns_hashtable.c`
- rbtree keyed by `uint64_t`: `include/nanosig/nanosig_rbtree.h` / `src/ds/ns_rbtree.c`
- aggregate include: `include/nanosig/nanosig_ds.h`

These APIs are not hidden behind `src/internal/`; they are part of the public
header surface and may be used by applications embedding nanosig.
`nanosig_ds.h` includes `nanosig_status.h`, so status constants such as
`NS_OK` and `NS_E_INVAL` are available when users include the data-structure
aggregate directly.
`nanosig_types.h` carries shared type/compiler helper macros such as
`NS_CONTAINER_OF`, `NS_ARRAY_SIZE`, `NS_STATIC_ASSERT`, and alignment helpers.

The structure fields are visible so callers can embed nodes and provide static
or caller-owned storage. Apart from documented initialization fields such as
ring buffer storage and hashtable buckets, users should mutate these structures
through the public functions/macros rather than editing links, cursors, hashes,
or tree internals directly.

The fixed-capacity Vyukov-style MPSC queue is internal and not public. Its
header lives in `include/nanosig/internal/ns_mpsc.h`, implementation lives in
`src/internal/ns_mpsc.c`, and memory ordering is documented in
`docs/MPSC_MEMORY_ORDER.md`. The internal queue capacity must be a power of two;
future public loop configuration can either reject or round user-provided
capacities when P4 wires the queue into `ns_loop_t`.

Each P2 structure has a plain-C unit test under `test/unit/`:

- `test_ds_list.c`
- `test_ds_slist.c`
- `test_ds_ringbuf.c`
- `test_ds_hashtable.c`
- `test_ds_rbtree.c`
- `test_data_structures_contract_compile.c`

P3 and shared header compile coverage add:

- `test_mpsc.c`
- `test_types_contract_compile.c`

The runtime tests cover normal operations plus representative invalid-argument
and zero-initialized object boundaries for the implementation-backed
structures.

## Loop Shape

Each loop owns:

- fixed-capacity MPSC queue backed by `src/internal/ns_mpsc.c`
- platform wakeup handle
- registration node in the internal loop manager
- current-thread ownership enforced through the loop manager TLS binding
- shutdown flag
- maximum payload size copied into queue slots

## Loop Manager Shape

The internal loop manager owns the one-loop-per-thread invariant:

- fast current-thread lookup slot backed by platform TLS when available
- core-owned loop registry for teardown and diagnostics
- lock protecting the registry when cross-thread management paths are added
- create-time duplicate check returning `NS_E_EXISTS`
- current-thread lookup failure returning `NS_E_NO_LOOP`

P1b deliberately does not expose platform thread id/equal/hash APIs. The loop
manager must therefore enforce duplicate create/current lookup through the TLS
current-loop pointer first; any future foreign-thread lookup needs a new
core-owned registry design rather than a hidden platform thread-id dependency.

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

The timer manager / future broker-owned timer source owns:

- wakeup handle
- rbtree ordered by deadline
- registry lock

The earlier standalone global timer thread design was superseded by the
`ns_event_broker_t` direction recorded in `docs/共识计划.md`.

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
