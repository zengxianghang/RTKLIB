# makefile for rnx2range

BINDIR = /usr/local/bin
SRC    = ../../src
CFLAGS = -O2 -std=c99 -Wall -Wextra -pedantic -I$(SRC)
LDLIBS = -lm

all: rnx2range

rnx2range: rnx2range.o rnxrange.o rtkcmn.o

rnx2range.o: rnx2range.c $(SRC)/rnxrange.h
	$(CC) -c $(CFLAGS) rnx2range.c
rnxrange.o: $(SRC)/rnxrange.c $(SRC)/rnxrange.h $(SRC)/rtklib.h
	$(CC) -c $(CFLAGS) $(SRC)/rnxrange.c
rtkcmn.o: $(SRC)/rtkcmn.c $(SRC)/rtklib.h
	$(CC) -c $(CFLAGS) $(SRC)/rtkcmn.c

install: rnx2range
	cp rnx2range $(BINDIR)

clean:
	rm -f rnx2range rnx2range.exe *.o

.PHONY: all install clean
