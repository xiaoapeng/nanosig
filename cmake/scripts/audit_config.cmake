# audit_config.cmake — verify that the active sanitizer preset is consistent.
#
# Invoked by `sanitize-all` per sanitizer sub-target with NANOSIG_AUDIT_KIND
# identifying which sanitizer this pipeline is auditing (asan / ubsan / tsan).
# Exits non-zero (FATAL_ERROR) on any inconsistency so the host build fails.

if(NOT DEFINED NANOSIG_AUDIT_KIND)
    message(FATAL_ERROR "[audit_config] NANOSIG_AUDIT_KIND not set")
endif()

set(_audit_kind "${NANOSIG_AUDIT_KIND}")
set(_asan "${NANOSIG_ENABLE_ASAN}")
set(_tsan "${NANOSIG_ENABLE_TSAN}")
set(_ubsan "${NANOSIG_ENABLE_UBSAN}")

message(STATUS "[audit_config] (${_audit_kind}) ASAN=${_asan} TSAN=${_tsan} UBSAN=${_ubsan}")

if(_asan AND _tsan)
    message(FATAL_ERROR "[audit_config] (${_audit_kind}) ASAN and TSAN cannot both be enabled")
endif()

set(_expected_kind "")
if(_asan)
    set(_expected_kind "asan")
elseif(_tsan)
    set(_expected_kind "tsan")
elseif(_ubsan)
    set(_expected_kind "ubsan")
else()
    message(FATAL_ERROR "[audit_config] (${_audit_kind}) no sanitizer is enabled")
endif()

if(NOT _expected_kind STREQUAL _audit_kind)
    message(FATAL_ERROR
        "[audit_config] audit kind '${_audit_kind}' does not match enabled sanitizer '${_expected_kind}'"
    )
endif()

if(_expected_kind STREQUAL "tsan" AND WIN32)
    message(FATAL_ERROR "[audit_config] (${_audit_kind}) TSAN is not supported on Windows")
endif()

if(DEFINED ENV{NANOSIG_BUILD_TESTS} AND "$ENV{NANOSIG_BUILD_TESTS}" STREQUAL "OFF")
    message(STATUS "[audit_config] (${_audit_kind}) NANOSIG_BUILD_TESTS=OFF; A1 api-contract anchor disabled")
endif()

message(STATUS "[audit_config] (${_audit_kind}) configuration OK")
