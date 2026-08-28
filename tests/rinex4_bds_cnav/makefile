SRC = ../../src
CFLAGS = -O2 -std=c99 -Wall -Wextra -pedantic -ffunction-sections -fdata-sections \
         -I$(SRC) -DENAGLO -DENAQZS -DENAGAL -DENACMP -DNFREQ=3
LDFLAGS = -Wl,--gc-sections
LDLIBS = -lm -lpthread
SOURCES = test_rinex4_bds_cnav.c $(SRC)/rinex.c $(SRC)/rtkcmn.c

all: test_rinex4_bds_cnav

test_rinex4_bds_cnav: $(SOURCES)
	$(CC) $(CFLAGS) $(SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

test: test_rinex4_bds_cnav
	./test_rinex4_bds_cnav

clean:
	rm -f test_rinex4_bds_cnav test_rinex4_bds_cnav.exe rinex4_bds_cnav_fixture.rnx

.PHONY: all test clean
