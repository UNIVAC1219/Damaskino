/*
 * test_engine.c - Phase 0 unit tests: JSON round-trip, sub-model sanity,
 * and end-to-end engine invariants.
 */
#include "damaskino.h"
#include "json.h"
#include "scenario.h"
#include "output.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else         { printf("ok  : %s\n", msg); } } while (0)

static void test_json_roundtrip(void) {
    JsonWriter w; json_writer_init(&w, 0);
    json_obj_begin(&w);
      json_kv_str(&w, "name", "Washington \"D.C.\"");
      json_kv_num(&w, "yield", 500.0);
      json_kv_int(&w, "n", 256);
      json_kv_bool(&w, "surface", 1);
      json_key(&w, "layers"); json_arr_begin(&w);
        json_obj_begin(&w); json_kv_num(&w, "alt", 0); json_kv_num(&w, "spd", 15); json_obj_end(&w);
        json_obj_begin(&w); json_kv_num(&w, "alt", 10000); json_kv_num(&w, "spd", 30); json_obj_end(&w);
      json_arr_end(&w);
    json_obj_end(&w);
    CHECK(!w.err, "json writer no error");

    const char *err = NULL;
    JsonValue *v = json_parse(w.buf, &err);
    CHECK(v != NULL, "written JSON re-parses");
    if (v) {
        CHECK(strcmp(json_get_string(v, "name", ""), "Washington \"D.C.\"") == 0,
              "string escaping round-trips");
        CHECK(json_get_number(v, "yield", 0) == 500.0, "number round-trips");
        CHECK(json_get_number(v, "n", 0) == 256, "int round-trips");
        CHECK(json_get_bool(v, "surface", 0) == 1, "bool round-trips");
        JsonValue *layers = json_get(v, "layers");
        CHECK(layers && layers->type == JSON_ARRAY && layers->u.array.count == 2,
              "array length correct");
        json_free(v);
    }
    json_writer_free(&w);
}

static void test_json_bom_and_errors(void) {
    const char *err = NULL;
    JsonValue *v = json_parse("\xEF\xBB\xBF{\"a\":1}", &err);
    CHECK(v != NULL, "BOM is tolerated");
    json_free(v);
    v = json_parse("{\"a\":}", &err);
    CHECK(v == NULL, "malformed JSON rejected");
}

static void test_particles(void) {
    DmkScenario sc; dmk_scenario_defaults(&sc);
    sc.weapon.is_surface_burst = 1;
    DmkParticles p; dmk_particles_compute(&sc, &p);
    real_t sum = 0;
    for (int i = 0; i < DMK_NUM_PARTICLE_CLASSES; i++) sum += p.mass_fraction[i];
    CHECK(fabs(sum - 1.0) < 1e-6, "mass fractions sum to 1");
}

static void test_settling_monotonic(void) {
    int mono = 1;
    real_t prev = -1;
    for (int i = 0; i < DMK_NUM_PARTICLE_CLASSES; i++) {
        real_t v = dmk_settling_velocity(DMK_PARTICLE_DIAMETERS[i], 0.0);
        if (v < prev) mono = 0;
        prev = v;
    }
    CHECK(mono, "settling velocity increases with particle size");
}

static void test_engine_run(void) {
    const char *scn =
        "{ \"location\": {\"name\":\"TestGZ\",\"lat\":38.9,\"lon\":-77.0},"
        "  \"weapon\": {\"yield_kt\":1000,\"burst\":\"surface\",\"fission_fraction\":1.0},"
        "  \"wind\": {\"surface\":{\"speed_kts\":20,\"direction_deg\":270}},"
        "  \"grid\": {\"n\":128,\"cell_km\":2.0,\"ref_time_hr\":1.0} }";
    DmkScenario sc; char err[256];
    int rc = dmk_scenario_parse(scn, &sc, err, sizeof err);
    CHECK(rc == 0, "scenario parses");

    DmkModel m;
    rc = dmk_run(&sc, &m);
    CHECK(rc == 0, "engine runs");

    /* Some dose deposited somewhere. */
    double total = 0; long hot = 0;
    for (size_t i = 0; i < (size_t)m.grid.n * m.grid.n; i++) {
        total += m.grid.cell[i].dose_rate_rhr;
        if (m.grid.cell[i].dose_rate_rhr > 1.0) hot++;
    }
    CHECK(total > 0.0, "nonzero total dose deposited");
    CHECK(hot > 0, "at least one hot cell (>1 R/hr)");

    /* Wind is FROM 270 (west) -> plume drifts EAST (+x). Compare dose mass
     * east vs west of GZ. */
    double east = 0, west = 0;
    for (int y = 0; y < m.grid.n; y++)
        for (int x = 0; x < m.grid.n; x++) {
            double d = m.grid.cell[(size_t)y*m.grid.n + x].dose_rate_rhr;
            if (x > m.gz_x) east += d; else if (x < m.gz_x) west += d;
        }
    CHECK(east > west, "plume biased downwind (east) of ground zero");

    /* Cloud scaling sanity. */
    CHECK(m.cloud.cloud_top_km > 0 && m.cloud.stem_diameter_km > 0, "cloud params positive");

    dmk_model_free(&m);
}

