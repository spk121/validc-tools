cmake_minimum_required(VERSION 3.16)
project(validc-tools LANGUAGES C)

add_compile_options(-g3 -O0 -Wall -Wextra)

# ------------------------------------------------------------------
# 1. Static library: libvc
# ------------------------------------------------------------------
add_library(vc STATIC
    src/lib/bre.c
    src/lib/bre.h
)
target_include_directories(vc PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/lib>
    $<INSTALL_INTERFACE:include>
)
set_target_properties(vc PROPERTIES
    OUTPUT_NAME "vc"
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)

# ------------------------------------------------------------------
# 2. Real ed executable
# ------------------------------------------------------------------
add_executable(ed
    src/ed/ed.c
    src/ed/ed.h
)
target_link_libraries(ed PRIVATE vc)
target_include_directories(ed PRIVATE src/lib src/ed)
set_target_properties(ed PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# ------------------------------------------------------------------
# 3. All the standalone ed tests (each .c becomes its own binary)
# ------------------------------------------------------------------
enable_testing()

set(ED_TEST_SOURCES
    test/ed/test_address_range_ctest.c
    test/ed/test_address_validation_ctest.c
    test/ed/test_append_line_ctest.c
    test/ed/test_bre_ctest.c
    test/ed/test_dirty_flag_ctest.c
    test/ed/test_file_operations_ctest.c
    test/ed/test_free_editor_ctest.c
    test/ed/test_global_commands_ctest.c
    test/ed/test_line_operations_ctest.c
    test/ed/test_long_lines_ctest.c
    test/ed/test_parse_address_ctest.c
    test/ed/test_regex_address_ctest.c
    test/ed/test_regex_debug.c
    test/ed/test_simple_commands_ctest.c
    test/ed/test_substitute_ctest.c
    test/ed/test_undo_marks_ctest.c
    test/ed/test_verbose_errors_ctest.c
    test/ed/test_write_file_ctest.c
)

foreach(test_src ${ED_TEST_SOURCES})
    get_filename_component(test_name ${test_src} NAME_WE)  # e.g. test_substitute_ctest
    add_executable(${test_name} ${test_src}
        src/ed/ed.c     # real implementation
        src/ed/ed.h
    )
    target_compile_definitions(${test_name} PRIVATE LED_TEST)
    target_link_libraries(${test_name} PRIVATE vc)
    target_include_directories(${test_name} PRIVATE src/lib src/ed)
    set_target_properties(${test_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test
    )

    # Register with CTest (TAP compatible)
    add_test(NAME ${test_name}
        COMMAND ${test_name}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/test
    )
    set_tests_properties(${test_name} PROPERTIES
        FAIL_REGULAR_EXPRESSION "not ok"
        PASS_REGULAR_EXPRESSION "1\\.\\.\\d+"  # any TAP plan is fine
    )
endforeach()

# ------------------------------------------------------------------
# 4. Compiled bre_test
# ------------------------------------------------------------------
add_executable(bre_test
    test/lib/bre_test.c
)
target_link_libraries(bre_test PRIVATE vc)
target_include_directories(bre_test PRIVATE src/lib)
set_target_properties(bre_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test
)
add_test(NAME bre_test COMMAND bre_test WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/test)
set_tests_properties(bre_test PROPERTIES
    FAIL_REGULAR_EXPRESSION "not ok"
)

# cmake/run_ed_script_test.cmake
# Called by CTest for each scripted test

if(NOT EXISTS ${TEST_DIR}/${test_name}.ed)
    message(FATAL_ERROR "Missing .ed script: ${TEST_DIR}/${test_name}.ed")
endif()

# Input file is optional — if missing, ed reads from stdin (which we'll make empty)
if(EXISTS ${TEST_DIR}/${test_name}_in.txt)
    set(INPUT_FILE ${TEST_DIR}/${test_name}_in.txt)
else()
    set(INPUT_FILE /dev/null)
endif()

# Run ed with -S script, input file, output to temp
execute_process(
    COMMAND ${ED_BINARY} -S ${TEST_DIR}/${test_name}.ed ${INPUT_FILE} -o ${OUT_FILE}
    RESULT_VARIABLE ed_exit_code
    OUTPUT_VARIABLE ed_stdout
    ERROR_VARIABLE ed_stderr
)

if(NOT ed_exit_code EQUAL 0)
    message("=== ed STDOUT ===\n${ed_stdout}")
    message("=== ed STDERR ===\n${ed_stderr}")
    message(FATAL_ERROR "TEST FAILED: ed exited with code ${ed_exit_code}")
endif()

# Compare output with expected
if(NOT EXISTS ${TEST_DIR}/${test_name}_expected.txt)
    message(FATAL_ERROR "Missing expected output: ${TEST_DIR}/${test_name}_expected.txt")
endif()

execute_process(
    COMMAND ${CMAKE_COMMAND} -E compare_files
        ${OUT_FILE} ${TEST_DIR}/${test_name}_expected.txt
    RESULT_VARIABLE diff_result
)

if(NOT diff_result EQUAL 0)
    message("=== ACTUAL OUTPUT (${OUT_FILE}) ===")
    file(READ ${OUT_FILE} actual_content)
    message("${actual_content}")
    message(FATAL_ERROR "TEST FAILED: output differs from expected")
endif()

message("Test ${test_name}: PASSED")
