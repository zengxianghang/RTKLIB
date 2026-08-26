SRC = ../../src
CFLAGS = -O2 -std=c99 -Wall -Wextra -pedantic -I$(SRC)
LDLIBS = -lm -lpthread

all: test_pntvel_ext

test_pntvel_ext: test_pntvel_ext.c $(SRC)/rtklib_pntvel_ext.c $(SRC)/rtkcmn.c
	$(CC) $(CFLAGS) test_pntvel_ext.c $(SRC)/rtklib_pntvel_ext.c $(SRC)/rtkcmn.c $(LDLIBS) -o $@

test: test_pntvel_ext
	./test_pntvel_ext

clean:
	rm -f test_pntvel_ext test_pntvel_ext.exe

.PHONY: all test clean
