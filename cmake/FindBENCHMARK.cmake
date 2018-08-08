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

set(BENCHMARK_ROOT_DIR "" CACHE PATH "Folder containing BENCHMARK")

find_path(BENCHMARK_INCLUDE_DIR "BENCHMARK/BENCHMARK.h"
  PATHS ${BENCHMARK_ROOT_DIR}
  PATH_SUFFIXES include
  NO_DEFAULT_PATH)
find_path(BENCHMARK_INCLUDE_DIR "BENCHMARK/BENCHMARK.h")

find_library(BENCHMARK_LIBRARY NAMES "BENCHMARK"
  PATHS ${BENCHMARK_ROOT_DIR}
  PATH_SUFFIXES lib lib64
  NO_DEFAULT_PATH)
find_library(BENCHMARK_LIBRARY NAMES "BENCHMARK")

include(FindPackageHandleStandardArgs)
# handle the QUIETLY and REQUIRED arguments and set BENCHMARK_FOUND to TRUE
# if all listed variables are TRUE
find_package_handle_standard_args(BENCHMARK FOUND_VAR BENCHMARK_FOUND
  REQUIRED_VARS BENCHMARK_LIBRARY
  BENCHMARK_INCLUDE_DIR)

if(BENCHMARK_FOUND)
  set(BENCHMARK_LIBRARIES ${BENCHMARK_LIBRARY})
  set(BENCHMARK_INCLUDE_DIRS ${BENCHMARK_INCLUDE_DIR})
endif()

mark_as_advanced(BENCHMARK_INCLUDE_DIR BENCHMARK_LIBRARY)
