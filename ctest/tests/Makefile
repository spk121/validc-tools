# POSIX Makefile for assert-based tests of ctest

CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2

# Default target
.PHONY: all
all: ctest_basic

ctest_basic: test_ctest_basic.o
	$(CC) $(CFLAGS) $^ -o $@

test_ctest_basic.o: test_ctest_basic.c ../ctest.c ../ctest.h
	$(CC) $(CFLAGS) -I.. -c $< -o $@

.PHONY: run
run: ctest_basic
	./ctest_basic

.PHONY: clean
clean:
	$(RM) ctest_basic test_ctest_basic.o
