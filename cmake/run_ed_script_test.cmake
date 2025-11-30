# cmake/run_ed_script_test.cmake
# Golden rule: NEVER modify the source tree. Work on copies only.

cmake_minimum_required(VERSION 3.16)

# Required variables passed from CMakeLists.txt
if(NOT DEFINED TEST_NAME OR NOT DEFINED TEST_DIR OR NOT DEFINED ED_BINARY)
    message(FATAL_ERROR "TEST_NAME, TEST_DIR, or ED_BINARY not defined")
endif()

# ------------------------------------------------------------------
# 1. Set up a clean, isolated sandbox for this test
# ------------------------------------------------------------------
set(sandbox "${CMAKE_CURRENT_BINARY_DIR}/scripted_${TEST_NAME}")
file(REMOVE_RECURSE "${sandbox}")        # start clean every time
file(MAKE_DIRECTORY "${sandbox}")

# ------------------------------------------------------------------
# 2. Copy the original input file (if any) into the sandbox
# ------------------------------------------------------------------
set(original_file "${TEST_DIR}/${TEST_NAME}_in.txt")
set(work_file     "${sandbox}/input.txt")   # name doesn't matter, ed will rename if needed

if(EXISTS "${original_file}")
    file(COPY "${original_file}" DESTINATION "${sandbox}")
    file(RENAME "${sandbox}/${TEST_NAME}_in.txt" "${work_file}")
else()
    # No input file → ed starts with empty buffer
    set(work_file "")
endif()

# ------------------------------------------------------------------
# 3. Build the ed command line
# ------------------------------------------------------------------
set(ed_cmd ${ED_BINARY})

# Optional script
set(script_file "${TEST_DIR}/${TEST_NAME}.ed")
if(EXISTS "${script_file}")
    list(APPEND ed_cmd -S "${script_file}")
endif()

# Optional input file
if(work_file)
    list(APPEND ed_cmd "${work_file}")
endif()

# ------------------------------------------------------------------
# 4. Run ed in the sandbox
# ------------------------------------------------------------------
execute_process(
    COMMAND ${ed_cmd}
    WORKING_DIRECTORY "${sandbox}"
    RESULT_VARIABLE ed_result
    OUTPUT_VARIABLE ed_out
    ERROR_VARIABLE ed_err
)

if(NOT ed_result EQUAL 0)
    message("=== ed STDOUT ===\n${ed_out}")
    message("=== ed STDERR ===\n${ed_err}")
    message(FATAL_ERROR "TEST FAILED: ed exited with status ${ed_result}")
endif()

# ------------------------------------------------------------------
# 5. Determine what file(s) should now exist and compare them
# ------------------------------------------------------------------
# Default assumption: ed wrote back to the input file (in-place)
set(result_file "${sandbox}/input.txt")

# But if a specific output was written via 'w filename', the test can provide:
#   ${TEST_NAME}_out.txt  → expected content of the file that was written
# We'll check for any _out.txt golden files in the test directory

file(GLOB expected_files "${TEST_DIR}/${TEST_NAME}_out*.txt")

if(expected_files)
    # Multiple possible output files (e.g. w file1, w file2)
    foreach(expected IN LISTS expected_files)
        get_filename_component(expected_name ${expected} NAME)
        set(actual "${sandbox}/${expected_name}")
        if(NOT EXISTS "${actual}")
            message(FATAL_ERROR "TEST FAILED: expected output file missing: ${expected_name}")
        endif()
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E compare_files "${actual}" "${expected}"
            RESULT_VARIABLE diff
        )
        if(NOT diff EQUAL 0)
            message("=== ACTUAL ${expected_name} ===")
            file(READ "${actual}" content)
            message("${content}")
            message(FATAL_ERROR "TEST FAILED: ${expected_name} differs")
        endif()
    endforeach()
else()
    # Classic case: single in-place edit or empty buffer → compare input.txt
    set(expected_final "${TEST_DIR}/${TEST_NAME}_expected.txt")
    if(NOT EXISTS "${expected_final}")
        message(FATAL_ERROR "Missing golden file: ${expected_final}")
    endif()
    if(NOT EXISTS "${result_file}")
        message(FATAL_ERROR "TEST FAILED: ed did not produce any output file")
    endif()
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E compare_files "${result_file}" "${expected_final}"
        RESULT_VARIABLE diff
    )
    if(NOT diff EQUAL 0)
        message("=== ACTUAL CONTENT ===")
        file(READ "${result_file}" content)
        message("${content}")
        message(FATAL_ERROR "TEST FAILED: final file differs from expected")
    endif()
endif()

message("Test ${TEST_NAME}: PASSED")
