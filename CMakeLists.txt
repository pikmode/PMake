cmake_minimum_required(VERSION 3.20)
project(pmake VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Concurrent Test)

add_library(pmake_core STATIC
    # --- Ядро (core) ---
    src/core/cache.cpp
    src/core/graph.cpp
    src/core/builtins.cpp
    src/core/expression.cpp
    src/core/functions.cpp

    # --- Движок (разбит) ---
    src/core/engine.cpp
    src/core/engine_scheduler.cpp
    src/core/engine_utils.cpp

    # --- Узел (разбит) ---
    src/core/node.cpp
    src/core/node_builder.cpp

    # --- Состояние (разбито) ---
    src/core/state.cpp
    src/core/ast_interpreter.cpp
    src/core/expand_vars.cpp

    # --- Зависимости ---
    src/deps/dep_parser.cpp
    src/deps/deps_log.cpp

    # --- Исполнитель ---
    src/executor/runner.cpp

    # --- Парсер (оставлен без изменений) ---
    src/parser/lexer.cpp
    src/parser/parser.cpp
    src/parser/parser_blocks.cpp
    src/parser/parser_control.cpp
    src/parser/parser_dispatch.cpp
    src/parser/parser_eval.cpp
    src/parser/parser_expr.cpp
    src/parser/parser_functions.cpp
    src/parser/parser_helpers.cpp
    src/parser/parser_parse.cpp
    src/parser/parser_rules.cpp
    src/parser/parser_utils.cpp

    # --- Пользовательский интерфейс ---
    src/ui/console_ui.cpp

    # --- Утилиты ---
    src/util/filesystem.cpp
    src/util/logging.cpp
    src/util/string_utils.cpp
)

target_link_libraries(pmake_core
    Qt6::Core
    Qt6::Concurrent
)

target_include_directories(pmake_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(${PROJECT_NAME}
    src/main.cpp
)

qt_add_resources(${PROJECT_NAME} PMake_RESOURCES
    PREFIX "/"
    FILES
        Licenses/PPL_V1.txt
        version.txt
)

target_link_libraries(${PROJECT_NAME}
    pmake_core
    Qt6::Core
    Qt6::Concurrent
)

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)