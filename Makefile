CC=gcc
CFLAGS=-Wall -Wextra -pthread

all:
	$(CC) main.c logging.c thread_workers.c -o gateway $(CFLAGS)

clean:
	rm -f gateway logFifo gateway.log