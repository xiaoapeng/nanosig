# nanosig MPSC Memory Order

Status: P3 implementation note.

The P3 queue is a fixed-capacity multi-producer, single-consumer queue.
It is available as a public low-level data-structure API and is also used as
the future loop work queue for cross-thread signal emit.

## Contract

- Multiple producer threads may call `ns_mpsc_try_push` concurrently.
- Exactly one consumer thread may call `ns_mpsc_try_pop`.
- Queue capacity is fixed at initialization time.
- Queue capacity must be a power of two and no larger than half of the
  `size_t` counter range.
- Push either copies one complete item or returns `NS_E_QUEUE_FULL`.
- Push and pop do not allocate memory.
- The caller provides both the slot array and item storage. Capacity is passed
  as the generic `ns_capacity_t` power-of-two enum so the slot array and item
  storage can be sized as `capacity` and `capacity * item_size`.
- The caller must not destroy or reinitialize the queue while any producer or
  the consumer may still access it.

The queue stores fixed-size items. Variable-size signal payloads are expected to
be copied into a fixed-size loop work item chosen by `ns_loop_config_t`; the MPSC
layer only sees that final item size.

## Shape

Each slot owns:

- a monotonically increasing sequence number.

The queue owns:

- an atomic enqueue position shared by producers;
- an atomic dequeue position advanced by the single consumer and read by
  observability helpers;
- caller-provided item storage addressed by slot index and item size.
- a slot mark equal to `capacity - 1`, rather than storing capacity directly.

Slot `i` is initialized with sequence `i`. A producer can claim position `pos`
only when `slot[pos & (capacity - 1)].sequence == pos`. After copying the item
into the slot, the producer publishes it by storing sequence `pos + 1`.

The consumer can read position `pos` only when
`slot[pos & (capacity - 1)].sequence == pos + 1`. After copying the item out,
it releases the slot for a future producer by storing sequence `pos + capacity`.

The power-of-two capacity constraint is part of correctness, not only a speed
optimization. The enqueue position is a wrapping `size_t` counter; if capacity
does not divide the counter modulus, `pos % capacity` would choose the wrong
next slot after counter wraparound.

## Memory Orders

Producer slot acquire load:

- observes the consumer's release store when a slot is recycled;
- prevents the producer's item copy from moving before the slot is known to be
  free.

Producer enqueue-position CAS:

- uses relaxed ordering;
- only assigns a unique position to one producer;
- does not publish item bytes.

Producer slot release store:

- happens after copying the item into slot storage;
- publishes the item to the consumer.

Consumer slot acquire load:

- observes the producer's release store;
- makes the copied item bytes visible before the consumer copies them out.

Consumer slot release store:

- happens after copying the item out;
- recycles the slot for producers.

The queue also exposes snapshot helpers for total capacity and current free
capacity. `ns_mpsc_free_capacity` is only an observation helper; it reads the
enqueue/dequeue positions with relaxed ordering and clamps concurrent snapshots
into the valid range `[0, capacity]`. Full and empty detection for queue
correctness still come from per-slot sequence numbers.

## Full And Empty

`ns_mpsc_try_push` returns `NS_E_QUEUE_FULL` when the target slot sequence is
behind the producer position, meaning all slots that could be claimed are still
owned by the consumer side or by already published work.

`ns_mpsc_try_pop` reports an empty queue by returning `NS_E_EMPTY`. Empty is a
distinct return code, not a success variant, so callers can branch on the return
value alone without an extra output parameter.

## Platform Boundary

The queue depends only on C11 atomics through `nanosig_atomic.h`. It does not
use platform mutexes, condition variables, wakeups, or thread handles. P4 will
combine this queue with the platform wakeup handle owned by `ns_loop_t`.
