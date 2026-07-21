/*
 * geo.h - Local tangent-plane geographic conversion.
 * Equirectangular approximation, accurate at the scale of a fallout grid
 * (hundreds of km). Global DEM work in Phase 2 uses proper UTM per tile.
 */
#ifndef DMK_GEO_H
#define DMK_GEO_H
#include <math.h>
#include "dmk_units.h"

/* Convert an east/north offset in km from (lat0,lon0) to lat/lon degrees.
 * Output is clamped to valid WGS84 ranges: latitude to [-90,90] and longitude
 * wrapped to [-180,180], so the result is always emittable as valid GeoJSON
 * even for pathological near-pole ground zeros. */
static inline void dmk_offset_to_latlon(real_t lat0, real_t lon0,
                                        real_t east_km, real_t north_km,
                                        real_t *lat, real_t *lon) {
    real_t km_per_deg_lat = 111.32;
    real_t km_per_deg_lon = 111.32 * cos(lat0 * DMK_DEG_TO_RAD);
    if (km_per_deg_lon < 1.0e-6) km_per_deg_lon = 1.0e-6;  /* near poles */

    real_t la = lat0 + north_km / km_per_deg_lat;
    real_t lo = lon0 + east_km / km_per_deg_lon;

    if (la > 90.0)  la = 90.0;
    if (la < -90.0) la = -90.0;
    while (lo > 180.0)  lo -= 360.0;   /* antimeridian wrap */
    while (lo < -180.0) lo += 360.0;

    *lat = la;
    *lon = lo;
}

#endif /* DMK_GEO_H */
