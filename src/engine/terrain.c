/* terrain.c - see terrain.h */
#include "terrain.h"
#include "geo.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dmk_portable.h"

int dmk_dem_load_asc(DmkDem *d, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    memset(d, 0, sizeof(*d));
    d->nodata = -9999.0;
    char key[64];
    if (fscanf(f, "%63s %d", key, &d->ncols) != 2) { fclose(f); return -1; }
    if (fscanf(f, "%63s %d", key, &d->nrows) != 2) { fclose(f); return -1; }
    if (fscanf(f, "%63s %lf", key, &d->xll) != 2) { fclose(f); return -1; }
    if (fscanf(f, "%63s %lf", key, &d->yll) != 2) { fclose(f); return -1; }
    if (fscanf(f, "%63s %lf", key, &d->cellsize) != 2) { fclose(f); return -1; }
    long pos = ftell(f);
    if (fscanf(f, "%63s", key) == 1) {
        if (strcasecmp(key, "NODATA_value") == 0) {
            if (fscanf(f, "%lf", &d->nodata) != 1) { fclose(f); return -1; }
        } else fseek(f, pos, SEEK_SET);
    }
    size_t n = (size_t)d->ncols * d->nrows;
    if (n == 0 || d->ncols < 0 || d->nrows < 0) { fclose(f); return -1; }
    d->elev = (float *)malloc(n * sizeof(float));
    if (!d->elev) { fclose(f); return -1; }
    for (size_t i = 0; i < n; i++) {
        double v;
        if (fscanf(f, "%lf", &v) != 1) { free(d->elev); d->elev = NULL; fclose(f); return -1; }
        d->elev[i] = (float)v;
    }
    fclose(f);
    d->loaded = 1;
    return 0;
}

void dmk_dem_free(DmkDem *d) {
    if (d && d->elev) { free(d->elev); d->elev = NULL; }
    if (d) d->loaded = 0;
}

int dmk_dem_ready(const DmkDem *d) { return d && d->loaded && d->elev; }

static float dem_at(const DmkDem *d, int col, int row) {
    if (col < 0) col = 0;
    if (col >= d->ncols) col = d->ncols - 1;
    if (row < 0) row = 0;
    if (row >= d->nrows) row = d->nrows - 1;
    float v = d->elev[(size_t)row * d->ncols + col];
    if ((double)v == d->nodata) return 0.0f;
    return v;
}

real_t dmk_dem_elev(const DmkDem *d, real_t lat, real_t lon) {
    if (!dmk_dem_ready(d)) return 0.0;
    /* Fractional grid coordinates; row 0 is the north edge. */
    double fx = (lon - d->xll) / d->cellsize - 0.5;
    double north_edge = d->yll + d->nrows * d->cellsize;
    double fy = (north_edge - lat) / d->cellsize - 0.5;
    if (fx < -0.5 || fx > d->ncols - 0.5 || fy < -0.5 || fy > d->nrows - 0.5)
        return 0.0;  /* outside tile */
    int c0 = (int)floor(fx), r0 = (int)floor(fy);
    double tx = fx - c0, ty = fy - r0;
    double v00 = dem_at(d, c0, r0),   v10 = dem_at(d, c0+1, r0);
    double v01 = dem_at(d, c0, r0+1), v11 = dem_at(d, c0+1, r0+1);
    double top = v00 * (1-tx) + v10 * tx;
    double bot = v01 * (1-tx) + v11 * tx;
    return top * (1-ty) + bot * ty;
}

int dmk_terrain_los(const DmkDem *d, real_t elat, real_t elon, real_t e_alt_asl_m,
                    real_t rlat, real_t rlon, real_t r_height_m) {
    if (!dmk_dem_ready(d)) return 1;  /* flat earth: always visible */

    /* Horizontal path length via local equirectangular metric. */
    double mlat = 0.5 * (elat + rlat);
    double dlat_km = (rlat - elat) * 111.32;
    double dlon_km = (rlon - elon) * 111.32 * cos(mlat * DMK_DEG_TO_RAD);
    double D_m = 1000.0 * sqrt(dlat_km*dlat_km + dlon_km*dlon_km);
    if (D_m < 1.0) return 1;

    double r_alt_asl = dmk_dem_elev(d, rlat, rlon) + r_height_m;

    /* Sample the profile; step ~ half a DEM cell (in meters), bounded. */
    double cell_m = d->cellsize * 111320.0 * cos(mlat * DMK_DEG_TO_RAD);
    if (cell_m < 30.0) cell_m = 30.0;
    int nsteps = (int)(D_m / (0.5 * cell_m));
    if (nsteps < 8) nsteps = 8;
    if (nsteps > 4000) nsteps = 4000;

    const double R = DMK_EARTH_RADIUS_M;
    for (int i = 1; i < nsteps; i++) {
        double t = (double)i / nsteps;
        double lat = elat + (rlat - elat) * t;
        double lon = elon + (rlon - elon) * t;
        double x = t * D_m;                          /* distance from emitter */
        /* Straight chord altitude between emitter and receiver (ASL). */
        double chord = e_alt_asl_m * (1.0 - t) + r_alt_asl * t;
        /* Earth curvature: in the (arc-distance, ASL-height) frame the straight
         * 3-D sightline dips below the chord by the sagitta x*(D-x)/2R, i.e. the
         * terrain effectively rises by that much relative to the sightline. */
        double drop = x * (D_m - x) / (2.0 * R);
        double terrain = dmk_dem_elev(d, lat, lon);
        if (terrain + drop > chord + 1.0) return 0;
    }
    return 1;
}
