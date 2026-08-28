SRC = ../../src
APP = ../../app/rnx2range
CFLAGS = -O2 -std=c99 -Wall -Wextra -pedantic -I$(SRC)
LDLIBS = -lm

all: test_rnxrange rnx2range_cli

test_rnxrange: test_rnxrange.o rnxrange.o rtkcmn.o rinex.o preceph.o
rnx2range_cli: rnx2range_cli.o rnxrange.o rtkcmn.o rinex.o preceph.o

test_rnxrange.o: test_rnxrange.c $(SRC)/rnxrange.h
	$(CC) -c $(CFLAGS) test_rnxrange.c
rnx2range_cli.o: $(APP)/rnx2range.c $(SRC)/rnxrange.h
	$(CC) -c $(CFLAGS) $(APP)/rnx2range.c -o $@
rnxrange.o: $(SRC)/rnxrange.c $(SRC)/rnxrange.h $(SRC)/rtklib.h
	$(CC) -c $(CFLAGS) $(SRC)/rnxrange.c
rtkcmn.o: $(SRC)/rtkcmn.c $(SRC)/rtklib.h
	$(CC) -c $(CFLAGS) $(SRC)/rtkcmn.c
rinex.o: $(SRC)/rinex.c $(SRC)/rtklib.h
	$(CC) -c $(CFLAGS) $(SRC)/rinex.c
preceph.o: $(SRC)/preceph.c $(SRC)/rtklib.h
	$(CC) -c $(CFLAGS) $(SRC)/preceph.c

test: test_rnxrange rnx2range_cli
	./test_rnxrange
	./rnx2range_cli --help >/dev/null
	./rnx2range_cli -i data/rinex4_mixed_obs.rnx -o cli_test.rangea --no-warnings 2>cli_test.report
	grep -q '^#RANGEA,' cli_test.rangea
	grep -q '^input_epochs=1$$' cli_test.report
	grep -q '^output_messages=1$$' cli_test.report
	grep -q '^source_nonempty_values=5$$' cli_test.report
	grep -q '^converted_values=5$$' cli_test.report
	grep -q '^conservation=5=5+0+0$$' cli_test.report
	! ./rnx2range_cli -i data/rinex4_mixed_obs.rnx -o data/rinex4_mixed_obs.rnx 2>cli_same_path.report
	grep -q 'input and output paths must differ' cli_same_path.report
	grep -q 'RINEX VERSION / TYPE' data/rinex4_mixed_obs.rnx

clean:
	rm -f test_rnxrange test_rnxrange.exe rnx2range_cli rnx2range_cli.exe *.o cli_test.rangea cli_test.report cli_same_path.report

.PHONY: all test clean
