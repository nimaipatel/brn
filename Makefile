CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -pedantic -ggdb

.PHONY: all clean

all: brn

%: %.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -rf ./brn
