/*
 * fallout.c - WSEG-10 fallout transport & deposition (Phase 0).
 *
 * Improvements over the original single-file version:
 *   - real_t (double) precision in the full profile
 *   - heap grid of arbitrary size/resolution
 *   - bounding-box deposition: each release deposits only within +/-4 sigma of
 *     its landing point, so cost scales with plume footprint, not grid area.
 *     This is what makes large/fine grids tractable.
 *
 * The Lagrangian rewrite (Phase 3) replaces the transport integrator; this
 * remains the lite / offline fallback.
 */
#include "damaskino.h"
#include "terrain.h"
#include "geo.h"
#include <math.h>
#include <stdlib.h>

#define DMK_NUM_RELEASE_POINTS 24     /* vertical release points in the column */
#define DMK_SIGMA_CUTOFF       4.0    /* deposit within +/- this many sigma */

static int wind_layer_for_altitude(const DmkAtmosphere *atm, real_t altitude_ft) {
    int layer = 0;
    for (int i = 1; i < atm->n_layers; i++)
        if (altitude_ft >= atm->layer[i].altitude_ft) layer = i;
    return layer;
}

static int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Precompute a per-cell terrain deposition modifier: fallout concentrates in
 * valleys (local low spots) and is reduced on ridges. Returns a malloc'd
 * n*n array of factors (~0.8..1.3), or NULL if no DEM. */
static real_t *terrain_factor_grid(const DmkModel *model) {
    const DmkDem *dem = model->dem;
    if (!dmk_dem_ready(dem)) return NULL;
    int n = model->grid.n;
    real_t cell_km = model->grid.cell_km;
    real_t *terr = (real_t *)malloc((size_t)n * n * sizeof(real_t));
    if (!terr) return NULL;
    const DmkScenario *sc = &model->scenario;
    for (int gy = 0; gy < n; gy++) {
        for (int gx = 0; gx < n; gx++) {
            real_t east = (gx - model->gz_x) * cell_km;
            real_t north = (gy - model->gz_y) * cell_km;
            real_t lat, lon; dmk_offset_to_latlon(sc->gz.lat, sc->gz.lon, east, north, &lat, &lon);
            real_t e  = dmk_dem_elev(dem, lat, lon);
            real_t dkm = cell_km;
            real_t latn, lonn;
            dmk_offset_to_latlon(sc->gz.lat, sc->gz.lon, east, north + dkm, &latn, &lonn);
            real_t en = dmk_dem_elev(dem, latn, lonn);
            dmk_offset_to_latlon(sc->gz.lat, sc->gz.lon, east, north - dkm, &latn, &lonn);
            real_t es = dmk_dem_elev(dem, latn, lonn);
            dmk_offset_to_latlon(sc->gz.lat, sc->gz.lon, east + dkm, north, &latn, &lonn);
            real_t ee = dmk_dem_elev(dem, latn, lonn);
            dmk_offset_to_latlon(sc->gz.lat, sc->gz.lon, east - dkm, north, &latn, &lonn);
            real_t ew = dmk_dem_elev(dem, latn, lonn);
            real_t local_avg = 0.25 * (en + es + ee + ew);
            real_t f = 1.0;
            if (e < local_avg - 30.0) f = 1.3;      /* valley: collects fallout */
            else if (e > local_avg + 60.0) f = 0.8; /* ridge: sheds fallout */
            terr[(size_t)gy * n + gx] = f;
        }
    }
    return terr;
}

