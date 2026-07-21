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
#include <math.h>

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

static void deposit_class(DmkModel *model, int pc) {
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
        real_t area_norm = M_PI * sigma_km * sigma_km;
        real_t base = class_act / (real_t)DMK_NUM_RELEASE_POINTS / area_norm;
        real_t arrival = model->cloud.stabilization_min / 60.0 + fall_hr;
        if (arrival < 0.1) arrival = 0.1;

        for (int gy = gy0; gy <= gy1; gy++) {
            real_t cell_y = (gy - model->gz_y) * cell_km;
            real_t dy = cell_y - land_y_km;
            for (int gx = gx0; gx <= gx1; gx++) {
                real_t cell_x = (gx - model->gz_x) * cell_km;
                real_t dx = cell_x - land_x_km;
                real_t dep = exp(-(dx * dx + dy * dy) * inv2s2);
                real_t dose = base * dep;

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
    for (int pc = 0; pc < DMK_NUM_PARTICLE_CLASSES; pc++)
        deposit_class(model, pc);

    /* Air bursts loft fine particles high: minimal local fallout. */
    real_t scale = model->scenario.weapon.is_surface_burst ? 1.0 : 0.005;
    if (scale != 1.0) {
        DmkGrid *g = &model->grid;
        for (size_t i = 0; i < (size_t)g->n * g->n; i++)
            g->cell[i].dose_rate_rhr *= scale;
    }
}
