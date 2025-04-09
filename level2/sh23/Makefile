CC = gcc
CFLAGS = -Wall -std=c23
OBJS = main.o execute.o variables.o builtins.o parser.o control.o functions.o utils.o

sh23: $(OBJS)
	$(CC) $(CFLAGS) -o sh23 $(OBJS)

main.o: main.c execute.h variables.h
	$(CC) $(CFLAGS) -c main.c

execute.o: execute.c execute.h builtins.h variables.h
	$(CC) $(CFLAGS) -c execute.c

variables.o: variables.c variables.h execute.h
	$(CC) $(CFLAGS) -c variables.c

builtins.o: builtins.c builtins.h variables.h
	$(CC) $(CFLAGS) -c builtins.c

parser.o: parser.c parser.h
	$(CC) $(CFLAGS) -c parser.c

control.o: control.c control.h
	$(CC) $(CFLAGS) -c control.c

functions.o: functions.c functions.h
	$(CC) $(CFLAGS) -c functions.c

utils.o: utils.c utils.h
	$(CC) $(CFLAGS) -c utils.c

clean:
	rm -f *.o sh23
