# eventhub_os Code Style Notes

Status: style reference for nanosig implementation.

This document summarizes the observed style of `tmp/eventhub_os` and records how
nanosig should borrow that style without becoming source-compatible with
eventhub_os.

## Evidence Base

Representative files inspected:

| Area | Files |
|------|-------|
| Public headers | `tmp/eventhub_os/src/include/eh_signal.h`, `eh_platform.h`, `eh_timer.h`, `eh_error.h`, `eh_atomic.h` |
| Generic utilities | `tmp/eventhub_os/src/general/include/eh_list.h`, `eh_ringbuf.h`, `tmp/eventhub_os/src/general/eh_ringbuf.c`, `eh_hashtbl.c` |
| Core/event logic | `tmp/eventhub_os/src/eh_event.c`, `eh_event_cb.c`, `eh_timer.c` |
| Platform code | `tmp/eventhub_os/src/platform/linux/platform.c`, `tmp/eventhub_os/src/platform/windows/platform.c` |
| Tests | `tmp/eventhub_os/test/test_signal.c`, `test_ringbuf.c`, `test_hashtbl.c` |

## Naming

eventhub_os uses a strict C prefix convention:

| Kind | eventhub_os Style | nanosig Adaptation |
|------|-------------------|--------------------|
| Public functions | `eh_*` | `ns_*` |
| Public types | `eh_*_t` | `ns_*_t` |
| Public constants | `EH_*` | `NS_*` |
| Internal helpers | `_eh_*`, `__*`, or unexported `static` helpers | `_ns_*` or `static` helpers |
| Platform implementation functions | `platform_*` | `ns_platform_*` in nanosig `nanosig/nanosig_port.h` |
| Config macros | `EH_CONFIG_*` | `NS_CONFIG_*` only if needed |

Function and variable names are snake_case. Struct names normally keep the
project prefix, for example `struct eh_event_timer` and `struct eh_list_head`.

## File Layout

Typical files follow this order:

1. Doxygen-style file header.
2. Include guard for headers.
3. Includes.
4. C++ `extern "C"` guard in public headers.
5. Type declarations and macros.
6. Function declarations or implementations.
7. Closing C++ guard and include guard comment.

Headers commonly use guards like `_EH_SIGNAL_H_` or `__EH_LIST_H__`. For
nanosig, prefer the already-started `NANOSIG_*_H` guard shape for consistency
with current files.

## Comment Style

### File Header

Most source and header files start with a Doxygen block:

```c
/**
 * @file eh_signal.h
 * @brief ...
 * @author simon.xiaoapeng (simon.xiaoapeng@gmail.com)
 * @date 2024-07-28
 *
 * @copyright Copyright (c) 2024  simon.xiaoapeng@gmail.com
 *
 */
```

For nanosig, use the same Doxygen shape but update file name, brief text, date,
and copyright owner. Do not copy mojibake from terminal output; the intended
comment language is Chinese.

## Encoding Discipline

All repository text files must remain valid UTF-8. Prefer UTF-8 without BOM for
new and edited source, header, CMake, and Markdown files.

The P2 data-structure import exposed an important failure mode: a file can still
be byte-valid UTF-8 while its Chinese comments are mojibake. Strict decoding is
therefore necessary but not sufficient; reviews must also scan for mojibake
markers such as replacement characters, Latin-1 artifacts, private-use
characters, and common UTF-8-as-GBK fragments in normal source and documentation
text. Do not write literal mojibake examples into project docs unless the
scanner explicitly excludes that example block.

Practical rules:

- Do not use PowerShell `Set-Content` / `Out-File` for bulk rewrites unless the
  command explicitly writes UTF-8 with the intended BOM policy. Their defaults
  can vary by shell version and can silently change encoding or line endings.
- Prefer `apply_patch` for manual edits, especially when Chinese comments are
  present.
- If a scripted rewrite is unavoidable, use an explicit strict UTF-8 writer, for
  example `.NET` `System.Text.UTF8Encoding($false, $true)`, and immediately
  rerun decoding plus mojibake scans.
- Git may quote non-ASCII filenames by default. Encoding validation scripts that
  consume `git ls-files` should use `git -c core.quotePath=false ...` and should
  ignore paths deleted in the working tree.
- Encoding verification has two layers:
  1. strict UTF-8 byte decoding for every existing Git-visible file;
  2. content scan for common mojibake markers in `include/`, `src/`, `test/`,
     `docs/`, `platform/`, `cmake/`, `demos/`, `bench/`, and top-level project
     metadata.
