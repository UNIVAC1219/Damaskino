/*
 * main.c - Damaskino full-profile command-line driver.
 *
 * Usage:
 *   damaskino run <scenario.json> [--json out.json] [--geojson out.geojson]
 *                                 [--threshold R/hr] [--quiet]
 *   damaskino version
 *
 * Reads a scenario, runs the engine, prints a teletype report, and optionally
 * writes machine-readable JSON and GeoJSON.
 */
#include "damaskino.h"
#include "scenario.h"
#include "output.h"
#include "json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int write_text_file(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "ERROR: cannot open %s for writing\n", path); return -1; }
    fputs(text, f);
    fclose(f);
    return 0;
}

static int cmd_run(int argc, char **argv) {
    const char *scenario_path = NULL, *json_out = NULL, *geojson_out = NULL;
    real_t threshold = 1.0;
    int quiet = 0;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0 && i+1 < argc) json_out = argv[++i];
        else if (strcmp(argv[i], "--geojson") == 0 && i+1 < argc) geojson_out = argv[++i];
        else if (strcmp(argv[i], "--threshold") == 0 && i+1 < argc) threshold = atof(argv[++i]);
        else if (strcmp(argv[i], "--quiet") == 0) quiet = 1;
        else if (argv[i][0] != '-') scenario_path = argv[i];
        else { fprintf(stderr, "Unknown option: %s\n", argv[i]); return 2; }
    }
    if (!scenario_path) { fprintf(stderr, "ERROR: no scenario file given\n"); return 2; }

    char *text = dmk_read_file(scenario_path);
    if (!text) { fprintf(stderr, "ERROR: cannot read %s\n", scenario_path); return 1; }

    DmkScenario sc;
    char err[256];
    if (dmk_scenario_parse(text, &sc, err, sizeof err) != 0) {
        fprintf(stderr, "ERROR: %s\n", err);
        free(text);
        return 1;
    }
    free(text);

    DmkModel model;
    int rc = dmk_run(&sc, &model);
    if (rc != 0) {
        fprintf(stderr, "ERROR: engine failed (code %d)\n", rc);
        return 1;
    }

    if (!quiet) dmk_write_teletype(&model, stdout);

    if (json_out) {
        JsonWriter w; json_writer_init(&w, 1);
        dmk_write_result_json(&model, &w);
        if (w.err) { fprintf(stderr, "ERROR: JSON writer OOM\n"); }
        else if (write_text_file(json_out, w.buf) == 0 && !quiet)
            fprintf(stderr, "Wrote %s\n", json_out);
        json_writer_free(&w);
    }
    if (geojson_out) {
        JsonWriter w; json_writer_init(&w, 0);
        dmk_write_geojson(&model, &w, threshold);
        if (w.err) { fprintf(stderr, "ERROR: GeoJSON writer OOM\n"); }
        else if (write_text_file(geojson_out, w.buf) == 0 && !quiet)
            fprintf(stderr, "Wrote %s\n", geojson_out);
        json_writer_free(&w);
    }

    dmk_model_free(&model);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,
            "Damaskino %s - nuclear-effects simulator (civil-defense / education)\n"
            "Usage:\n"
            "  %s run <scenario.json> [--json out.json] [--geojson out.geojson]\n"
            "                         [--threshold R/hr] [--quiet]\n"
            "  %s version\n",
            DMK_VERSION_STRING, argv[0], argv[0]);
        return 2;
    }
    if (strcmp(argv[1], "version") == 0) {
        printf("damaskino %s\n", DMK_VERSION_STRING);
        return 0;
    }
    if (strcmp(argv[1], "run") == 0)
        return cmd_run(argc - 2, argv + 2);

    fprintf(stderr, "Unknown command: %s\n", argv[1]);
    return 2;
}
