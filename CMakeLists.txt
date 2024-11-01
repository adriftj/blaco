cmake_minimum_required(VERSION 3.20.0)

project(blaco VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_BLACO_TESTS "Build blaco tests" ON)

string(TOLOWER ${CMAKE_SYSTEM_NAME} osname)

# Optional IPO/LTO.
#include(CheckIPOSupported)
#check_ipo_supported(RESULT is_support_IPO OUTPUT output_support_IPO)
#if(is_support_IPO)
#    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
#else()
#    message(WARNING "IPO is not supported: ${output_support_IPO}")
#endif()

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "11.3.0")
        message(FATAL_ERROR "Insufficient gcc version, requires gcc 11.3 or above")
    endif()

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")

    if (${CMAKE_BUILD_TYPE} MATCHES Debug)
        message("Enable -fsanitize for gcc")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=undefined,address,leak -fno-omit-frame-pointer")
    else()
        message(${CMAKE_BUILD_TYPE} "is not Debug")
        message("Disable -fsanitize for gcc")
    endif()

    if (${CMAKE_BUILD_TYPE} MATCHES Release)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native")
    endif()

    message("CMAKE_CXX_FLAGS = ${CMAKE_CXX_FLAGS}")
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "14.0.0")
        message(FATAL_ERROR "Insufficient clang version, requires clang 14.0 or above")
    endif()

    if (FORCE_TO_USE_LIBCXX)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -stdlib=libc++ -lc++abi")    
    endif()

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsized-deallocation -std=c++20")

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")

    if (${CMAKE_BUILD_TYPE} MATCHES Debug)
        message("Enable -fsanitize for clang")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=undefined,address")
    else()
        message(${CMAKE_BUILD_TYPE})
        message("Disable -fsanitize for clang")
    endif()

    if (${CMAKE_BUILD_TYPE} MATCHES Release)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native")
    endif()

    message("CMAKE_CXX_FLAGS = ${CMAKE_CXX_FLAGS}")
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
else()
    message(FATAL_ERROR "[g++/clang/MSVC] is required, but the compiler id is ${CMAKE_CXX_COMPILER_ID}.")
endif()

add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

if ("${osname}" STREQUAL "windows")
    set(OSLIBS wsock32 ws2_32 shlwapi)
else()
    set(OSLIBS -luring)
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${osname})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${osname})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${osname})

set(BLACO_INC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

include_directories(${BLACO_INC_DIR})

add_subdirectory(./src)

if(BUILD_BLACO_TESTS)
    add_subdirectory(./test)
endif()
