cmake_minimum_required(VERSION 3.16)
project(validc-tools LANGUAGES C)

add_compile_options(-g3 -O0 -Wall -Wextra)

# ------------------------------------------------------------------
# 1. Static library: libvc
# ------------------------------------------------------------------
add_library(vc STATIC
    src/lib/bre.c
    src/lib/bre.h
    src/lib/getopt.c
    src/lib/getopt.h
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

# Object library for the shared CTest framework used by all ed tests
add_library(ctest_obj OBJECT
    test/ctest/ctest.c
    test/ctest/ctest.h
)
target_include_directories(ctest_obj PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/test/ctest
)

# Make sure it sees ed.h and bre.h
# target_include_directories(ctest_obj PRIVATE src/ed src/lib)

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
        $<TARGET_OBJECTS:ctest_obj>
    )
    target_compile_definitions(${test_name} PRIVATE LED_TEST)
    target_link_libraries(${test_name} PRIVATE vc)
    target_include_directories(${test_name} PRIVATE
        src/lib
        src/ed
        ${CMAKE_CURRENT_SOURCE_DIR}/test/ctest
    )
    set_target_properties(${test_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test
    )

    # Register with CTest (TAP compatible)
    add_test(NAME ${test_name}
        COMMAND ${test_name}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/test
    )

    set_tests_properties(${test_name} PROPERTIES
        FAIL_REGULAR_EXPRESSION "not ok|\\b(Bail out!|Bailout!)\\b"
        PASS_REGULAR_EXPRESSION "ok"
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

# ------------------------------------------------------------------
# getopt_test - tests for getopt/getopt_long
# ------------------------------------------------------------------
add_executable(getopt_test
    test/lib/getopt_test.c
    src/lib/getopt.c
    src/lib/getopt.h
)
target_link_libraries(getopt_test PRIVATE vc)
target_include_directories(getopt_test PRIVATE src/lib)
set_target_properties(getopt_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test
)
add_test(NAME getopt_test COMMAND getopt_test WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/test)
set_tests_properties(getopt_test PROPERTIES
    FAIL_REGULAR_EXPRESSION "not ok"
)

# ------------------------------------------------------------------
# 5. Scripted integration tests using real 'ed' binary
# ------------------------------------------------------------------
set(SCRIPTED_TESTS
    empty
    simple
    verbose
    global_brace
)

foreach(test_name IN LISTS SCRIPTED_TESTS)
    set(test_dir ${CMAKE_CURRENT_SOURCE_DIR}/test/ed/scripts)

    add_test(
        NAME scripted_${test_name}
        COMMAND ${CMAKE_COMMAND}
            -D ED_BINARY=$<TARGET_FILE:ed>
            -D TEST_DIR=${test_dir}
	    -D TEST_NAME=${test_name}
            -D OUT_FILE=${CMAKE_BINARY_DIR}/test/${test_name}_out.txt
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_ed_script_test.cmake
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/test
    )

    # Fail if ed exits non-zero OR if output differs
    set_tests_properties(scripted_${test_name} PROPERTIES
        FAIL_REGULAR_EXPRESSION "TEST FAILED"
        TIMEOUT 10
    )
endforeach()

# ------------------------------------------------------------------
# Utilities
# ------------------------------------------------------------------
add_executable(cat
    src/cat/cat.c
)
set_target_properties(cat PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_executable(grep
    src/grep/grep.c
)
target_link_libraries(grep PRIVATE vc)
target_include_directories(grep PRIVATE src/lib)

set_target_properties(grep PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_executable(tr
    src/tr/tr.c
)
set_target_properties(tr PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
