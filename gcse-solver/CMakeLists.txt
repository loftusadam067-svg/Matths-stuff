# GCSE math solver — standalone CMake project.
#
# llama.cpp source is expected at the project root (one level up). When this
# CMakeLists.txt is configured standalone, it pulls in the parent llama.cpp
# build as a subdirectory and exposes the `llama` target.

cmake_minimum_required(VERSION 3.20)
project(gcse_math_solver CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

include(FetchContent)

# ---------------------------------------------------------------------------
# llama.cpp — already cloned into the parent directory of this CMakeLists.
# ---------------------------------------------------------------------------
get_filename_component(LLAMA_CPP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

if(NOT TARGET llama)
    if(EXISTS "${LLAMA_CPP_ROOT}/CMakeLists.txt" AND EXISTS "${LLAMA_CPP_ROOT}/include/llama.h")
        message(STATUS "gcse-solver: using llama.cpp at ${LLAMA_CPP_ROOT}")
        set(LLAMA_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
        set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(LLAMA_BUILD_SERVER   OFF CACHE BOOL "" FORCE)
        set(LLAMA_BUILD_TOOLS    OFF CACHE BOOL "" FORCE)
        add_subdirectory(${LLAMA_CPP_ROOT} ${CMAKE_BINARY_DIR}/llama-build EXCLUDE_FROM_ALL)
    else()
        message(FATAL_ERROR
            "gcse-solver: llama.cpp not found at ${LLAMA_CPP_ROOT}. "
            "This project expects llama.cpp to be cloned at the parent directory.")
    endif()
endif()

# ---------------------------------------------------------------------------
# ExprTk (header-only).
# ---------------------------------------------------------------------------
FetchContent_Declare(
    exprtk
    GIT_REPOSITORY https://github.com/ArashPartow/exprtk.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(exprtk)

# ---------------------------------------------------------------------------
# Lua 5.4 — system package preferred, fallback to FetchContent if absent.
# ---------------------------------------------------------------------------
find_package(Lua 5.4 QUIET)
if(NOT LUA_FOUND)
    message(STATUS "gcse-solver: system Lua 5.4 not found, fetching upstream")
    FetchContent_Declare(
        lua
        URL https://www.lua.org/ftp/lua-5.4.7.tar.gz
        URL_HASH SHA256=9fbf5e28ef86c69858f6d3d34eccc32e911c1a28b4120ff3e84aaa70cfbf1e30
    )
    FetchContent_MakeAvailable(lua)
    file(GLOB LUA_SOURCES "${lua_SOURCE_DIR}/src/*.c")
    list(REMOVE_ITEM LUA_SOURCES "${lua_SOURCE_DIR}/src/lua.c" "${lua_SOURCE_DIR}/src/luac.c")
    add_library(gcse_lua STATIC ${LUA_SOURCES})
    target_include_directories(gcse_lua PUBLIC ${lua_SOURCE_DIR}/src)
    target_compile_definitions(gcse_lua PUBLIC LUA_USE_POSIX)
    set(GCSE_LUA_TARGET gcse_lua)
else()
    add_library(gcse_lua INTERFACE)
    target_include_directories(gcse_lua INTERFACE ${LUA_INCLUDE_DIR})
    target_link_libraries(gcse_lua INTERFACE ${LUA_LIBRARIES})
    set(GCSE_LUA_TARGET gcse_lua)
endif()

# ---------------------------------------------------------------------------
# nlohmann/json (header-only, currently unused at runtime but reserved for
# future structured I/O — kept here per project spec).
# ---------------------------------------------------------------------------
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
)
FetchContent_MakeAvailable(json)

# ---------------------------------------------------------------------------
# Solver binary.
# ---------------------------------------------------------------------------
add_executable(solver
    src/main.cpp
    src/llm_engine.cpp
    src/executor.cpp
)

target_include_directories(solver PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${exprtk_SOURCE_DIR}
    ${LLAMA_CPP_ROOT}/include
)

target_link_libraries(solver PRIVATE
    llama
    ${GCSE_LUA_TARGET}
    nlohmann_json::nlohmann_json
)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(solver PRIVATE -O3 -Wall -Wextra -Wpedantic)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        target_compile_options(solver PRIVATE -march=armv8-a -mtune=cortex-a53 -ffast-math)
    endif()
else()
    target_compile_options(solver PRIVATE -g -O0 -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# Tests (Doctest, fetched on demand).
# ---------------------------------------------------------------------------
option(GCSE_BUILD_TESTS "Build solver unit tests" ON)
if(GCSE_BUILD_TESTS)
    FetchContent_Declare(
        doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG        v2.4.11
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(doctest)

    add_executable(test_executor
        tests/test_executor.cpp
        src/executor.cpp
    )
    target_include_directories(test_executor PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${exprtk_SOURCE_DIR}
        ${doctest_SOURCE_DIR}/doctest
    )
    target_link_libraries(test_executor PRIVATE
        ${GCSE_LUA_TARGET}
    )
    target_compile_options(test_executor PRIVATE -g -O0 -Wall -Wextra -Wpedantic)

    enable_testing()
    add_test(NAME executor_tests COMMAND test_executor)
endif()
