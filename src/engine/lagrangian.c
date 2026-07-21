/*
 * lagrangian.c - Modern Lagrangian particle-dispersion fallout model.
 *
 * Replaces the WSEG-10 single-column transport when a real wind field is
 * available. Size-resolved particle parcels are released across the stabilized
 * cloud (disk of finite radius, from base to top), then advected through the
 * multi-level wind column with size-dependent gravitational settling. Each
 * parcel deposits where it reaches the ground (terrain-aware), spread by a
 * turbulent-diffusion sigma that grows with travel time. Precipitation drives
 * wet scavenging (rainout), depositing activity en route and producing the
 * concentrated hotspots that dry models miss.
 *
 * Activity normalization keeps the WSEG H+1 convention (1.6e6 R/hr at 1 NM per
 * MT fission) with Way-Wigner decay, and applies a simple Freiling-style
 * fractionation (refractory activity biased to larger/earlier-falling
 * particles). See docs/PHYSICS_NOTES.md for the deferred full fission-product
 * inventory.
 *
 * References: HYSPLIT/FLEXPART Lagrangian dispersion; Freiling fractionation;
 * Glasstone & Dolan.
 */
#include "damaskino.h"
#include "terrain.h"
#include "weather.h"
#include "geo.h"
#include <math.h>
#include <stdlib.h>

#define DMK_LAGR_ALT_LEVELS   16      /* release altitudes across the cloud */
#define DMK_LAGR_AZIMUTHS      8      /* initial horizontal samples (ring) */
#define DMK_LAGR_RINGS         2      /* radial samples within the cloud disk */
#define DMK_LAGR_DT            30.0   /* transport timestep, seconds */
#define DMK_LAGR_SIGMA_CUTOFF  3.5
#define DMK_EDDY_DIFFUSIVITY   40.0   /* horizontal K, m^2/s (turbulent spread) */

/* Freiling fractionation: refractory nuclides condense into the melt early and
 * ride the larger particles; volatiles plate out later onto small ones. We bias
 * the per-class activity toward larger diameters relative to the mass fraction.
 * r50 ~ the size above which refractory dominance sets in. */
