CC = c++

CFLAGS += -x c --std=c99 -Wpedantic -Wall -Werror

CFLAGS  += $(shell llvm-config --cppflags)
LDFLAGS += $(shell llvm-config --ldflags)
LDLIBS  += $(shell llvm-config --libs core analysis bitwriter --system-libs)

.PHONY: default all clean
default: all
all: basic
basic: basic.o parse.o lex.o

basic.o lex.o: parse.h basic.h
parse.o: basic.h

parse.h: parse.y
	$(YACC) -d -o /dev/null $<
	mv -f y.tab.h $@

clean:
	$(RM) basic y.tab.* *.o lex.c parse.c parse.h
