CC      = gcc
CFLAGS  = -Wall -Wextra -O2

all: monitor.exe

monitor.exe: monitor.c
	$(CC) $(CFLAGS) -o monitor.exe monitor.c

clean:
	rm -f monitor.exe *.o
