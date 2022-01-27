CC = gcc
CFLAGS = -Wall -Wextra -pedantic -ggdb

.PHONY: all clean

all: brn

%: %.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -rf ./brn
