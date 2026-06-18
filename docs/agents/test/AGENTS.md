<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-05-16 | Updated: 2026-05-24 -->

# test

## Purpose
Guidance for source directory `test/`, which contains nanosig unit, integration, stress, audit, and compile-only API checks wired into CTest through the root CMake build.

## Key Files
| File | Description |
|------|-------------|
| `unit/test_macro_expansion.c` | Compile-only check for the public signal/slot macro matrix. |
| `unit/test_platform_contract_compile.c` | Compile-only check for `platform/port.h` and internal atomic contract. |
| `unit/test_platform_backend.c` | Runtime smoke test for the platform backend. |
| `unit/test_loop.c` | Loop lifecycle runtime tests. |
| `unit/test_signal.c` | Signal/slot runtime tests. |
| `unit/test_timer.c` | Timer manager runtime tests. |
| `unit/test_broker.c` | Event broker runtime tests. |

## Subdirectories
| Directory | Purpose |
|-----------|---------|
| `unit/` | Runtime unit tests plus compile-only API/contract checks. |

## For AI Agents

### Working In `test/`
- Add focused tests with the implementation that introduces behavior.
- Keep tests deterministic; demos should assert exit status, not stdout ordering.
- Keep compile-only API tests free of runtime assumptions; put runtime behavior in linked unit tests.

### Testing Requirements
- Register tests with CTest.
- For macro-only files, run `ctest --preset windows-release` or `cmake --build --preset windows-release --target api-compile-checks`.
- Run targeted tests plus sanitizer presets for concurrency-sensitive code.

### Common Patterns
- Unit tests cover happy path and edge case for each public API.
- Stress tests belong in a separate stress area and should be label-gated when long-running.

## Dependencies

### Internal
- Runtime tests depend on `nanosig::nanosig`, `src/`, and `platform/`.

### External
- CTest and configured compiler sanitizers.

<!-- MANUAL: Any manually added notes below this line are preserved on regeneration -->
