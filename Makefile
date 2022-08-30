CC ?= clang
CXX ?= clang++
YACC = bison
LEX = flex
LLVM_CONFIG ?= llvm-config

INSTALL = install -c

DEBUG = -ggdb3 -DGC_DEBUG
RELEASE = -O2 -DNDEBUG

CFLAGS = --std=c99 -D_POSIX_C_SOURCE=200809L \
		 -Wpedantic -Wall -Wextra -Werror $(DEBUG)
LFLAGS = -D_POSIX_C_SOURCE=200809L

LLVM_MODULES = core executionengine mcjit interpreter analysis native bitwriter

CFLAGS   += $(shell $(LLVM_CONFIG) --cflags)
CXXFLAGS += $(shell $(LLVM_CONFIG) --cppflags)
LDFLAGS  += $(shell $(LLVM_CONFIG) --ldflags)
LDLIBS   += $(shell $(LLVM_CONFIG) --libs $(LLVM_MODULES) --system-libs)

.PHONY: default all clean test install uninstall

default: all

all: sinc

install: all
	$(INSTALL) -d $(PREFIX)/bin/
	$(INSTALL) sinc $(PREFIX)/bin/

uninstall:
	$(RM) $(PREFIX)/bin/sinc

sinc.o: sinc.h parse.h debug.h error.h scope.h llvm_codegen.h \
	graphviz_codegen.h
llvm_codegen.o: llvm_codegen.h sinc.h scope.h debug.h parse.h error.h
sinter_codegen.o: sinter_codegen.h parse.h
graphviz_codegen.o: graphviz_codegen.h parse.h error.h
parse.o:  sinc.h
lex.o:    sinc.h parse.h
scope.o:  scope.h error.h debug.h
common.o: debug.h

sinc: sinc.o parse.o lex.o scope.o llvm_codegen.o sinter_codegen.o \
	graphviz_codegen.o
	$(LINK.cc) $(OUTPUT_OPTION) $^ $(LDLIBS)

test: all
	$(MAKE) -C examples test

parse.o: parse.c
	$(CC) -c --std=c99 $(OUTPUT_OPTION) $<

lex.o: lex.c
	$(CC) -c --std=c99 $(OUTPUT_OPTION) $<

%.h: %.y
	$(YACC) --defines=$@ $<

%.c: %.y
	$(YACC) -o $@ $<

clean:
	$(RM) sinc *.tab.* *.o lex.c parse.c parse.h
