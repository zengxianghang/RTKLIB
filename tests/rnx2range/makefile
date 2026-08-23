SRC = ../../src
CFLAGS = -O2 -std=c99 -Wall -Wextra -pedantic -I$(SRC)
LDLIBS = -lm

all: test_rnxrange

test_rnxrange: test_rnxrange.o rnxrange.o rtkcmn.o rinex.o preceph.o

test_rnxrange.o: test_rnxrange.c $(SRC)/rnxrange.h
	$(CC) -c $(CFLAGS) test_rnxrange.c
rnxrange.o: $(SRC)/rnxrange.c $(SRC)/rnxrange.h $(SRC)/rtklib.h
	$(CC) -c $(CFLAGS) $(SRC)/rnxrange.c
rtkcmn.o: $(SRC)/rtkcmn.c $(SRC)/rtklib.h
	$(CC) -c $(CFLAGS) $(SRC)/rtkcmn.c
rinex.o: $(SRC)/rinex.c $(SRC)/rtklib.h
	$(CC) -c $(CFLAGS) $(SRC)/rinex.c
preceph.o: $(SRC)/preceph.c $(SRC)/rtklib.h
	$(CC) -c $(CFLAGS) $(SRC)/preceph.c

test: test_rnxrange
	./test_rnxrange

clean:
	rm -f test_rnxrange test_rnxrange.exe *.o

.PHONY: all test clean
