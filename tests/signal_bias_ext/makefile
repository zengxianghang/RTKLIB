SRC = ../../src
CFLAGS = -O2 -std=c99 -Wall -Wextra -pedantic -I$(SRC) \
         -DENAGLO -DENAQZS -DENAGAL -DENACMP -DNFREQ=3
LDLIBS = -lm -lpthread

all: test_signal_bias_ext

test_signal_bias_ext: test_signal_bias_ext.c $(SRC)/rtkcmn.c
	$(CC) $(CFLAGS) test_signal_bias_ext.c $(SRC)/rtkcmn.c $(LDLIBS) -o $@

test: test_signal_bias_ext
	./test_signal_bias_ext

clean:
	rm -f test_signal_bias_ext test_signal_bias_ext.exe

.PHONY: all test clean
