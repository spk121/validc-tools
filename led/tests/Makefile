# POSIX Makefile for building led tests with gcc

CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2
LDFLAGS ?=

# Source paths
LED_DIR := ..
TEST_DIR := .

# Objects
OBJS := \
	$(TEST_DIR)/test_append_line_ctest.o \
	$(LED_DIR)/led.o \
	../../level1/bre.o \
	../../ctest/ctest.o

WRITE_OBJS := \
	$(TEST_DIR)/test_write_file_ctest.o \
	$(LED_DIR)/led.o \
	../../level1/bre.o \
	../../ctest/ctest.o

PARSE_OBJS := \
	$(TEST_DIR)/test_parse_address_ctest.o \
	$(LED_DIR)/led.o \
	../../level1/bre.o \
	../../ctest/ctest.o

VALIDATION_OBJS := \
	$(TEST_DIR)/test_address_validation_ctest.o \
	$(LED_DIR)/led.o \
	../../level1/bre.o \
	../../ctest/ctest.o

DIRTY_OBJS := \
	$(TEST_DIR)/test_dirty_flag_ctest.o \
	$(LED_DIR)/led.o \
	../../level1/bre.o \
	../../ctest/ctest.o

LONG_OBJS := \
	$(TEST_DIR)/test_long_lines_ctest.o \
	$(LED_DIR)/led.o \
	../../level1/bre.o \
	../../ctest/ctest.o

VERBOSE_OBJS := \
	$(TEST_DIR)/test_verbose_errors_ctest.o \
	$(LED_DIR)/led.o \
	../../level1/bre.o \
	../../ctest/ctest.o

FREE_OBJS := \
	$(TEST_DIR)/test_free_editor_ctest.o \
	$(LED_DIR)/led.o \
	../../level1/bre.o \
	../../ctest/ctest.o

RANGE_OBJS := \
	$(TEST_DIR)/test_address_range_ctest.o \
	$(LED_DIR)/led.o \
	../../level1/bre.o \
	../../ctest/ctest.o

SIMPLE_OBJS := \
	$(TEST_DIR)/test_simple_commands_ctest.o \
	$(LED_DIR)/led.o \
	../../level1/bre.o \
	../../ctest/ctest.o

FILE_OPS_OBJS := \
	$(TEST_DIR)/test_file_operations_ctest.o \
	$(LED_DIR)/led.o \
	../../level1/bre.o \
	../../ctest/ctest.o

LINE_OPS_OBJS := \
	$(TEST_DIR)/test_line_operations_ctest.o \
	$(LED_DIR)/led.o \
	../../level1/bre.o \
	../../ctest/ctest.o

BRE_OBJS := \
	$(TEST_DIR)/test_bre_ctest.o \
	../../level1/bre.o \
	../../ctest/ctest.o

SUBSTITUTE_OBJS := \
	$(TEST_DIR)/test_substitute_ctest.o \
	$(LED_DIR)/led.o \
	../../level1/bre.o \
	../../ctest/ctest.o

REGEX_ADDR_OBJS := \
	$(TEST_DIR)/test_regex_address_ctest.o \
	$(LED_DIR)/led.o \
	../../level1/bre.o \
	../../ctest/ctest.o

GLOBAL_CMDS_OBJS := \
	$(TEST_DIR)/test_global_commands_ctest.o \
	$(LED_DIR)/led.o \
	../../level1/bre.o \
	../../ctest/ctest.o

UNDO_MARKS_OBJS := \
	$(TEST_DIR)/test_undo_marks_ctest.o \
	$(LED_DIR)/led.o \
	../../level1/bre.o \
	../../ctest/ctest.o

# Default target
.PHONY: all
all: append_line_tests write_file_tests parse_address_tests validation_tests dirty_tests long_line_tests verbose_tests free_editor_tests range_tests simple_tests file_ops_tests line_ops_tests bre_tests substitute_tests regex_address_tests global_commands_tests undo_marks_tests script_mode_tests

$(TEST_DIR)/test_append_line_ctest.o: $(TEST_DIR)/test_append_line_ctest.c $(LED_DIR)/led.h ../../ctest/ctest.h
	$(CC) $(CFLAGS) -I$(LED_DIR) -I../../ctest -c $< -o $@

$(TEST_DIR)/test_write_file_ctest.o: $(TEST_DIR)/test_write_file_ctest.c $(LED_DIR)/led.h ../../ctest/ctest.h
	$(CC) $(CFLAGS) -I$(LED_DIR) -I../../ctest -c $< -o $@

$(TEST_DIR)/test_parse_address_ctest.o: $(TEST_DIR)/test_parse_address_ctest.c $(LED_DIR)/led.h ../../ctest/ctest.h
	$(CC) $(CFLAGS) -I$(LED_DIR) -I../../ctest -c $< -o $@

