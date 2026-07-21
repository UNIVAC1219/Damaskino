/*
 * terrain.h - Global digital elevation model (DEM) and terrain line-of-sight.
 *
 * Loads an ESRI ASCII-grid DEM tile (the format OpenTopography / SRTM15+ /
 * Copernicus export) covering the area of interest and samples elevation at
 * any lat/lon by bilinear interpolation. Terrain line-of-sight (with Earth-
 * curvature correction) gates thermal radiation and prompt-radiation masking;
 * elevation modulates fallout deposition.
 *
 * Only the DEM *delivery* differs between a single regional tile and a global
 * tiled pyramid; the sampling/LOS API is identical, so the global pyramid is a
 * data step behind this interface.
 */
#ifndef DMK_TERRAIN_H
#define DMK_TERRAIN_H
#include "damaskino.h"

typedef struct DmkDem {
    float *elev;          /* nrows*ncols, row 0 = north edge */
    int    ncols, nrows;
    double xll, yll;      /* lower-left corner (lon, lat) */
    double cellsize;      /* degrees */
    double nodata;
    int    loaded;
} DmkDem;

int    dmk_dem_load_asc(DmkDem *d, const char *path);   /* 0 on success */
void   dmk_dem_free(DmkDem *d);
int    dmk_dem_ready(const DmkDem *d);

/* Elevation (m ASL) at lat/lon, bilinear. Returns 0 outside the tile. */
real_t dmk_dem_elev(const DmkDem *d, real_t lat, real_t lon);

/* Line-of-sight from an emitter at (elat,elon) at altitude e_alt_asl_m to a
 * receiver at (rlat,rlon) standing r_height_m above local terrain. Walks the
 * DEM along the path with Earth-curvature correction. Returns 1 if the emitter
 * is visible (unobstructed), 0 if terrain occludes it. If the DEM is not
 * loaded, returns 1 (flat-earth: always visible). */
int dmk_terrain_los(const DmkDem *d, real_t elat, real_t elon, real_t e_alt_asl_m,
                    real_t rlat, real_t rlon, real_t r_height_m);

#endif /* DMK_TERRAIN_H */
