CC=gcc
CFLAGS=-Wall -Wextra -std=c99 -O2
TARGET=monitor.exe

$(TARGET): monitor.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(TARGET) *.o

.PHONY: clean
