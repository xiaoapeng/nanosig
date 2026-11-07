<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-05-16 | Updated: 2026-05-24 -->

# cmake

## Purpose
Guidance for source directory `cmake/`, which contains reusable CMake modules for compiler warnings, sanitizer configuration, and compile-only API checks used by the top-level build.

## Key Files
| File | Description |
|------|-------------|
| `ApiCompileChecks.cmake` | Registers syntax-only API compile checks as custom targets and CTest tests. |
| `CompileWarnings.cmake` | Defines helper functions that apply warning flags and MSVC `/Zc:preprocessor`. |
| `Sanitizers.cmake` | Defines sanitizer options and helper logic for ASAN, TSAN, and UBSAN flags. |
| `Platform.cmake` | Selects one backend directory and applies its public/private configuration. |
| `ModuleConfig.cmake` | Exposes optional module settings; empty values preserve source defaults. |
| `TestPrograms.cmake` | Shared setup for test, demo, and benchmark executables. |
| `Coverage.cmake` | Applies coverage instrumentation and registers the report target. |

## Subdirectories
| Directory | Purpose |
|-----------|---------|
| _None_ | This directory currently has no nested source structure. |

## For AI Agents

### Working In `cmake/`
- Keep helpers small and target-scoped; do not add global compiler flags unless the top-level build explicitly needs them.
- Preserve `/Zc:preprocessor` on MSVC because the public API macro design depends on conforming preprocessing.
- Keep API compile checks syntax-only and share the library's public definitions and include paths.
- Register executable tests, demos, and benchmarks in their own directory's CMakeLists.txt; preserve their root build output paths and working directories.
- Module settings use existing C macro names as cache entries; empty means the source default, while `0` remains an explicit value. See `docs/BUILD_CONFIG.md`.

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
