SOURCES = lex.c main.c gram.c
CFLAGS = -g -O -Wall

all: gen4asm

y.tab.h: gram.c

gen4asm: ${SOURCES}
	cc ${CFLAGS} -o gen4asm ${SOURCES}

clean:
	rm -f gram.c lex.c
	rm -f *.o gen4asm