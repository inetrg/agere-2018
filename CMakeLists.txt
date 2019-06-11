cmake_minimum_required(VERSION 2.8.12)
project(newb_measurements C CXX)

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

# find necessary libraries
find_package(OpenSSL REQUIRED)
if (OPENSSL_FOUND)
  include_directories(${OPENSSL_INCLUDE_DIR})
else ()
  message(FATAL_ERROR "could not find OpenSSL")
endif ()

find_package(PTLS REQUIRED)
if (PTLS_FOUND)
  include_directories(${PTLS_INCLUDE_DIRS})
  message(STATUS "PTLS_INCLUDE_DIRS: ${PTLS_INCLUDE_DIRS}")
  message(STATUS "PTLS_LIBRARIES: ${PTLS_LIBRARIES}")
endif ()

if (NOT QUICLY_ROOT_DIR)
  set(QUICLY_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/quicly")
endif()
find_package(Quicly REQUIRED)
if (QUICLY_FOUND)
  include_directories(${QUICLY_INCLUDE_DIRS})
  message(STATUS "QUICLY_INCLUDE_DIRS: ${QUICLY_INCLUDE_DIRS}")
endif()

#set(THREADS_PREFER_PTHREAD_FLAG ON)
#find_package(Threads REQUIRED)

find_package(Threads)

if(NOT BENCHMARK_ROOT_DIR)
  set(BENCHMARK_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/benchmark/")
endif()
find_package(BENCHMARK REQUIRED)
if (BENCHMARK_FOUND)
  include_directories(${BENCHMARK_INCLUDE_DIRS})
endif ()

add_custom_target(newb_measurements)

macro(add folder name)
  add_executable(${name} ${folder}/${name}.cpp ${ARGN})
  target_link_libraries(${name}
                        ${CMAKE_DL_LIBS}
                        ${CAF_LIBRARY_IO}
                        ${CAF_LIBRARY_CORE}
                        ${BENCHMARK_LIBRARIES})
  add_dependencies(${name} newb_measurements)
  #target_link_libraries(${name} Threads::Threads)
  target_link_libraries(${name} ${CMAKE_THREAD_LIBS_INIT})
endmacro()

add(src one_raw_udp)
add(src one_raw_tcp)
add(src one_basp_udp)
add(src one_basp_tcp)
add(src layers)
add(src pingpong_udp)
add(src pingpong_tcp)
add(src pp_tcp_pure)
add(src tcp-big-data)

include_directories(caf)
include_directories(caf/detail)
include_directories(caf/policy)


add_executable(quic_big_data
               src/newb_quicly.cpp
               src/quicly_cb.cpp
               src/quic_big_data.cpp)

target_link_libraries(quic_big_data
                      ${CMAKE_DL_LIBS}
                      ${CAF_LIBRARY_IO}
                      ${CAF_LIBRARY_CORE}
                      ${BENCHMARK_LIBRARIES}
                      ${CMAKE_THREAD_LIBS_INIT}
                      ${QUICLY_LIBRARIES}
                      ${PTLS_LIBRARIES}
                      ${OPENSSL_LIBRARIES})


add_executable(pingpong_quic-1
    src/newb_quicly.cpp
    src/quicly_cb.cpp
    src/pingpong_quicly.1.cpp)

target_link_libraries(pingpong_quic-1
    ${CMAKE_DL_LIBS}
    ${CAF_LIBRARY_IO}
    ${CAF_LIBRARY_CORE}
    ${BENCHMARK_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
    ${QUICLY_LIBRARIES}
    ${PTLS_LIBRARIES}
    ${OPENSSL_LIBRARIES})



add_executable(pingpong_quic
               src/newb_quicly.cpp
               src/quicly_cb.cpp
               src/pingpong_quicly.cpp)

target_link_libraries(pingpong_quic
                      ${CMAKE_DL_LIBS}
                      ${CAF_LIBRARY_IO}
                      ${CAF_LIBRARY_CORE}
                      ${BENCHMARK_LIBRARIES}
                      ${CMAKE_THREAD_LIBS_INIT}
                      ${QUICLY_LIBRARIES}
                      ${PTLS_LIBRARIES}
                      ${OPENSSL_LIBRARIES})