$(TEST_DIR)/test_address_validation_ctest.o: $(TEST_DIR)/test_address_validation_ctest.c $(LED_DIR)/led.h ../../ctest/ctest.h
	$(CC) $(CFLAGS) -I$(LED_DIR) -I../../ctest -c $< -o $@

$(TEST_DIR)/test_dirty_flag_ctest.o: $(TEST_DIR)/test_dirty_flag_ctest.c $(LED_DIR)/led.h ../../ctest/ctest.h
	$(CC) $(CFLAGS) -I$(LED_DIR) -I../../ctest -c $< -o $@

$(TEST_DIR)/test_long_lines_ctest.o: $(TEST_DIR)/test_long_lines_ctest.c $(LED_DIR)/led.h ../../ctest/ctest.h
	$(CC) $(CFLAGS) -I$(LED_DIR) -I../../ctest -c $< -o $@

$(TEST_DIR)/test_verbose_errors_ctest.o: $(TEST_DIR)/test_verbose_errors_ctest.c $(LED_DIR)/led.h ../../ctest/ctest.h
	$(CC) $(CFLAGS) -I$(LED_DIR) -I../../ctest -c $< -o $@

$(TEST_DIR)/test_free_editor_ctest.o: $(TEST_DIR)/test_free_editor_ctest.c $(LED_DIR)/led.h ../../ctest/ctest.h
	$(CC) $(CFLAGS) -I$(LED_DIR) -I../../ctest -c $< -o $@

$(TEST_DIR)/test_address_range_ctest.o: $(TEST_DIR)/test_address_range_ctest.c $(LED_DIR)/led.h ../../ctest/ctest.h
	$(CC) $(CFLAGS) -I$(LED_DIR) -I../../ctest -c $< -o $@

$(TEST_DIR)/test_simple_commands_ctest.o: $(TEST_DIR)/test_simple_commands_ctest.c $(LED_DIR)/led.h ../../ctest/ctest.h
	$(CC) $(CFLAGS) -I$(LED_DIR) -I../../ctest -c $< -o $@

$(TEST_DIR)/test_file_operations_ctest.o: $(TEST_DIR)/test_file_operations_ctest.c $(LED_DIR)/led.h ../../ctest/ctest.h
	$(CC) $(CFLAGS) -I$(LED_DIR) -I../../ctest -c $< -o $@

$(TEST_DIR)/test_line_operations_ctest.o: $(TEST_DIR)/test_line_operations_ctest.c $(LED_DIR)/led.h ../../ctest/ctest.h
	$(CC) $(CFLAGS) -I$(LED_DIR) -I../../ctest -c $< -o $@

$(TEST_DIR)/test_bre_ctest.o: $(TEST_DIR)/test_bre_ctest.c ../../level1/bre.h ../../ctest/ctest.h
	$(CC) $(CFLAGS) -I../../level1 -I../../ctest -c $< -o $@

$(TEST_DIR)/test_substitute_ctest.o: $(TEST_DIR)/test_substitute_ctest.c $(LED_DIR)/led.h ../../ctest/ctest.h
	$(CC) $(CFLAGS) -I$(LED_DIR) -I../../ctest -c $< -o $@

$(TEST_DIR)/test_regex_address_ctest.o: $(TEST_DIR)/test_regex_address_ctest.c $(LED_DIR)/led.h ../../ctest/ctest.h
	$(CC) $(CFLAGS) -I$(LED_DIR) -I../../ctest -c $< -o $@

$(TEST_DIR)/test_global_commands_ctest.o: $(TEST_DIR)/test_global_commands_ctest.c $(LED_DIR)/led.h ../../ctest/ctest.h
	$(CC) $(CFLAGS) -I$(LED_DIR) -I../../ctest -c $< -o $@

$(TEST_DIR)/test_undo_marks_ctest.o: $(TEST_DIR)/test_undo_marks_ctest.c $(LED_DIR)/led.h ../../ctest/ctest.h
	$(CC) $(CFLAGS) -I$(LED_DIR) -I../../ctest -c $< -o $@

$(LED_DIR)/led.o: $(LED_DIR)/led.c $(LED_DIR)/led.h
	$(CC) $(CFLAGS) -DLED_TEST -c $< -o $@

../../ctest/ctest.o: ../../ctest/ctest.c ../../ctest/ctest.h
	$(CC) $(CFLAGS) -c $< -o $@

../../level1/bre.o: ../../level1/bre.c ../../level1/bre.h
	$(CC) $(CFLAGS) -c $< -o $@

# Link the test executables
.PHONY: append_line_tests
append_line_tests: $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)

.PHONY: write_file_tests
write_file_tests: $(WRITE_OBJS)
	$(CC) $(CFLAGS) $(WRITE_OBJS) -o $@ $(LDFLAGS)

.PHONY: parse_address_tests
parse_address_tests: $(PARSE_OBJS)
	$(CC) $(CFLAGS) $(PARSE_OBJS) -o $@ $(LDFLAGS)

