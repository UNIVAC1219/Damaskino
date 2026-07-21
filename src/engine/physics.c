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

/* ---- Cloud parameters ------------------------------------------------- */
/* Stabilized-cloud dimensions recalibrated to Glasstone & Dolan Ch. 9/2:
 * the visible cloud top for 1 Mt is ~19-21 km (vs. the WSEG-lite 12.5 km),
 * which sets the release altitude and hence downwind reach. We use a two-branch
 * fit (troposphere then stratospheric overshoot) with the tropopause limiting
 * small yields, matching ~7 km at 1 kt, ~12 km at 100 kt, ~20 km at 1 Mt,
 * ~30 km at 10 Mt. Cloud bottom is ~0.55 of the top for large yields. */
void dmk_cloud_compute(const DmkScenario *sc, DmkCloud *out) {
    real_t yield_mt = sc->weapon.yield_kt / 1000.0;

    /* Cloud top (km): 21.5 * W_MT^0.2 captures the observed scaling
     * (1 Mt -> 21.5 km; 100 kt -> 13.6 km; 1 kt -> 5.4 km; 10 Mt -> 34 km). */
    real_t top = 21.5 * pow(yield_mt, 0.2);
    if (top < 2.0) top = 2.0;
    out->cloud_top_km = top;

    /* Fallout release base. Fallout-bearing debris is not confined to the
     * mushroom cap (~0.55*top): the STEM carries coarse debris down nearly to
     * the ground, and that low-altitude coarse fraction produces the near-GZ
     * hotspot. So the surface-burst release column extends from low in the stem
     * (~0.1*top) to the cloud top; coarse particles released low deposit close
     * in while fine particles released high drift downwind. */
    if (sc->weapon.is_surface_burst)
        out->cloud_base_km = out->cloud_top_km * 0.10;
    else
        out->cloud_base_km = sc->weapon.hob_m / 1000.0;

    /* Cloud radius / stem diameter and stabilization time. */
    out->stem_diameter_km   = 2.0 * pow(yield_mt, 0.33);
    out->stabilization_min  = 10.0 * pow(yield_mt, 0.25);
}

/* ---- Particle size distribution (log-normal in MASS) ------------------ */
/* median_microns is the MASS-median (activity-median) diameter -- the standard
 * WSEG input -- so the mass fraction is a log-normal directly in ln(d):
 *   dM/d(ln d) proportional to exp(-0.5 z^2),  z = (ln d - ln d50) / ln sigma_g.
 * (The earlier extra d^3 factor was only valid for a NUMBER-median input and
 * pushed the mass median to ~1500 um -- far too coarse.) Because the sampled
 * diameters are not equally spaced in ln(d), each class is weighted by its
 * bin width d(ln d). */
void dmk_particles_compute(const DmkScenario *sc, DmkParticles *out) {
    /* Activity/mass-median diameter for LOCAL fallout. Surface-burst local
     * fallout is carried by the coarse fraction (activity-median ~200-400 um,
     * Glasstone Ch. 9 / DELFIC); the fine tail becomes worldwide fallout that
     * leaves any regional grid. 200 um / sigma_g 2.0 keeps the locally-
     * depositing coarse mass without an unphysical fine escape (the earlier
     * d^3-weighted form implied ~1500 um, far too coarse). */
    out->median_microns = 200.0;
    out->geometric_sigma = 2.0;
    if (!sc->weapon.is_surface_burst) {
        out->median_microns = 60.0;   /* air burst: finer, minimal local fallout */
        out->geometric_sigma = 2.5;
    }

    real_t ln_med = log(out->median_microns);
    real_t ln_sig = log(out->geometric_sigma);
    const int N = DMK_NUM_PARTICLE_CLASSES;
    real_t total = 0.0;
    for (int i = 0; i < N; i++) {
        real_t lnd = log(DMK_PARTICLE_DIAMETERS[i]);
        /* bin width in ln(d): midpoints to neighbors, one-sided at the ends */
        real_t lnlo = (i > 0)     ? 0.5 * (log(DMK_PARTICLE_DIAMETERS[i-1]) + lnd)
                                  : lnd - 0.5 * (log(DMK_PARTICLE_DIAMETERS[1]) - lnd);
        real_t lnhi = (i < N - 1) ? 0.5 * (lnd + log(DMK_PARTICLE_DIAMETERS[i+1]))
                                  : lnd + 0.5 * (lnd - log(DMK_PARTICLE_DIAMETERS[N-2]));
        real_t dln = lnhi - lnlo;
        real_t z = (lnd - ln_med) / ln_sig;
        real_t m = exp(-0.5 * z * z) * dln;
        out->mass_fraction[i] = m;
        total += m;
    }
    if (total > 0.0)
        for (int i = 0; i < N; i++)
            out->mass_fraction[i] /= total;
}

/* ---- Settling velocity (implicit terminal-velocity solve) ------------- */
/* Terminal velocity balances weight against drag F_d = Cd * 0.5 * rho_air *
 * (pi/4 d^2) * v^2, giving v = sqrt( 4 g d (rho_p - rho_air) / (3 Cd rho_air) )
 * with Cd = Cd(Re), Re = rho_air v d / mu. Cd uses the Clift-Gauvin
 * correlation, continuous from the Stokes regime (Cd = 24/Re) through the
 * transition to the Newton regime (Cd -> ~0.42) -- so the settling curve has
 * no regime-boundary discontinuity. Solved by relaxed fixed-point iteration
 * seeded from the Stokes velocity. */
real_t dmk_settling_velocity(real_t diameter_microns, real_t altitude_m) {
    real_t d = diameter_microns * DMK_MICRON_TO_M;
    real_t rho_air = DMK_AIR_RHO_SL * exp(-altitude_m / DMK_SCALE_HEIGHT_M);
    real_t drho = DMK_PARTICLE_RHO - rho_air;

    real_t v = DMK_GRAVITY_MS2 * d * d * drho / (18.0 * DMK_AIR_MU);  /* Stokes seed */
    for (int it = 0; it < 60; it++) {
        real_t re = rho_air * v * d / DMK_AIR_MU;
        if (re < 1.0e-6) re = 1.0e-6;
        real_t cd = (24.0 / re) * (1.0 + 0.15 * pow(re, 0.687))
                  + 0.42 / (1.0 + 4.25e4 * pow(re, -1.16));
        real_t vnew = sqrt(4.0 * DMK_GRAVITY_MS2 * d * drho / (3.0 * cd * rho_air));
        real_t rel = fabs(vnew - v) / (v > 0.0 ? v : 1.0);
        v = 0.5 * (v + vnew);   /* relaxed update for stability */
        if (rel < 1.0e-6) break;
    }
    return v;
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
