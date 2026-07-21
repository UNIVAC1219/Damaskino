/*
 * main.c - Damaskino full-profile command-line driver.
 *
 * Usage:
 *   damaskino run <scenario.json> [options]
 *     --json FILE         write comprehensive JSON report
 *     --geojson FILE      write GeoJSON (effect rings + fallout cells)
 *     --threshold R/hr    fallout cell threshold for GeoJSON (default 1)
 *     --visibility KM     atmospheric visibility for thermal (default 20)
 *     --pop-density N     uniform population density (people/km^2) -> casualties
 *     --pop-asc FILE      population raster (ESRI ASCII grid) -> casualties
 *     --pf N              sheltering protection factor (default 1)
 *     --thermal-exposed F fraction with line-of-sight to fireball (default 0.5)
 *     --exposure H        fallout dose window in hours (default 48)
 *     --time day|night    time-of-day population multiplier
 *     --quiet             suppress teletype report
 *   damaskino version
 */
#include "damaskino.h"
#include "scenario.h"
#include "output.h"
#include "output_report.h"
#include "effects.h"
#include "casualties.h"
#include "catalog.h"
#include "terrain.h"
#include "weather.h"
#include "ensemble.h"
#include "validate.h"
#include "json.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dmk_portable.h"

static int write_text_file(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "ERROR: cannot open %s for writing\n", path); return -1; }
    fputs(text, f);
    fclose(f);
    return 0;
}

