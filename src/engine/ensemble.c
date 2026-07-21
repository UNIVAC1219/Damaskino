/* ensemble.c - see ensemble.h */
#include "ensemble.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

void dmk_ensemble_spec_defaults(DmkEnsembleSpec *s) {
    s->yield_cv = 0.20;       /* +/-20% yield uncertainty */
    s->wind_speed_cv = 0.25;
    s->wind_dir_sd = 15.0;    /* degrees */
    s->fission_sd = 0.10;
}

/* Deterministic PRNG (xorshift) so results are reproducible from the seed. */
static unsigned long long g_state;
static void rng_seed(unsigned s) { g_state = 0x9E3779B97F4A7C15ULL ^ (s + 1); }
static double rng_uniform(void) {
    g_state ^= g_state << 13; g_state ^= g_state >> 7; g_state ^= g_state << 17;
    return ((g_state >> 11) & 0x1FFFFFFFFFFFFFULL) / (double)0x20000000000000ULL;
}
static double rng_normal(void) {
    /* Box-Muller */
    double u1 = rng_uniform(), u2 = rng_uniform();
    if (u1 < 1e-12) u1 = 1e-12;
    return sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
}

static void perturb(const DmkScenario *base, const DmkEnsembleSpec *sp, DmkScenario *out) {
    *out = *base;
    /* lognormal yield */
    double z = rng_normal();
    out->weapon.yield_kt = base->weapon.yield_kt * exp(sp->yield_cv * z);
    if (out->weapon.yield_kt < 0.001) out->weapon.yield_kt = 0.001;
    /* fission fraction (clamped) */
    double f = base->weapon.fission_fraction + sp->fission_sd * rng_normal();
    if (f < 0.0) f = 0.0;
    if (f > 1.0) f = 1.0;
    out->weapon.fission_fraction = f;
    /* wind: perturb every layer's speed (shared factor) and direction (shared offset) */
    double spd_factor = exp(sp->wind_speed_cv * rng_normal());
    double dir_offset = sp->wind_dir_sd * rng_normal();
    for (int i = 0; i < out->atmosphere.n_layers; i++) {
        out->atmosphere.layer[i].speed_kts *= spd_factor;
        double d = out->atmosphere.layer[i].direction_deg + dir_offset;
        while (d < 0) d += 360.0;
        while (d >= 360.0) d -= 360.0;
        out->atmosphere.layer[i].direction_deg = d;
    }
}

int dmk_ensemble_run(const DmkScenario *scenario, const DmkEnsembleSpec *spec,
                     int samples, unsigned seed,
                     const real_t *levels, int nlevels,
                     DmkEnsembleResult *out) {
    if (samples < 1 || nlevels < 1 || nlevels > 8) return -1;
    memset(out, 0, sizeof(*out));
    int n = scenario->cfg.grid_n;
    size_t ncell = (size_t)n * n;
    out->n = n; out->cell_km = scenario->cfg.cell_km;
    out->samples = samples; out->nlevels = nlevels;
    for (int l = 0; l < nlevels; l++) {
        out->levels[l] = levels[l];
        out->prob[l] = (real_t *)calloc(ncell, sizeof(real_t));
        if (!out->prob[l]) { dmk_ensemble_free(out); return -2; }
    }

    rng_seed(seed);
    int ok = 0;
    for (int s = 0; s < samples; s++) {
        DmkScenario pert;
        perturb(scenario, spec, &pert);
        DmkModel m;
        if (dmk_run(&pert, &m) != 0) continue;   /* skip a bad draw */
        out->gz_x = m.gz_x; out->gz_y = m.gz_y;
        for (size_t i = 0; i < ncell; i++) {
            real_t dose = m.grid.cell[i].dose_rate_rhr;
            for (int l = 0; l < nlevels; l++)
                if (dose >= levels[l]) out->prob[l][i] += 1.0;
        }
        dmk_model_free(&m);
        ok++;
    }
    if (ok == 0) { dmk_ensemble_free(out); return -3; }
    for (int l = 0; l < nlevels; l++)
        for (size_t i = 0; i < ncell; i++)
            out->prob[l][i] /= (real_t)ok;
    out->samples = ok;
    return 0;
}

void dmk_ensemble_free(DmkEnsembleResult *r) {
    if (!r) return;
    for (int l = 0; l < 8; l++) { free(r->prob[l]); r->prob[l] = NULL; }
}