static real_t fractionation_weight(real_t diameter_um) {
    /* smoothly rises from ~0.6 (fine) to ~1.4 (coarse) about 100 um */
    return 0.6 + 0.8 / (1.0 + exp(-(log(diameter_um) - log(100.0)) * 1.5));
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* Deposit `activity` at grid location (land_x,land_y) km from GZ, spread by
 * sigma_km, tracking arrival time. Returns the activity that landed on-grid
 * (the rest fell outside the domain). */
static real_t deposit_parcel(DmkModel *m, real_t land_x_km, real_t land_y_km,
                             real_t sigma_km, real_t activity, real_t arrival_hr) {
    DmkGrid *g = &m->grid;
    real_t cell_km = g->cell_km;
    real_t cell_area = cell_km * cell_km;
    if (sigma_km < 1.0e-3) sigma_km = 1.0e-3;
    real_t half = DMK_LAGR_SIGMA_CUTOFF * sigma_km;
    int gx0 = clampi((int)floor((land_x_km - half)/cell_km) + m->gz_x, 0, g->n-1);
    int gx1 = clampi((int)ceil ((land_x_km + half)/cell_km) + m->gz_x, 0, g->n-1);
    int gy0 = clampi((int)floor((land_y_km - half)/cell_km) + m->gz_y, 0, g->n-1);
    int gy1 = clampi((int)ceil ((land_y_km + half)/cell_km) + m->gz_y, 0, g->n-1);

    real_t inv2s2 = 1.0 / (2.0 * sigma_km * sigma_km);
    /* dose_rate is an areal density; normalize so integral over area == activity */
    real_t norm = activity / (2.0 * M_PI * sigma_km * sigma_km);
    real_t on_grid = 0.0;

    for (int gy = gy0; gy <= gy1; gy++) {
        real_t cy = (gy - m->gz_y) * cell_km, dy = cy - land_y_km;
        for (int gx = gx0; gx <= gx1; gx++) {
            real_t cx = (gx - m->gz_x) * cell_km, dx = cx - land_x_km;
            real_t dep = norm * exp(-(dx*dx + dy*dy) * inv2s2);
            DmkCell *c = dmk_grid_at(g, gx, gy);
            c->dose_rate_rhr += dep;
            if (dep > 0.0 && (c->arrival_hr == 0.0 || arrival_hr < c->arrival_hr))
                c->arrival_hr = arrival_hr;
            on_grid += dep * cell_area;
        }
    }
    return on_grid;
}

void dmk_lagrangian_deposit(DmkModel *model) {
    const DmkScenario *sc = &model->scenario;
    const DmkWindColumn *wind = model->wind;
    const DmkDem *dem = model->dem;

    real_t total_act = dmk_fission_activity(sc->weapon.yield_kt, sc->cfg.ref_time_hr,
                                            sc->weapon.fission_fraction);

    /* Cloud geometry */
    real_t base_km = model->cloud.cloud_base_km;
    real_t top_km  = model->cloud.cloud_top_km;
    real_t cloud_radius_km = 0.5 * model->cloud.stem_diameter_km + 0.1 * top_km;
    real_t stab_hr = model->cloud.stabilization_min / 60.0;

    /* Ground elevation under GZ (terrain-aware sink height). */
    real_t gz_elev_m = dmk_dem_ready(dem) ? dmk_dem_elev(dem, sc->gz.lat, sc->gz.lon) : 0.0;

    /* Precipitation scavenging coefficient (per second): Lambda = a P^b, with
     * P in mm/hr (Marshall-Palmer-like). */
    real_t precip = wind ? wind->precip_mm_hr : 0.0;
    real_t scav = (precip > 0.0) ? 1.0e-4 * pow(precip, 0.8) : 0.0;

    /* Normalize activity across the parcel ensemble (weighted by mass frac and
     * fractionation). Precompute the total weight. */
    real_t class_w[DMK_NUM_PARTICLE_CLASSES];
    real_t wsum = 0.0;
    for (int pc = 0; pc < DMK_NUM_PARTICLE_CLASSES; pc++) {
        class_w[pc] = model->particles.mass_fraction[pc]
                    * fractionation_weight(DMK_PARTICLE_DIAMETERS[pc]);
        wsum += class_w[pc];
    }
    if (wsum <= 0.0) wsum = 1.0;

    int n_horiz = DMK_LAGR_AZIMUTHS * DMK_LAGR_RINGS + 1;  /* +1 center */
    model->activity_emitted = 0.0;
    model->activity_on_grid = 0.0;

    for (int pc = 0; pc < DMK_NUM_PARTICLE_CLASSES; pc++) {
        if (model->particles.mass_fraction[pc] < 1.0e-4) continue;
        real_t diameter = DMK_PARTICLE_DIAMETERS[pc];
        real_t class_act = total_act * class_w[pc] / wsum;
        real_t per_release = class_act / (DMK_LAGR_ALT_LEVELS * n_horiz);

        for (int ia = 0; ia < DMK_LAGR_ALT_LEVELS; ia++) {
            real_t af = (DMK_LAGR_ALT_LEVELS == 1) ? 0.5
                      : (real_t)ia / (DMK_LAGR_ALT_LEVELS - 1);
            real_t rel_alt_m = (base_km + af * (top_km - base_km)) * DMK_KM_TO_M;

            for (int ih = 0; ih < n_horiz; ih++) {
                /* initial horizontal offset within the cloud disk */
                real_t ox_km = 0.0, oy_km = 0.0;
                if (ih > 0) {
                    int ring = (ih - 1) / DMK_LAGR_AZIMUTHS;
                    int az = (ih - 1) % DMK_LAGR_AZIMUTHS;
                    real_t rr = cloud_radius_km * (ring + 1.0) / DMK_LAGR_RINGS;
                    real_t th = 2.0 * M_PI * az / DMK_LAGR_AZIMUTHS;
                    ox_km = rr * cos(th); oy_km = rr * sin(th);
                }

                /* Advect this parcel to the ground. */
                real_t x_m = ox_km * DMK_KM_TO_M, y_m = oy_km * DMK_KM_TO_M;
                real_t alt = rel_alt_m;
                real_t fall_s = 0.0;
                real_t act = per_release;
                model->activity_emitted += per_release;

                int guard = 0;
                while (alt > gz_elev_m && guard++ < 100000) {
                    real_t u, v; dmk_wind_at(wind, alt, &u, &v);
                    real_t vset = dmk_settling_velocity(diameter, alt);
                    real_t dt = DMK_LAGR_DT;
                    /* limit dt so we don't overshoot the ground */
                    if (vset * dt > (alt - gz_elev_m)) dt = (alt - gz_elev_m) / vset;

                    x_m += u * dt; y_m += v * dt;
                    alt -= vset * dt;
                    fall_s += dt;

                    /* Wet scavenging: deposit a fraction here, en route. */
                    if (scav > 0.0) {
                        real_t frac = 1.0 - exp(-scav * dt);
                        real_t dep_act = act * frac;
                        act -= dep_act;
                        if (dep_act > 0.0) {
                            real_t sig = cloud_radius_km
                                       + sqrt(2.0 * DMK_EDDY_DIFFUSIVITY * fall_s) * DMK_M_TO_KM;
                            real_t arr = stab_hr + fall_s / 3600.0;
                            model->activity_on_grid += deposit_parcel(model,
                                x_m*DMK_M_TO_KM, y_m*DMK_M_TO_KM, sig, dep_act, arr);
                        }
                    }
                }

                /* Dry deposition of the remaining activity at the landing point. */
                real_t land_x_km = x_m * DMK_M_TO_KM, land_y_km = y_m * DMK_M_TO_KM;
                real_t sigma_km = cloud_radius_km
                                + sqrt(2.0 * DMK_EDDY_DIFFUSIVITY * fall_s) * DMK_M_TO_KM;
                real_t arrival = stab_hr + fall_s / 3600.0;
                if (arrival < 0.1) arrival = 0.1;
                model->activity_on_grid += deposit_parcel(model, land_x_km, land_y_km,
                                                          sigma_km, act, arrival);
            }
        }
    }

    /* Air bursts loft the fine debris; minimal local fallout. */
    if (!sc->weapon.is_surface_burst) {
        for (size_t i = 0; i < (size_t)model->grid.n * model->grid.n; i++)
            model->grid.cell[i].dose_rate_rhr *= 0.005;
    }

    model->off_grid_fraction = (model->activity_emitted > 0.0)
        ? 1.0 - model->activity_on_grid / model->activity_emitted : 0.0;
    if (model->off_grid_fraction < 0.0) model->off_grid_fraction = 0.0;
}
