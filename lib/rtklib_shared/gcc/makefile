# Build the versioned downstream-facing RTKLIB shared adapter archive.
# This target is additive: it does not replace any existing RTKLIB executable.

SRC = ../../../src
BUILD = .build
LIB = librtklib_shared.a
PIC_BUILD = .picbuild
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
SHARED = librtklib_shared.dylib
SHARED_LDFLAGS = -dynamiclib
else
SHARED = librtklib_shared.so
SHARED_LDFLAGS = -shared
endif

CORE = $(SRC)/rtkcmn.c $(SRC)/rinex.c $(SRC)/ephemeris.c \
	$(SRC)/preceph.c $(SRC)/qzslex.c $(SRC)/sbas.c $(SRC)/rtcm.c
CORE += $(SRC)/rtcm2.c $(SRC)/rtcm3.c $(SRC)/rtcm3e.c
EXT = $(SRC)/rtklib_signal_state_ext.c $(SRC)/rtklib_obs_ext.c
ADAPTER = $(SRC)/rtklib_shared_api.c
SOURCES = $(CORE) $(EXT) $(ADAPTER)
OBJECTS = $(patsubst $(SRC)/%.c,$(BUILD)/%.o,$(SOURCES))
PIC_OBJECTS = $(patsubst $(SRC)/%.c,$(PIC_BUILD)/%.o,$(SOURCES))

CC ?= gcc
AR ?= ar
CFLAGS ?= -O2 -std=c99 -Wall -Wextra -pedantic
CFLAGS += -I$(SRC) -DENAGLO -DENAQZS -DENAGAL -DENACMP -DNFREQ=3

all: $(LIB)

shared: $(SHARED)

$(LIB): $(OBJECTS)
	$(AR) rcs $@ $^

$(BUILD)/%.o: $(SRC)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(SHARED): $(PIC_OBJECTS)
	$(CC) $(SHARED_LDFLAGS) $(LDFLAGS) -o $@ $^ -lm -lpthread

$(PIC_BUILD)/%.o: $(SRC)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

clean:
	rm -rf $(BUILD) $(PIC_BUILD) $(LIB) $(SHARED)

.PHONY: all shared clean
