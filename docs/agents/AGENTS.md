<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-05-24 | Updated: 2026-05-24 -->

# agents

## Purpose
This subtree centralizes the directory-specific AGENTS instructions that were moved out of the source tree. It mirrors the code areas under a single docs-owned location while the root and `tmp/` AGENTS files stay in place.

## Key Files
| File | Description |
|------|-------------|
| `bench/AGENTS.md` | Mirrored guidance for the benchmark area. |
| `cmake/AGENTS.md` | Mirrored guidance for the CMake helper area. |
| `demos/AGENTS.md` | Mirrored guidance for the API review demos. |
| `include/AGENTS.md` | Mirrored guidance for public headers. |
| `include/nanosig/AGENTS.md` | Mirrored guidance for the public nanosig header subdirectory. |
| `platform/AGENTS.md` | Mirrored guidance for platform code. |
| `src/AGENTS.md` | Mirrored guidance for implementation sources. |
| `test/AGENTS.md` | Mirrored guidance for tests. |

## Subdirectories
| Directory | Purpose |
|-----------|---------|
| `bench/` | Central copy of the benchmark-area instructions. |
| `cmake/` | Central copy of the CMake helper instructions. |
| `demos/` | Central copy of the demo instructions. |
| `include/` | Central copy of the public-header instructions. |
| `platform/` | Central copy of the platform instructions. |
| `src/` | Central copy of the source implementation instructions. |
| `test/` | Central copy of the test instructions. |

## For AI Agents
- Keep these mirrored instructions in sync with the source-tree directories they describe.
- Keep the root-level `AGENTS.md` and every `tmp/**/AGENTS.md` in place.
- When source-tree guidance changes, update the corresponding copy here in the same change.

## Dependencies

### Internal
- `docs/AGENTS.md` as the docs parent and `docs/agents/` as the centralized instruction subtree.

### External
- None.

<!-- MANUAL: Any manually added notes below this line are preserved on regeneration -->