- Encoding cleanup should be text-only unless the user explicitly asks for logic
  changes. In particular, fixing comments must not become an opportunity to
  change data-structure behavior.

### Public API Comments

Public functions and public macros are documented with `/** ... */` blocks
using Chinese first:

```c
/**
 * @brief                   定时器启动
 * @param  timer            定时器实例指针
 * @return int
 */
extern int eh_timer_start(eh_event_timer_t *timer);
```

Common traits:

- `@brief`, `@param`, and `@return` are used for public APIs.
- Alignment is visual rather than mechanically uniform.
- Long contracts are written as multi-line Doxygen comments, not hidden in
  separate docs only.
- Internal implementation comments use short `/* ... */` blocks before the
  relevant branch or algorithm step.
- `//` comments appear in tests or temporary/debug examples, but block comments
  are more common in library code.

For nanosig, keep every public API declaration and public macro documented in
Chinese. Use comments to explain ownership, lifetime, threading, and error
contracts, not only to restate the function name.

## Macro Style

eventhub_os uses macros heavily for:

- static object initialization,
- platform indirection,
- module registration,
- intrusive data-structure iteration,
- lightweight inline API wrappers.

Typical macro patterns:

```c
#define EH_TIMER_INIT(timer)    {                                               \
        .event = EH_EVENT_INIT(timer.event),                                    \
        .rb_node = EH_RBTREE_NODE_INIT(timer.rb_node),                          \
        .expire = 0,                                                            \
        .interval = 0,                                                          \
        .attrribute = 0,                                                        \
    }

#define eh_timer_set_attr(timer, attr)                      \
    do{                                                     \
        (timer)->attrribute = attr;                         \
    }while(0)
```

Observed traits:

- Constant-like macros are uppercase.
- Pure function-wrapper macros can be lowercase when they behave like inline
  functions.
- Multi-statement macros use `do{ ... }while(0)`.
- Initializers use designated fields where readability matters.
- Backslashes are visually aligned in larger public macros.

For nanosig, follow the user-approved PD rule: function-wrapper signal connect,
emit, and dynamic init public macros are lowercase and use the `ns_signal_*` prefix
(`ns_signal_emit`, `ns_signal_connect_typed`,
`ns_signal_connect_typed_to`, `ns_signal_init`). Public macros that declare,
compute type/payload metadata, or only perform type checks are uppercase
(`NS_SIGNAL_DECLARE`, `NS_DEFINE_SLOT`,
`NS_SIGNAL_PAYLOAD_SIZE`, `NS_SIGNAL_PAYLOAD_PTR_SIZE`, `NS_SLOT_TYPECHECK`,
`NS_NO_PAYLOAD`, `NS_LOOP_CONFIG_DEFAULT`), and constants remain uppercase
(`NS_OK`, `NS_TIMER_ATTR_REPEAT`, `NS_TIMER_ATTR_RELOAD_FROM_NOW`). No-payload signals use the public marker type
`ns_no_payload_t` instead of a separate `0` suffix function family. The marker
type is never a copied payload object; macros map it to 0 payload bytes and
emit uses `NS_NO_PAYLOAD`.

Timer style deliberately follows the useful `eh_timer` shape: caller-owned
timer storage, an embedded event/signal as the first field, microsecond
intervals, and attr bits for repeat plus current-time based reload. nanosig
adapts this to `ns_timer_t.signal` and no-payload signal delivery.

Do not split low-level functions when the difference is only a default parameter
versus an explicit parameter. Default-loop and explicit-loop connect both go
through `ns_signal_connect(..., target_loop, ...)`; wrapper macros may pass
`NULL` or a concrete loop, but implementation functions must be single-sourced.

## Formatting

Observed formatting is compact C:

- 4-space indentation.
- Opening braces are mixed: many functions use `function(...){`, while some
  inline helpers in headers put `{` on the next line.
- Single-line guard clauses are common:
  `if(ptr == NULL) return ...;`
- Blank lines are used generously between logical blocks.
- Struct fields and macro values are often column-aligned for readability.
- Pointer spacing is inconsistent in the reference (`type *ptr` and `type* ptr`
  both appear). For nanosig, prefer one local style per file.

For nanosig, use compact C but avoid copying inconsistent whitespace blindly.
Keep diffs readable and consistent with the surrounding file.

