/*
 * effects.c - Prompt weapon effects: air blast, thermal radiation, initial
 * nuclear radiation. Analytic models from the open civil-defense literature.
 *
 * References:
 *   - Glasstone & Dolan, "The Effects of Nuclear Weapons", 3rd ed. (1977).
 *   - Kingery & Bulmash airblast scaling (cube-root similarity).
 *   - Fetter et al., "Casualties due to the blast, heat, and radioactive
 *     fallout from nuclear weapons" (1990) for initial-radiation form.
 *
 * Blast uses cube-root scaling of a 1-kiloton peak-overpressure reference
 * curve anchored to the widely-cited Glasstone benchmarks (1 Mt optimum air
 * burst: 1 psi @ 17.5 km, 5 psi @ 6.9 km, 20 psi @ 3.1 km). Surface bursts
 * apply a documented reflection/HOB factor. Absolute calibration and the full
 * height-of-burst Mach-stem surface are refined and validated in Phase 3.5;
 * see docs/PHYSICS_NOTES.md.
 */
#include "effects.h"
#include <math.h>

/* ====================================================================== */
/* Air blast                                                              */
/* ====================================================================== */

/* 1-kt optimum-air-burst reference: peak static overpressure (psi) -> ground
 * range (km). Monotonic; interpolated log-log. Anchors verified against the
 * canonical 1 Mt values after cube-root scaling (x10). */
static const real_t BLAST_PSI[]   = {
    0.5,  1.0,  2.0,  3.0,  5.0,  7.0, 10.0, 15.0, 20.0, 30.0, 50.0, 100.0, 200.0, 500.0
};
static const real_t BLAST_R1KT[]  = { /* km, 1 kt */
    2.60, 1.75, 1.25, 0.96, 0.69, 0.58, 0.47, 0.37, 0.31, 0.245,0.185,0.120, 0.080, 0.047
};
enum { BLAST_N = (int)(sizeof(BLAST_PSI)/sizeof(BLAST_PSI[0])) };

/* Surface-burst correction vs. optimum air burst: reflection lengthens the
 * high-overpressure near field, while the loss of the Mach optimum shortens
 * the low-overpressure far field. Documented approximation; calibrated later. */
static real_t surface_factor(real_t psi) {
    if (psi <= 5.0)  return 0.87;
    if (psi >= 50.0) return 1.12;
    /* log-linear between the two anchors */
    real_t t = (log(psi) - log(5.0)) / (log(50.0) - log(5.0));
    return 0.87 + t * (1.12 - 0.87);
}

static real_t loglog_interp(const real_t *xs, const real_t *ys, int n, real_t x,
                            int decreasing_y) {
    /* xs strictly increasing; interpolate y(x) in log-log with end clamping. */
    if (x <= xs[0])   return ys[0];
    if (x >= xs[n-1]) return ys[n-1];
    for (int i = 1; i < n; i++) {
        if (x <= xs[i]) {
            real_t lx0 = log(xs[i-1]), lx1 = log(xs[i]);
            real_t ly0 = log(ys[i-1]), ly1 = log(ys[i]);
            real_t t = (log(x) - lx0) / (lx1 - lx0);
            (void)decreasing_y;
            return exp(ly0 + t * (ly1 - ly0));
        }
    }
    return ys[n-1];
}

real_t dmk_blast_range_km(real_t yield_kt, int is_surface_burst, real_t psi) {
    if (yield_kt <= 0.0 || psi <= 0.0) return 0.0;
    real_t r1kt = loglog_interp(BLAST_PSI, BLAST_R1KT, BLAST_N, psi, 1);
    real_t r = r1kt * cbrt(yield_kt);
    if (is_surface_burst) r *= surface_factor(psi);
    return r;
}

real_t dmk_blast_overpressure_psi(real_t yield_kt, int is_surface_burst, real_t range_km) {
    if (yield_kt <= 0.0 || range_km <= 0.0) return 0.0;
    /* Range decreases monotonically with overpressure; bisect on psi so that
     * range(psi) == range_km. Consistent with the surface-factor correction. */
    real_t lo = 0.2, hi = 600.0;
    if (dmk_blast_range_km(yield_kt, is_surface_burst, hi) > range_km) return hi;
    if (dmk_blast_range_km(yield_kt, is_surface_burst, lo) < range_km) return 0.0;
    for (int i = 0; i < 60; i++) {
        real_t mid = 0.5 * (lo + hi);
        real_t r = dmk_blast_range_km(yield_kt, is_surface_burst, mid);
        if (r > range_km) lo = mid; else hi = mid;  /* larger psi -> smaller range */
    }
    return 0.5 * (lo + hi);
}