.PHONY: validation_tests
validation_tests: $(VALIDATION_OBJS)
	$(CC) $(CFLAGS) $(VALIDATION_OBJS) -o $@ $(LDFLAGS)

.PHONY: dirty_tests
dirty_tests: $(DIRTY_OBJS)
	$(CC) $(CFLAGS) $(DIRTY_OBJS) -o $@ $(LDFLAGS)

.PHONY: long_line_tests
long_line_tests: $(LONG_OBJS)
	$(CC) $(CFLAGS) $(LONG_OBJS) -o $@ $(LDFLAGS)

.PHONY: verbose_tests
verbose_tests: $(VERBOSE_OBJS)
	$(CC) $(CFLAGS) $(VERBOSE_OBJS) -o $@ $(LDFLAGS)

.PHONY: free_editor_tests
free_editor_tests: $(FREE_OBJS)
	$(CC) $(CFLAGS) $(FREE_OBJS) -o $@ $(LDFLAGS)

.PHONY: range_tests
range_tests: $(RANGE_OBJS)
	$(CC) $(CFLAGS) $(RANGE_OBJS) -o $@ $(LDFLAGS)

.PHONY: simple_tests
simple_tests: $(SIMPLE_OBJS)
	$(CC) $(CFLAGS) $(SIMPLE_OBJS) -o $@ $(LDFLAGS)

.PHONY: file_ops_tests
file_ops_tests: $(FILE_OPS_OBJS)
	$(CC) $(CFLAGS) $(FILE_OPS_OBJS) -o $@ $(LDFLAGS)

.PHONY: line_ops_tests
line_ops_tests: $(LINE_OPS_OBJS)
	$(CC) $(CFLAGS) $(LINE_OPS_OBJS) -o $@ $(LDFLAGS)

.PHONY: bre_tests
bre_tests: $(BRE_OBJS)
	$(CC) $(CFLAGS) $(BRE_OBJS) -o $@ $(LDFLAGS)

.PHONY: substitute_tests
substitute_tests: $(SUBSTITUTE_OBJS)
	$(CC) $(CFLAGS) $(SUBSTITUTE_OBJS) -o $@ $(LDFLAGS)

.PHONY: regex_address_tests
regex_address_tests: $(REGEX_ADDR_OBJS)
	$(CC) $(CFLAGS) $(REGEX_ADDR_OBJS) -o $@ $(LDFLAGS)

.PHONY: global_commands_tests
global_commands_tests: $(GLOBAL_CMDS_OBJS)
	$(CC) $(CFLAGS) $(GLOBAL_CMDS_OBJS) -o $@ $(LDFLAGS)

.PHONY: undo_marks_tests
undo_marks_tests: $(UNDO_MARKS_OBJS)
	$(CC) $(CFLAGS) $(UNDO_MARKS_OBJS) -o $@ $(LDFLAGS)

# Run tests
.PHONY: run
run: append_line_tests write_file_tests parse_address_tests validation_tests dirty_tests long_line_tests verbose_tests free_editor_tests range_tests simple_tests file_ops_tests line_ops_tests bre_tests substitute_tests regex_address_tests global_commands_tests undo_marks_tests
	./append_line_tests
	./write_file_tests
	./parse_address_tests
	./validation_tests
	./dirty_tests
	./long_line_tests
	./verbose_tests
	./free_editor_tests
	./range_tests
	./simple_tests
	./file_ops_tests
	./line_ops_tests
	./bre_tests
	./substitute_tests
	./regex_address_tests
	./global_commands_tests
	./undo_marks_tests
	./run_script_tests.bat

# Clean
.PHONY: clean
clean:
	$(RM) $(OBJS) $(WRITE_OBJS) $(PARSE_OBJS) $(VALIDATION_OBJS) $(DIRTY_OBJS) $(LONG_OBJS) $(VERBOSE_OBJS) $(FREE_OBJS) $(RANGE_OBJS) $(SIMPLE_OBJS) $(FILE_OPS_OBJS) $(LINE_OPS_OBJS) $(BRE_OBJS) $(SUBSTITUTE_OBJS) $(REGEX_ADDR_OBJS) $(GLOBAL_CMDS_OBJS) $(UNDO_MARKS_OBJS) append_line_tests write_file_tests parse_address_tests validation_tests dirty_tests long_line_tests verbose_tests free_editor_tests range_tests simple_tests file_ops_tests line_ops_tests bre_tests substitute_tests regex_address_tests global_commands_tests undo_marks_tests *.tmp *.txt scripts\*.out.txt scripts\actual_*.txt

# Build standalone editor executable for script tests (without LED_TEST)
.PHONY: led_exe
led_exe: ../led_exe

../led_exe: ../led.c ../../level1/bre.o
	$(CC) $(CFLAGS) ../led.c ../../level1/bre.o -o $@

.PHONY: script_mode_tests
script_mode_tests: led_exe
	./run_script_tests.bat
