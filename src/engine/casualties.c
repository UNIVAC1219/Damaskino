/*
 * casualties.c - see casualties.h
 *
 * Lethality functions are logistic in ln(dose) with anchors from the open
 * literature:
 *   - Blast:     ~50% fatal at 10 psi (building-collapse dominated), ~10% at
 *                5 psi, ~90% at 20 psi.
 *   - Thermal:   ~50% fatal at 12 cal/cm^2 (extensive 3rd-degree burns without
 *                burn care), ~25% at the 8 cal/cm^2 3rd-degree threshold.
 *   - Radiation: acute whole-body LD50 ~ 450 rem without treatment.
 * These are consequence estimates for preparedness/education; magnitudes are
 * calibrated in Phase 3.5. See docs/PHYSICS_NOTES.md.
 */
#include "casualties.h"
#include "effects.h"
#include "terrain.h"
#include "geo.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dmk_portable.h"

/* ---- Population ------------------------------------------------------- */
void dmk_pop_init_uniform(DmkPopulation *p, real_t density_km2, real_t day_night) {
    memset(p, 0, sizeof(*p));
    p->mode = 0;
    p->uniform_density = density_km2;
    p->day_night_factor = (day_night > 0.0) ? day_night : 1.0;
}

int dmk_pop_load_asc(DmkPopulation *p, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    memset(p, 0, sizeof(*p));
    p->mode = 1;
    p->day_night_factor = 1.0;
    p->nodata = -9999.0;
    char key[64];
    /* ESRI ASCII grid header: ncols, nrows, xllcorner, yllcorner, cellsize, NODATA_value */
    if (fscanf(f, "%63s %d", key, &p->ncols) != 2) { fclose(f); return -1; }
    if (fscanf(f, "%63s %d", key, &p->nrows) != 2) { fclose(f); return -1; }
    if (fscanf(f, "%63s %lf", key, &p->xll) != 2) { fclose(f); return -1; }
    if (fscanf(f, "%63s %lf", key, &p->yll) != 2) { fclose(f); return -1; }
    if (fscanf(f, "%63s %lf", key, &p->cellsize) != 2) { fclose(f); return -1; }
    /* optional NODATA_value */
    long pos = ftell(f);
    if (fscanf(f, "%63s", key) == 1) {
        if (strcasecmp(key, "NODATA_value") == 0) {
            if (fscanf(f, "%lf", &p->nodata) != 1) { fclose(f); return -1; }
        } else {
            fseek(f, pos, SEEK_SET);  /* it was data */
        }
    }
    size_t n = (size_t)p->ncols * p->nrows;
    p->data = (float *)malloc(n * sizeof(float));
    if (!p->data) { fclose(f); return -1; }
    for (size_t i = 0; i < n; i++) {
        double v;
        if (fscanf(f, "%lf", &v) != 1) { free(p->data); p->data = NULL; fclose(f); return -1; }
        p->data[i] = (float)v;
    }
    fclose(f);
    return 0;
}

void dmk_pop_free(DmkPopulation *p) {
    if (p && p->data) { free(p->data); p->data = NULL; }
}

real_t dmk_pop_density(const DmkPopulation *p, real_t lat, real_t lon) {
    if (p->mode == 0)
        return p->uniform_density * p->day_night_factor;
    /* raster: map lat/lon to col/row (row 0 is the north/top edge) */
    int col = (int)((lon - p->xll) / p->cellsize);
    int row = (int)((p->yll + p->nrows * p->cellsize - lat) / p->cellsize);
    if (col < 0 || col >= p->ncols || row < 0 || row >= p->nrows) return 0.0;
    float v = p->data[(size_t)row * p->ncols + col];
    if ((double)v == p->nodata || v < 0.0f) return 0.0;
    return v * p->day_night_factor;
}