real_t dmk_blast_wind_ms(real_t overpressure_psi) {
    /* Peak wind (particle velocity) behind the shock, Rankine-Hugoniot:
     *   u = (c0/gamma) * (dp/P0) / sqrt(1 + ((gamma+1)/(2 gamma)) * dp/P0)
     * with gamma=1.4, ambient P0=14.7 psi, c0=340 m/s. */
    const real_t gamma = 1.4, P0 = 14.7, c0 = 340.0;
    real_t x = overpressure_psi / P0;
    return (c0 / gamma) * x / sqrt(1.0 + ((gamma + 1.0) / (2.0 * gamma)) * x);
}

/* ====================================================================== */
/* Thermal radiation                                                      */
/* ====================================================================== */

static real_t thermal_fraction(int is_surface_burst) {
    /* Fraction of yield emitted as thermal radiation. ~0.35 for air bursts;
     * surface bursts radiate less efficiently (fireball ground interaction). */
    return is_surface_burst ? 0.20 : 0.35;
}

real_t dmk_thermal_fluence(real_t yield_kt, int is_surface_burst,
                           real_t range_km, real_t visibility_km) {
    if (yield_kt <= 0.0 || range_km <= 0.0) return 0.0;
    if (visibility_km <= 0.0) visibility_km = 20.0;
    real_t f = thermal_fraction(is_surface_burst);
    /* Extinction: meteorological visibility V -> sigma = 3.912/V per km. */
    real_t tau = exp(-3.912 * range_km / visibility_km);
    /* Q [cal/cm^2] = f * Y_kt * 100 * tau / (4 pi R_km^2)  (1 kt = 1e12 cal). */
    return f * yield_kt * 100.0 * tau / (4.0 * M_PI * range_km * range_km);
}

real_t dmk_thermal_range_km(real_t yield_kt, int is_surface_burst,
                            real_t cal_cm2, real_t visibility_km) {
    if (yield_kt <= 0.0 || cal_cm2 <= 0.0) return 0.0;
    if (visibility_km <= 0.0) visibility_km = 20.0;
    /* Solve Q(R) = cal_cm2 for R. Q decreases monotonically with R, so
     * bisect between a small inner radius and a large outer radius. */
    real_t lo = 0.01, hi = 500.0;
    if (dmk_thermal_fluence(yield_kt, is_surface_burst, lo, visibility_km) < cal_cm2)
        return 0.0;  /* threshold never reached even very close in */
    if (dmk_thermal_fluence(yield_kt, is_surface_burst, hi, visibility_km) > cal_cm2)
        return hi;   /* still above threshold at the outer bound */
    for (int i = 0; i < 60; i++) {
        real_t mid = 0.5 * (lo + hi);
        real_t q = dmk_thermal_fluence(yield_kt, is_surface_burst, mid, visibility_km);
        if (q > cal_cm2) lo = mid; else hi = mid;
    }
    return 0.5 * (lo + hi);
}

/* ====================================================================== */
/* Initial (prompt) nuclear radiation                                     */
/* ====================================================================== */

/* Combined neutron+gamma dose. Two exponential-attenuation components with
 * sea-level relaxation lengths (neutron ~235 m, gamma ~410 m), each ~1/R^2
 * geometric. Constants anchored so a ~15 kt pure-fission weapon delivers
 * ~500 rem near 1.1 km slant (Hiroshima-scale initial radiation). */
#define DMK_PROMPT_LAMBDA_N 235.0
#define DMK_PROMPT_LAMBDA_G 410.0
#define DMK_PROMPT_KN       3.6e8
#define DMK_PROMPT_KG       3.6e8

real_t dmk_prompt_dose_rem(real_t yield_kt, real_t fission_fraction, real_t range_km) {
    if (yield_kt <= 0.0 || range_km <= 0.0) return 0.0;
    real_t R = range_km * 1000.0;  /* m */
    real_t f = fission_fraction;
    if (f < 0.0) f = 0.0;
    if (f > 1.0) f = 1.0;
    real_t neutron = DMK_PROMPT_KN * exp(-R / DMK_PROMPT_LAMBDA_N);
    real_t gamma   = DMK_PROMPT_KG * exp(-R / DMK_PROMPT_LAMBDA_G);
    return f * yield_kt * (neutron + gamma) / (R * R);
}

real_t dmk_prompt_range_km(real_t yield_kt, real_t fission_fraction, real_t dose_rem) {
    if (yield_kt <= 0.0 || dose_rem <= 0.0) return 0.0;
    real_t lo = 0.01, hi = 20.0;
    if (dmk_prompt_dose_rem(yield_kt, fission_fraction, lo) < dose_rem) return 0.0;
    if (dmk_prompt_dose_rem(yield_kt, fission_fraction, hi) > dose_rem) return hi;
    for (int i = 0; i < 60; i++) {
        real_t mid = 0.5 * (lo + hi);
        real_t d = dmk_prompt_dose_rem(yield_kt, fission_fraction, mid);
        if (d > dose_rem) lo = mid; else hi = mid;
    }
    return 0.5 * (lo + hi);
}

/* ====================================================================== */
/* Ring generators                                                        */
/* ====================================================================== */

