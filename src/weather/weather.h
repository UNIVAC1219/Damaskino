/*
 * weather.h - Ingested wind column for the Lagrangian fallout model.
 *
 * The JSON interchange (produced by tools/fetch_weather.py from ERA5 / GFS)
 * carries u/v wind on altitude levels plus a surface precipitation rate for
 * wet deposition. This module loads it and interpolates wind vs. altitude.
 * The hand-entered wind profile remains the offline / UNIVAC fallback.
 */
#ifndef DMK_WEATHER_H
#define DMK_WEATHER_H
#include "damaskino.h"

typedef struct DmkWindColumn {
    int     nlev;
    real_t *alt_m;        /* ascending altitude, m */
    real_t *u_ms;         /* eastward wind, m/s */
    real_t *v_ms;         /* northward wind, m/s */
    real_t  precip_mm_hr; /* surface precipitation (rainout) */
    real_t  lat, lon;
    char    source[32];
    int     loaded;
} DmkWindColumn;

int  dmk_weather_load(const char *path, DmkWindColumn *out);  /* 0 on success */
void dmk_weather_free(DmkWindColumn *w);
int  dmk_weather_ready(const DmkWindColumn *w);

/* Wind (u,v m/s) at an altitude by linear interpolation, clamped at the ends. */
void dmk_wind_at(const DmkWindColumn *w, real_t alt_m, real_t *u, real_t *v);

/* Build a wind column from a scenario's hand-entered layer profile (fallback
 * when no weather file is supplied). Converts knots/bearing to u/v m/s. */
void dmk_weather_from_atmosphere(const DmkAtmosphere *atm, DmkWindColumn *out);

#endif /* DMK_WEATHER_H */
