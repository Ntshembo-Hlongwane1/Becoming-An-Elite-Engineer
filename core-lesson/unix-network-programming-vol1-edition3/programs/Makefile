UNPDIR = ../unpv13e
UNPLIB = $(UNPDIR)/lib

CC      = gcc -std=gnu89
CFLAGS  = -I$(UNPLIB) -g -O2 -D_REENTRANT -Wall
LIBS    = $(UNPDIR)/libunp.a -lpthread

SRCS = $(wildcard *.c)
PROGS = $(SRCS:.c=)

all: $(PROGS)

%: %.c
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

clean:
	rm -f $(PROGS) *.o