int dmk_blast_rings(const DmkScenario *sc, DmkEffectRing *out, int max) {
    static const struct { real_t psi; const char *label; } L[] = {
        {20.0, "20 psi: reinforced concrete buildings destroyed"},
        {10.0, "10 psi: most factories/commercial buildings destroyed"},
        { 5.0, "5 psi: most residences collapse, widespread fatalities"},
        { 3.0, "3 psi: residential walls fail, many injuries"},
        { 1.0, "1 psi: windows shatter, light injuries"},
    };
    int n = 0;
    for (unsigned i = 0; i < sizeof(L)/sizeof(L[0]) && n < max; i++) {
        real_t r = dmk_blast_range_km(sc->weapon.yield_kt, sc->weapon.is_surface_burst, L[i].psi);
        if (r > 0.0) { out[n].value = L[i].psi; out[n].range_km = r; out[n].label = L[i].label; n++; }
    }
    return n;
}

int dmk_thermal_rings(const DmkScenario *sc, real_t visibility_km,
                      DmkEffectRing *out, int max) {
    static const struct { real_t cal; const char *label; } L[] = {
        {8.0, "3rd-degree burns (full-thickness)"},
        {5.0, "2nd-degree burns (blistering)"},
        {2.5, "1st-degree burns (painful reddening)"},
    };
    int n = 0;
    for (unsigned i = 0; i < sizeof(L)/sizeof(L[0]) && n < max; i++) {
        real_t r = dmk_thermal_range_km(sc->weapon.yield_kt, sc->weapon.is_surface_burst,
                                        L[i].cal, visibility_km);
        if (r > 0.0) { out[n].value = L[i].cal; out[n].range_km = r; out[n].label = L[i].label; n++; }
    }
    return n;
}

/* ====================================================================== */
/* Cratering                                                              */
/* ====================================================================== */
DmkCrater dmk_crater(real_t yield_kt, int is_surface_burst) {
    DmkCrater c = {0.0, 0.0};
    if (!is_surface_burst || yield_kt <= 0.0) return c;
    /* Apparent crater radius scales ~ W^0.3 in dry soil, anchored to ~150 m
     * radius / ~45 m depth for a 1 Mt contact surface burst (Glasstone & Dolan,
     * dry soil). Cratering is strongly HOB-sensitive; this assumes a contact
     * burst (refined with soil/HOB dependence later). */
    c.radius_m = 150.0 * pow(yield_kt / 1000.0, 0.3);
    c.depth_m  = 0.30 * c.radius_m;
    return c;
}

/* ====================================================================== */
/* High-altitude EMP                                                      */
/* ====================================================================== */
real_t dmk_hemp_footprint_km(real_t burst_alt_m) {
    if (burst_alt_m < 30000.0) return 0.0;  /* HEMP regime is high altitude */
    /* Tangent radius to the horizon from altitude h: sqrt(2 R_e h + h^2). */
    real_t R = DMK_EARTH_RADIUS_M;
    return sqrt(2.0 * R * burst_alt_m + burst_alt_m * burst_alt_m) / 1000.0;
}
real_t dmk_hemp_peak_field_kvm(void) { return 50.0; }  /* nominal E1 peak */

/* ====================================================================== */
/* Neutron activation                                                     */
/* ====================================================================== */
real_t dmk_activation_dose_h1_rhr(real_t yield_kt, real_t fission_fraction, real_t range_km) {
    if (yield_kt <= 0.0 || range_km <= 0.0) return 0.0;
    /* Soil activation tracks the initial neutron fluence ~ fission yield with
     * the neutron relaxation length; converted to an induced H+1 gamma dose
     * rate. Empirically a minor contributor vs. fallout for surface bursts but
     * significant for low-fallout (air/enhanced-radiation) bursts. Anchored so
     * a 1 kt fission burst gives ~O(100) R/hr at H+1 within a few hundred m. */
    real_t R = range_km * 1000.0;
    real_t f = fission_fraction; if (f < 0.0) f = 0.0; if (f > 1.0) f = 1.0;
    real_t K = 4.0e7;
    return f * yield_kt * K * exp(-R / DMK_PROMPT_LAMBDA_N) / (R * R);
}

int dmk_radiation_rings(const DmkScenario *sc, DmkEffectRing *out, int max) {
    static const struct { real_t rem; const char *label; } L[] = {
        {1000.0, "1000 rem: rapidly fatal (LD100 without care)"},
        { 500.0, "500 rem: ~LD50 within weeks without treatment"},
        { 100.0, "100 rem: acute radiation sickness, rarely fatal"},
    };
    int n = 0;
    for (unsigned i = 0; i < sizeof(L)/sizeof(L[0]) && n < max; i++) {
        real_t r = dmk_prompt_range_km(sc->weapon.yield_kt, sc->weapon.fission_fraction, L[i].rem);
        if (r > 0.0) { out[n].value = L[i].rem; out[n].range_km = r; out[n].label = L[i].label; n++; }
    }
    return n;
}
