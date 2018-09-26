# FindBENCHMARK.cmake
# - Try to find BENCHMARK
#
# The following variables are optionally searched for defaults
#  BENCHMARK_ROOT_DIR:  Base directory where all BENCHMARK components are found
#
# Once done this will define
#  BENCHMARK_FOUND - System has BENCHMARK
#  BENCHMARK_INCLUDE_DIRS - The BENCHMARK include directories
#  BENCHMARK_LIBRARIES - The libraries needed to use BENCHMARK

find_path(BENCHMARK_INCLUDE_DIR "benchmark/benchmark.h"
  HINTS
    ${BENCHMARK_ROOT_DIR}/include
  PATHS
    ${BENCHMARK_ROOT_DIR}
  PATH_SUFFIXES
  NO_DEFAULT_PATH)
find_path(BENCHMARK_INCLUDE_DIR "benchmark/benchmark.h")

find_library(BENCHMARK_LIBRARY
  NAMES 
    "benchmark"
  HINTS
    ${BENCHMARK_ROOT_DIR}/build/
    ${BENCHMARK_ROOT_DIR}/build/src/
  PATHS
    ${BENCHMARK_ROOT_DIR}
  PATH_SUFFIXES
    lib
    lib64
  )
  
find_library(BENCHMARK_LIBRARY NAMES "benchmark")

include(FindPackageHandleStandardArgs)
# handle the QUIETLY and REQUIRED arguments and set BENCHMARK_FOUND to TRUE
# if all listed variables are TRUE
find_package_handle_standard_args(BENCHMARK
  FOUND_VAR
    BENCHMARK_FOUND
  REQUIRED_VARS
    BENCHMARK_LIBRARY
    BENCHMARK_INCLUDE_DIR)

if(BENCHMARK_FOUND)
  set(BENCHMARK_LIBRARIES ${BENCHMARK_LIBRARY})
  set(BENCHMARK_INCLUDE_DIRS ${BENCHMARK_INCLUDE_DIR})
endif()

mark_as_advanced(BENCHMARK_INCLUDE_DIR BENCHMARK_LIBRARY)
