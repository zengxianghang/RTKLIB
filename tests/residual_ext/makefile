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
HANDOVER_SOURCES = test_transmit_epoch_handover.c unused_correction_stubs.c \
          $(SRC)/rtkcmn.c $(SRC)/ephemeris.c $(SRC)/pntpos.c \
          $(SRC)/rtklib_signal_state_ext.c $(SRC)/rtklib_residual_ext.c

all: test_residual_ext test_glo_l3oc_residual test_external_state_residual \
     test_transmit_epoch_handover

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

test_transmit_epoch_handover: $(HANDOVER_SOURCES)
	$(CC) $(CFLAGS) $(HANDOVER_SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

test: all
	./test_residual_ext
	./test_glo_l3oc_residual
	./test_external_state_residual
	./test_transmit_epoch_handover

clean:
	rm -f test_residual_ext test_residual_ext.exe test_glo_l3oc_residual \
	      test_glo_l3oc_residual.exe test_external_state_residual \
	      test_external_state_residual.exe test_transmit_epoch_handover \
	      test_transmit_epoch_handover.exe

.PHONY: all test clean