static void deposit_class(DmkModel *model, int pc, const real_t *terr,
                          double *sum_plain, double *sum_weighted) {
    const DmkScenario *sc = &model->scenario;
    const DmkAtmosphere *atm = &sc->atmosphere;
    DmkGrid *g = &model->grid;

    real_t diameter = DMK_PARTICLE_DIAMETERS[pc];
    real_t mass_frac = model->particles.mass_fraction[pc];
    if (mass_frac < 1.0e-3) return;

    real_t total_act = dmk_fission_activity(sc->weapon.yield_kt,
                                            sc->cfg.ref_time_hr,
                                            sc->weapon.fission_fraction);
    real_t class_act = total_act * mass_frac;

    real_t cell_km = g->cell_km;

    for (int r = 0; r < DMK_NUM_RELEASE_POINTS; r++) {
        real_t frac = (real_t)r / (real_t)(DMK_NUM_RELEASE_POINTS - 1);
        real_t rel_alt_m = (model->cloud.cloud_base_km +
            frac * (model->cloud.cloud_top_km - model->cloud.cloud_base_km)) * DMK_KM_TO_M;

        /* Integrate the fall through the wind column. */
        real_t altitude = rel_alt_m;
        real_t drift_x = 0.0, drift_y = 0.0, fall_hr = 0.0;
        while (altitude > 0.0) {
            real_t altitude_ft = altitude * DMK_M_TO_FT;
            int layer = wind_layer_for_altitude(atm, altitude_ft);
            real_t wspd = atm->layer[layer].speed_kts * DMK_KNOTS_TO_MS;

            real_t v = dmk_settling_velocity(diameter, altitude);
            real_t dh = (altitude > 500.0) ? 500.0 : altitude;
            real_t dt = dh / v;

            /* Meteorological convention: wind blows FROM direction_deg, so it
             * transports debris TO the reciprocal bearing. Compass bearings are
             * clockwise from north, so a bearing b maps to (east=sin b, north=cos b). */
            real_t transport = (atm->layer[layer].direction_deg + 180.0) * DMK_DEG_TO_RAD;
            drift_x += wspd * dt * sin(transport);   /* +x = east  */
            drift_y += wspd * dt * cos(transport);   /* +y = north */
            fall_hr += dt / 3600.0;
            altitude -= dh;
        }

        real_t land_x_km = drift_x * DMK_M_TO_KM;
        real_t land_y_km = drift_y * DMK_M_TO_KM;
        real_t dist_km = sqrt(land_x_km * land_x_km + land_y_km * land_y_km);
        real_t sigma_km = model->cloud.stem_diameter_km * 0.5 + 0.1 * dist_km;
        if (sigma_km < 1.0e-3) sigma_km = 1.0e-3;

        /* Bounding box in grid coordinates (+/- cutoff sigma). */
        real_t half = DMK_SIGMA_CUTOFF * sigma_km;
        int gx0 = clampi((int)floor((land_x_km - half) / cell_km) + model->gz_x, 0, g->n - 1);
        int gx1 = clampi((int)ceil ((land_x_km + half) / cell_km) + model->gz_x, 0, g->n - 1);
        int gy0 = clampi((int)floor((land_y_km - half) / cell_km) + model->gz_y, 0, g->n - 1);
        int gy1 = clampi((int)ceil ((land_y_km + half) / cell_km) + model->gz_y, 0, g->n - 1);

        real_t inv2s2 = 1.0 / (2.0 * sigma_km * sigma_km);
        /* A 2-D isotropic Gaussian integrates to 2*pi*sigma^2, so this is the
         * activity-conserving normalization: sum over the plane of
         * base*exp(-r^2/2sigma^2) == class_act/N. */
        real_t area_norm = 2.0 * M_PI * sigma_km * sigma_km;
        real_t release_act = class_act / (real_t)DMK_NUM_RELEASE_POINTS;
        real_t base = release_act / area_norm;
        real_t arrival = model->cloud.stabilization_min / 60.0 + fall_hr;
        if (arrival < 0.1) arrival = 0.1;

        /* Track how much of this release's activity lands within the grid, so
         * off-domain clipping is reported rather than silently lost. The
         * fraction inside [gmin,gmax] of a 1-D Gaussian is
         *   0.5*(erf((gmax-mu)/(sigma*sqrt2)) - erf((gmin-mu)/(sigma*sqrt2))). */
        {
            real_t s2 = sigma_km * 1.4142135623730951;
            real_t xmin = (0 - model->gz_x) * cell_km, xmax = (g->n - 1 - model->gz_x) * cell_km;
            real_t ymin = (0 - model->gz_y) * cell_km, ymax = (g->n - 1 - model->gz_y) * cell_km;
            real_t fx = 0.5 * (erf((xmax - land_x_km) / s2) - erf((xmin - land_x_km) / s2));
            real_t fy = 0.5 * (erf((ymax - land_y_km) / s2) - erf((ymin - land_y_km) / s2));
            model->activity_emitted += release_act;
            model->activity_on_grid += release_act * fx * fy;
        }

        for (int gy = gy0; gy <= gy1; gy++) {
            real_t cell_y = (gy - model->gz_y) * cell_km;
            real_t dy = cell_y - land_y_km;
            for (int gx = gx0; gx <= gx1; gx++) {
                real_t cell_x = (gx - model->gz_x) * cell_km;
                real_t dx = cell_x - land_x_km;
                real_t dep = exp(-(dx * dx + dy * dy) * inv2s2);
                real_t base_dose = base * dep;
                real_t dose = terr ? base_dose * terr[(size_t)gy * g->n + gx] : base_dose;
                /* Track plain vs terrain-weighted on-grid activity so terrain
                 * can be renormalized to a mass-conserving redistribution. */
                real_t ca = cell_km * cell_km;
                *sum_plain    += base_dose * ca;
                *sum_weighted += dose * ca;

                DmkCell *c = dmk_grid_at(g, gx, gy);
                c->dose_rate_rhr += dose;
                if (dose > 0.0) {
                    if (c->arrival_hr == 0.0 || arrival < c->arrival_hr)
                        c->arrival_hr = arrival;
                    c->particle_class_max = pc;
                }
            }
        }
    }
}

void dmk_fallout_deposit(DmkModel *model) {
    model->activity_emitted = 0.0;
    model->activity_on_grid = 0.0;
    real_t *terr = terrain_factor_grid(model);
    double sum_plain = 0.0, sum_weighted = 0.0;
    for (int pc = 0; pc < DMK_NUM_PARTICLE_CLASSES; pc++)
        deposit_class(model, pc, terr, &sum_plain, &sum_weighted);

    /* Terrain is a mass-conserving REDISTRIBUTION (valleys collect what ridges
     * shed): rescale so the terrain-weighted total equals the plain total. */
    if (terr && sum_weighted > 0.0) {
        real_t k = (real_t)(sum_plain / sum_weighted);
        DmkGrid *g = &model->grid;
        for (size_t i = 0; i < (size_t)g->n * g->n; i++)
            g->cell[i].dose_rate_rhr *= k;
    }
    free(terr);

    model->off_grid_fraction = (model->activity_emitted > 0.0)
        ? 1.0 - model->activity_on_grid / model->activity_emitted
        : 0.0;
    if (model->off_grid_fraction < 0.0) model->off_grid_fraction = 0.0;

    /* Air bursts loft fine particles high: minimal local fallout. */
    real_t scale = model->scenario.weapon.is_surface_burst ? 1.0 : 0.005;
    if (scale != 1.0) {
        DmkGrid *g = &model->grid;
        for (size_t i = 0; i < (size_t)g->n * g->n; i++)
            g->cell[i].dose_rate_rhr *= scale;
    }
}
