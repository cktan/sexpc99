HFILES = sexp.h
CFILES = sexp.c
OBJ = $(CFILES:.c=.o)
EXEC = pprint
LIB = libsexp.a
LIB_SHARED = libsexp.so

CFLAGS = -std=c99 -Wall -Wextra -fpic


# to compile for debug: make DEBUG=1
# to compile for no debug: make
ifdef DEBUG
    CFLAGS += -O0 -g
else
    CFLAGS += -O2 -DNDEBUG
endif


all: $(LIB) $(LIB_SHARED) $(EXEC)


libsexp.a: sexp.o
	ar -rcs $@ $^

libsexp.so: sexp.o
	$(CC) -shared -o $@ $^

*.o: $(HFILES)

pprint: pprint.c sexp.o

prefix ?= /usr/local

install: all
	install -d ${prefix}/include ${prefix}/lib
	install sepx.h ${prefix}/include
	install $(LIB) ${prefix}/lib
	install $(LIB_SHARED) ${prefix}/lib

clean:
	rm -f *.o $(EXEC) $(LIB) $(LIB_SHARED)
