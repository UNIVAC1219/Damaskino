/*
 * engine.c - Orchestration: allocate the heap grid, run the model pipeline,
 * and manage lifetime. Records model-version provenance.
 */
#include "damaskino.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void dmk_scenario_defaults(DmkScenario *sc) {
    memset(sc, 0, sizeof(*sc));

    sc->weapon.yield_kt = 100.0;
    sc->weapon.is_surface_burst = 1;
    sc->weapon.hob_m = 0.0;
    sc->weapon.fission_fraction = 0.5;

    sc->atmosphere.n_layers = DMK_NUM_WIND_LAYERS;
    sc->atmosphere.shear_factor = 1.0;
    for (int i = 0; i < DMK_NUM_WIND_LAYERS; i++) {
        sc->atmosphere.layer[i].altitude_ft = DMK_DEFAULT_WIND_ALTITUDES_FT[i];
        sc->atmosphere.layer[i].speed_kts = 0.0;
        sc->atmosphere.layer[i].direction_deg = 270.0;  /* from the west */
    }

    sc->gz.lat = 0.0;
    sc->gz.lon = 0.0;
    strncpy(sc->gz.name, "UNSPECIFIED", sizeof(sc->gz.name) - 1);

    sc->cfg.grid_n = 128;          /* full-profile default */
    sc->cfg.cell_km = 2.0;
    sc->cfg.ref_time_hr = 1.0;
    sc->cfg.max_range_km = 0.0;    /* 0 => derive from grid extent */
    sc->terrain_loc = 0;
}

static int grid_alloc(DmkGrid *g, int n, real_t cell_km) {
    if (n < 3 || n > 8192) return -1;
    g->n = n;
    g->cell_km = cell_km;
    g->cell = (DmkCell *)calloc((size_t)n * n, sizeof(DmkCell));
    return g->cell ? 0 : -1;
}

int dmk_run(const DmkScenario *scenario, DmkModel *model) {
    return dmk_run_ex(scenario, NULL, model);
}

int dmk_run_ex(const DmkScenario *scenario, const struct DmkDem *dem, DmkModel *model) {
    if (!scenario || !model) return -1;

    /* Defensive parameter guard: reject values that would produce NaN/inf
     * (negative yield -> pow(neg,0.25); ref_time 0 -> pow(0,-1.2)=inf). This
     * mirrors dmk_scenario_parse but also protects direct API / UNIVAC callers. */
    if (!(scenario->weapon.yield_kt > 0.0) || scenario->weapon.yield_kt > 1.0e7) return -3;
    if (!(scenario->cfg.ref_time_hr > 0.0)) return -3;
    if (scenario->weapon.fission_fraction < 0.0 || scenario->weapon.fission_fraction > 1.0) return -3;
    if (!(scenario->cfg.cell_km > 0.0)) return -3;

    memset(model, 0, sizeof(*model));
    model->scenario = *scenario;
    model->dem = dem;

    /* Derive analysis range if unset. */
    if (model->scenario.cfg.max_range_km <= 0.0)
        model->scenario.cfg.max_range_km =
            0.5 * model->scenario.cfg.grid_n * model->scenario.cfg.cell_km;

    if (grid_alloc(&model->grid, model->scenario.cfg.grid_n,
                   model->scenario.cfg.cell_km) != 0)
        return -2;

    model->gz_x = model->grid.n / 2;
    model->gz_y = model->grid.n / 2;

    /* Fill winds aloft if only the surface layer was supplied. */
    /* (Callers that provide a full profile set n_layers accordingly.) */

    dmk_cloud_compute(&model->scenario, &model->cloud);
    dmk_particles_compute(&model->scenario, &model->particles);
    dmk_fallout_deposit(model);

    snprintf(model->model_versions, sizeof(model->model_versions),
             "engine=%s;fallout=WSEG-10(ported);cloud=WSEG-10;decay=Way-Wigner-1.2;terrain=%s",
             DMK_VERSION_STRING, dem ? "DEM" : "flat");
    return 0;
}

void dmk_model_free(DmkModel *model) {
    if (!model) return;
    free(model->grid.cell);
    model->grid.cell = NULL;
    model->grid.n = 0;
}
