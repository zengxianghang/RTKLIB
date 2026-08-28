# makefile for rnx2range

BINDIR = /usr/local/bin
SRC    = ../../../src

CFLAGS = -O2 -std=c99 -Wall -Wextra -pedantic -I$(SRC)
LDLIBS = -lm

all: rnx2range

rnx2range: rnx2range.o rnxrange.o rtkcmn.o rinex.o preceph.o

rnx2range.o: ../rnx2range.c $(SRC)/rnxrange.h
	$(CC) -c $(CFLAGS) ../rnx2range.c
rnxrange.o: $(SRC)/rnxrange.c $(SRC)/rnxrange.h $(SRC)/rtklib.h
	$(CC) -c $(CFLAGS) $(SRC)/rnxrange.c
rtkcmn.o: $(SRC)/rtkcmn.c $(SRC)/rtklib.h
	$(CC) -c $(CFLAGS) $(SRC)/rtkcmn.c
rinex.o: $(SRC)/rinex.c $(SRC)/rtklib.h
	$(CC) -c $(CFLAGS) $(SRC)/rinex.c
preceph.o: $(SRC)/preceph.c $(SRC)/rtklib.h
	$(CC) -c $(CFLAGS) $(SRC)/preceph.c

test: rnx2range
	./rnx2range --help >/dev/null
	./rnx2range -i ../../../tests/rnx2range/data/rinex4_mixed_obs.rnx -o test.rangea --no-warnings 2>test.report
	grep -q '^#RANGEA,' test.rangea
	grep -q '^input_epochs=1$$' test.report
	grep -q '^output_messages=1$$' test.report
	grep -q '^conservation=' test.report
	$(RM) test.rangea test.report

install: rnx2range
	cp rnx2range $(BINDIR)

clean:
	$(RM) rnx2range rnx2range.exe *.o test.rangea test.report

.PHONY: all test install clean
