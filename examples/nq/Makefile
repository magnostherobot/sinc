CC = clang
CXX = clang++
SINC = ../../sinter

DEBUG = -g -O3

CFLAGS = $(DEBUG) -Werror -Wpedantic -Wall -I$(HOME)/usr/include
LDFLAGS = $(DEBUG) -L$(HOME)/usr/lib
LDLIBS = -lgc

LLCFLAGS = $(DEBUG) -Werror -Wpedantic -Wall
SINCFLAGS = -v

TRGT = nqueens

include ../../common.mak

.PHONY: default all clean

default: all

all: $(TRGT)

$(TRGT): nqueens.o util.o either.o list.o closures.o run_func.o glue.o

clean:
	$(RM) *.ll *.dot *.svg *.o $(TRGT)
