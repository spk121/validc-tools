# POSIX-compliant Makefile for building sh23

# Compiler and flags
CC = gcc
CFLAGS = -std=c23 -Wall -Wextra -ggdb -O0 -c
LDFLAGS =

# Program name
PROGRAM = sh23

# Source and object files
SOURCES = main.c string.c logging.c alias.c alias_store.c alias_array.c \
  token.c token_array.c tokenizer.c
OBJECTS = main.o string.o logging.o alias.o alias_store.o alias_array.o \
  token.o token_array.o tokenizer.o

# Generated files
GEN_HEADERS = string_array.h alias_array.h token_array.h
GEN_SOURCES = string_array.c alias_array.c token_array.c

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
main.o: main.c string.h alias.h alias_store.h logging.h token_array.h
	$(CC) $(CFLAGS) main.c -o main.o

string.o: string.c string.h logging.h
	$(CC) $(CFLAGS) string.c -o string.o

logging.o: logging.c logging.h
	$(CC) $(CFLAGS) logging.c -o logging.o

alias.o: alias.c alias.h string.h logging.h
	$(CC) $(CFLAGS) alias.c -o alias.o

alias_store.o: alias_store.c alias_store.h alias_array.h string.h logging.h
	$(CC) $(CFLAGS) alias_store.c -o alias_store.o

alias_array.o: alias_array.c alias_array.h alias.h string.h logging.h
	$(CC) $(CFLAGS) alias_array.c -o alias_array.o

string_array.o: string_array.c string_array.h string.h logging.h
	$(CC) $(CFLAGS) string_array.c -o string_array.o

token.o: token.c token.h string.h logging.h
	$(CC) $(CFLAGS) token.c -o token.o

token_array.o: token_array.c token_array.h token.h logging.h
	$(CC) $(CFLAGS) token_array.c -o token_array.o

tokenizer.o: tokenizer.c tokenizer.h token_array.h logging.h
	$(CC) $(CFLAGS) tokenizer.c -o tokenizer.o

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

# Generate alias_array.h using awk
alias_array.h: ptr_array.h.in $(AWK_SCRIPT)
	awk -f $(AWK_SCRIPT) -v CLASS_NAME=AliasArray \
	-v CLASS_NAME_UPPER=ALIAS_ARRAY_H -v CLASS_NAME_LOWER=alias_array \
	-v ELEMENT_TYPE="Alias *" -v PREFIX=alias_array_ \
	-v include1="#include \"alias.h\"" ptr_array.h.in > alias_array.h

# Generate alias_array.c using awk
alias_array.c: ptr_array.c.in $(AWK_SCRIPT)
	awk -f $(AWK_SCRIPT) -v CLASS_NAME=AliasArray \
	-v CLASS_NAME_UPPER=ALIAS_ARRAY_H -v CLASS_NAME_LOWER=alias_array \
	-v ELEMENT_TYPE="Alias *" -v PREFIX=alias_array_ \
	-v include1="#include \"alias.h\"" ptr_array.c.in > alias_array.c

# Generate alias_array.h using awk
token_array.h: ptr_array.h.in $(AWK_SCRIPT)
	awk -f $(AWK_SCRIPT) -v CLASS_NAME=TokenArray \
	-v CLASS_NAME_UPPER=TOKEN_ARRAY_H -v CLASS_NAME_LOWER=token_array \
	-v ELEMENT_TYPE="Token *" -v PREFIX=token_array_ \
	-v include1="#include \"token.h\"" ptr_array.h.in > token_array.h

# Generate alias_array.c using awk
token_array.c: ptr_array.c.in $(AWK_SCRIPT)
	awk -f $(AWK_SCRIPT) -v CLASS_NAME=TokenArray \
	-v CLASS_NAME_UPPER=TOKEN_ARRAY_H -v CLASS_NAME_LOWER=token_array \
	-v ELEMENT_TYPE="Token *" -v PREFIX=token_array_ \
	-v include1="#include \"token.h\"" ptr_array.c.in > token_array.c

# Clean up generated files, objects, and executable
clean:
	rm -f $(PROGRAM) $(OBJECTS) $(GEN_HEADERS) $(GEN_SOURCES)

# Phony targets
.PHONY: all clean
