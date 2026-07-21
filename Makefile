# Damaskino build system
#
#   make            -> full profile CLI (build/damaskino)
#   make univac     -> UNIVAC-lite teletype build (build/damaskino_univac)
#   make test       -> build and run unit tests
#   make clean
#
# The full and lite profiles share one engine core; the lite profile is
# selected with -DUNIVAC (float precision, no JSON/weather/DEM).

CC      ?= cc
CSTD    ?= -std=c11
WARN     = -Wall -Wextra -Wno-unused-parameter
OPT     ?= -O2
CFLAGS   = $(CSTD) $(WARN) $(OPT) -Iinclude -Isrc/json -Isrc/io -Isrc/engine
LDLIBS   = -lm

BUILD    = build

# Shared engine sources (compile in both profiles)
ENGINE_SRC = \
	src/engine/physics.c \
	src/engine/fallout.c \
	src/engine/engine.c \
	src/io/output_teletype.c

# Full-profile-only sources (JSON / scenario / structured output)
FULL_SRC = \
	$(ENGINE_SRC) \
	src/json/json.c \
	src/io/output.c \
	src/io/scenario.c \
	src/cli/main.c

UNIVAC_SRC = \
	$(ENGINE_SRC) \
	src/univac/main.c

.PHONY: all full univac test clean dirs

all: full

dirs:
	@mkdir -p $(BUILD)

full: dirs
	$(CC) $(CFLAGS) $(FULL_SRC) -o $(BUILD)/damaskino $(LDLIBS)
	@echo "Built $(BUILD)/damaskino (full profile)"

univac: dirs
	$(CC) $(CFLAGS) -DUNIVAC $(UNIVAC_SRC) -o $(BUILD)/damaskino_univac $(LDLIBS)
	@echo "Built $(BUILD)/damaskino_univac (UNIVAC-lite profile)"

test: dirs
	$(CC) $(CFLAGS) \
		src/engine/physics.c src/engine/fallout.c src/engine/engine.c \
		src/json/json.c src/io/output.c src/io/scenario.c src/io/output_teletype.c \
		tests/test_engine.c -o $(BUILD)/test_engine $(LDLIBS)
	./$(BUILD)/test_engine

clean:
	rm -rf $(BUILD)
