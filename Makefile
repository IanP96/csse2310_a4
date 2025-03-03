# REF: taken from my assignment 3

CC = gcc
CFLAGS = -g -Wall -Wextra -pedantic -std=gnu99 -I/local/courses/csse2310/include -L/local/courses/csse2310/lib -lcsse2310a4 -pthread
TARGETS = uqchessclient uqchessserver

.DEFAULT_GOAL := all
all: $(TARGETS)
.PHONY: all clean style

uqchessclient: uqchessclient.c shared.c shared.h
	$(CC) $(CFLAGS) $^ -o $@

uqchessserver: uqchessserver.c shared.c shared.h
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -f $(TARGETS)

style:
	2310reformat.sh *.c *.h
	2310stylecheck.sh *.c *.h