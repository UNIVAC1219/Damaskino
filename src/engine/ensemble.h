/*
 * ensemble.h - Monte Carlo uncertainty propagation.
 *
 * Real inputs carry error bars. This perturbs YIELD (lognormal), WIND SPEED
 * (lognormal factor) and WIND DIRECTION (normal offset), and FISSION FRACTION
 * (normal, clamped), then runs the engine over the ensemble to yield
 * probabilistic dose contours (exceedance probability per cell) -- bands, not
 * single lines. When a wind column is supplied the SAME per-member speed factor
 * and direction rotation are applied to it and the Lagrangian model runs;
 * otherwise the WSEG path runs on the perturbed hand-entered profile.
 * (HOB and particle-size perturbation are not yet implemented.)
 */
#ifndef DMK_ENSEMBLE_H
#define DMK_ENSEMBLE_H
#include "damaskino.h"

struct DmkWindColumn;

typedef struct {
    real_t yield_cv;      /* lognormal coefficient of variation on yield */
    real_t wind_speed_cv; /* fractional sigma on wind speed */
    real_t wind_dir_sd;   /* sigma on wind direction, degrees */
    real_t fission_sd;    /* sigma on fission fraction */
} DmkEnsembleSpec;

void dmk_ensemble_spec_defaults(DmkEnsembleSpec *s);

/* Per dose level, the fraction of ensemble members in which each cell exceeds
 * the level. levels[] ascending; prob[l] is an n*n array (caller-owned via the
 * result struct). */
typedef struct {
    int     n;                 /* grid side */
    real_t  cell_km;
    int     samples;
    int     nlevels;
    real_t  levels[8];         /* R/hr thresholds */
    real_t *prob[8];           /* n*n exceedance probability grids */
    int     gz_x, gz_y;
    int     used_lagrangian;   /* 1 if the Lagrangian+wind model drove members */
} DmkEnsembleResult;

/* Run `samples` perturbed members of `scenario`. If `wind` is a ready column
 * the Lagrangian model runs (perturbed wind); otherwise WSEG. Returns 0 on
 * success; caller frees with dmk_ensemble_free. Deterministic given `seed`. */
int  dmk_ensemble_run(const DmkScenario *scenario, const DmkEnsembleSpec *spec,
                      int samples, unsigned seed,
                      const real_t *levels, int nlevels,
                      const struct DmkWindColumn *wind,
                      DmkEnsembleResult *out);
void dmk_ensemble_free(DmkEnsembleResult *r);

#endif /* DMK_ENSEMBLE_H */