static int cmd_run(int argc, char **argv) {
    const char *scenario_path = NULL, *json_out = NULL, *geojson_out = NULL;
    const char *pop_asc = NULL, *dem_path = NULL, *weather_path = NULL;
    real_t threshold = 1.0, pop_density = 0.0;
    int quiet = 0, want_casualties = 0;
    DmkCasualtyOpts opts; dmk_casualty_opts_defaults(&opts);
    real_t day_night = 1.0;

    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--json") && i+1 < argc) json_out = argv[++i];
        else if (!strcmp(argv[i], "--geojson") && i+1 < argc) geojson_out = argv[++i];
        else if (!strcmp(argv[i], "--threshold") && i+1 < argc) threshold = atof(argv[++i]);
        else if (!strcmp(argv[i], "--visibility") && i+1 < argc) opts.visibility_km = atof(argv[++i]);
        else if (!strcmp(argv[i], "--pop-density") && i+1 < argc) { pop_density = atof(argv[++i]); want_casualties = 1; }
        else if (!strcmp(argv[i], "--pop-asc") && i+1 < argc) { pop_asc = argv[++i]; want_casualties = 1; }
        else if (!strcmp(argv[i], "--dem") && i+1 < argc) dem_path = argv[++i];
        else if (!strcmp(argv[i], "--weather") && i+1 < argc) weather_path = argv[++i];
        else if (!strcmp(argv[i], "--pf") && i+1 < argc) opts.pf_fallout = atof(argv[++i]);
        else if (!strcmp(argv[i], "--pf-prompt") && i+1 < argc) opts.pf_prompt = atof(argv[++i]);
        else if (!strcmp(argv[i], "--thermal-exposed") && i+1 < argc) opts.thermal_exposed_frac = atof(argv[++i]);
        else if (!strcmp(argv[i], "--exposure") && i+1 < argc) opts.exposure_hours = atof(argv[++i]);
        else if (!strcmp(argv[i], "--time") && i+1 < argc) {
            const char *t = argv[++i];
            day_night = (!strcmp(t, "day")) ? 1.4 : (!strcmp(t, "night")) ? 0.8 : 1.0;
        }
        else if (!strcmp(argv[i], "--quiet")) quiet = 1;
        else if (argv[i][0] != '-') scenario_path = argv[i];
        else { fprintf(stderr, "Unknown option: %s\n", argv[i]); return 2; }
    }
    if (!scenario_path) { fprintf(stderr, "ERROR: no scenario file given\n"); return 2; }

    char *text = dmk_read_file(scenario_path);
    if (!text) { fprintf(stderr, "ERROR: cannot read %s\n", scenario_path); return 1; }

    DmkScenario sc; char err[256];
    if (dmk_scenario_parse(text, &sc, err, sizeof err) != 0) {
        fprintf(stderr, "ERROR: %s\n", err); free(text); return 1;
    }
    free(text);

    /* Optional terrain DEM */
    DmkDem dem; int have_dem = 0;
    if (dem_path) {
        if (dmk_dem_load_asc(&dem, dem_path) != 0)
            fprintf(stderr, "WARNING: could not load DEM %s; using flat terrain\n", dem_path);
        else { have_dem = 1; if (!quiet) fprintf(stderr, "Loaded DEM %s (%dx%d)\n", dem_path, dem.ncols, dem.nrows); }
    }

    /* Optional real weather (enables the Lagrangian fallout model) */
    DmkWindColumn wind; int have_wind = 0;
    if (weather_path) {
        if (dmk_weather_load(weather_path, &wind) != 0)
            fprintf(stderr, "WARNING: could not load weather %s; using profile winds (WSEG)\n", weather_path);
        else { have_wind = 1; if (!quiet) fprintf(stderr, "Loaded weather %s (%s, %d levels, %.1f mm/hr precip)\n",
                                                  weather_path, wind.source, wind.nlev, (double)wind.precip_mm_hr); }
    }

    DmkModel model;
    int rc = dmk_run_full(&sc, have_dem ? &dem : NULL, have_wind ? &wind : NULL, &model);
    if (rc != 0) {
        const char *why = (rc == -3) ? "invalid physical parameters"
                        : (rc == -2) ? "grid allocation failed" : "bad arguments";
        fprintf(stderr, "ERROR: engine failed (code %d: %s)\n", rc, why);
        if (have_dem) dmk_dem_free(&dem);
        if (have_wind) dmk_weather_free(&wind);
        return 1;
    }

    /* Casualties (optional) */
    DmkPopulation pop; int have_pop = 0;
    DmkCasualties cas; int have_cas = 0;
    if (want_casualties) {
        if (pop_asc) {
            if (dmk_pop_load_asc(&pop, pop_asc) != 0) {
                fprintf(stderr, "WARNING: could not load population raster %s; skipping casualties\n", pop_asc);
            } else { pop.day_night_factor = day_night; have_pop = 1; }
        } else {
            dmk_pop_init_uniform(&pop, pop_density, day_night);
            have_pop = 1;
            if (!quiet)
                fprintf(stderr, "NOTE: uniform population (%.0f/km^2) fills the entire "
                        "%.0f x %.0f km domain (incl. water/rural). Use --pop-asc for real "
                        "counts.\n", (double)pop_density,
                        (double)(sc.cfg.grid_n * sc.cfg.cell_km),
                        (double)(sc.cfg.grid_n * sc.cfg.cell_km));
        }
        if (have_pop) {
            dmk_casualties_compute(&model, &pop, &opts, &cas);
            have_cas = 1;
        }
    }

    if (!quiet)
        dmk_write_report_teletype(&model, &opts, have_cas ? &cas : NULL, stdout);

    if (json_out) {
        JsonWriter w; json_writer_init(&w, 1);
        dmk_write_report_json(&model, have_pop ? &pop : NULL, &opts,
                              have_cas ? &cas : NULL, &w);
        if (!w.err && write_text_file(json_out, w.buf) == 0 && !quiet)
            fprintf(stderr, "Wrote %s\n", json_out);
        json_writer_free(&w);
    }
    if (geojson_out) {
        JsonWriter w; json_writer_init(&w, 0);
        dmk_write_report_geojson(&model, &opts, threshold, &w);
        if (!w.err && write_text_file(geojson_out, w.buf) == 0 && !quiet)
            fprintf(stderr, "Wrote %s\n", geojson_out);
        json_writer_free(&w);
    }

    if (have_pop) dmk_pop_free(&pop);
    if (have_dem) dmk_dem_free(&dem);
    if (have_wind) dmk_weather_free(&wind);
    dmk_model_free(&model);
    return 0;
}

