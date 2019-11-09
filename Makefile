CC = clang
CXX = clang++
YACC = bison
LEX = flex

DEBUG = -ggdb3 -O0 -DGC_DEBUG
RELEASE = -O3 -DNDEBUG

CFLAGS = --std=c99 -Wpedantic -Wall -Wextra -Werror $(DEBUG)
LFLAGS = -D_POSIX_C_SOURCE=200809L

LLVM_MODULES = core executionengine mcjit interpreter analysis native bitwriter

CFLAGS   += $(shell llvm-config --cflags)
CXXFLAGS += $(shell llvm-config --cppflags)
LDFLAGS  += $(shell llvm-config --ldflags)
LDLIBS   += $(shell llvm-config --libs $(LLVM_MODULES) --system-libs) -lgc

.PHONY: default all clean
default: all
all: basic

basic.o: basic.h parse.h debug.h error.h scope.h
parse.o: basic.h
lex.o:   basic.h parse.h
scope.o: scope.h error.h

basic: basic.o parse.o lex.o scope.o
	$(LINK.cc) $(OUTPUT_OPTION) $^ $(LDLIBS)

# explicit rules for parse and lex prevent -Weverything being passed to them
parse.o: parse.c
	$(CC) -c --std=c99 $(OUTPUT_OPTION) $<

lex.o: lex.c
	$(CC) -c --std=c99 $(OUTPUT_OPTION) $<

%.h: %.y
	$(YACC) --defines=$@ $<

%.c: %.y
	$(YACC) -o $@ $<

# test stuff (warning: influenced by compiler flags declared above!)
# TODO should probably rearrange the system so tests don't use the same build
# configs as the compiler

simple: simple.o common.o

%.o: %.c
	$(COMPILE.c) $(OUTPUT_OPTION) $<

%.o: %.ll
	clang -c $(OUTPUT_OPTION) $<

%.o: %.bc
	clang -c $(OUTPUT_OPTION) $<

%.ll: %.int basic
	./basic -clo $@ $<

%.bc: %.int basic
	./basic -cbo $@ $<

%.ll: %.c
	clang -S -emit-llvm $(OUTPUT_OPTION) $<

clean:
	$(RM) basic simple y.tab.* *.ll *.o lex.c parse.c parse.h
