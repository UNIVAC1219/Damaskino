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
CFLAGS   = $(CSTD) $(WARN) $(OPT) -Iinclude -Isrc/json -Isrc/io -Isrc/engine -Isrc/weather
LDLIBS   = -lm

BUILD    = build

# Shared engine sources (compile in both profiles)
ENGINE_SRC = \
	src/engine/physics.c \
	src/engine/fallout.c \
	src/engine/terrain.c \
	src/engine/engine.c \
	src/io/output_teletype.c

# Full-profile-only sources (JSON / scenario / effects / casualties / output)
FULL_SRC = \
	$(ENGINE_SRC) \
	src/engine/effects.c \
	src/engine/casualties.c \
	src/engine/lagrangian.c \
	src/weather/weather.c \
	src/json/json.c \
	src/io/output.c \
	src/io/output_report.c \
	src/io/scenario.c \
	src/io/catalog.c \
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

ENGINE_OBJS_FOR_TEST = \
	src/engine/physics.c src/engine/fallout.c src/engine/terrain.c src/engine/engine.c \
	src/engine/effects.c src/engine/casualties.c src/engine/lagrangian.c \
	src/weather/weather.c \
	src/json/json.c src/io/output.c src/io/output_report.c \
	src/io/scenario.c src/io/catalog.c src/io/output_teletype.c

test: dirs
	$(CC) $(CFLAGS) $(ENGINE_OBJS_FOR_TEST) tests/test_engine.c -o $(BUILD)/test_engine $(LDLIBS)
	$(CC) $(CFLAGS) src/engine/effects.c tests/test_effects.c -o $(BUILD)/test_effects $(LDLIBS)
	$(CC) $(CFLAGS) $(ENGINE_OBJS_FOR_TEST) tests/test_casualties.c -o $(BUILD)/test_casualties $(LDLIBS)
	$(CC) $(CFLAGS) src/engine/terrain.c src/engine/effects.c tests/test_terrain.c -o $(BUILD)/test_terrain $(LDLIBS)
	$(CC) $(CFLAGS) $(ENGINE_OBJS_FOR_TEST) tests/test_lagrangian.c -o $(BUILD)/test_lagrangian $(LDLIBS)
	./$(BUILD)/test_engine
	./$(BUILD)/test_effects
	./$(BUILD)/test_casualties
	./$(BUILD)/test_terrain
	./$(BUILD)/test_lagrangian

clean:
	rm -rf $(BUILD)
