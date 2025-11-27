cmake_minimum_required(VERSION 3.16)  # Good baseline in 2025
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
    $<INSTALL_INTERFACE:include>  # if you ever install it
)
# Give it the proper output name "vc" → libvc.a / vc.lib
set_target_properties(vc PROPERTIES
    OUTPUT_NAME "vc"
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)

# ------------------------------------------------------------------
# 2. Test executable
# ------------------------------------------------------------------

add_executable(bre_test
    test/lib/bre_test.c
)

target_link_libraries(bre_test PRIVATE vc)

# Make sure it can find bre.h without any path hacks
target_include_directories(bre_test PRIVATE src/lib)

# Put the test binary in a nice place
set_target_properties(bre_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test
)

# ------------------------------------------------------------------
# 3. Register the test with CTest (TAP compatible)
# ------------------------------------------------------------------
enable_testing()

add_test(
    NAME bre_test
    COMMAND bre_test
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/test
)

# Optional: make CTest treat non-zero exit code as failure (this is default,
# but some people like to be explicit)
set_tests_properties(bre_test PROPERTIES
    PASS_REGULAR_EXPRESSION "0"   # TAP plan usually ends with "1..0" or similar if no tests
    FAIL_REGULAR_EXPRESSION "not ok"
)

# ------------------------------------------------------------------
# Convenience: `ctest` or `make test` works out of the box
# ------------------------------------------------------------------
