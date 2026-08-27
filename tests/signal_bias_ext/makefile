SRC = ../../src
CFLAGS = -O2 -std=c99 -Wall -Wextra -pedantic -I$(SRC) \
         -DENAGLO -DENAQZS -DENAGAL -DENACMP -DNFREQ=3
LDLIBS = -lm -lpthread

all: test_signal_bias_ext test_glo_signal_bias

test_signal_bias_ext: test_signal_bias_ext.c $(SRC)/rtkcmn.c
	$(CC) $(CFLAGS) test_signal_bias_ext.c $(SRC)/rtkcmn.c $(LDLIBS) -o $@

test_glo_signal_bias: test_glo_signal_bias.c $(SRC)/rtkcmn.c
	$(CC) $(CFLAGS) test_glo_signal_bias.c $(SRC)/rtkcmn.c $(LDLIBS) -o $@

test: all
	./test_signal_bias_ext
	./test_glo_signal_bias

clean:
	rm -f test_signal_bias_ext test_signal_bias_ext.exe test_glo_signal_bias test_glo_signal_bias.exe

.PHONY: all test clean
