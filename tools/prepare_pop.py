#!/usr/bin/env python3
"""
prepare_pop.py - Convert ANY population raster into the population grid the engine
reads (`--pop-asc`): people per square kilometre, in geographic EPSG:4326, clipped
to a target area.  This is the piece that makes GHS-POP / WorldPop / Meta-HRSL
usable, because the engine samples population DENSITY on a lon/lat grid and most
sources ship either counts-per-cell and/or a projected CRS (e.g. GHS-POP is
per-cell counts in Mollweide ESRI:54009).  This tool fixes both:

  * reprojects the source to EPSG:4326 (lon/lat), and
  * converts per-cell COUNTS to density (people/km^2) using each source pixel's
    true ground area (exact for equal-area projections like Mollweide; latitude-
    dependent for geographic sources).

This reads only a WINDOW around the target, so it works equally on a single tile
or on one big GLOBAL file (e.g. the global GHS-POP raster or NASA SEDAC GPWv4) --
it never loads the whole thing into memory.  If the JRC download server keeps
timing out, fetch the global/tile file with a RESUMABLE downloader so a dropped
connection just continues instead of restarting:
    Windows PowerShell:  Start-BitsTransfer -Source "<url>" -Destination pop.zip
    aria2c (fast, multi-connection):  aria2c -x8 -s8 -c "<url>"
    curl / wget:  curl -C - -O "<url>"   |   wget -c "<url>"
Reliable alternatives to the JRC server: NASA SEDAC GPWv4 (global, WGS84, already
people/km^2 -> use --units density), or Google Earth Engine export of GHS_POP.

Usage:
  # GHS-POP 100 m tile (per-cell counts, Mollweide):
  python tools/prepare_pop.py --raster GHS_POP_..._R4_C20.tif --units count \
         --lat 55.7558 --lon 37.6173 --radius-km 60 --out pop/moscow.asc

  # WorldPop / a raster that is already people/km^2:
  python tools/prepare_pop.py --raster worldpop_density.tif --units density \
         --lat 39.9042 --lon 116.4074 --radius-km 60 --out pop/beijing.asc

Requires rasterio + numpy (see requirements.txt).
"""
import argparse, math, os, sys


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--raster", required=True, help="source population raster (any CRS)")
    ap.add_argument("--lat", type=float, required=True)
    ap.add_argument("--lon", type=float, required=True)
    ap.add_argument("--radius-km", type=float, default=60.0)
    ap.add_argument("--out", required=True)
    ap.add_argument("--units", choices=["count", "density", "auto"], default="auto",
                    help="'count' = people per source cell (GHS-POP, WorldPop count); "
                         "'density' = already people/km^2; 'auto' guesses (default)")
    ap.add_argument("--max-cells", type=int, default=1200,
                    help="cap the output grid side to bound file size")
    args = ap.parse_args()

    try:
        import numpy as np
        import rasterio
        from rasterio.warp import reproject, Resampling, transform_bounds
        from rasterio.transform import from_origin
    except ImportError:
        print("ERROR: needs rasterio + numpy (pip install -r requirements.txt)", file=sys.stderr)
        return 1

    # ---- destination grid: EPSG:4326 box around the target --------------------
    dlat = args.radius_km / 111.32
    dlon = args.radius_km / (111.32 * max(math.cos(math.radians(args.lat)), 1e-6))
    W, E = args.lon - dlon, args.lon + dlon
    S, N = args.lat - dlat, args.lat + dlat
    span = max(E - W, N - S)
    cellsize = span / args.max_cells
    ncols = max(1, int(round((E - W) / cellsize)))
    nrows = max(1, int(round((N - S) / cellsize)))
    dst_transform = from_origin(W, N, cellsize, cellsize)
    dst = np.full((nrows, ncols), -9999.0, dtype="float32")

    with rasterio.open(args.raster) as src:
        from rasterio.windows import from_bounds, Window
        # Read only a WINDOW around the target (plus a margin) so a GLOBAL source
        # file works without loading gigabytes.  Transform the EPSG:4326 target
        # box into the source CRS to locate the window.
        try:
            sb = transform_bounds("EPSG:4326", src.crs, W, S, E, N, densify_pts=21)
        except Exception:
            sb = (W, S, E, N)
        mx = (sb[2] - sb[0]) * 0.10 or abs(src.transform.a)
        my = (sb[3] - sb[1]) * 0.10 or abs(src.transform.e)
        win = from_bounds(sb[0] - mx, sb[1] - my, sb[2] + mx, sb[3] + my,
                          src.transform).round_offsets().round_lengths()
        full = Window(0, 0, src.width, src.height)
        try:
            win = win.intersection(full)
        except Exception:
            win = None
        if win is None or win.width < 1 or win.height < 1:
            print("ERROR: the target area is outside this raster's coverage "
                  "(wrong tile for this lat/lon?).", file=sys.stderr)
            return 1
        src_arr = src.read(1, window=win).astype("float64")
        win_transform = src.window_transform(win)
        src_nodata = src.nodata

        mask = np.zeros(src_arr.shape, dtype=bool)
        if src_nodata is not None:
            mask |= (src_arr == src_nodata)
        mask |= ~np.isfinite(src_arr)
        mask |= (src_arr < 0)

        units = args.units
        if units == "auto":
            # Heuristic: geographic degrees + small values -> likely density;
            # projected meters (GHS-POP Mollweide) -> almost always counts.
            hasvalid = np.any(~mask)
            peak = float(np.max(src_arr[~mask])) if hasvalid else 0.0
            units = "density" if (src.crs and src.crs.is_geographic and peak < 1000) \
                    else "count"
            print(f"  --units auto -> treating source as '{units}'")

        if units == "count":
            area_km2 = _pixel_area_km2(win_transform, src.crs, src_arr.shape, np)
            with np.errstate(divide="ignore", invalid="ignore"):
                density = src_arr / area_km2
        else:
            density = src_arr
        density = density.astype("float32")
        density[mask] = -9999.0

        reproject(
            source=density, destination=dst,
            src_transform=win_transform, src_crs=src.crs,
            dst_transform=dst_transform, dst_crs="EPSG:4326",
            src_nodata=-9999.0, dst_nodata=-9999.0,
            resampling=Resampling.bilinear,
        )

    valid = dst[dst != -9999.0]
    from _asc import write_ascii_grid
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    yll = N - nrows * cellsize
    write_ascii_grid(args.out, dst, W, yll, cellsize, nodata=-9999, fmt="{:.4g}")
    peak = float(valid.max()) if valid.size else 0.0
    mean = float(valid.mean()) if valid.size else 0.0
    print(f"Wrote {args.out}: {ncols}x{nrows} @ {cellsize:.6f} deg/cell "
          f"(people/km^2, EPSG:4326)")
    print(f"  density: mean {mean:.0f}, peak {peak:.0f} people/km^2 "
          f"({args.radius_km} km around {args.lat},{args.lon})")
    return 0


def _pixel_area_km2(transform, crs, shape, np):
    """Ground area (km^2) of each source pixel, for count->density conversion.

    `transform` is the (windowed) source affine.  Exact & constant for
    equal-area/projected metre CRSs (e.g. GHS-POP Mollweide); latitude-dependent
    for a geographic (degree) source."""
    a = transform.a          # x pixel size
    e = transform.e          # y pixel size (negative, north-up)
    if crs and crs.is_geographic:
        # degrees -> km; area varies with latitude (row).
        nrows = shape[0]
        rows = np.arange(nrows)
        lats = transform.f + (rows + 0.5) * e          # latitude at each row centre
        dlat_km = abs(e) * 111.32
        dlon_km = abs(a) * 111.32 * np.cos(np.radians(lats))
        col_area = (dlat_km * dlon_km)                 # per-row area
        return col_area[:, None] * np.ones((1, shape[1]))
    # projected: assume linear unit = metre (Mollweide 54009, UTM, etc.)
    return np.full(shape, abs(a * e) / 1.0e6)


if __name__ == "__main__":
    sys.exit(main())
