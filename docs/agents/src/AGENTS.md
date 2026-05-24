<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-05-16 | Updated: 2026-05-24 -->

# src

## Purpose
Guidance for source directory `src/`, which contains nanosig implementation sources and internal headers. The public runtime is still mostly pending, but P1a added internal atomic helpers used by later implementation phases.

## Key Files
| File | Description |
|------|-------------|
| `.gitkeep` | Placeholder that keeps the directory present before implementation phases. |
| `nanosig.c` | Empty PD-stage compile anchor; it must not contain implementation logic before sign-off. |

## Subdirectories
| Directory | Purpose |
|-----------|---------|
| `internal/` | Internal helper headers such as `ns_atomic.h`. |

## For AI Agents

### Working In `src/`
- Keep `nanosig.c` limited to the compile-anchor include until core runtime phases start.
- When implementation starts, keep OS-specific code out of this directory; use `platform/` instead.

### Testing Requirements
- Future source changes require targeted unit/integration tests plus relevant sanitizer presets.

### Common Patterns
- Internal symbols should use `_ns_*` where cross-file visibility is needed.
- Emit-path internals must remain allocation-free once implemented.

## Dependencies

### Internal
- Will depend on `include/nanosig/` and `platform/port.h`.

### External
- Standard C and platform abstraction only.

<!-- MANUAL: Any manually added notes below this line are preserved on regeneration -->
