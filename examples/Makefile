CC = clang
CXX = clang++
SINC = ../sinter

CFLAGS = -g -O0 -Werror -Wpedantic -Wall
LDFLAGS = -L$(HOME)/usr/lib -g
LDLIBS = -lgc

LLCFLAGS = $(CFLAGS)
SINCFLAGS =

TRGT =

include ../common.mak

.PHONY: default all clean

default: all

all: $(TRGT)

clean:
	$(RM) *.ll *.dot *.svg *.o $(TRGT)
