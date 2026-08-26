SRC = ../../src
CFLAGS = -O2 -std=c99 -Wall -Wextra -pedantic -I$(SRC)
LDLIBS = -lm -lpthread

all: test_obs_codes_ext

test_obs_codes_ext: test_obs_codes_ext.c $(SRC)/rtklib_obs_ext.c $(SRC)/rtkcmn.c
	$(CC) $(CFLAGS) test_obs_codes_ext.c $(SRC)/rtklib_obs_ext.c $(SRC)/rtkcmn.c $(LDLIBS) -o $@

test: test_obs_codes_ext
	./test_obs_codes_ext

clean:
	rm -f test_obs_codes_ext test_obs_codes_ext.exe

.PHONY: all test clean