/* ---- Options --------------------------------------------------------- */
void dmk_casualty_opts_defaults(DmkCasualtyOpts *o) {
    o->pf_fallout = 1.0;           /* unsheltered */
    o->pf_prompt = 1.0;            /* prompt radiation barely shielded by default */
    o->thermal_exposed_frac = 0.5; /* half have direct LOS to the fireball */
    o->exposure_hours = 48.0;
    o->visibility_km = 20.0;
}

/* ---- Lethality functions --------------------------------------------- */
static real_t logistic_ln(real_t x, real_t x50, real_t k) {
    if (x <= 0.0) return 0.0;
    return 1.0 / (1.0 + exp(-k * (log(x) - log(x50))));
}
real_t dmk_pfatal_blast(real_t psi)      { return logistic_ln(psi, 10.0, 3.17); }
real_t dmk_pfatal_thermal(real_t cal)    { return logistic_ln(cal, 12.0, 2.70); }
real_t dmk_pfatal_radiation(real_t rem)  { return logistic_ln(rem, 450.0, 4.30); }

/* Injury thresholds (non-fatal casualties): blast ~50% injured at 2 psi
 * (debris/translation), thermal ~50% at 3.5 cal/cm^2 (2nd-degree), radiation
 * ~50% at 150 rem (acute radiation sickness). */
real_t dmk_pinjury_blast(real_t psi)     { return logistic_ln(psi, 2.0, 2.5); }
real_t dmk_pinjury_thermal(real_t cal)   { return logistic_ln(cal, 3.5, 2.5); }
real_t dmk_pinjury_radiation(real_t rem) { return logistic_ln(rem, 150.0, 3.0); }

/* Accumulated fallout dose (R ~ rem) from a cell's H+1 dose rate, integrated
 * from arrival to the exposure window using the Way-Wigner t^-1.2 law:
 *   D = integral_ta^tf I(1) t^-1.2 dt = 5 I(1) (ta^-0.2 - tf^-0.2). */
real_t dmk_fallout_dose_rem(real_t rate_h1, real_t arrival_hr,
                            real_t exposure_hr, real_t pf) {
    if (rate_h1 <= 0.0) return 0.0;
    real_t ta = arrival_hr > 0.1 ? arrival_hr : 0.1;
    real_t tf = exposure_hr;
    if (tf <= ta) return 0.0;
    real_t dose = 5.0 * rate_h1 * (pow(ta, -0.2) - pow(tf, -0.2));
    if (pf > 1.0) dose /= pf;
    return dose;
}

