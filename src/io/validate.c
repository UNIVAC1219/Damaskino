/*
 * validate.c - Validation mode: check the effect models against the published
 * benchmarks in data/validation.json (Glasstone, DS02, Bravo). Reports each
 * case as PASS/FAIL within its tolerance; fallout cases are strongly wind-
 * dependent and reported as informational (approximate).
 */
#include "validate.h"
#include "scenario.h"
#include "effects.h"
#include "casualties.h"
#include "json.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <math.h>

static int is_surface(const char *b) { return b && strcasecmp(b, "air") != 0; }

static int check(const char *cat, const char *name, double got, double want,
                 double tol, int approximate) {
    double lo = want * (1.0 - tol), hi = want * (1.0 + tol);
    int pass = (got >= lo && got <= hi);
    printf("  [%-4s] %-8s %-52.52s model %8.2f  ref %8.2f (+/-%.0f%%)\n",
           pass ? "PASS" : (approximate ? "~~~~" : "FAIL"),
           cat, name, got, want, tol * 100);
    return pass;
}

/* Run a WSEG fallout scenario and return the max downwind extent (km) of a
 * dose-rate (R/hr) or accumulated-dose (rem, 48 h) contour. */
static double fallout_extent(double yield, int surface, double fission,
                             const char *metric, double level) {
    DmkScenario sc; dmk_scenario_defaults(&sc);
    sc.weapon.yield_kt = yield; sc.weapon.is_surface_burst = surface;
    sc.weapon.fission_fraction = fission;
    sc.atmosphere.layer[0].speed_kts = 15; sc.atmosphere.layer[0].direction_deg = 270;
    dmk_atmosphere_estimate_aloft(&sc.atmosphere);
    sc.cfg.grid_n = 300; sc.cfg.cell_km = 2.0; sc.cfg.ref_time_hr = 1.0;
    DmkModel m;
    if (dmk_run(&sc, &m) != 0) return 0.0;
    int use_dose = (metric && strstr(metric, "rem"));
    double mx = 0.0;
    for (int y = 0; y < m.grid.n; y++)
        for (int x = 0; x < m.grid.n; x++) {
            const DmkCell *c = &m.grid.cell[(size_t)y*m.grid.n + x];
            double v = use_dose ? dmk_fallout_dose_rem(c->dose_rate_rhr, c->arrival_hr, 48.0, 1.0)
                                : c->dose_rate_rhr;
            if (v >= level) {
                double dx=(x-m.gz_x)*m.grid.cell_km, dy=(y-m.gz_y)*m.grid.cell_km;
                double d=sqrt(dx*dx+dy*dy); if (d>mx) mx=d;
            }
        }
    dmk_model_free(&m);
    return mx;
}

static void run_array(JsonValue *root, const char *key, const char *cat,
                      int *pass, int *total,
                      double (*model_fn)(JsonValue *)) {
    JsonValue *arr = json_get(root, key);
    if (!arr || arr->type != JSON_ARRAY) return;
    for (size_t i = 0; i < arr->u.array.count; i++) {
        JsonValue *e = arr->u.array.items[i];
        double got = model_fn(e);
        double want, tol; const char *name = json_get_string(e, "case", "?");
        if (!strcmp(cat, "blast"))   { want = json_get_number(e, "ground_range_km", 0); }
        else if (!strcmp(cat, "therm")) { want = json_get_number(e, "ground_range_km", 0); }
        else if (!strcmp(cat, "prmpt")) { want = json_get_number(e, "ground_range_km", 0); }
        else if (!strcmp(cat, "crat"))  { want = json_get_number(e, "radius_m", 0); }
        else { want = json_get_number(e, "reference_km", 0); }
        tol = json_get_number(e, "tolerance_frac", 0.2);
        int approx = json_get_bool(e, "approximate", 0);
        int ok = check(cat, name, got, want, tol, approx);
        if (!approx) { (*total)++; if (ok) (*pass)++; }
    }
}

static double m_blast(JsonValue *e) {
    return dmk_blast_range_km(json_get_number(e,"yield_kt",0),
                              is_surface(json_get_string(e,"burst","air")),
                              json_get_number(e,"overpressure_psi",0));
}
static double m_therm(JsonValue *e) {
    return dmk_thermal_range_km(json_get_number(e,"yield_kt",0),
                                is_surface(json_get_string(e,"burst","air")),
                                json_get_number(e,"cal_cm2",0),
                                json_get_number(e,"visibility_km",20));
}
static double m_prompt(JsonValue *e) {
    return dmk_prompt_range_km(json_get_number(e,"yield_kt",0),
                               json_get_number(e,"fission_fraction",1.0),
                               json_get_number(e,"dose_rem",0));
}
static double m_crater(JsonValue *e) {
    return dmk_crater(json_get_number(e,"yield_kt",0), 1).radius_m;
}
static double m_fallout(JsonValue *e) {
    return fallout_extent(json_get_number(e,"yield_kt",0),
                          is_surface(json_get_string(e,"burst","surface")),
                          json_get_number(e,"fission_fraction",1.0),
                          json_get_string(e,"metric",""),
                          json_get_number(e,"level",0));
}

int dmk_run_validation(const char *path) {
    char *text = dmk_read_file(path);
    if (!text) { fprintf(stderr, "ERROR: cannot read %s\n", path); return 2; }
    const char *err = NULL;
    JsonValue *root = json_parse(text, &err);
    free(text);
    if (!root) { fprintf(stderr, "ERROR: bad validation JSON: %s\n", err?err:"?"); return 2; }

    printf("=== Damaskino validation vs published benchmarks ===\n");
    printf("  (~~~~  = approximate / wind-dependent, informational only)\n\n");
    int pass = 0, total = 0;
    run_array(root, "blast", "blast", &pass, &total, m_blast);
    run_array(root, "thermal", "therm", &pass, &total, m_therm);
    run_array(root, "prompt_radiation", "prmpt", &pass, &total, m_prompt);
    run_array(root, "crater", "crat", &pass, &total, m_crater);
    run_array(root, "fallout", "fall", &pass, &total, m_fallout);

    printf("\n%d/%d graded benchmarks passed.\n", pass, total);
    json_free(root);
    return (pass == total) ? 0 : 1;
}
