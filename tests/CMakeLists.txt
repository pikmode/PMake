cmake_minimum_required(VERSION 3.20)
project(pmake_tests)

# Подключаем Qt6 Test (если ещё не найден)
find_package(Qt6 REQUIRED COMPONENTS Test)

# Список исходников тестов
set(TEST_SOURCES
#    test_lexer.cpp
     test_parser.cpp
#    test_expression.cpp
#    test_integration.cpp
)

# Для каждого теста создаём отдельный исполняемый файл
foreach(test_src ${TEST_SOURCES})
    get_filename_component(test_name ${test_src} NAME_WE)

    add_executable(${test_name} ${test_src})

    # Линкуем с основной библиотекой и Qt6::Test
    target_link_libraries(${test_name}
        PRIVATE
            pmake_core
            Qt6::Test
    )

    # Добавляем путь к заголовкам src/
    target_include_directories(${test_name}
        PRIVATE
            ${CMAKE_SOURCE_DIR}/src
    )

    # Определяем макрос TEST_DATA_DIR для доступа к тестовым данным
    target_compile_definitions(${test_name}
        PRIVATE
            TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )

    # Регистрируем тест в CTest
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()