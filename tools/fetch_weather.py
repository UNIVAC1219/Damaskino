#!/usr/bin/env python3
"""
fetch_weather.py - Extract a wind column from ERA5 or GFS/GDAS into the simple
JSON interchange the Damaskino engine ingests for the Lagrangian fallout model.

Design: GRIB2 / NetCDF decoding lives here (Python: cfgrib/xarray/eccodes),
where the libraries exist and are maintained. The C engine stays dependency-
free and reads a small JSON wind column. This is exactly the plan's "keep
GRIB2/NetCDF behind the weather module" boundary.

Sources:
  - ERA5 reanalysis (Copernicus CDS): NetCDF or GRIB, pressure-level u/v/z + tp.
  - GFS / GDAS (NOAA NCEP): GRIB2, UGRD/VGRD/HGT on isobaric levels + APCP.

Interchange schema (written to --out):
  {
    "source": "GFS", "valid_time": "2026-07-21T12:00Z",
    "lat": 38.90, "lon": -77.04,
    "precip_mm_hr": 0.0,
    "levels": [ {"altitude_m": 110, "u_ms": 3.1, "v_ms": -2.0,
                 "pressure_hpa": 1000}, ... ]   # ascending altitude
  }

Usage:
  fetch_weather.py --grib gfs.t12z.pgrb2.0p25.f000 --lat 38.9 --lon -77.04 \
                   --out weather/dc.json --source GFS
"""
import argparse, json, math, sys

def geopotential_to_altitude_m(z_gpm):
    """Geopotential height (gpm) -> geometric altitude (m). Small correction."""
    R = 6371000.0
    return R * z_gpm / (R - z_gpm) if z_gpm < R else z_gpm

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--grib", help="GRIB2/NetCDF file (GFS/GDAS/ERA5)")
    ap.add_argument("--lat", type=float, required=True)
    ap.add_argument("--lon", type=float, required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--source", default="GFS")
    ap.add_argument("--accum-hours", type=float, default=1.0,
                    help="accumulation window for tp/apcp precip fields (hours)")
    args = ap.parse_args()

    try:
        import xarray as xr
        import numpy as np
    except ImportError:
        print("ERROR: needs xarray + cfgrib (pip install xarray cfgrib netcdf4)",
              file=sys.stderr)
        return 1

    # Open isobaric u/v/gh. cfgrib exposes them per typeOfLevel.
    try:
        ds = xr.open_dataset(args.grib, engine="cfgrib",
                             backend_kwargs={"filter_by_keys":
                                             {"typeOfLevel": "isobaricInhPa"}})
    except Exception as e:
        print(f"ERROR: could not open {args.grib}: {e}", file=sys.stderr)
        return 1

    lon = args.lon % 360 if float(ds.longitude.max()) > 180 else args.lon
    col = ds.sel(latitude=args.lat, longitude=lon, method="nearest")

    u = col["u"].values; v = col["v"].values
    gh = col["gh"].values if "gh" in col else col["z"].values / 9.80665
    plev = col["isobaricInhPa"].values

    levels = []
    for i in range(len(plev)):
        if not (math.isfinite(u[i]) and math.isfinite(v[i]) and math.isfinite(gh[i])):
            continue
        levels.append({
            "altitude_m": round(geopotential_to_altitude_m(float(gh[i])), 1),
            "u_ms": round(float(u[i]), 3),
            "v_ms": round(float(v[i]), 3),
            "pressure_hpa": float(plev[i]),
        })
    levels.sort(key=lambda L: L["altitude_m"])

    # Surface precipitation (rainout) if available.
    precip = 0.0
    try:
        dsp = xr.open_dataset(args.grib, engine="cfgrib",
                              backend_kwargs={"filter_by_keys":
                                              {"typeOfLevel": "surface"}})
        for name in ("prate", "tp", "apcp"):
            if name in dsp:
                val = float(dsp[name].sel(latitude=args.lat, longitude=lon,
                                          method="nearest").values)
                if name == "prate":                 # kg/m^2/s -> mm/hr
                    precip = val * 3600.0
                elif name == "tp":                  # ERA5 accumulated metres -> mm/hr
                    precip = val * 1000.0 / max(args.accum_hours, 1e-6)
                else:                               # GFS apcp: accumulated mm
                    precip = val / max(args.accum_hours, 1e-6)
                break
    except Exception:
        pass

    out = {
        "source": args.source,
        "valid_time": str(col.get("valid_time", "unknown").values) if hasattr(col, "get") else "unknown",
        "lat": args.lat, "lon": args.lon,
        "precip_mm_hr": round(precip, 3),
        "levels": levels,
    }
    with open(args.out, "w") as f:
        json.dump(out, f, indent=1)
    print(f"Wrote {args.out}: {len(levels)} levels, precip {precip:.2f} mm/hr")
    return 0

if __name__ == "__main__":
    sys.exit(main())
