cmake_minimum_required(VERSION 3.20)
project(nanosig_static_assert_check LANGUAGES CXX)

get_filename_component(NANOSIG_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_REQUIRED_INCLUDES "${NANOSIG_SOURCE_DIR}/include")
include(CheckCXXSourceCompiles)

# Recheck on every invocation so a cached result cannot hide a regression.
unset(NANOSIG_TRUE_STATIC_ASSERT_COMPILES CACHE)
check_cxx_source_compiles("
    #include <nanosig/nanosig_types.h>
    NS_STATIC_ASSERT(true, \"true assertion must compile\");
" NANOSIG_TRUE_STATIC_ASSERT_COMPILES)
if(NOT NANOSIG_TRUE_STATIC_ASSERT_COMPILES)
    message(FATAL_ERROR "The C++11 static assertion positive control failed")
endif()

unset(NANOSIG_FALSE_STATIC_ASSERT_COMPILES CACHE)
check_cxx_source_compiles("
    #include <nanosig/nanosig_types.h>
    NS_STATIC_ASSERT(false, \"false assertion must fail\");
" NANOSIG_FALSE_STATIC_ASSERT_COMPILES)
if(NANOSIG_FALSE_STATIC_ASSERT_COMPILES)
    message(FATAL_ERROR "NS_STATIC_ASSERT silently accepted a false C++11 assertion")
endif()