static void test_result_json_valid(void) {
    DmkScenario sc; dmk_scenario_defaults(&sc);
    sc.weapon.yield_kt = 500; sc.weapon.is_surface_burst = 1;
    sc.atmosphere.layer[0].speed_kts = 10;
    dmk_atmosphere_estimate_aloft(&sc.atmosphere);
    sc.cfg.grid_n = 64; sc.cfg.cell_km = 2.0;
    DmkModel m; dmk_run(&sc, &m);

    JsonWriter w; json_writer_init(&w, 1);
    dmk_write_result_json(&m, &w);
    const char *err = NULL;
    JsonValue *v = json_parse(w.buf, &err);
    CHECK(v != NULL, "result JSON is valid JSON");
    if (v) {
        CHECK(json_get(v, "contours") != NULL, "result has contours");
        json_free(v);
    }
    json_writer_free(&w);

    JsonWriter g; json_writer_init(&g, 0);
    dmk_write_geojson(&m, &g, 1.0);
    JsonValue *gv = json_parse(g.buf, &err);
    CHECK(gv != NULL, "GeoJSON is valid JSON");
    if (gv) {
        CHECK(strcmp(json_get_string(gv, "type", ""), "FeatureCollection") == 0,
              "GeoJSON is a FeatureCollection");
        json_free(gv);
    }
    json_writer_free(&g);
    dmk_model_free(&m);
}

static void test_input_validation(void) {
    DmkScenario sc; char err[256];
    /* negative yield rejected */
    int rc = dmk_scenario_parse("{\"weapon\":{\"yield_kt\":-5}}", &sc, err, sizeof err);
    CHECK(rc != 0, "negative yield rejected by parser");
    /* ref_time 0 rejected */
    rc = dmk_scenario_parse("{\"weapon\":{\"yield_kt\":100},\"grid\":{\"ref_time_hr\":0}}", &sc, err, sizeof err);
    CHECK(rc != 0, "ref_time_hr=0 rejected by parser");
    /* fission fraction out of range rejected */
    rc = dmk_scenario_parse("{\"weapon\":{\"yield_kt\":100,\"fission_fraction\":2}}", &sc, err, sizeof err);
    CHECK(rc != 0, "fission_fraction>1 rejected by parser");

    /* engine guard: build a bad scenario directly and confirm rc=-3, no NaN */
    DmkScenario bad; dmk_scenario_defaults(&bad);
    bad.weapon.yield_kt = -1.0;
    DmkModel m;
    CHECK(dmk_run(&bad, &m) == -3, "engine guard rejects negative yield (rc=-3)");
}

static void test_offgrid_detection(void) {
    /* A large plume on a tiny domain must report clipping; a big domain must not. */
    DmkScenario sc; dmk_scenario_defaults(&sc);
    sc.weapon.yield_kt = 1000; sc.weapon.is_surface_burst = 1;
    sc.atmosphere.layer[0].speed_kts = 30; sc.atmosphere.layer[0].direction_deg = 270;
    dmk_atmosphere_estimate_aloft(&sc.atmosphere);
    sc.cfg.cell_km = 1.0; sc.cfg.ref_time_hr = 1.0;

    sc.cfg.grid_n = 32;                 /* 32 km span: too small */
    DmkModel small; dmk_run(&sc, &small);
    CHECK(small.off_grid_fraction > 0.05, "small domain flags off-grid activity");

    /* A 1 Mt surface burst legitimately sends a fine-particle tail beyond a
     * regional grid (worldwide fallout), so containment improves monotonically
     * with domain size rather than reaching ~0 at a few hundred km. */
    sc.cfg.grid_n = 400;  DmkModel mid; dmk_run(&sc, &mid);
    sc.cfg.grid_n = 1200; DmkModel big; dmk_run(&sc, &big);
    CHECK(big.off_grid_fraction < mid.off_grid_fraction,
          "larger domain contains more activity (monotonic)");
    CHECK(big.off_grid_fraction < 0.25, "a 1200 km domain contains the bulk of the pattern");
    dmk_model_free(&small); dmk_model_free(&mid); dmk_model_free(&big);
}

static void test_activity_normalization(void) {
    /* On a domain large enough to contain essentially all activity, the
     * deposited dose integrated over area should recover the emitted activity
     * (within the on-grid fraction). This guards the 2*pi*sigma^2 kernel. */
    DmkScenario sc; dmk_scenario_defaults(&sc);
    sc.weapon.yield_kt = 500; sc.weapon.is_surface_burst = 1;
    sc.weapon.fission_fraction = 1.0;
    sc.atmosphere.layer[0].speed_kts = 10;
    dmk_atmosphere_estimate_aloft(&sc.atmosphere);
    sc.cfg.grid_n = 512; sc.cfg.cell_km = 1.0; sc.cfg.ref_time_hr = 1.0;
    DmkModel m; dmk_run(&sc, &m);

    double integrated = 0.0;
    double cell_area = m.grid.cell_km * m.grid.cell_km;
    for (size_t i = 0; i < (size_t)m.grid.n*m.grid.n; i++)
        integrated += m.grid.cell[i].dose_rate_rhr * cell_area;
    /* emitted activity (same normalization the kernel uses) */
    double emitted = dmk_fission_activity(sc.weapon.yield_kt, sc.cfg.ref_time_hr,
                                          sc.weapon.fission_fraction);
    double ratio = integrated / (emitted * m.activity_on_grid / m.activity_emitted);
    CHECK(ratio > 0.9 && ratio < 1.1, "deposited activity conserves emitted (2pi-sigma kernel)");
    dmk_model_free(&m);
}

int main(void) {
    printf("=== Damaskino Phase 0 tests ===\n");
    test_json_roundtrip();
    test_json_bom_and_errors();
    test_particles();
    test_settling_monotonic();
    test_engine_run();
    test_input_validation();
    test_offgrid_detection();
    test_activity_normalization();
    test_result_json_valid();
    printf("\n%s (%d failure%s)\n", failures ? "TESTS FAILED" : "ALL TESTS PASSED",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
