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
	../../ctest/ctest.o

WRITE_OBJS := \
	$(TEST_DIR)/test_write_file_ctest.o \
	$(LED_DIR)/led.o \
	../../ctest/ctest.o

PARSE_OBJS := \
	$(TEST_DIR)/test_parse_address_ctest.o \
	$(LED_DIR)/led.o \
	../../ctest/ctest.o

VALIDATION_OBJS := \
	$(TEST_DIR)/test_address_validation_ctest.o \
	$(LED_DIR)/led.o \
	../../ctest/ctest.o

DIRTY_OBJS := \
	$(TEST_DIR)/test_dirty_flag_ctest.o \
	$(LED_DIR)/led.o \
	../../ctest/ctest.o

LONG_OBJS := \
	$(TEST_DIR)/test_long_lines_ctest.o \
	$(LED_DIR)/led.o \
	../../ctest/ctest.o

VERBOSE_OBJS := \
	$(TEST_DIR)/test_verbose_errors_ctest.o \
	$(LED_DIR)/led.o \
	../../ctest/ctest.o

FREE_OBJS := \
	$(TEST_DIR)/test_free_editor_ctest.o \
	$(LED_DIR)/led.o \
	../../ctest/ctest.o

# Default target
.PHONY: all
all: append_line_tests write_file_tests parse_address_tests validation_tests dirty_tests long_line_tests verbose_tests free_editor_tests

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

$(LED_DIR)/led.o: $(LED_DIR)/led.c $(LED_DIR)/led.h
	$(CC) $(CFLAGS) -DLED_TEST -c $< -o $@

../../ctest/ctest.o: ../../ctest/ctest.c ../../ctest/ctest.h
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

# Run tests
.PHONY: run
run: append_line_tests write_file_tests parse_address_tests validation_tests dirty_tests long_line_tests verbose_tests free_editor_tests
	./append_line_tests
	./write_file_tests
	./parse_address_tests
	./validation_tests
	./dirty_tests
	./long_line_tests
	./verbose_tests
	./free_editor_tests

# Clean
.PHONY: clean
clean:
	$(RM) $(OBJS) $(WRITE_OBJS) $(PARSE_OBJS) $(VALIDATION_OBJS) $(DIRTY_OBJS) $(LONG_OBJS) $(VERBOSE_OBJS) $(FREE_OBJS) append_line_tests write_file_tests parse_address_tests validation_tests dirty_tests long_line_tests verbose_tests free_editor_tests *.tmp *.txt
