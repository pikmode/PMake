
cmake_minimum_required(VERSION 3.25)
project(pmake VERSION 1.3.2 LANGUAGES CXX)


# ============================================================
# C++23 
# ============================================================
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================
# IDE: compile_commands.json (VS Code + clangd)
# ============================================================
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ============================================================
# Default Release
# ============================================================
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ============================================================
# Options
# ============================================================
option(PMAKE_STATIC "Static link (-static, no DLL)" ON)
option(PMAKE_STRIP  "Strip symbols in Release"      ON)
option(PMAKE_LTO    "Parallel LTO (-flto=auto)"     ON)

# native = GCC сам определит:
#   Core 2 Duo E6600 → core2  (SSSE3)
#   Core 2 Duo E8400 → penryn (SSE4.1)
#   i5-14400         → raptorlake (AVX2, AVX-512, BMI2, FMA)
set(PMAKE_MARCH "native" CACHE STRING
    "CPU march: native | x86-64-v3 | x86-64-v2 | core2 | penryn")

# ============================================================
# Warnings
# ============================================================
add_compile_options(-Wall -Wextra -Wpedantic)

# ============================================================
# Sources — как в build.pmake:
#   PMAKE_CORE_SOURCES → libpmake_core.a (35 .cpp)
#   PMAKE_EXE_SOURCES  → PMake.exe (main.cpp + lib)
# ============================================================
set(PMAKE_CORE_SOURCES
    src/ui/console_ui.cpp
    src/util/filesystem.cpp
    src/util/hash.cpp
    src/util/logging.cpp
    src/util/string_utils.cpp
    src/deps/deps_log.cpp
    src/deps/dep_parser.cpp
    src/executor/runner.cpp
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
    src/parser/ninja-parser/lexer.cpp
    src/parser/ninja-parser/parser.cpp
)

set(PMAKE_EXE_SOURCES
    src/main.cpp
)

# ============================================================
# Флаги компиляции — из build.pmake FLAGS
# ============================================================
set(PMAKE_COMMON_FLAGS
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:Release>:-DNDEBUG>
    $<$<CONFIG:Release>:-march=${PMAKE_MARCH}>
    $<$<CONFIG:Release>:-mtune=native>
    $<$<CONFIG:Release>:-ffunction-sections>
    $<$<CONFIG:Release>:-fdata-sections>
    $<$<CONFIG:Release>:-fomit-frame-pointer>
    $<$<CONFIG:Release>:-fno-plt>
    $<$<CONFIG:Release>:-fno-semantic-interposition>
    $<$<CONFIG:Release>:-pipe>
    $<$<CONFIG:Release>:-fno-ident>
)

if(PMAKE_LTO)
    list(APPEND PMAKE_COMMON_FLAGS
        $<$<CONFIG:Release>:-flto=auto>)
endif()

# ============================================================
# 1. libpmake_core.a — static library (35 .cpp)
# ============================================================
add_library(pmake_core STATIC ${PMAKE_CORE_SOURCES})
target_include_directories(pmake_core PUBLIC src)
target_compile_options(pmake_core PRIVATE ${PMAKE_COMMON_FLAGS})

if(PMAKE_LTO)
    set_target_properties(pmake_core PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

# ============================================================
# 2. PMake.exe — main.cpp + libpmake_core.a
# ============================================================
add_executable(pmake ${PMAKE_EXE_SOURCES})
target_include_directories(pmake PRIVATE src)
target_link_libraries(pmake PRIVATE pmake_core)
target_compile_options(pmake PRIVATE ${PMAKE_COMMON_FLAGS})

# ============================================================
# LTO на финальной линковке
# ============================================================
if(PMAKE_LTO)
    target_link_options(pmake PRIVATE $<$<CONFIG:Release>:-flto=auto>)
endif()

# ============================================================
# Static link + strip + GC (только на .exe, не на .a)
# ============================================================
if(PMAKE_STATIC)
    target_link_options(pmake PRIVATE
        $<$<CONFIG:Release>:-static>
        $<$<CONFIG:Release>:-static-libgcc>
        $<$<CONFIG:Release>:-static-libstdc++>)
endif()

if(PMAKE_STRIP)
    target_link_options(pmake PRIVATE $<$<CONFIG:Release>:-s>)
endif()

target_link_options(pmake PRIVATE
    $<$<CONFIG:Release>:-Wl,--gc-sections>
    $<$<CONFIG:Release>:-Wl,-O2>
    $<$<CONFIG:Release>:-Wl,--as-needed>
    $<$<CONFIG:Release>:-Wl,--no-insert-timestamp>
    $<$<CONFIG:Release>:-Wl,--build-id=none>)

# MinGW: <expected>/<print>/<format>
if(MINGW)
    target_link_libraries(pmake PRIVATE -lstdc++exp)
endif()

# ============================================================
# Windows system libraries
# ============================================================
if(WIN32)
    target_link_libraries(pmake PRIVATE
        mpr userenv kernel32 user32 gdi32 winspool
        shell32 ole32 oleaut32 uuid comdlg32 advapi32)
endif()

# ============================================================
# Debug: ASan/UBSan (не-MSVC)
# ============================================================
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT MSVC)
    target_compile_options(pmake_core PRIVATE
        -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_compile_options(pmake PRIVATE
        -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(pmake PRIVATE -fsanitize=address,undefined)
endif()

# ============================================================
# Info
# ============================================================
message(STATUS "")
message(STATUS "PMake (STL, MinGW) — lib + exe, как build.pmake")
message(STATUS "  CXX_STANDARD = ${CMAKE_CXX_STANDARD}")
message(STATUS "  BUILD_TYPE   = ${CMAKE_BUILD_TYPE}")
message(STATUS "  MARCH        = ${PMAKE_MARCH}")
message(STATUS "  LTO          = ${PMAKE_LTO}")
message(STATUS "  STATIC       = ${PMAKE_STATIC}")
message(STATUS "  STRIP        = ${PMAKE_STRIP}")
message(STATUS "")