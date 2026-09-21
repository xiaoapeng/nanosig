#!/bin/sh
# audit_runner.sh — driver invoked by `nanosig_audit_*` custom targets.
#
# Arguments:
#   $1  audit kind (asan / ubsan / tsan)
#   $2  NANOSIG_ENABLE_ASAN (ON / OFF)
#   $3  NANOSIG_ENABLE_TSAN (ON / OFF)
#   $4  NANOSIG_ENABLE_UBSAN (ON / OFF)
#   $5  NANOSIG_BUILD_API_CHECKS (ON / OFF)
#   $6  source directory (PROJECT_SOURCE_DIR)
#   $7  binary directory (CMAKE_BINARY_DIR)
#   $8  Python 3 interpreter (may be empty)
#   $9  cmake path
#
# Runs the A1..A4 audit pipeline for the active sanitizer kind. Returns
# non-zero on the first failing step; otherwise returns 0.

set -eu

KIND="$1"
ASAN_FLAG="$2"
TSAN_FLAG="$3"
UBSAN_FLAG="$4"
API_CHECKS_FLAG="$5"
SOURCE_DIR="$6"
BINARY_DIR="$7"
PYTHON3="$8"
CMAKE="$9"

if [ -z "$KIND" ] || [ -z "$SOURCE_DIR" ] || [ -z "$BINARY_DIR" ] || [ -z "$CMAKE" ]; then
    echo "[audit_runner] missing required argument" >&2
    exit 2
fi

export NANOSIG_ENABLE_ASAN="${ASAN_FLAG}"
export NANOSIG_ENABLE_TSAN="${TSAN_FLAG}"
export NANOSIG_ENABLE_UBSAN="${UBSAN_FLAG}"

echo "[${KIND}] audit_runner starting"

# --- A1: api-compile-checks anchor (skip if NANOSIG_BUILD_API_CHECKS=OFF) ---
if [ "${API_CHECKS_FLAG:-ON}" = "OFF" ]; then
    echo "[${KIND}] A1 api-compile-checks disabled (NANOSIG_BUILD_API_CHECKS=OFF), skipped"
else
    echo "[${KIND}] A1 api-compile-checks starting"
    API_TARGETS=""
    for tgt in nanosig_api_check_macro_matrix \
               nanosig_api_check_platform_contract \
               nanosig_api_check_broker_contract \
               nanosig_api_check_data_structures \
               nanosig_api_check_types \
               nanosig_api_check_cpp_compat; do
        if grep -q "^${tgt}:" "${BINARY_DIR}/Makefile" 2>/dev/null || \
           grep -q "^${tgt}:" "${BINARY_DIR}/build.ninja" 2>/dev/null; then
            API_TARGETS="${API_TARGETS} ${tgt}"
        fi
    done
    if [ -n "${API_TARGETS}" ]; then
        ( cd "${BINARY_DIR}" && "${CMAKE}" --build . --target ${API_TARGETS} )
    else
        echo "[${KIND}] A1 api-compile-checks targets not present, skipped"
    fi
fi

# --- A2: configuration consistency ---
echo "[${KIND}] A2 configuration consistency"
"${CMAKE}" \
    -DNANOSIG_AUDIT_KIND="${KIND}" \
    -DNANOSIG_ENABLE_ASAN="${ASAN_FLAG:-OFF}" \
    -DNANOSIG_ENABLE_TSAN="${TSAN_FLAG:-OFF}" \
    -DNANOSIG_ENABLE_UBSAN="${UBSAN_FLAG:-OFF}" \
    -P "${SOURCE_DIR}/cmake/scripts/audit_config.cmake"

# --- A3 / A4: python-based audits (skipped if python3 unavailable) ---
if [ -z "${PYTHON3}" ]; then
    echo "[${KIND}] A3/A4 python3 not found, skipped"
else
    echo "[${KIND}] A3 header thread-safety"
    "${PYTHON3}" "${SOURCE_DIR}/cmake/scripts/audit_headers.py" \
        --kind "${KIND}" --source-dir "${SOURCE_DIR}"

    echo "[${KIND}] A4 encoding UTF-8 + mojibake"
    "${PYTHON3}" "${SOURCE_DIR}/cmake/scripts/audit_encoding.py" \
        --source-dir "${SOURCE_DIR}"
fi

echo "[${KIND}] sanitize-all audit OK"
