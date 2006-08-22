SOURCES = lex.c main.c gram.c
CFLAGS = -g -O -Wall

all: lextest

y.tab.h: gram.c

lextest: ${SOURCES}
	cc ${CFLAGS} -o lextest ${SOURCES}

clean:
	rm -f gram.c lex.c
	rm -f *.o lextest