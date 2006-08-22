SOURCES = lex.c main.c gram.c

all: lextest

y.tab.h: gram.c

lextest: ${SOURCES}
	cc -o lextest ${SOURCES}

clean:
	rm -f *.o lextest