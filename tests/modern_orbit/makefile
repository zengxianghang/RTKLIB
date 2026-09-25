SRC = ../../src
LIBDIR = ../../lib/rtklib_shared/gcc
LIB = $(LIBDIR)/librtklib_shared.a
CFLAGS = -O2 -std=c99 -Wall -Wextra -pedantic -I$(SRC) \
         -DENAGLO -DENAQZS -DENAGAL -DENACMP -DNFREQ=3
LDLIBS = -lm -lpthread
FIXTURE = fixtures/brd400_2024030_modern_orbit.rnx
ORACLE = fixtures/oracle_vectors.csv
BASELINE = fixtures/legacy_baseline.csv
URA_ORACLE = fixtures/ura_oracle_vectors.csv

TESTS = test_modern_orbit_terms test_shared_modern_orbit test_modern_ura \
	test_signal_family

all: $(TESTS)

lib:
	$(MAKE) -C $(LIBDIR)

test_modern_orbit_terms: test_modern_orbit_terms.c lib
	$(CC) $(CFLAGS) $< $(LIB) $(LDFLAGS) $(LDLIBS) -o $@

test_shared_modern_orbit: test_shared_modern_orbit.c lib
	$(CC) $(CFLAGS) $< $(LIB) $(LDFLAGS) $(LDLIBS) -o $@

test_modern_ura: test_modern_ura.c lib
	$(CC) $(CFLAGS) $< $(LIB) $(LDFLAGS) $(LDLIBS) -o $@

test_signal_family: test_signal_family.c lib
	$(CC) $(CFLAGS) $< $(LIB) $(LDFLAGS) $(LDLIBS) -o $@

test: all
	./test_modern_orbit_terms $(FIXTURE) $(ORACLE) $(BASELINE)
	./test_shared_modern_orbit $(FIXTURE) $(ORACLE)
	./test_modern_ura $(FIXTURE) $(URA_ORACLE)
	./test_signal_family $(FIXTURE)

# Regenerate the independent oracle vectors (needs python3).
oracle:
	python3 tools/oracle_vectors.py $(FIXTURE) $(ORACLE)
	cd tools && python3 ura_oracle.py ../$(FIXTURE) --output ../$(URA_ORACLE)

clean:
	rm -f $(TESTS) $(addsuffix .exe,$(TESTS))

.PHONY: all lib test oracle clean
