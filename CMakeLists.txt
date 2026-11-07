cmake_minimum_required(VERSION 3.20)

project(nanosig
    VERSION 0.1.0
    DESCRIPTION "C11 thread-based signal/slot library"
    LANGUAGES C CXX
)

# ── 版本头文件生成 ──────────────────────────────────────────────
# 以 CMakeLists.txt 中 project(VERSION) 为唯一事实来源，
# 自动生成 include/nanosig/nanosig_version.h（含 MAJOR / MINOR / PATCH / STRING）。
configure_file(
    ${PROJECT_SOURCE_DIR}/include/nanosig/nanosig_version.h.in
    ${PROJECT_BINARY_DIR}/include/nanosig/nanosig_version.h
    @ONLY
)

include(cmake/CompileWarnings.cmake)
include(cmake/Sanitizers.cmake)
include(cmake/Coverage.cmake)
include(cmake/SanitizeAll.cmake)
include(cmake/ModuleConfig.cmake)
include(cmake/TestPrograms.cmake)

if(UNIX)
    find_package(Threads REQUIRED)
endif()

option(NANOSIG_BUILD_TESTS "Build nanosig tests" ON)
option(NANOSIG_BUILD_DEMOS "Build nanosig demo executables" ON)
option(NANOSIG_BUILD_BENCH "Build nanosig benchmarks" OFF)
option(NANOSIG_BUILD_API_CHECKS "Register compile-only API checks" ON)

include(cmake/Platform.cmake)

set(NANOSIG_DS_SOURCES
    src/ds/ns_hashtbl.c
    src/ds/ns_mpsc_record_ring.c
    src/ds/ns_rbtree.c
    src/ds/ns_ringbuf.c
)

# 裸机后端不编入 broker（依赖 thread/waitset）与 hashtbl（signal+timer 路径上
# 没有任何调用者）。源列表是显式列表而非 glob，故此处逐项裁掉。
if(NANOSIG_PLATFORM STREQUAL "baremetal")
    set(NANOSIG_BROKER_SOURCES "")
    list(REMOVE_ITEM NANOSIG_DS_SOURCES src/ds/ns_hashtbl.c)
else()
    set(NANOSIG_BROKER_SOURCES src/ns_broker.c)
endif()

# 库源列表单独成变量：负向链接门需要把同一批源直接编进 try_compile，
# 而不是 -lnanosig（configure 期归档尚不存在，链接一定报 "library not found"，
# 那样的门永远"通过"却什么都没验证）。
set(NANOSIG_LIB_SOURCES
    src/nanosig.c
    ${NANOSIG_BROKER_SOURCES}
    src/ns_timer.c
    src/ns_formatio.c
    src/ns_debug.c
    ${NANOSIG_PLATFORM_SOURCES}
    ${NANOSIG_DS_SOURCES}
)

add_library(nanosig STATIC ${NANOSIG_LIB_SOURCES})
add_library(nanosig::nanosig ALIAS nanosig)

# Deterministic float formatting: the %e extreme-value path derives its digits
# from a power-of-10 polynomial approximation, which is sensitive to FP
# contraction (FMA). arm64 always has FMA and Clang/GCC contract by default,
# while MSVC /fp:precise and x86-64 baseline builds do not — producing
# different low-order digits. Disabling contraction keeps the formatter output
# identical across supported compilers/architectures.
if(NOT MSVC)
    set_source_files_properties(src/ns_formatio.c PROPERTIES
        COMPILE_OPTIONS "-ffp-contract=off"
    )
endif()

target_compile_features(nanosig PUBLIC c_std_11)
target_include_directories(nanosig PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_include_directories(nanosig PRIVATE
    ${PROJECT_SOURCE_DIR}
)

nanosig_apply_platform(nanosig)
nanosig_apply_module_config(nanosig)
nanosig_apply_private_warnings(nanosig)
nanosig_apply_sanitizers(nanosig)
nanosig_apply_coverage(nanosig)

enable_testing()
add_subdirectory(test)
if(NANOSIG_BUILD_DEMOS AND NANOSIG_PLATFORM STREQUAL "host")
    add_subdirectory(demos)
endif()
if(NANOSIG_BUILD_BENCH AND NANOSIG_PLATFORM STREQUAL "host")
    add_subdirectory(bench)
endif()

# sanitize-all — driven by cmake/SanitizeAll.cmake; see that module for the
# audit pipeline (A1 api-contract, A2 config consistency, A3 header tags, A4
# encoding). Runs only when NANOSIG_ENABLE_ASAN / TSAN / UBSAN is ON.
nanosig_configure_sanitize_all()

# review-docs-check — 校验 docs/review 的 frontmatter、分区、锚点与定位。
find_program(NANOSIG_PYTHON3 python3)
if(NANOSIG_PYTHON3)
    add_custom_target(review-docs-check
        COMMAND ${NANOSIG_PYTHON3}
                ${CMAKE_CURRENT_SOURCE_DIR}/cmake/scripts/review_docs.py validate
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Validating review documents"
        VERBATIM
    )
endif()

nanosig_configure_coverage_report()
