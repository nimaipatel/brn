CC = gcc
CFLAGS = -Wall -Wextra -pedantic -ggdb
PREFIX ?= /usr/local

.PHONY: all clean install

all: brn

%: %.c
	$(CC) $(CFLAGS) $< -o $@

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	install -m 0755 brn $(DESTDIR)$(PREFIX)/bin/brn

clean:
	rm -rf ./brn
