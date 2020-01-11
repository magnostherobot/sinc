CC = clang
CXX = clang++

DEBUG = -g -O0

CFLAGS = $(DEBUG) -Werror -Wpedantic -Wall -I$(HOME)/usr/include
LDFLAGS = $(DEBUG) -L$(HOME)/usr/lib
LDLIBS = -lgc

LLCFLAGS = -O0
SINCFLAGS = -u never -w 32

TRGT = nqueens

SINS = $(wildcard *.sin)
LLEXT = $(SINS:.sin=.ll)

include ../../common.mak

.PHONY: default all clean

default: all

all: $(TRGT)

$(TRGT): nqueens.o util.o either.o list.o closures.o run_func.o glue.o gglue.o

clean:
	$(RM) $(LLEXT) *.dot *.svg *.o $(TRGT)