/* ---- Integrator ------------------------------------------------------ */
void dmk_casualties_compute(const DmkModel *m, const DmkPopulation *pop,
                            const DmkCasualtyOpts *opts, DmkCasualties *out) {
    memset(out, 0, sizeof(*out));
    const DmkScenario *sc = &m->scenario;
    real_t cell_km = m->grid.cell_km;
    real_t cell_area = cell_km * cell_km;
    real_t yield = sc->weapon.yield_kt;
    int surface = sc->weapon.is_surface_burst;
    real_t fission = sc->weapon.fission_fraction;

    /* Terrain line-of-sight: emitter is the fireball at the burst point. */
    int use_terrain = dmk_dem_ready(m->dem);
    real_t fireball_m = 55.0 * pow(yield, 0.4);          /* nominal fireball radius */
    real_t burst_h = surface ? fireball_m : sc->weapon.hob_m;
    real_t e_alt_asl = (use_terrain ? dmk_dem_elev(m->dem, sc->gz.lat, sc->gz.lon) : 0.0) + burst_h;
    /* LOS masking only matters where thermal or prompt are non-negligible; beyond
     * that radius both are ~0, so skip the (expensive) raycast. */
    real_t los_max_km = 0.0;
    if (use_terrain) {
        real_t tr = dmk_thermal_range_km(yield, surface, 2.5, opts->visibility_km);
        real_t pr = dmk_prompt_range_km(yield, fission, 100.0);
        los_max_km = (tr > pr ? tr : pr) * 1.1;
    }

    for (int gy = 0; gy < m->grid.n; gy++) {
        for (int gx = 0; gx < m->grid.n; gx++) {
            real_t east = (gx - m->gz_x) * cell_km;
            real_t north = (gy - m->gz_y) * cell_km;
            real_t r_km = sqrt(east * east + north * north);

            real_t lat, lon;
            dmk_offset_to_latlon(sc->gz.lat, sc->gz.lon, east, north, &lat, &lon);
            real_t density = dmk_pop_density(pop, lat, lon);
            if (density <= 0.0) continue;
            real_t people = density * cell_area;
            out->population_in_domain += people;

            /* Per-mechanism dose/intensity at this cell. */
            real_t psi = dmk_blast_overpressure_psi(yield, surface, r_km);
            real_t pb = dmk_pfatal_blast(psi);

            /* Thermal is a POPULATION SPLIT: a fraction have line-of-sight to
             * the fireball at full fluence; the rest (indoors/shadowed) get
             * ~none. Applying the fraction to fluence would bias the nonlinear
             * probit, so we partition at the combination step below. */
            real_t cal_full = dmk_thermal_fluence(yield, surface, r_km, opts->visibility_km);
            real_t pt_full = dmk_pfatal_thermal(cal_full);
            real_t exp_frac = opts->thermal_exposed_frac;

            /* Prompt radiation attenuates along the SLANT path from the burst
             * point; for an air burst that is sqrt(ground^2 + HOB^2). */
            real_t hob_km = sc->weapon.hob_m / 1000.0;
            real_t slant_km = sqrt(r_km * r_km + hob_km * hob_km);
            real_t prem = dmk_prompt_dose_rem(yield, fission, slant_km) / opts->pf_prompt;

            /* Terrain masking: if the fireball is hidden behind terrain, direct
             * thermal is blocked entirely and prompt radiation drops to residual
             * skyshine (~10%). */
            if (use_terrain && r_km > 0.05 && r_km <= los_max_km) {
                int los = dmk_terrain_los(m->dem, sc->gz.lat, sc->gz.lon, e_alt_asl,
                                          lat, lon, 1.7);
                if (!los) { exp_frac = 0.0; prem *= 0.1; }
            }
            real_t pp = dmk_pfatal_radiation(prem);

            const DmkCell *c = &m->grid.cell[(size_t)gy * m->grid.n + gx];
            real_t frem = dmk_fallout_dose_rem(c->dose_rate_rhr, c->arrival_hr,
                                               opts->exposure_hours,
                                               opts->pf_fallout);
            real_t pf = dmk_pfatal_radiation(frem);

            /* Combined survival across independent mechanisms, thermal applied
             * only to the exposed sub-population. */
            real_t non_thermal_surv = (1.0 - pb) * (1.0 - pp) * (1.0 - pf);
            real_t surv = non_thermal_surv * (exp_frac * (1.0 - pt_full) + (1.0 - exp_frac));
            out->fatalities += people * (1.0 - surv);

            /* Attribution (non-exclusive expected counts). */
            out->fatal_blast   += people * pb;
            out->fatal_thermal += people * exp_frac * pt_full;
            out->fatal_prompt  += people * pp;
            out->fatal_fallout += people * pf;

            /* Injuries among survivors: continuous per-mechanism dose-response
             * (not a flat fraction), thermal weighted by the exposed fraction. */
            real_t ib = dmk_pinjury_blast(psi);
            real_t it = exp_frac * dmk_pinjury_thermal(cal_full);
            real_t ip = dmk_pinjury_radiation(prem);
            real_t ifa = dmk_pinjury_radiation(frem);
            real_t p_injury = 1.0 - (1.0 - ib) * (1.0 - it) * (1.0 - ip) * (1.0 - ifa);
            out->injuries += people * surv * p_injury;
        }
    }
}
