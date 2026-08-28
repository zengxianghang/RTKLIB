SRC = ../../src
CFLAGS = -O2 -std=c99 -Wall -Wextra -pedantic -ffunction-sections -fdata-sections -I$(SRC)
LDFLAGS = -Wl,--gc-sections
LDLIBS = -lm -lpthread
SOURCES = test_nav_sva_serialization.c $(SRC)/rtkcmn.c

all: test_nav_sva_serialization

test_nav_sva_serialization: $(SOURCES)
	$(CC) $(CFLAGS) $(SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

test: all
	./test_nav_sva_serialization

clean:
	rm -f test_nav_sva_serialization test_nav_sva_serialization.exe \
	      test_nav_sva_serialization.tmp

.PHONY: all test clean
