#!/usr/bin/env python3
"""
prepare_dem.py - Clip a global DEM to a target area and write the ESRI ASCII
grid the engine reads (`--dem`).

The engine consumes any DEM as an ESRI ASCII grid in geographic (lon/lat)
coordinates; this tool produces one for a target from a global source raster:

  - SRTM15+  (global topo+bathymetry, ~450 m) - the dataset this repo already
    references (readme.txt), e.g. from OpenTopography.
  - Copernicus GLO-30 (30 m over land).

Usage:
  prepare_dem.py --raster SRTM15Plus.tif --lat 38.8951 --lon -77.0364 \
                 --radius-km 60 --out data/dem/washington_dc.asc

Only the DEM *delivery* differs between a single regional tile and a global
tiled pyramid; the engine's sampling/LOS is identical, so a pyramid is a
drop-in extension of this step.

Requires rasterio + numpy (see requirements.txt).
"""
import argparse, os, sys, math

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--raster", required=True, help="source global DEM (GeoTIFF/asc, EPSG:4326)")
    ap.add_argument("--lat", type=float, required=True)
    ap.add_argument("--lon", type=float, required=True)
    ap.add_argument("--radius-km", type=float, default=60.0)
    ap.add_argument("--out", required=True)
    ap.add_argument("--max-cells", type=int, default=1200,
                    help="cap the output grid side to bound file size")
    args = ap.parse_args()

    try:
        import rasterio
        from rasterio.windows import from_bounds
        import numpy as np
    except ImportError:
        print("ERROR: needs rasterio + numpy (pip install -r requirements.txt)", file=sys.stderr)
        return 1

    # bounding box in degrees
    dlat = args.radius_km / 111.32
    dlon = args.radius_km / (111.32 * max(math.cos(math.radians(args.lat)), 1e-6))
    min_lon, max_lon = args.lon - dlon, args.lon + dlon
    min_lat, max_lat = args.lat - dlat, args.lat + dlat

    with rasterio.open(args.raster) as src:
        win = from_bounds(min_lon, min_lat, max_lon, max_lat, src.transform)
        data = src.read(1, window=win)
        # optional decimation to respect --max-cells
        step = max(1, int(max(data.shape) / args.max_cells))
        data = data[::step, ::step]
        nrows, ncols = data.shape
        # transform of the (decimated) window
        wt = src.window_transform(win)
        cellsize = wt.a * step  # degrees per output cell (assumes square, EPSG:4326)
        xll = wt.c
        yll = wt.f + wt.e * (nrows * step)  # wt.e is negative (north-up)
        nodata = src.nodata if src.nodata is not None else -9999

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w") as f:
        f.write(f"ncols {ncols}\nnrows {nrows}\n")
        f.write(f"xllcorner {xll}\nyllcorner {yll}\n")
        f.write(f"cellsize {abs(cellsize)}\nNODATA_value {int(nodata)}\n")
        for r in range(nrows):
            f.write(" ".join(str(int(v)) for v in data[r]) + "\n")

    print(f"Wrote {args.out}: {ncols}x{nrows} @ {abs(cellsize):.6f} deg/cell "
          f"({args.radius_km} km around {args.lat},{args.lon})")
    return 0

if __name__ == "__main__":
    sys.exit(main())
