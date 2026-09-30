SRC = ../../src
CFLAGS = -O2 -std=c99 -Wall -Wextra -pedantic -ffunction-sections -fdata-sections \
         -I$(SRC) -DENAGLO -DENAQZS -DENAGAL -DENACMP -DNFREQ=3
LDFLAGS = -Wl,--gc-sections
LDLIBS = -lm -lpthread
SOURCES = test_rinex3_glo_orbit4.c $(SRC)/rinex.c $(SRC)/rtkcmn.c

all: test_rinex3_glo_orbit4

test_rinex3_glo_orbit4: $(SOURCES)
	$(CC) $(CFLAGS) $(SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

test: test_rinex3_glo_orbit4
	./test_rinex3_glo_orbit4

clean:
	rm -f test_rinex3_glo_orbit4 test_rinex3_glo_orbit4.exe rinex3_glo_orbit4_304.rnx rinex3_glo_orbit4_305.rnx

.PHONY: all test clean
