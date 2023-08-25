CC = gcc
LD = $(CC)
CFLAGS = -O3 -g -std=c99 -Wall -Wextra \
	 -Wcast-qual \
	 -Wconversion \
	 -Wformat \
	 -Wformat-security \
	 -Wmissing-prototypes \
	 -Wshadow \
	 -Wsign-compare \
	 -Wstrict-prototypes


PROG = expr-test
OBJS = main.o boolexpr.o

all: $(PROG)


$(PROG): $(OBJS)
	$(LD) -o $@ $^

main.o: main.c boolexpr.h
boolexpr.o: boolexpr.c boolexpr.h

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<


.PHONY: clean
clean:
	rm -f $(OBJS)
	rm -f $(PROG)