## Error Handling

eventhub_os uses negative integer error codes and sometimes encodes errors as
pointers:

- `EH_RET_OK` is `0`.
- Failures are negative constants such as `EH_RET_INVALID_PARAM`,
  `EH_RET_BUSY`, and `EH_RET_MALLOC_ERROR`.
- `eh_param_assert(condition)` returns `EH_RET_INVALID_PARAM`.
- Pointer-returning constructors may return `eh_error_to_ptr(error)`, and
  callers check with `eh_ptr_to_error(ptr)`.

Cleanup paths often use `goto out;` or named error labels when locks/resources
must be released in one place.

For nanosig:

- Keep the simpler public `int` status-code surface already drafted.
- Use `NS_OK == 0` and negative `NS_E_*` values.
- Use single-exit cleanup labels where resource release or lock release needs
  to be centralized.
- Examples that acquire multiple nanosig resources must use named cleanup
  labels and release every successfully initialized resource.
- Do not introduce public error-pointer APIs unless a later design explicitly
  chooses that tradeoff.

## Memory And Ownership

eventhub_os routes most allocations through `eh_malloc` / `eh_free`, even inside
generic utilities. Some platform module-section code uses raw `malloc/free`
because it is platform/bootstrap specific.

Ownership comments are usually close to the data structure or operation. Tests
and implementation code often use explicit cleanup labels to release resources.

For nanosig:

- Non-platform code should use `ns_platform_alloc/free` once the platform layer
  exists.
- The emit path must remain allocation-free.
- Document ownership and lifetime in public headers, especially `user_data`,
  queued payload copies, timers, and loop teardown.

## Data Structures

The reference favors intrusive C data structures:

- list heads embedded in owner structs,
- rbtree nodes embedded in timer/event structures,
- hashtable nodes that combine node header plus key/value storage,
- static assertions for layout invariants, for example timer event member
  offset.

For nanosig, keep the same intrusive style for reusable data structures and
expose the generic list/slist/ringbuf/hashtable/rbtree APIs publicly under
`include/nanosig/`. Core runtime handles such as loop and connection remain
opaque where the public API does not require direct embedding.

## Platform Style

eventhub_os public platform macros in `eh_platform.h` forward to backend
`platform_*` functions. Backends then implement the same small surface for
Linux, Windows, macOS, and MCU targets.

For nanosig:

- `nanosig/nanosig_port.h` should be the only OS-coupling point.
- Keep Linux/Windows backend names parallel.
- Do not add empty MCU backend directories in v1; document the extension path.

## Test Style

Reference tests are plain C programs:

- one subsystem per `test_*.c`,
- file header block at top,
- local helpers near the top,
- direct assertions via debug/error macros such as `EH_DBG_ERROR_EXEC`,
- many explicit numbered use-case comments for edge cases,
- stress-like randomized loops for data structures.

For nanosig:

- Keep tests plain C and CTest-registered.
- Preserve the "happy path + edge cases" style.
- Prefer deterministic assertions for CI; keep long randomized/stress loops
  under stress labels or nightly gates.

## nanosig-Specific Overrides

These are deliberate deviations from eventhub_os style:

- Do not expose `__safety` in public nanosig headers for now.
- Function-wrapper connect/emit/init public macros are lowercase; declaration,
  payload metadata, and type-only macros are uppercase.
- API examples and configuration structs use designated initializers
  (`.field = value`) for readability.
- The public API is not source-compatible with eventhub_os.
- Coroutine/module-init APIs are not ported to nanosig v1.
- `tmp/eventhub_os` remains a reference tree, not a build dependency.

## Practical Checklist For Future Implementation

- Add Doxygen file headers to new `.h` and `.c` files.
- Use Chinese comments for public API contracts and important implementation
  invariants.
- Keep source, headers, tests, docs, and CMake files valid UTF-8 without
  mojibake; run both strict UTF-8 decoding and mojibake-marker scans after any
  bulk edit or cross-shell rewrite.
- Keep public API naming under `ns_*`, `ns_*_t`, and `NS_*`.
- Use lowercase names for function-wrapper connect/emit public macros.
- Prefer intrusive nodes for reusable list/rbtree/queue-style structures.
- Use explicit cleanup labels for multi-resource functions.
- Keep platform-specific code below `platform/`.
- Update this document if the user changes the accepted nanosig style again.
