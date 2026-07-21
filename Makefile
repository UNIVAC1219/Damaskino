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
	src/engine/ensemble.c \
	src/weather/weather.c \
	src/json/json.c \
	src/io/output.c \
	src/io/output_report.c \
	src/io/scenario.c \
	src/io/catalog.c \
	src/io/validate.c \
	src/cli/main.c

UNIVAC_SRC = \
	$(ENGINE_SRC) \
	src/univac/main.c

.PHONY: all full univac test clean dirs wasm example

# WASM build (emscripten): engine as string-in/string-out for the web frontend.
WASM_SRC = \
	src/engine/physics.c src/engine/fallout.c src/engine/terrain.c \
	src/engine/engine.c src/engine/effects.c src/engine/casualties.c \
	src/engine/lagrangian.c src/weather/weather.c src/json/json.c \
	src/io/output.c src/io/output_report.c src/io/scenario.c \
	src/io/catalog.c src/io/output_teletype.c src/web/dmk_web.c
WASM_INC = -Iinclude -Isrc/json -Isrc/io -Isrc/engine -Isrc/weather

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
	src/engine/ensemble.c src/weather/weather.c \
	src/json/json.c src/io/output.c src/io/output_report.c \
	src/io/scenario.c src/io/catalog.c src/io/validate.c src/io/output_teletype.c

test: dirs
	$(CC) $(CFLAGS) $(ENGINE_OBJS_FOR_TEST) tests/test_engine.c -o $(BUILD)/test_engine $(LDLIBS)
	$(CC) $(CFLAGS) src/engine/effects.c tests/test_effects.c -o $(BUILD)/test_effects $(LDLIBS)
	$(CC) $(CFLAGS) $(ENGINE_OBJS_FOR_TEST) tests/test_casualties.c -o $(BUILD)/test_casualties $(LDLIBS)
	$(CC) $(CFLAGS) src/engine/terrain.c src/engine/effects.c tests/test_terrain.c -o $(BUILD)/test_terrain $(LDLIBS)
	$(CC) $(CFLAGS) $(ENGINE_OBJS_FOR_TEST) tests/test_lagrangian.c -o $(BUILD)/test_lagrangian $(LDLIBS)
	$(CC) $(CFLAGS) $(ENGINE_OBJS_FOR_TEST) tests/test_ensemble.c -o $(BUILD)/test_ensemble $(LDLIBS)
	./$(BUILD)/test_engine
	./$(BUILD)/test_effects
	./$(BUILD)/test_casualties
	./$(BUILD)/test_terrain
	./$(BUILD)/test_lagrangian
	./$(BUILD)/test_ensemble

wasm:
	@command -v emcc >/dev/null 2>&1 || { echo "emcc not found; install emscripten (https://emscripten.org) to build the live web engine"; exit 1; }
	emcc $(WASM_SRC) $(WASM_INC) -O2 \
	  -s EXPORTED_FUNCTIONS='["_dmk_web_geojson","_dmk_web_report","_dmk_web_free","_dmk_web_version","_malloc","_free"]' \
	  -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","UTF8ToString","stringToUTF8","lengthBytesUTF8"]' \
	  -s ALLOW_MEMORY_GROWTH=1 -s MODULARIZE=1 -s EXPORT_NAME=Damaskino \
	  -o web/damaskino.js
	@echo "Built web/damaskino.js + web/damaskino.wasm (live in-browser engine)"

# Generate the example GeoJSON the static viewer loads by default.
example: full
	@mkdir -p web
	./$(BUILD)/damaskino run examples/dc_500kt_surface.json --pop-density 4000 \
	  --quiet --geojson web/example.geojson
	@echo "Wrote web/example.geojson"

clean:
	rm -rf $(BUILD)
