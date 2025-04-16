# POSIX-compliant Makefile for building sh23

# Compiler and flags
CC = gcc
CFLAGS = -std=c23 -Wall -Wextra -c
LDFLAGS =

# Program name
PROGRAM = sh23

# Source and object files
SOURCES = main.c string.c logging.c string_array.c
OBJECTS = main.o string.o logging.o string_array.o

# Generated files
GEN_HEADERS = string_array.h
GEN_SOURCES = string_array.c

# Templates
TEMPLATES = ptr_array.h.in ptr_array.c.in

# awk script
AWK_SCRIPT = generate_array.awk

# Default target
all: $(PROGRAM)

# Link object files into executable
$(PROGRAM): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $(PROGRAM)

# Compile source files to object files
main.o: main.c string.h string_array.h logging.h
	$(CC) $(CFLAGS) main.c -o main.o

string.o: string.c string.h logging.h
	$(CC) $(CFLAGS) string.c -o string.o

logging.o: logging.c logging.h
	$(CC) $(CFLAGS) logging.c -o logging.o

string_array.o: string_array.c string_array.h string.h logging.h
	$(CC) $(CFLAGS) string_array.c -o string_array.o

# Generate string_array.h using awk
string_array.h: ptr_array.h.in $(AWK_SCRIPT)
	awk -f $(AWK_SCRIPT) -v CLASS_NAME=StringArray \
	-v CLASS_NAME_UPPER=STRING_ARRAY_H -v CLASS_NAME_LOWER=string_array \
	-v ELEMENT_TYPE="String *" -v PREFIX=string_array_ \
	-v include1="#include \"string.h\"" ptr_array.h.in > string_array.h

# Generate string_array.c using awk
string_array.c: ptr_array.c.in $(AWK_SCRIPT)
	awk -f $(AWK_SCRIPT) -v CLASS_NAME=StringArray \
	-v CLASS_NAME_UPPER=STRING_ARRAY_H -v CLASS_NAME_LOWER=string_array \
	-v ELEMENT_TYPE="String *" -v PREFIX=string_array_ \
	-v include1="#include \"string.h\"" ptr_array.c.in > string_array.c

# Clean up generated files, objects, and executable
clean:
	rm -f $(PROGRAM) $(OBJECTS) $(GEN_HEADERS) $(GEN_SOURCES)

# Phony targets
.PHONY: all clean
