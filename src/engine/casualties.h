/*
 * casualties.h - Casualty estimation via probit/LD50 lethality functions for
 * blast, thermal, prompt radiation, and fallout dose, integrated over gridded
 * population with sheltering and time-of-day.
 *
 * Population comes from a pluggable source: a uniform areal density (default)
 * or an ESRI ASCII-grid raster (e.g., WorldPop / GHS-POP clipped to the area).
 * Global gridded-population wiring is a data step; the model here is complete
 * and consumes whatever density source is provided.
 */
#ifndef DMK_CASUALTIES_H
#define DMK_CASUALTIES_H
#include "damaskino.h"

/* ---- Population source ------------------------------------------------ */
typedef struct {
    int    mode;              /* 0 = uniform, 1 = raster */
    real_t uniform_density;   /* people per km^2 (mode 0) */
    real_t day_night_factor;  /* multiplier: >1 daytime downtown, <1 night */
    /* ESRI ASCII grid (mode 1) */
    float *data;
    int    ncols, nrows;
    double xll, yll, cellsize, nodata;
} DmkPopulation;

void   dmk_pop_init_uniform(DmkPopulation *p, real_t density_km2, real_t day_night);
int    dmk_pop_load_asc(DmkPopulation *p, const char *path);  /* 0 on success */
void   dmk_pop_free(DmkPopulation *p);
real_t dmk_pop_density(const DmkPopulation *p, real_t lat, real_t lon); /* people/km^2 */

/* ---- Casualty options ------------------------------------------------- */
typedef struct {
    real_t protection_factor;      /* fallout/prompt dose divided by this (>=1) */
    real_t thermal_exposed_frac;   /* fraction with direct line-of-sight to fireball */
    real_t exposure_hours;         /* fallout dose integration window (default 48) */
    real_t visibility_km;          /* for thermal */
} DmkCasualtyOpts;

void dmk_casualty_opts_defaults(DmkCasualtyOpts *o);

/* ---- Results ---------------------------------------------------------- */
typedef struct {
    double population_in_domain;
    double fatalities;
    double injuries;
    /* attribution (a fatality may have multiple causes; these are the
     * expected counts attributable to each mechanism, not exclusive) */
    double fatal_blast;
    double fatal_thermal;
    double fatal_prompt;
    double fatal_fallout;
} DmkCasualties;

/* Lethality probabilities (0..1) for a single mechanism. */
real_t dmk_pfatal_blast(real_t overpressure_psi);
real_t dmk_pfatal_thermal(real_t cal_cm2);
real_t dmk_pfatal_radiation(real_t dose_rem);

/* Integrate casualties over the model grid using the effect models and the
 * fallout dose field. */
void dmk_casualties_compute(const DmkModel *model, const DmkPopulation *pop,
                            const DmkCasualtyOpts *opts, DmkCasualties *out);

#endif /* DMK_CASUALTIES_H */
