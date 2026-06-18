<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-05-16 | Updated: 2026-05-24 -->

# bench

## Purpose
Guidance for source directory `bench/`, which is reserved for benchmark programs and result scripts. Currently a placeholder.

## Key Files
| File | Description |
|------|-------------|
| `.gitkeep` | Placeholder before benchmark harness files exist. |

## Subdirectories
| Directory | Purpose |
|-----------|---------|
| _None_ | Future `results/` output should be generated, not hand-authored. |

## For AI Agents

### Working In `bench/`
- Benchmarks report measurements; they should not enforce hard latency SLOs for v1.
- Keep generated benchmark outputs out of source control unless explicitly requested.

### Testing Requirements
- Future benchmark programs should compile on Linux and Windows.

### Common Patterns
- Planned benches: same-thread emit latency, cross-thread enqueue/dequeue latency, and sustained emit throughput.

## Dependencies

### Internal
- Future benchmark targets depend on the implemented nanosig library.

### External
- Platform clocks through nanosig/platform APIs where possible.

<!-- MANUAL: Any manually added notes below this line are preserved on regeneration -->
