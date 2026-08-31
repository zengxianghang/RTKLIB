SRC = ../../src
LIBDIR = ../../lib/rtklib_shared/gcc
LIB = $(LIBDIR)/librtklib_shared.a
CFLAGS = -O2 -std=c99 -Wall -Wextra -pedantic -I$(SRC) \
         -DENAGLO -DENAQZS -DENAGAL -DENACMP -DNFREQ=3
CXXFLAGS = -O2 -std=c++11 -Wall -Wextra -pedantic -I$(SRC)
LDLIBS = -lm -lpthread
FIXTURE = fixtures/brd400_selected.rnx
BOUNDARY_FIXTURE = fixtures/dlf100_g02_week_boundary.rnx

TESTS = test_rtklib_shared_api test_public_c test_public_cpp

all: $(TESTS)

lib:
	$(MAKE) -C $(LIBDIR)

test_rtklib_shared_api: test_rtklib_shared_api.c lib
	$(CC) $(CFLAGS) $< $(LIB) $(LDLIBS) -o $@

test_public_c: test_public_c.c lib
	$(CC) $(CFLAGS) $< $(LIB) $(LDLIBS) -o $@

test_public_cpp: test_public_cpp.cpp lib
	$(CXX) $(CXXFLAGS) $< $(LIB) $(LDLIBS) -o $@

test: all
	./test_public_c
	./test_public_cpp
	./test_rtklib_shared_api $(FIXTURE)
	./test_rtklib_shared_api $(BOUNDARY_FIXTURE)

clean:
	rm -f $(TESTS) $(addsuffix .exe,$(TESTS))

.PHONY: all lib test clean
