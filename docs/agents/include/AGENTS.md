<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-05-16 | Updated: 2026-05-24 -->

# include

## Purpose
Guidance for source directory `include/`, the container for public headers installed or included by users of nanosig.

## Key Files
| File | Description |
|------|-------------|
| _None_ | Public headers live under `nanosig/`. |

## Subdirectories
| Directory | Purpose |
|-----------|---------|
| `nanosig/` | Public nanosig API headers (see `nanosig/AGENTS.md`). |

## For AI Agents

### Working In `include/`
- Keep public API headers under `include/nanosig/`; do not add implementation-only headers here.

### Testing Requirements
- Syntax-check API demos after public header changes.

### Common Patterns
- Public headers use include guards, C linkage guards, and C11-compatible declarations.

## Dependencies

### Internal
- Included by demos, future tests, and implementation files.

### External
- Standard C headers only at the current PD stage.

<!-- MANUAL: Any manually added notes below this line are preserved on regeneration -->
