cmake_minimum_required(VERSION 2.8.12)
project(newb_measurements C CXX)
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

set (CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# silence policy CMP0042 warning by enabling RPATH explicitly
if(APPLE AND NOT DEFINED CMAKE_MACOSX_RPATH)
  set(CMAKE_MACOSX_RPATH true)
endif()

# prohibit in-source builds
if ("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
    message(FATAL_ERROR "In-source builds are not allowed. Please use "
                        "./configure to choose a build directory and "
                        "initialize the build configuration.")
endif ()

if(CAF_ENABLE_ADDRESS_SANITIZER)
  # check whether address sanitizer is available
  set(CXXFLAGS_BACKUP "${CMAKE_CXX_FLAGS}")
  set(CMAKE_CXX_FLAGS "-fsanitize=address -fno-omit-frame-pointer")
  try_run(ProgramResult
          CompilationSucceeded
          "${CMAKE_CURRENT_BINARY_DIR}"
          "${CMAKE_CURRENT_SOURCE_DIR}/cmake/get_compiler_version.cpp")
  if(NOT CompilationSucceeded)
    message(WARNING "Address Sanitizer is not available on selected compiler")
  else()
    message(STATUS "Enable Address Sanitizer")
    set(EXTRA_FLAGS "${EXTRA_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
  endif()
  # restore CXX flags
  set(CMAKE_CXX_FLAGS "${CXXFLAGS_BACKUP}")
endif(CAF_ENABLE_ADDRESS_SANITIZER)

# check if the user provided CXXFLAGS, set defaults otherwise
if(NOT CMAKE_CXX_FLAGS)
  set(CMAKE_CXX_FLAGS                   "-std=c++11 -Wextra -Wall -pedantic ${EXTRA_FLAGS}")
endif()
if(NOT CMAKE_CXX_FLAGS_DEBUG)
  set(CMAKE_CXX_FLAGS_DEBUG             "-O0 -g")
endif()
if(NOT CMAKE_CXX_FLAGS_MINSIZEREL)
  set(CMAKE_CXX_FLAGS_MINSIZEREL        "-Os")
endif()
if(NOT CMAKE_CXX_FLAGS_RELEASE)
  set(CMAKE_CXX_FLAGS_RELEASE           "-O3 -DNDEBUG")
endif()
if(NOT CMAKE_CXX_FLAGS_RELWITHDEBINFO)
  set(CMAKE_CXX_FLAGS_RELWITHDEBINFO    "-O2 -g")
endif()

# set build default build type to RelWithDebInfo if not set
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

string(TOUPPER "${CMAKE_BUILD_TYPE}" UPPER_BUILD_TYPE)
set(ALL_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${UPPER_BUILD_TYPE}}")

message(STATUS "CXX_FLAGS: ${ALL_CXX_FLAGS}")

if(NOT CAF_ROOT_DIR)
  set(CAF_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/actor-framework/build/")
endif()
find_package(CAF COMPONENTS core io REQUIRED)
if (CAF_FOUND)
  include_directories(${CAF_INCLUDE_DIRS})
endif ()

if (NOT MOZQUIC_ROOT_DIR)
  set(MOZQUIC_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/mozquic/)
endif ()
  find_package(Mozquic)
if (MOZQUIC_FOUND)
  include_directories(${MOZQUIC_INCLUDE_DIRS})
else ()
  message(FATAL_ERROR "Could not find Mozquic!")
endif ()
#### NSS ####
find_package(NSS)
if (NSS_FOUND)
  include_directories(${NSS_INCLUDE_DIRS})
  message(STATUS "NSS_INCLUDE_DIRS: ${NSS_INCLUDE_DIRS}")
else ()
  message(FATAL_ERROR "Could not find NSS.")
endif ()

#### NSPR ####
find_package(NSPR)
if(NSPR_FOUND)
  include_directories(${NSPR_INCLUDE_DIRS})
  message(STATUS "NSPR_INCLUDE_DIRS: ${NSPR_INCLUDE_DIRS}")
else()
  message(FATAL_ERROR "could not find NSPR.")
endif()

find_package(Threads)

if(NOT BENCHMARK_ROOT_DIR)
  set(BENCHMARK_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/benchmark/")
endif()
find_package(BENCHMARK REQUIRED)
if (BENCHMARK_FOUND)
  include_directories(${BENCHMARK_INCLUDE_DIRS})
endif ()

include_directories(caf)
include_directories(caf/detail)
include_directories(caf/policy)

add_custom_target(newb_measurements)

macro(add folder name)
  add_executable(${name} ${folder}/${name}.cpp ${ARGN})
  target_link_libraries(${name}
                        ${CMAKE_DL_LIBS}
                        ${CAF_LIBRARY_IO}
                        ${CAF_LIBRARY_CORE}
                        ${BENCHMARK_LIBRARIES})
  add_dependencies(${name} newb_measurements)
  target_link_libraries(${name} ${CMAKE_THREAD_LIBS_INIT})
endmacro()

add(src layers)
add(src pingpong_udp)
add(src pingpong_tcp)
add(src pp_tcp_pure)

add_executable(pingpong_quic
  src/pingpong_quic.cpp
  src/quic_acceptor.cpp
  src/quic_transport.cpp
  src/mozquic_CB.cpp
)

target_link_libraries(
  pingpong_quic
  ${CMAKE_DL_LIBS}
  ${CAF_LIBRARY_IO}
  ${CAF_LIBRARY_CORE}
  ${BENCHMARK_LIBRARIES}
  ${NSS_LIBRARIES}
  ${NSPR_LIBRARIES}
  ${MOZQUIC_LIBRARIES}
  -lstdc++
)

add_dependencies(pingpong_quic newb_measurements)
