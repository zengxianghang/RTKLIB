SRC = ../../src
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
LOCAL_LDFLAGS =
else
LOCAL_LDFLAGS = -Wl,--gc-sections
endif
CFLAGS = -O2 -std=c99 -Wall -Wextra -pedantic -ffunction-sections -fdata-sections -I$(SRC)
LDLIBS = -lm -lpthread
SOURCES = test_nav_sva_serialization.c $(SRC)/rtkcmn.c

all: test_nav_sva_serialization

test_nav_sva_serialization: $(SOURCES)
	$(CC) $(CFLAGS) $(SOURCES) $(LOCAL_LDFLAGS) $(LDFLAGS) $(LDLIBS) -o $@

test: all
	./test_nav_sva_serialization

clean:
	rm -f test_nav_sva_serialization test_nav_sva_serialization.exe \
	      test_nav_sva_serialization.tmp

.PHONY: all test clean
