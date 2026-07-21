/*
 * test_casualties.c - Casualty model: probit anchors, population source,
 * and integration invariants.
 */
#include "damaskino.h"
#include "casualties.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static int failures = 0;
#define CHECK(c,msg) do{ if(!(c)){printf("FAIL: %s\n",msg);failures++;} else printf("ok  : %s\n",msg);}while(0)

static void run_scn(DmkModel *m, double yield, int surface, double density,
                    double day_night, DmkPopulation *pop, DmkCasualties *cas,
                    DmkCasualtyOpts *opts) {
    DmkScenario sc; dmk_scenario_defaults(&sc);
    sc.gz.lat = 40.0; sc.gz.lon = -75.0;
    sc.weapon.yield_kt = yield; sc.weapon.is_surface_burst = surface;
    sc.weapon.fission_fraction = 0.5;
    sc.atmosphere.layer[0].speed_kts = 15;
    dmk_atmosphere_estimate_aloft(&sc.atmosphere);
    sc.cfg.grid_n = 200; sc.cfg.cell_km = 1.0; sc.cfg.ref_time_hr = 1.0;
    dmk_run(&sc, m);
    dmk_pop_init_uniform(pop, density, day_night);
    dmk_casualties_compute(m, pop, opts, cas);
}

int main(void) {
    printf("=== Damaskino casualty tests ===\n");

    /* Probit anchors */
    CHECK(fabs(dmk_pfatal_blast(10.0) - 0.5) < 0.02, "blast LD50 at 10 psi");
    CHECK(fabs(dmk_pfatal_radiation(450.0) - 0.5) < 0.02, "radiation LD50 at 450 rem");
    CHECK(fabs(dmk_pfatal_thermal(12.0) - 0.5) < 0.02, "thermal LD50 at 12 cal/cm2");
    CHECK(dmk_pfatal_blast(3.0) < 0.1 && dmk_pfatal_blast(30.0) > 0.9, "blast probit monotone");
    CHECK(dmk_pfatal_radiation(100.0) < 0.05, "100 rem rarely fatal");

    /* Population raster round-trip */
    {
        const char *path = "/tmp/pop_test.asc";
        FILE *f = fopen(path, "w");
        fprintf(f, "ncols 4\nnrows 4\nxllcorner -75.02\nyllcorner 39.98\ncellsize 0.01\nNODATA_value -9999\n");
        for (int r = 0; r < 4; r++) { for (int c = 0; c < 4; c++) fprintf(f, "%d ", 1000*(r+1)); fprintf(f, "\n"); }
        fclose(f);
        DmkPopulation p;
        CHECK(dmk_pop_load_asc(&p, path) == 0, "ESRI ASCII population raster loads");
        double d = dmk_pop_density(&p, 40.0, -75.0);
        CHECK(d > 0.0, "raster density sampled > 0 inside extent");
        CHECK(dmk_pop_density(&p, 10.0, 10.0) == 0.0, "density 0 outside extent");
        dmk_pop_free(&p);
    }

    DmkCasualtyOpts opts; dmk_casualty_opts_defaults(&opts);

    /* Integration invariants */
    DmkModel m1; DmkPopulation p1; DmkCasualties c1;
    run_scn(&m1, 500, 1, 3000.0, 1.0, &p1, &c1, &opts);
    CHECK(c1.fatalities > 0.0, "nonzero fatalities for populated surface burst");
    CHECK(c1.fatalities <= c1.population_in_domain, "fatalities <= population");
    CHECK(c1.injuries >= 0.0, "injuries nonnegative");
    printf("     (500kt/3000pkm2: pop=%.0f fatalities=%.0f injuries=%.0f)\n",
           c1.population_in_domain, c1.fatalities, c1.injuries);

    /* Larger yield -> more fatalities at same density */
    DmkModel m2; DmkPopulation p2; DmkCasualties c2;
    run_scn(&m2, 1000, 1, 3000.0, 1.0, &p2, &c2, &opts);
    CHECK(c2.fatalities > c1.fatalities, "higher yield -> more fatalities");

    /* Sheltering reduces fatalities */
    DmkCasualtyOpts sheltered = opts; sheltered.protection_factor = 40.0;
    DmkCasualties c3; dmk_casualties_compute(&m1, &p1, &sheltered, &c3);
    CHECK(c3.fatal_fallout < c1.fatal_fallout, "sheltering (PF=40) cuts fallout fatalities");

    /* Day vs night population scaling */
    DmkModel md; DmkPopulation pd; DmkCasualties cd;
    run_scn(&md, 500, 1, 3000.0, 1.4, &pd, &cd, &opts);
    CHECK(cd.fatalities > c1.fatalities, "daytime (1.4x) population -> more fatalities");

    dmk_model_free(&m1); dmk_model_free(&m2); dmk_model_free(&md);

    printf("\n%s (%d failure%s)\n", failures ? "TESTS FAILED" : "ALL TESTS PASSED",
           failures, failures==1?"":"s");
    return failures ? 1 : 0;
}
