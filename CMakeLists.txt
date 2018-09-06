cmake_minimum_required(VERSION 3.10)
project(newb_measurements C CXX)
set (CMAKE_C_COMPILER clang)
set (CMAKE_CXX_COMPILER clang++)
set (CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
set (CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_STATIC_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")

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
    set(CMAKE_CXX_FLAGS                   "-std=c++17 -Wextra -Wall -pedantic ${EXTRA_FLAGS}")
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
    set(CAF_ROOT_DIR "~/Git/actor-framework/build/")
endif()
find_package(CAF COMPONENTS core io REQUIRED)
if (CAF_FOUND)
    include_directories(${CAF_INCLUDE_DIRS})
endif ()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

find_package(BENCHMARK REQUIRED)
if (BENCHMARK_FOUND)
    include_directories(${BENCHMARK_INCLUDE_DIRS})
endif ()

if (NOT MOZQUIC_ROOT_DIR)
  set(MOZQUIC_ROOT_DIR "/home/jakob/CLionProjects/mozquic/")
  #set(MOZQUIC_ROOT_DIR "/home/boss/Desktop/mozilla/mozquic/")
endif ()
find_package(Mozquic)
if (MOZQUIC_FOUND)
    #include_directories(${MOZQUIC_INCLUDE_DIRS})
    set(INCLUDE_DIRS ${INCLUDE_DIRS} ${MOZQUIC_INCLUDE_DIRS})
else ()
    message(FATAL_ERROR "could not find Mozquic")
endif ()

find_package(NSS)
if (NSS_FOUND)
    set(INCLUDE_DIRS ${INCLUDE_DIRS} ${NSS_INCLUDE_DIRS})
else ()
    message(FATAL_ERROR "could not find NSS")
endif()

find_package(NSPR)
if (NSPR_FOUND)
    set(INCLUDE_DIRS ${INCLUDE_DIRS} ${NSPR_INCLUDE_DIRS})
else ()
    message(FATAL_ERROR "could not find NSPR")
endif()

include_directories(${INCLUDE_DIRS})
add_custom_target(newb_measurements)

macro(add folder name)
    add_executable(${name} ${folder}/${name}.cpp ${ARGN})
    target_link_libraries(${name}
            ${CMAKE_DL_LIBS}
            ${CAF_LIBRARY_IO}
            ${CAF_LIBRARY_CORE}
            ${BENCHMARK_LIBRARIES})
    add_dependencies(${name} newb_measurements)
    target_link_libraries(${name} Threads::Threads)
endmacro()

add(src one_raw_udp)
add(src one_raw_tcp)
add(src one_basp_udp)
add(src one_basp_tcp)
add(src layers)
add(src pingpong)
add(src pingpong_tcp)
add(src pp_tcp_pure)

add_executable(one_raw_quic
        src/quic_newb_chat_test.cpp
  src/newb_quic.cpp
  caf/policy/newb_quic.hpp
)
target_link_libraries(
        one_raw_quic
        ${CAF_LIBRARY_IO}
        ${CAF_LIBRARY_CORE}
        ${BENCHMARK_LIBRARIES}
        ${NSS_LIBRARIES}
        ${NSPR_LIBRARIES}
        ${MOZQUIC_LIBRARIES})
add_dependencies(one_raw_quic newb_measurements)
target_link_libraries(one_raw_quic Threads::Threads)

