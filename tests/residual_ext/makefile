SRC = ../../src
CFLAGS = -O2 -std=c99 -Wall -Wextra -pedantic -ffunction-sections -fdata-sections \
         -I$(SRC) -DENAGLO -DENAQZS -DENAGAL -DENACMP -DNFREQ=3
LDFLAGS = -Wl,--gc-sections
LDLIBS = -lm -lpthread
SOURCES = test_residual_ext.c $(SRC)/rtkcmn.c $(SRC)/ephemeris.c \
          $(SRC)/pntpos.c $(SRC)/rtklib_signal_state_ext.c \
          $(SRC)/rtklib_residual_ext.c

all: test_residual_ext

test_residual_ext: $(SOURCES)
	$(CC) $(CFLAGS) $(SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

test: test_residual_ext
	./test_residual_ext

clean:
	rm -f test_residual_ext test_residual_ext.exe

.PHONY: all test clean
