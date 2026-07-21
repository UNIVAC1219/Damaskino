/*
 * physics.c - Cloud dynamics, particle distribution, settling, activity,
 * and wind-aloft estimation. Phase 0 uses the WSEG-10 formulation ported to
 * real_t; the Lagrangian rewrite (Phase 3) replaces the transport core while
 * keeping these sub-models available for the lite profile.
 */
#include "damaskino.h"
#include <math.h>
#include <string.h>

/* WSEG-10 particle size classes, microns. */
const real_t DMK_PARTICLE_DIAMETERS[DMK_NUM_PARTICLE_CLASSES] = {
    10, 20, 40, 70, 100, 150, 250, 400, 600, 900, 1300, 2000
};

const real_t DMK_DEFAULT_WIND_ALTITUDES_FT[DMK_NUM_WIND_LAYERS] = {
    0, 5000, 10000, 20000, 30000, 50000
};

/* ---- Cloud parameters (WSEG-10 scaling) ------------------------------- */
void dmk_cloud_compute(const DmkScenario *sc, DmkCloud *out) {
    real_t yield_mt = sc->weapon.yield_kt / 1000.0;

    out->cloud_top_km = 12.5 * pow(yield_mt, 0.25);
    if (sc->weapon.is_surface_burst)
        out->cloud_base_km = out->cloud_top_km * 0.3;
    else
        out->cloud_base_km = sc->weapon.hob_m / 1000.0;
    out->stem_diameter_km   = 1.5 * pow(yield_mt, 0.33);
    out->stabilization_min  = 10.0 * pow(yield_mt, 0.25);
}

/* ---- Particle size distribution (log-normal, mass-weighted) ----------- */
void dmk_particles_compute(const DmkScenario *sc, DmkParticles *out) {
    out->median_microns = 120.0;
    out->geometric_sigma = 2.5;
    if (!sc->weapon.is_surface_burst) {
        out->median_microns = 20.0;   /* air burst: fine particles */
        out->geometric_sigma = 3.0;
    }

    real_t total = 0.0;
    real_t ln_med = log(out->median_microns);
    real_t ln_sig = log(out->geometric_sigma);
    for (int i = 0; i < DMK_NUM_PARTICLE_CLASSES; i++) {
        real_t d = DMK_PARTICLE_DIAMETERS[i];
        real_t z = (log(d) - ln_med) / ln_sig;
        /* number pdf * d^3 -> mass distribution */
        real_t m = d * d * d * exp(-0.5 * z * z);
        out->mass_fraction[i] = m;
        total += m;
    }
    if (total > 0.0)
        for (int i = 0; i < DMK_NUM_PARTICLE_CLASSES; i++)
            out->mass_fraction[i] /= total;
}

/* ---- Settling velocity (Stokes with Newton-regime correction) --------- */
real_t dmk_settling_velocity(real_t diameter_microns, real_t altitude_m) {
    real_t d = diameter_microns * DMK_MICRON_TO_M;
    real_t rho_air = DMK_AIR_RHO_SL * exp(-altitude_m / DMK_SCALE_HEIGHT_M);

    real_t v_stokes = DMK_GRAVITY_MS2 * d * d * (DMK_PARTICLE_RHO - rho_air)
                    / (18.0 * DMK_AIR_MU);
    real_t re = rho_air * v_stokes * d / DMK_AIR_MU;

    if (re < 1.0) return v_stokes;
    if (re < 1000.0) {
        real_t corr = 1.0 / (1.0 + 0.15 * pow(re, 0.687));
        return v_stokes * corr;
    }
    /* Newton drag: v ~ sqrt(d) */
    return sqrt(4.0 * DMK_GRAVITY_MS2 * d * (DMK_PARTICLE_RHO - rho_air)
               / (3.0 * rho_air));
}

/* ---- Fission product activity (Way-Wigner decay) ---------------------- */
real_t dmk_fission_activity(real_t yield_kt, real_t time_hr, real_t fission_fraction) {
    real_t fission_mt = (yield_kt * fission_fraction) / 1000.0;
    real_t a1 = DMK_REF_ACTIVITY_1MT_1HR * fission_mt;
    return a1 * pow(time_hr, -DMK_WAY_WIGNER_EXP);
}

/* ---- Wind-aloft estimation from a surface wind ------------------------ */
void dmk_atmosphere_estimate_aloft(DmkAtmosphere *atm) {
    real_t sfc_speed = atm->layer[0].speed_kts;
    real_t sfc_dir   = atm->layer[0].direction_deg;

    for (int i = 1; i < atm->n_layers; i++) {
        real_t alt = atm->layer[i].altitude_ft;
        real_t mult;
        if (alt <= 10000.0)      mult = 1.0 + (alt / 10000.0) * 0.8;
        else if (alt <= 30000.0) mult = 1.8 + ((alt - 10000.0) / 20000.0) * 1.2;
        else                     mult = 3.0 + ((alt - 30000.0) / 20000.0) * 1.5;

        real_t est = sfc_speed * mult;
        real_t cap = 200.0 + sfc_speed * 0.5;
        if (cap > 250.0) cap = 250.0;
        atm->layer[i].speed_kts = (est > cap) ? cap : est;

        real_t dir = sfc_dir + i * 5.0;   /* Ekman veer */
        while (dir >= 360.0) dir -= 360.0;
        atm->layer[i].direction_deg = dir;
    }
}
