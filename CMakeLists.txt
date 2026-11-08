cmake_minimum_required(VERSION 3.25)
project(pmake_stl LANGUAGES CXX)

# ============================================================
# C++23
# ============================================================
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================
# Compiler warnings
# ============================================================
if(MSVC)
    add_compile_options(/W4 /permissive- /Zc:__cplusplus /utf-8)
    add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ============================================================
# Sources (STL)
# ============================================================
set(PMAKE_SOURCES
    # main
    src/main.cpp

    # ui
    src/ui/console_ui.cpp

    # util
    src/util/filesystem.cpp
    src/util/hash.cpp
    src/util/logging.cpp
    src/util/string_utils.cpp

    # deps
    src/deps/deps_log.cpp
    src/deps/dep_parser.cpp

    # executor
    src/executor/runner.cpp

    # core
    src/core/state.cpp
    src/core/expand_vars.cpp
    src/core/ast_interpreter.cpp
    src/core/builtins.cpp
    src/core/expression.cpp
    src/core/functions.cpp
    src/core/graph.cpp
    src/core/node.cpp
    src/core/node_builder.cpp
    src/core/cache.cpp
    src/core/engine.cpp
    src/core/engine_scheduler.cpp
    src/core/engine_utils.cpp

    # parser (pmake DSL)
    src/parser/lexer.cpp
    src/parser/parser.cpp
    src/parser/parser_parse.cpp
    src/parser/parser_helpers.cpp
    src/parser/parser_blocks.cpp
    src/parser/parser_control.cpp
    src/parser/parser_dispatch.cpp
    src/parser/parser_eval.cpp
    src/parser/parser_expr.cpp
    src/parser/parser_functions.cpp
    src/parser/parser_rules.cpp
    src/parser/parser_utils.cpp

    # parser (ninja)
    src/parser/ninja-parser/lexer.cpp
    src/parser/ninja-parser/parser.cpp
)

# ============================================================
# Executable
# ============================================================
add_executable(pmake ${PMAKE_SOURCES})

target_include_directories(pmake PRIVATE src)

# ============================================================
# Release optimizations
# ============================================================
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(MSVC)
        target_compile_options(pmake PRIVATE /O2 /DNDEBUG)
    else()
        target_compile_options(pmake PRIVATE -O3 -DNDEBUG)
    endif()
endif()

# ============================================================
# Debug: sanitizers (GCC/Clang)
# ============================================================
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT MSVC)
    target_compile_options(pmake PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(pmake PRIVATE -fsanitize=address,undefined)
endif()

# ============================================================
# Windows: линковка системных библиотек
# ============================================================
if(WIN32)
    target_link_libraries(pmake PRIVATE
        mpr userenv kernel32 user32 gdi32 winspool
        shell32 ole32 oleaut32 uuid comdlg32 advapi32
    )
endif()

# ============================================================
# Info
# ============================================================
message(STATUS "")
message(STATUS "PMake (STL, no Qt)")
message(STATUS "  CXX_STANDARD = ${CMAKE_CXX_STANDARD}")
message(STATUS "  BUILD_TYPE   = ${CMAKE_BUILD_TYPE}")
message(STATUS "  GENERATOR    = ${CMAKE_GENERATOR}")
message(STATUS "  Sources      = ${CMAKE_CURRENT_SOURCE_DIR}/src")
message(STATUS "")