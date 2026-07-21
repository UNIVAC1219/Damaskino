/*
 * test_terrain.c - DEM sampling, terrain line-of-sight occlusion, and the
 * cratering / EMP / activation models.
 */
#include "damaskino.h"
#include "terrain.h"
#include "effects.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int failures = 0;
#define CHECK(c,msg) do{ if(!(c)){printf("FAIL: %s\n",msg);failures++;} else printf("ok  : %s\n",msg);}while(0)

/* Write a synthetic DEM: flat 100 m everywhere, with a tall N-S ridge (2000 m)
 * one column east of centre. 0.001 deg cells (~111 m), 101x101 around (40,-75). */
static void write_dem(const char *path) {
    int N = 101;
    double cs = 0.001;
    double xll = -75.0 - (N/2)*cs, yll = 40.0 - (N/2)*cs;
    FILE *f = fopen(path, "w");
    fprintf(f, "ncols %d\nnrows %d\nxllcorner %f\nyllcorner %f\ncellsize %f\nNODATA_value -9999\n",
            N, N, xll, yll, cs);
    int ridge_col = N/2 + 5;   /* a few cells east of centre */
    for (int r = 0; r < N; r++) {
        for (int c = 0; c < N; c++)
            fprintf(f, "%d ", (c >= ridge_col-1 && c <= ridge_col+1) ? 2000 : 100);
        fprintf(f, "\n");
    }
    fclose(f);
}

int main(void) {
    printf("=== Damaskino terrain tests ===\n");

    const char *path = "/tmp/dem_test.asc";
    write_dem(path);
    DmkDem d;
    CHECK(dmk_dem_load_asc(&d, path) == 0, "DEM loads");
    CHECK(dmk_dem_ready(&d), "DEM ready");

    /* Sampling: flat area 100 m, ridge column 2000 m. */
    CHECK(fabs(dmk_dem_elev(&d, 40.0, -75.0) - 100.0) < 1.0, "flat area samples ~100 m");
    double ridge_lon = -75.0 + 5*0.001;
    CHECK(dmk_dem_elev(&d, 40.0, ridge_lon) > 1500.0, "ridge samples high");
    CHECK(dmk_dem_elev(&d, 10.0, 10.0) == 0.0, "outside tile -> 0");

    /* Line of sight: a low emitter at centre (100 m + 50 m) to a receiver just
     * east of the 2000 m ridge should be OCCLUDED; to a receiver west (open)
     * should be VISIBLE. */
    double east_of_ridge = -75.0 + 8*0.001;
    double west_open = -75.0 - 8*0.001;
    int occ = dmk_terrain_los(&d, 40.0, -75.0, 150.0, 40.0, east_of_ridge, 1.7);
    int vis = dmk_terrain_los(&d, 40.0, -75.0, 150.0, 40.0, west_open, 1.7);
    CHECK(occ == 0, "receiver behind 2000 m ridge is occluded");
    CHECK(vis == 1, "receiver in the open is visible");

    /* A high-altitude airburst (12 km) clears the ridge for this near receiver;
     * note a 5 km source would NOT (the chord at the ridge is ~1.6 km, below
     * the 2 km ridge) -- terrain shadowing is geometrically real. */
    int high = dmk_terrain_los(&d, 40.0, -75.0, 12000.0, 40.0, east_of_ridge, 1.7);
    CHECK(high == 1, "12 km airburst clears the 2 km ridge for the near receiver");

    dmk_dem_free(&d);

    /* Flat-earth (no DEM): always visible. */
    DmkDem empty; memset(&empty, 0, sizeof empty);
    CHECK(dmk_terrain_los(&empty, 40, -75, 100, 41, -75, 1.7) == 1, "no DEM -> visible");

    /* Cratering */
    DmkCrater c1000 = dmk_crater(1000.0, 1);
    CHECK(c1000.radius_m > 150.0 && c1000.radius_m < 260.0, "1 Mt surface crater ~200 m radius");
    CHECK(dmk_crater(1000.0, 0).radius_m == 0.0, "air burst leaves no crater");
    CHECK(dmk_crater(8000.0, 1).radius_m > c1000.radius_m, "bigger yield -> bigger crater");

    /* HEMP */
    CHECK(dmk_hemp_footprint_km(0.0) == 0.0, "surface burst: no HEMP");
    double fp = dmk_hemp_footprint_km(100000.0);
    CHECK(fp > 1000.0 && fp < 1200.0, "100 km burst HEMP footprint ~1130 km radius");

    /* Activation: positive, decreasing with range, scales with fission. */
    CHECK(dmk_activation_dose_h1_rhr(100, 1.0, 0.5) > dmk_activation_dose_h1_rhr(100, 1.0, 1.0),
          "activation decreases with range");
    CHECK(dmk_activation_dose_h1_rhr(100, 1.0, 0.5) > dmk_activation_dose_h1_rhr(100, 0.5, 0.5),
          "activation scales with fission fraction");

    printf("\n%s (%d failure%s)\n", failures ? "TESTS FAILED" : "ALL TESTS PASSED",
           failures, failures==1?"":"s");
    return failures ? 1 : 0;
}
