#!/usr/bin/env python3
"""
fetch_dem.py - Download a target-area DEM straight from the OpenTopography Global
DEM API and write the ESRI ASCII grid the engine reads (`--dem`).  No web form,
no map-box drawing, and it handles the +/-180 deg antimeridian automatically by
splitting the request in two and mosaicking - so the "Queries across the 180
degrees longitude line are only supported for GMRT" error can't happen.

You need a free OpenTopography API key (portal.opentopography.org -> My Account).
Pass it with --api-key or set the OPENTOPO_API_KEY environment variable.

Usage:
  set OPENTOPO_API_KEY=xxxx: (Windows)   or   export OPENTOPO_API_KEY=xxxx (Unix)
  python tools/fetch_dem.py --lat 55.7558 --lon 37.6173 --radius-km 60 \
                            --out dem/moscow.asc

  # far-eastern target near the dateline - handled transparently:
  python tools/fetch_dem.py --lat 64.7 --lon 177.5 --radius-km 80 --out dem/anadyr.asc

demtypes (all EPSG:4326): SRTM15Plus (default, global +bathymetry, ~450 m),
COP30 (Copernicus 30 m land), COP90, SRTMGL1 (30 m), SRTMGL3 (90 m), NASADEM.

Requires requests + rasterio + numpy (see requirements.txt).
"""
import argparse, io, math, os, sys

API = "https://portal.opentopography.org/API/globaldem"


def _fetch_box(demtype, south, north, west, east, key):
    """Return raw GeoTIFF bytes for one bounding box from the OpenTopography API."""
    import requests
    params = {"demtype": demtype, "south": south, "north": north,
              "west": west, "east": east, "outputFormat": "GTiff", "API_Key": key}
    r = requests.get(API, params=params, timeout=180)
    if r.status_code != 200 or not r.content[:2] in (b"II", b"MM"):
        # API returns a text/JSON error body on failure (bad key, too large, etc.)
        msg = r.text[:300] if r.text else f"HTTP {r.status_code}"
        raise RuntimeError(f"OpenTopography API error ({r.status_code}): {msg}")
    return r.content


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--lat", type=float, required=True)
    ap.add_argument("--lon", type=float, required=True, help="in [-180, 180]")
    ap.add_argument("--radius-km", type=float, default=60.0)
    ap.add_argument("--out", required=True, help="output .asc (engine grid) or .tif (raw)")
    ap.add_argument("--demtype", default="SRTM15Plus")
    ap.add_argument("--api-key", default=os.environ.get("OPENTOPO_API_KEY"))
    ap.add_argument("--max-cells", type=int, default=1500,
                    help="cap the output grid side for .asc output")
    args = ap.parse_args()

    if not args.api_key:
        print("ERROR: no API key. Get a free one at portal.opentopography.org and\n"
              "       pass --api-key KEY or set OPENTOPO_API_KEY.", file=sys.stderr)
        return 2

    try:
        import numpy as np
        import rasterio
        from rasterio.io import MemoryFile
        from rasterio.merge import merge
    except ImportError:
        print("ERROR: needs rasterio + numpy + requests (pip install -r requirements.txt)",
              file=sys.stderr)
        return 1

    # Bounding box in degrees around the target.
    dlat = args.radius_km / 111.32
    dlon = args.radius_km / (111.32 * max(math.cos(math.radians(args.lat)), 1e-6))
    south, north = args.lat - dlat, args.lat + dlat
    west, east = args.lon - dlon, args.lon + dlon
    south, north = max(south, -90.0), min(north, 90.0)

    # Split at the antimeridian if the box runs off either edge.
    boxes = []
    if west < -180.0:
        boxes.append((south, north, west + 360.0, 180.0))   # wrapped west part
        boxes.append((south, north, -180.0, east))
    elif east > 180.0:
        boxes.append((south, north, west, 180.0))
        boxes.append((south, north, -180.0, east - 360.0))  # wrapped east part
    else:
        boxes.append((south, north, west, east))

    if len(boxes) > 1:
        print(f"Target is within {args.radius_km} km of the 180 deg meridian; "
              f"fetching {len(boxes)} tiles and mosaicking.")

    datasets = []
    for (s, n, w, e) in boxes:
        print(f"  requesting {args.demtype}  S{s:.3f} N{n:.3f} W{w:.3f} E{e:.3f} ...")
        tif = _fetch_box(args.demtype, s, n, w, e, args.api_key)
        datasets.append(MemoryFile(tif).open())

    # For a two-tile antimeridian mosaic, shift the wrapped tile so the two are
    # spatially contiguous (the engine grid is a small local patch, so using a
    # continuous longitude across 180 is correct and keeps cells square).
    if len(datasets) == 2 and (west < -180.0 or east > 180.0):
        merged, mtransform = _merge_across_dateline(datasets, np, rasterio)
    else:
        arr, mtransform = merge(datasets)
        merged = arr[0]

    nodata = datasets[0].nodata if datasets[0].nodata is not None else -9999
    for d in datasets:
        d.close()

    out = args.out
    if out.lower().endswith(".tif"):
        with rasterio.open(out, "w", driver="GTiff", height=merged.shape[0],
                           width=merged.shape[1], count=1, dtype=merged.dtype,
                           crs="EPSG:4326", transform=mtransform, nodata=nodata) as dst:
            dst.write(merged, 1)
        print(f"Wrote {out}: {merged.shape[1]}x{merged.shape[0]} GeoTIFF")
        return 0

    # ESRI ASCII (.asc) for the engine, optionally decimated to bound size.
    from _asc import write_ascii_grid
    step = max(1, int(max(merged.shape) / args.max_cells))
    data = merged[::step, ::step]
    cellsize = abs(mtransform.a) * step
    xll = mtransform.c
    yll = mtransform.f + mtransform.e * merged.shape[0]  # e < 0 (north-up)
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    write_ascii_grid(out, np.rint(data).astype("int64"), xll, yll, cellsize,
                     nodata=int(nodata), fmt="{:d}")
    print(f"Wrote {out}: {data.shape[1]}x{data.shape[0]} @ {cellsize:.6f} deg/cell "
          f"({args.radius_km} km around {args.lat},{args.lon}, {args.demtype})")
    return 0


def _merge_across_dateline(datasets, np, rasterio):
    """Mosaic two tiles that sit either side of +/-180 by re-referencing the
    eastern/western wrapped tile to a continuous longitude, then hstacking."""
    from rasterio.merge import merge
    # Re-base each dataset's transform onto a continuous axis: whichever tile has
    # the smaller left edge stays; the other is shifted by +360 or -360 so they
    # abut.  Simplest robust route: read both, place by rounded column offset.
    a0, t0 = merge([datasets[0]])
    a1, t1 = merge([datasets[1]])
    b0, b1 = a0[0], a1[0]
    # Decide order by longitude of left edge, treating the wrapped one continuously.
    left0, left1 = t0.c, t1.c
    if left1 < left0:
        left1 += 360.0
    if left1 < left0:
        b0, b1, t0, t1, left0, left1 = b1, b0, t1, t0, left1, left0
    # Align rows (they share latitude bounds); pad to same height if off-by-one.
    h = min(b0.shape[0], b1.shape[0])
    b0, b1 = b0[:h], b1[:h]
    merged = np.hstack([b0, b1])
    return merged, t0


if __name__ == "__main__":
    sys.exit(main())
