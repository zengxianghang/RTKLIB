SRC = ../../src
CFLAGS = -O2 -std=c99 -Wall -Wextra -pedantic -ffunction-sections -fdata-sections \
         -I$(SRC) -DENAGLO -DENAQZS -DENAGAL -DENACMP -DNFREQ=3
LDFLAGS = -Wl,--gc-sections
LDLIBS = -lm -lpthread
SOURCES = test_rinex4_glo_l3oc.c $(SRC)/rinex.c $(SRC)/rtkcmn.c

all: test_rinex4_glo_l3oc

test_rinex4_glo_l3oc: $(SOURCES)
	$(CC) $(CFLAGS) $(SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

test: test_rinex4_glo_l3oc
	./test_rinex4_glo_l3oc

clean:
	rm -f test_rinex4_glo_l3oc test_rinex4_glo_l3oc.exe rinex4_glo_l3oc_fixture.rnx

.PHONY: all test clean
