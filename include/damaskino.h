/*
 * damaskino.h - Public engine API and core data model.
 *
 * Damaskino: a globally-applicable nuclear-effects simulator for civil-defense,
 * preparedness, and education. Models consequences from public science and
 * public datasets. Contains no weapon-design content.
 *
 * See plan.md for the full architecture and roadmap.
 */
#ifndef DAMASKINO_H
#define DAMASKINO_H

#include <stddef.h>
#include "dmk_units.h"

#define DMK_VERSION_MAJOR 0
#define DMK_VERSION_MINOR 1
#define DMK_VERSION_PATCH 0
#define DMK_VERSION_STRING "0.1.0"

/* Particle size classes (log-normal WSEG-10 distribution), microns. */
#define DMK_NUM_PARTICLE_CLASSES 12

/* Wind altitude layers, feet. Default profile; overridable per scenario. */
#define DMK_NUM_WIND_LAYERS 6

struct DmkDem;         /* forward decl; see src/engine/terrain.h */
struct DmkWindColumn;  /* forward decl; see src/weather/weather.h */

/* ---- Scenario inputs -------------------------------------------------- */

typedef struct {
    real_t yield_kt;          /* total weapon yield, kilotons */
    int    is_surface_burst;  /* 1 = surface, 0 = air burst */
    real_t hob_m;             /* height of burst, meters (0 for surface) */
    real_t fission_fraction;  /* fraction of yield from fission [0,1] */
} DmkWeapon;

typedef struct {
    real_t altitude_ft;       /* layer altitude */
    real_t speed_kts;         /* wind speed */
    real_t direction_deg;     /* meteorological: FROM this bearing */
} DmkWindLayer;

typedef struct {
    DmkWindLayer layer[DMK_NUM_WIND_LAYERS];
    int    n_layers;
    real_t shear_factor;
} DmkAtmosphere;

typedef struct {
    real_t lat;               /* ground zero latitude, degrees */
    real_t lon;               /* ground zero longitude, degrees */
    char   name[128];
} DmkGroundZero;

typedef struct {
    int    grid_n;            /* cells per side (square grid) */
    real_t cell_km;           /* cell resolution, km */
    real_t ref_time_hr;       /* reference time for dose rate (H+ hours) */
    real_t max_range_km;      /* analysis range for sector/contour reports */
} DmkConfig;

typedef struct {
    DmkWeapon     weapon;
    DmkAtmosphere atmosphere;
    DmkGroundZero gz;
    DmkConfig     cfg;
    int           terrain_loc;   /* embedded-terrain index; 0 = flat (UNIVAC path) */
} DmkScenario;

/* ---- Compute state / results ------------------------------------------ */

typedef struct {
    real_t cloud_top_km;
    real_t cloud_base_km;
    real_t stem_diameter_km;
    real_t stabilization_min;
} DmkCloud;

typedef struct {
    real_t median_microns;
    real_t geometric_sigma;
    real_t mass_fraction[DMK_NUM_PARTICLE_CLASSES];
} DmkParticles;

typedef struct {
    real_t dose_rate_rhr;     /* dose rate at H+1, R/hr */
    real_t arrival_hr;        /* first fallout arrival, hours */
    int    particle_class_max;
} DmkCell;

typedef struct {
    int     n;                /* cells per side */
    real_t  cell_km;
    DmkCell *cell;            /* heap array, n*n, row-major (y*n + x) */
} DmkGrid;

typedef struct {
    DmkScenario  scenario;
    DmkCloud     cloud;
    DmkParticles particles;
    DmkGrid      grid;
    int          gz_x, gz_y;  /* ground zero grid coordinates */
    const struct DmkDem *dem; /* optional terrain (NULL = flat) */
    const struct DmkWindColumn *wind; /* optional real winds (NULL = WSEG) */
    /* Activity accounting (fallout mass conservation / domain adequacy) */
    real_t       activity_emitted;   /* total activity released into the column */
    real_t       activity_on_grid;   /* activity deposited within the grid */
    real_t       off_grid_fraction;  /* 1 - on_grid/emitted; >0 => domain too small */
    /* Provenance */
    char         model_versions[256];
} DmkModel;

/* ---- Terrain (embedded 4-bit path; global DEM added in Phase 2) ------- */
typedef struct {
    unsigned char *data;      /* heap, w*h elevation nibbles (0-15) */
    int  w, h;
    int  active;
} DmkTerrain;

/* ---- Engine API ------------------------------------------------------- */

/* Allocate and run the full model for a scenario. Returns 0 on success.
 * On success, caller must call dmk_model_free(model). */
int  dmk_run(const DmkScenario *scenario, DmkModel *model);
/* As dmk_run, but attaches an optional terrain DEM (may be NULL) that
 * modulates fallout deposition. */
int  dmk_run_ex(const DmkScenario *scenario, const struct DmkDem *dem, DmkModel *model);
/* Full form: optional DEM and optional real wind column. When a wind column is
 * supplied the modern Lagrangian fallout model is used; otherwise WSEG-10. */
int  dmk_run_full(const DmkScenario *scenario, const struct DmkDem *dem,
                  const struct DmkWindColumn *wind, DmkModel *model);
void dmk_model_free(DmkModel *model);

/* Lagrangian particle fallout (Phase 3), used when a wind column is present. */
void dmk_lagrangian_deposit(DmkModel *model);

/* Apply terrain valley/ridge redistribution to the fallout grid (mass-
 * conserving; no-op without a DEM). Applied after either fallout model. */
void dmk_terrain_redistribute(DmkModel *model);

/* Grid helpers */
static inline DmkCell *dmk_grid_at(DmkGrid *g, int x, int y) {
    return &g->cell[(size_t)y * g->n + x];
}

/* Cloud / particle / atmosphere sub-models (exposed for testing) */
void   dmk_cloud_compute(const DmkScenario *sc, DmkCloud *out);
void   dmk_particles_compute(const DmkScenario *sc, DmkParticles *out);
void   dmk_atmosphere_estimate_aloft(DmkAtmosphere *atm);
real_t dmk_settling_velocity(real_t diameter_microns, real_t altitude_m);
real_t dmk_fission_activity(real_t yield_kt, real_t time_hr, real_t fission_fraction);

/* Fallout deposition (Phase 0: WSEG-10; modernized in Phase 3). */
void dmk_fallout_deposit(DmkModel *model);

/* Default scenario (calm, flat, sensible grid). */
void dmk_scenario_defaults(DmkScenario *sc);

extern const real_t DMK_PARTICLE_DIAMETERS[DMK_NUM_PARTICLE_CLASSES];
extern const real_t DMK_DEFAULT_WIND_ALTITUDES_FT[DMK_NUM_WIND_LAYERS];

#endif /* DAMASKINO_H */