static int cmd_targets(int argc, char **argv) {
    const char *search = NULL, *state = NULL, *catalog = NULL;
    int limit = 40;
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--search") && i+1 < argc) search = argv[++i];
        else if (!strcmp(argv[i], "--state") && i+1 < argc) state = argv[++i];
        else if (!strcmp(argv[i], "--limit") && i+1 < argc) limit = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--catalog") && i+1 < argc) catalog = argv[++i];
    }
    const char *path = catalog ? catalog : dmk_default_targets_path();
    DmkTarget *t = NULL; int n = 0;
    if (dmk_load_targets(path, &t, &n) != 0) {
        fprintf(stderr, "ERROR: cannot load %s\n", path);
        return 1;
    }
    printf("%-5s %-34s %-16s %-14s %8s %s\n", "ID", "NAME", "STATE", "CATEGORY", "YIELD", "BURST");
    int shown = 0;
    for (int i = 0; i < n && shown < limit; i++) {
        if (search) {
            char hay[192]; snprintf(hay, sizeof hay, "%s %s %s", t[i].name, t[i].state, t[i].category);
            int hit = 0; size_t hl = strlen(hay), nl = strlen(search);
            for (size_t j = 0; nl && j + nl <= hl; j++) if (strncasecmp(hay+j, search, nl)==0){hit=1;break;}
            if (!hit) continue;
        }
        if (state && strcasecmp(t[i].state, state) != 0) continue;
        printf("%-5d %-34.34s %-16.16s %-14.14s %7.0fkt %s\n",
               t[i].id, t[i].name, t[i].state, t[i].category, t[i].yield_kt,
               t[i].is_surface ? "surface" : "air");
        shown++;
    }
    printf("(%d of %d targets shown)\n", shown, n);
    free(t);
    return 0;
}

static int cmd_weapons(void) {
    DmkWeaponPreset *w = NULL; int n = 0;
    if (dmk_load_weapons(dmk_default_weapons_path(), &w, &n) != 0) {
        fprintf(stderr, "ERROR: cannot load %s\n", dmk_default_weapons_path());
        return 1;
    }
    printf("%-14s %-34s %10s %8s %s\n", "ID", "NAME", "YIELD_KT", "FISSION", "BURST");
    for (int i = 0; i < n; i++)
        printf("%-14s %-34.34s %10.0f %8.2f %s\n", w[i].id, w[i].name,
               w[i].yield_kt, w[i].fission_fraction, w[i].is_surface ? "surface" : "air");
    free(w);
    return 0;
}

static int cmd_dose(int argc, char **argv) {
    /* Personal dose calculator: given the local H+1 dose rate (from a run's
     * fallout grid), when fallout arrived, a shelter protection factor, and a
     * stay-time window, report accumulated dose and when a target is reached. */
    double rate_h1 = 0, arrival = 1.0, window = 48.0, pf = 1.0;
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--rate") && i+1 < argc) rate_h1 = atof(argv[++i]);
        else if (!strcmp(argv[i], "--arrival") && i+1 < argc) arrival = atof(argv[++i]);
        else if (!strcmp(argv[i], "--window") && i+1 < argc) window = atof(argv[++i]);
        else if (!strcmp(argv[i], "--pf") && i+1 < argc) pf = atof(argv[++i]);
    }
    if (rate_h1 <= 0) {
        fprintf(stderr, "Usage: damaskino dose --rate <H+1 R/hr> [--arrival H] [--window H] [--pf N]\n");
        return 2;
    }
    double dose = dmk_fallout_dose_rem(rate_h1, arrival, window, pf);
    printf("Personal fallout dose estimate\n");
    printf("  H+1 dose rate      : %.1f R/hr\n", rate_h1);
    printf("  Fallout arrival    : H+%.1f h\n", arrival);
    printf("  Shelter PF         : %.0f\n", pf);
    printf("  Stay window        : %.1f h\n", window);
    printf("  Accumulated dose   : %.0f rem\n", dose);
    /* Effect summary */
    const char *effect =
        dose < 50 ? "below acute-effect threshold" :
        dose < 150 ? "possible mild radiation sickness" :
        dose < 450 ? "radiation sickness likely; medical care needed" :
        dose < 1000 ? "severe; ~50% lethal without treatment" : "likely fatal";
    printf("  Assessment         : %s\n", effect);
    /* Time to reach 50 rem (a shelter-exit planning threshold) unsheltered-equiv */
    for (double t = arrival + 0.5; t <= 168.0; t += 0.5) {
        if (dmk_fallout_dose_rem(rate_h1, arrival, t, pf) >= 50.0) {
            printf("  Reaches 50 rem at  : H+%.1f h (%.1f h after arrival)\n", t, t - arrival);
            break;
        }
    }
    return 0;
}

