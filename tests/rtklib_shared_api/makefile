SRC = ../../src
LIBDIR = ../../lib/rtklib_shared/gcc
LIB = $(LIBDIR)/librtklib_shared.a
CFLAGS = -O2 -std=c99 -Wall -Wextra -pedantic -I$(SRC) \
         -DENAGLO -DENAQZS -DENAGAL -DENACMP -DNFREQ=3
CXXFLAGS = -O2 -std=c++11 -Wall -Wextra -pedantic -I$(SRC)
LDLIBS = -lm -lpthread
LOCAL_LDFLAGS =
FIXTURE = fixtures/brd400_selected.rnx
PROVENANCE = fixtures/brd400_selected.provenance
BOUNDARY_FIXTURE = fixtures/dlf100_g02_week_boundary.rnx
RINEX2_SVA_FIXTURE = ../../test/data/rinex/brdc1820.10n

TESTS = test_rtklib_shared_api test_public_c test_public_cpp \
	test_rinex2_legacy_sva_metric test_public_legacy_sva_metric \
	test_modern_urai_private_parse test_modern_urai_containment

all: $(TESTS)

lib:
	$(MAKE) -C $(LIBDIR)

test_rtklib_shared_api: test_rtklib_shared_api.c lib
	$(CC) $(CFLAGS) $< $(LIB) $(LOCAL_LDFLAGS) $(LDFLAGS) $(LDLIBS) -o $@

test_public_c: test_public_c.c lib
	$(CC) $(CFLAGS) $< $(LIB) $(LOCAL_LDFLAGS) $(LDFLAGS) $(LDLIBS) -o $@

test_public_cpp: test_public_cpp.cpp lib
	$(CXX) $(CXXFLAGS) $< $(LIB) $(LOCAL_LDFLAGS) $(LDFLAGS) $(LDLIBS) -o $@

test_rinex2_legacy_sva_metric: test_rinex2_legacy_sva_metric.c lib
	$(CC) $(CFLAGS) $< $(LIB) $(LOCAL_LDFLAGS) $(LDFLAGS) $(LDLIBS) -o $@

test_public_legacy_sva_metric: test_public_legacy_sva_metric.c lib
	$(CC) $(CFLAGS) $< $(LIB) $(LOCAL_LDFLAGS) $(LDFLAGS) $(LDLIBS) -o $@

test_modern_urai_private_parse: test_modern_urai_private_parse.c lib
	$(CC) $(CFLAGS) $< $(LIB) $(LDLIBS) -o $@

test_modern_urai_containment: test_modern_urai_containment.c lib
	$(CC) $(CFLAGS) $< $(LIB) $(LDLIBS) -o $@

test: all
	./test_public_c
	./test_public_cpp
	./test_rtklib_shared_api $(FIXTURE)
	./test_rtklib_shared_api $(BOUNDARY_FIXTURE)
	./test_rinex2_legacy_sva_metric $(RINEX2_SVA_FIXTURE)
	./test_public_legacy_sva_metric $(RINEX2_SVA_FIXTURE)
	./test_modern_urai_private_parse $(FIXTURE) $(PROVENANCE)
	./test_modern_urai_containment $(FIXTURE)

clean:
	rm -f $(TESTS) $(addsuffix .exe,$(TESTS))

.PHONY: all lib test clean
