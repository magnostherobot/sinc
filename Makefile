CFLAGS = --std=c99 -Wpedantic -Wall

CFLAGS   += $(shell llvm-config --cflags)
CXXFLAGS += $(shell llvm-config --cppflags)
LDFLAGS  += $(shell llvm-config --ldflags)
LDLIBS   += $(shell llvm-config --libs core analysis --system-libs)

.PHONY: default all clean
default: all
all: basic

basic.o: basic.h parse.h debug.h error.h scope.h
parse.o: basic.h
lex.o:   basic.h parse.h
scope.o: scope.h error.h

basic: basic.o parse.o lex.o scope.o
	$(LINK.cc) $(OUTPUT_OPTION) $^ $(LDLIBS)

parse.h: parse.y
	$(YACC) -d -o /dev/null $<
	mv -f y.tab.h $@

# test stuff (warning: influenced by compiler flags declared above!)
# TODO should probably rearrange the system so tests don't use the same build
# configs as the compiler

simple: simple.o common.o

%.o: %.c
	$(COMPILE.c) $(OUTPUT_OPTION) $<

%.o: %.ll
	clang -c $(OUTPUT_OPTION) $<

%.ll: %.int basic
	< $< ./basic > $@

%.ll: %.c
	clang -S -emit-llvm $(OUTPUT_OPTION) $<

clean:
	$(RM) basic simple y.tab.* *.ll *.o lex.c parse.c parse.h
