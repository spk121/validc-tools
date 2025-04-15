CC = gcc
CFLAGS_EMACS = -fdiagnostics-plain-output
CFLAGS = $(CFLAGS_EMACS) -Wall -std=c23 -I../c23lib
OBJS = main.o execute.o variables.o builtins.o parser.o control.o functions.o utils.o tokenizer.o

sh23: $(OBJS)
	$(CC) $(CFLAGS) -o sh23 $(OBJS)

main.o: main.c execute.h variables.h parser.h builtins.h
	$(CC) $(CFLAGS) -c main.c

execute.o: execute.c execute.h builtins.h variables.h
	$(CC) $(CFLAGS) -c execute.c

variables.o: variables.c variables.h execute.h
	$(CC) $(CFLAGS) -c variables.c

builtins.o: builtins.c builtins.h variables.h
	$(CC) $(CFLAGS) -c builtins.c

parser.o: parser.c parser.h builtins.h
	$(CC) $(CFLAGS) -c parser.c

control.o: control.c control.h
	$(CC) $(CFLAGS) -c control.c

functions.o: functions.c functions.h
	$(CC) $(CFLAGS) -c functions.c

utils.o: utils.c utils.h
	$(CC) $(CFLAGS) -c utils.c

tokenizer.o: tokenizer.c logging.h
	$(CC) $(CFLAGS) -c tokenizer.c

clean:
	rm -f *.o sh23
