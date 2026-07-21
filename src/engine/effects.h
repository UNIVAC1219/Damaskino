/*
 * effects.h - Prompt weapon effects: air blast, thermal radiation, and
 * initial (prompt) nuclear radiation. Analytic scaling models from the open
 * literature (Glasstone & Dolan 1977; Kingery-Bulmash airblast; Fetter et al.
 * 1990 for initial radiation). Absolute magnitudes are validated/calibrated
 * against benchmarks in Phase 3.5; see docs/PHYSICS_NOTES.md.
 *
 * All ranges are GROUND ranges in km unless noted. These are effects on an
 * unobstructed flat surface; terrain line-of-sight masking is layered in
 * Phase 2.
 */
#ifndef DMK_EFFECTS_H
#define DMK_EFFECTS_H
#include "damaskino.h"

/* ---- Air blast -------------------------------------------------------- */
/* Ground range (km) at which peak static overpressure equals `psi`, for a
 * weapon of `yield_kt`, surface vs. air burst. Returns 0 if the level is not
 * reached. */
real_t dmk_blast_range_km(real_t yield_kt, int is_surface_burst, real_t psi);

/* Peak overpressure (psi) at a given ground range (km). */
real_t dmk_blast_overpressure_psi(real_t yield_kt, int is_surface_burst, real_t range_km);

/* Dynamic pressure / max wind speed (m/s) behind the shock at an overpressure. */
real_t dmk_blast_wind_ms(real_t overpressure_psi);

/* ---- Thermal radiation ------------------------------------------------ */
/* Ground range (km) at which thermal fluence equals `cal_cm2`, accounting for
 * atmospheric transmittance at meteorological visibility `visibility_km`. */
real_t dmk_thermal_range_km(real_t yield_kt, int is_surface_burst,
                            real_t cal_cm2, real_t visibility_km);

/* Thermal fluence (cal/cm^2) at a ground range (km). */
real_t dmk_thermal_fluence(real_t yield_kt, int is_surface_burst,
                           real_t range_km, real_t visibility_km);

/* ---- Initial (prompt) nuclear radiation ------------------------------- */
/* Prompt neutron+gamma dose (rem/roentgen-equivalent) at a slant/ground range
 * (km) for `yield_kt` with fission fraction `fission_fraction`. */
real_t dmk_prompt_dose_rem(real_t yield_kt, real_t fission_fraction, real_t range_km);

/* Ground range (km) at which the prompt dose equals `dose_rem`. */
real_t dmk_prompt_range_km(real_t yield_kt, real_t fission_fraction, real_t dose_rem);

/* ---- Named thresholds (for ring generation) --------------------------- */
typedef struct {
    real_t value;        /* threshold in the effect's native units */
    real_t range_km;     /* computed ground range */
    const char *label;   /* human description */
} DmkEffectRing;

/* Populate standard blast/thermal/radiation rings; returns count written. */
int dmk_blast_rings(const DmkScenario *sc, DmkEffectRing *out, int max);
int dmk_thermal_rings(const DmkScenario *sc, real_t visibility_km, DmkEffectRing *out, int max);
int dmk_radiation_rings(const DmkScenario *sc, DmkEffectRing *out, int max);

/* ---- Cratering (surface bursts) --------------------------------------- */
typedef struct { real_t radius_m; real_t depth_m; } DmkCrater;
/* Apparent crater in soil; zero for air bursts. Scales ~ W^0.3, anchored to a
 * ~200 m radius / ~60 m depth for a 1 Mt surface burst (Glasstone & Dolan). */
DmkCrater dmk_crater(real_t yield_kt, int is_surface_burst);

/* ---- High-altitude EMP (HEMP) ----------------------------------------- */
/* Ground footprint radius (km) illuminated by E1 from a burst at burst_alt_m;
 * this is the tangent-horizon radius sqrt(2 R_e h). Returns 0 below ~30 km
 * (bursts there produce local source-region EMP instead). */
real_t dmk_hemp_footprint_km(real_t burst_alt_m);
real_t dmk_hemp_peak_field_kvm(void);   /* nominal E1 peak, ~50 kV/m */

/* ---- Neutron activation ----------------------------------------------- */
/* Induced-activity gamma dose rate (R/hr) at H+1 from soil activation near GZ,
 * a residual-radiation source distinct from fission-product fallout. */
real_t dmk_activation_dose_h1_rhr(real_t yield_kt, real_t fission_fraction, real_t range_km);

#endif /* DMK_EFFECTS_H */
