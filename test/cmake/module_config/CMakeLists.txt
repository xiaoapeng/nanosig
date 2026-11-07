cmake_minimum_required(VERSION 3.20)
project(nanosig_module_config_check LANGUAGES C CXX)

get_filename_component(NANOSIG_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(NANOSIG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NANOSIG_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(NANOSIG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(NANOSIG_BUILD_API_CHECKS ON CACHE BOOL "" FORCE)
add_subdirectory("${NANOSIG_SOURCE_DIR}" nanosig)

add_executable(module_config_check main.c)
target_link_libraries(module_config_check PRIVATE nanosig::nanosig)
if(CHECK_CUSTOM_CONFIG)
    target_compile_definitions(module_config_check PRIVATE CHECK_CUSTOM_CONFIG)
endif()
if(CHECK_ZERO_CONFIG)
    target_compile_definitions(module_config_check PRIVATE CHECK_ZERO_CONFIG)
endif()
if(CHECK_ESCAPED_CONFIG)
    target_compile_definitions(module_config_check PRIVATE CHECK_ESCAPED_CONFIG)
endif()
enable_testing()
add_test(NAME module_config_check COMMAND module_config_check)

# Compile the same consumer contract through both syntax-only entry points.
configure_file(public_config.c.in public_config.c @ONLY)
configure_file(public_config.c.in public_config.cpp @ONLY)
nanosig_add_api_compile_check(module_config_api_c
    "${CMAKE_CURRENT_BINARY_DIR}/public_config.c")
nanosig_add_api_compile_check(module_config_api_cpp
    "${CMAKE_CURRENT_BINARY_DIR}/public_config.cpp" LANGUAGE CXX)
