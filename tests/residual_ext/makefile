SRC = ../../src
CFLAGS = -O2 -std=c99 -Wall -Wextra -pedantic -ffunction-sections -fdata-sections \
         -I$(SRC) -DENAGLO -DENAQZS -DENAGAL -DENACMP -DNFREQ=3
LDFLAGS = -Wl,--gc-sections
LDLIBS = -lm -lpthread
SOURCES = test_residual_ext.c unused_correction_stubs.c $(SRC)/rtkcmn.c \
          $(SRC)/ephemeris.c $(SRC)/pntpos.c $(SRC)/rtklib_signal_state_ext.c \
          $(SRC)/rtklib_residual_ext.c
EXTERNAL_STATE_SOURCES = test_external_state_residual.c unused_correction_stubs.c \
          $(SRC)/rtkcmn.c $(SRC)/ephemeris.c $(SRC)/pntpos.c \
          $(SRC)/rtklib_signal_state_ext.c $(SRC)/rtklib_residual_ext.c

all: test_residual_ext test_glo_l3oc_residual test_external_state_residual

test_residual_ext: $(SOURCES)
	$(CC) $(CFLAGS) $(SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

test_glo_l3oc_residual: test_glo_l3oc_residual.c unused_correction_stubs.c $(SRC)/rtkcmn.c \
          $(SRC)/ephemeris.c $(SRC)/pntpos.c $(SRC)/rtklib_signal_state_ext.c \
          $(SRC)/rtklib_residual_ext.c
	$(CC) $(CFLAGS) test_glo_l3oc_residual.c unused_correction_stubs.c $(SRC)/rtkcmn.c \
          $(SRC)/ephemeris.c $(SRC)/pntpos.c $(SRC)/rtklib_signal_state_ext.c \
          $(SRC)/rtklib_residual_ext.c $(LDFLAGS) $(LDLIBS) -o $@

test_external_state_residual: $(EXTERNAL_STATE_SOURCES)
	$(CC) $(CFLAGS) $(EXTERNAL_STATE_SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

test: all
	./test_residual_ext
	./test_glo_l3oc_residual
	./test_external_state_residual

clean:
	rm -f test_residual_ext test_residual_ext.exe test_glo_l3oc_residual \
	      test_glo_l3oc_residual.exe test_external_state_residual \
	      test_external_state_residual.exe

.PHONY: all test clean
