/*
 * dmk_web.c - Thin embedding wrapper exposing the engine as string-in/string-out
 * for a browser (WASM) or any host. Takes a scenario JSON, runs the model, and
 * returns GeoJSON (effect rings + fallout) or the full JSON report.
 *
 * Build to WASM with emscripten (see `make wasm`); also compiles with a normal
 * C compiler for host-side testing. The returned buffer is malloc'd; the caller
 * frees it via dmk_web_free (WASM: Module._dmk_web_free).
 */
#include "damaskino.h"
#include "scenario.h"
#include "output_report.h"
#include "casualties.h"
#include "json.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define DMK_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define DMK_EXPORT
#endif

static char *run_common(const char *scenario_json, double pop_density,
                        double pf_fallout, int want_geojson) {
    DmkScenario sc; char err[256];
    if (dmk_scenario_parse(scenario_json, &sc, err, sizeof err) != 0) {
        /* Return a JSON error object so the UI can display it. */
        char *msg = (char *)malloc(320);
        if (msg) snprintf(msg, 320, "{\"error\":\"%s\"}", err);
        return msg;
    }
    DmkModel model;
    if (dmk_run(&sc, &model) != 0) {
        char *msg = (char *)malloc(64);
        if (msg) snprintf(msg, 64, "{\"error\":\"engine failed\"}");
        return msg;
    }

    DmkPopulation pop; int have_pop = 0;
    DmkCasualties cas; int have_cas = 0;
    DmkCasualtyOpts opts; dmk_casualty_opts_defaults(&opts);
    if (pf_fallout > 0) opts.pf_fallout = pf_fallout;
    if (pop_density > 0) {
        dmk_pop_init_uniform(&pop, pop_density, 1.0);
        have_pop = 1;
        dmk_casualties_compute(&model, &pop, &opts, &cas);
        have_cas = 1;
    }

    JsonWriter w; json_writer_init(&w, want_geojson ? 0 : 1);
    if (want_geojson) dmk_write_report_geojson(&model, &opts, 1.0, &w);
    else dmk_write_report_json(&model, have_pop ? &pop : NULL, &opts,
                               have_cas ? &cas : NULL, &w);
    char *out = json_writer_take(&w);
    json_writer_free(&w);
    if (have_pop) dmk_pop_free(&pop);
    dmk_model_free(&model);
    return out;
}

DMK_EXPORT char *dmk_web_geojson(const char *scenario_json, double pop_density, double pf_fallout) {
    return run_common(scenario_json, pop_density, pf_fallout, 1);
}

DMK_EXPORT char *dmk_web_report(const char *scenario_json, double pop_density, double pf_fallout) {
    return run_common(scenario_json, pop_density, pf_fallout, 0);
}

DMK_EXPORT void dmk_web_free(char *p) { free(p); }

DMK_EXPORT const char *dmk_web_version(void) { return DMK_VERSION_STRING; }
