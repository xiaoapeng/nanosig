<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-05-16 | Updated: 2026-05-24 -->

# cmake

## Purpose
Guidance for source directory `cmake/`, which contains reusable CMake modules for compiler warnings, sanitizer configuration, and compile-only PD API checks used by the top-level build.

## Key Files
| File | Description |
|------|-------------|
| `ApiCompileChecks.cmake` | Registers syntax-only API compile checks as custom targets and CTest tests. |
| `CompileWarnings.cmake` | Defines helper functions that apply warning flags and MSVC `/Zc:preprocessor`. |
| `Sanitizers.cmake` | Defines sanitizer options and helper logic for ASAN, TSAN, and UBSAN flags. |

## Subdirectories
| Directory | Purpose |
|-----------|---------|
| _None_ | This directory currently has no nested source structure. |

## For AI Agents

### Working In `cmake/`
- Keep helpers small and target-scoped; do not add global compiler flags unless the top-level build explicitly needs them.
- Preserve `/Zc:preprocessor` on MSVC because the public API macro design depends on conforming preprocessing.
- Keep API compile checks syntax-only; PD demos must not link until runtime implementation phases land.

### Testing Requirements
- Reconfigure at least one preset after editing these files.
- For sanitizer logic changes, configure the matching sanitizer preset where the local toolchain supports it.

### Common Patterns
- Helper functions are named `nanosig_apply_*`.
- API compile-check helpers are named `nanosig_add_*`.
- Build options use `NANOSIG_ENABLE_*`.

## Dependencies

### Internal
- Loaded by root `CMakeLists.txt`.

### External
- CMake 3.20 or newer.
- Compiler sanitizer support varies by platform.

<!-- MANUAL: Any manually added notes below this line are preserved on regeneration -->
