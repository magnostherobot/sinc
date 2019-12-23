CC = clang
SINC = ../../sinter

TRGT = $(patsubst %.sin,%,$(wildcard test*.sin))
LDFLAGS = -L$(HOME)/usr/lib
LDLIBS = -lgc

include ../../common.mak

.PHONY: default all clean

default: all

all: $(TRGT)

$(TRGT): test_framework.o ../../common.o

clean:
	$(RM) *.o *.ll $(TRGT)