static int cmd_ensemble(int argc, char **argv) {
    const char *scenario_path = NULL, *json_out = NULL, *weather_path = NULL;
    int samples = 300; unsigned seed = 1;
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--samples") && i+1 < argc) samples = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && i+1 < argc) seed = (unsigned)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--json") && i+1 < argc) json_out = argv[++i];
        else if (!strcmp(argv[i], "--weather") && i+1 < argc) weather_path = argv[++i];
        else if (argv[i][0] != '-') scenario_path = argv[i];
    }
    if (!scenario_path) { fprintf(stderr, "Usage: damaskino ensemble <scenario.json> [--samples N] [--seed S] [--weather f] [--json f]\n"); return 2; }
    char *text = dmk_read_file(scenario_path);
    if (!text) { fprintf(stderr, "ERROR: cannot read %s\n", scenario_path); return 1; }
    DmkScenario sc; char err[256];
    if (dmk_scenario_parse(text, &sc, err, sizeof err) != 0) { fprintf(stderr, "ERROR: %s\n", err); free(text); return 1; }
    free(text);

    DmkWindColumn wind; int have_wind = 0;
    if (weather_path) {
        if (dmk_weather_load(weather_path, &wind) != 0)
            fprintf(stderr, "WARNING: could not load weather %s; ensemble uses WSEG\n", weather_path);
        else have_wind = 1;
    }

    DmkEnsembleSpec spec; dmk_ensemble_spec_defaults(&spec);
    real_t levels[] = {1, 10, 100, 1000};
    int nlev = 4;
    DmkEnsembleResult r;
    if (dmk_ensemble_run(&sc, &spec, samples, seed, levels, nlev,
                         have_wind ? &wind : NULL, &r) != 0) {
        fprintf(stderr, "ERROR: ensemble run failed\n");
        if (have_wind) dmk_weather_free(&wind);
        return 1;
    }

    printf("=== Monte Carlo ensemble: %d members (%s model) ===\n", r.samples,
           r.used_lagrangian ? "Lagrangian+wind" : "WSEG");
    printf("  (yield CV %.0f%%, wind speed CV %.0f%%, dir sd %.0f deg, fission sd %.2f)\n",
           spec.yield_cv*100, spec.wind_speed_cv*100, spec.wind_dir_sd, spec.fission_sd);
    printf("  Note: P>=90%% (high-confidence) edges need more members than P>=50%%;\n"
           "        raise --samples for stable tail bands.\n\n");
    printf("  Dose (R/hr) | P>=90%% area | P>=50%% area | P>=10%% area | P50 extent\n");
    printf("  ------------|-------------|-------------|-------------|----------\n");
    double cellA = r.cell_km * r.cell_km;
    for (int l = 0; l < nlev; l++) {
        long c90=0,c50=0,c10=0; double ext=0;
        for (int y=0;y<r.n;y++) for (int x=0;x<r.n;x++){
            double p = r.prob[l][(size_t)y*r.n+x];
            if (p>=0.9) c90++;
            if (p>=0.5){
                c50++;
                double dx=(x-r.gz_x)*r.cell_km, dy=(y-r.gz_y)*r.cell_km;
                double d=sqrt(dx*dx+dy*dy);
                if(d>ext)ext=d;
            }
            if (p>=0.1) c10++;
        }
        printf("  %10.0f  | %9.0f   | %9.0f   | %9.0f   | %6.1f km\n",
               levels[l], c90*cellA, c50*cellA, c10*cellA, ext);
    }

    if (json_out) {
        JsonWriter w; json_writer_init(&w, 1);
        json_obj_begin(&w);
        json_kv_str(&w, "tool", "damaskino"); json_kv_str(&w, "analysis", "monte_carlo_ensemble");
        json_kv_str(&w, "model", r.used_lagrangian ? "Lagrangian+wind" : "WSEG");
        json_kv_int(&w, "members", r.samples);
        json_key(&w, "exceedance"); json_arr_begin(&w);
        for (int l = 0; l < nlev; l++) {
            long c90=0,c50=0,c10=0; double ext=0;
            for (int y=0;y<r.n;y++) for (int x=0;x<r.n;x++){
                double p=r.prob[l][(size_t)y*r.n+x];
                if(p>=0.9) c90++;
                if(p>=0.5){
                    c50++;
                    double dx=(x-r.gz_x)*r.cell_km,dy=(y-r.gz_y)*r.cell_km;
                    double d=sqrt(dx*dx+dy*dy);
                    if(d>ext)ext=d;
                }
                if(p>=0.1) c10++;
            }
            json_obj_begin(&w);
            json_kv_num(&w,"level_rhr",levels[l]);
            json_kv_num(&w,"area_p90_km2",c90*cellA);
            json_kv_num(&w,"area_p50_km2",c50*cellA);
            json_kv_num(&w,"area_p10_km2",c10*cellA);
            json_kv_num(&w,"extent_p50_km",ext);
            json_obj_end(&w);
        }
        json_arr_end(&w); json_obj_end(&w);
        FILE *f = fopen(json_out, "wb"); if (f){ fputs(w.buf,f); fclose(f); }
        json_writer_free(&w);
    }
    dmk_ensemble_free(&r);
    if (have_wind) dmk_weather_free(&wind);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,
            "Damaskino %s - nuclear-effects simulator (civil-defense / education)\n"
            "Usage:\n"
            "  %s run <scenario.json> [--json f] [--geojson f] [--pop-density N]\n"
            "     [--pop-asc f] [--dem f.asc] [--pf N] [--pf-prompt N] [--visibility KM]\n"
            "     [--exposure H] [--thermal-exposed F] [--time day|night]\n"
            "     [--threshold R/hr] [--quiet]\n"
            "  %s targets [--search TERM] [--state ST] [--limit N] [--catalog f]\n"
            "  %s weapons\n"
            "  %s dose --rate <H+1 R/hr> [--arrival H] [--window H] [--pf N]\n"
            "  %s ensemble <scenario.json> [--samples N] [--seed S] [--json f]\n"
            "  %s validate [benchmarks.json]\n"
            "  %s version\n"
            "\nScenario JSON may reference the catalog: \"target\": <id|name>,\n"
            "\"weapon_preset\": \"<id>\" (see: damaskino targets / weapons).\n",
            DMK_VERSION_STRING, argv[0], argv[0], argv[0], argv[0], argv[0], argv[0], argv[0]);
        return 2;
    }
    if (!strcmp(argv[1], "version")) { printf("damaskino %s\n", DMK_VERSION_STRING); return 0; }
    if (!strcmp(argv[1], "run")) return cmd_run(argc - 2, argv + 2);
    if (!strcmp(argv[1], "targets")) return cmd_targets(argc - 2, argv + 2);
    if (!strcmp(argv[1], "weapons")) return cmd_weapons();
    if (!strcmp(argv[1], "dose")) return cmd_dose(argc - 2, argv + 2);
    if (!strcmp(argv[1], "ensemble")) return cmd_ensemble(argc - 2, argv + 2);
    if (!strcmp(argv[1], "validate"))
        return dmk_run_validation(argc >= 3 ? argv[2] : "data/validation.json");
    fprintf(stderr, "Unknown command: %s\n", argv[1]);
    return 2;
}
