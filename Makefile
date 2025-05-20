CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g

all: test_threads

uthreads.o: uthreads.c uthreads.h
	$(CC) $(CFLAGS) -c uthreads.c

main.o: main.c uthreads.h
	$(CC) $(CFLAGS) -c main.c

test_threads: main.o uthreads.o
	$(CC) $(CFLAGS) -o test_threads main.o uthreads.o

clean:
	rm -f *.o test_threads

