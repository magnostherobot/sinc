CC = clang
SINC = ../../sinc -w 32

TRGT = $(patsubst %.sin,%,$(wildcard test*.sin))
LDLIBS += -lgc

include ../../common.mak

.PHONY: default all clean

default: all

all: $(TRGT)

$(TRGT): test_framework.o

clean:
	$(RM) *.o *.ll $(TRGT)
