# Damaskino

A globally-applicable **nuclear-effects simulator** for civil-defense,
preparedness, and education. Given a weapon, a location, and weather, it
estimates air blast, thermal radiation, initial (prompt) nuclear radiation,
radioactive fallout, and human casualties, and exports results as machine-
readable JSON and GeoJSON for mapping.

Damaskino models **consequences** from public science and public datasets
(Glasstone & Dolan, WSEG-10, Kingery-Bulmash, published lethality models). It
contains **no weapon-design information**. It is an educational / civil-defense
tool in the spirit of NUKEMAP.

> ⚠️ Effect magnitudes are analytic estimates. They are anchored to canonical
> benchmarks and calibrated against declassified events in validation mode
> (Phase 3.5). Treat outputs as illustrative, not operational.

See [`plan.md`](plan.md) for the full architecture and roadmap and
[`docs/PHYSICS_NOTES.md`](docs/PHYSICS_NOTES.md) for model fidelity notes.

## Building

```sh
make full      # full-fidelity CLI            -> build/damaskino
make univac    # UNIVAC 1219B lite profile    -> build/damaskino_univac
make test      # build and run the test suites
```

The full and lite profiles share one engine core. The lite profile
(`-DUNIVAC`) uses `float` precision, a small heap grid, and teletype I/O with
no JSON/weather/DEM dependencies, so it cross-compiles for vintage hardware.

## Usage

```sh
# Run a scenario, print a report, and export JSON + GeoJSON
damaskino run examples/dc_500kt_surface.json \
    --pop-density 4000 --pf 3 --time day \
    --json out.json --geojson out.geojson

# Browse the target and weapon catalogs
damaskino targets --search "Norfolk"
damaskino weapons
```

### `run` options

| Option | Meaning |
|---|---|
| `--json FILE` | comprehensive JSON report |
| `--geojson FILE` | GeoJSON: effect-ring circles + fallout cells |
| `--pop-density N` | uniform population (people/km²) → enables casualties |
| `--pop-asc FILE` | population raster (ESRI ASCII grid; WorldPop/GHS-POP) |
| `--dem FILE` | terrain DEM (ESRI ASCII grid) → line-of-sight + fallout |
| `--pf N` | fallout sheltering protection factor (dose ÷ N) |
| `--pf-prompt N` | prompt-radiation protection factor (default 1) |
| `--thermal-exposed F` | fraction with line-of-sight to the fireball |
| `--exposure H` | fallout dose integration window (hours, default 48) |
| `--visibility KM` | atmospheric visibility for thermal (default 20) |
| `--time day\|night` | time-of-day population multiplier |
| `--threshold R/hr` | fallout cell threshold for GeoJSON |

## Scenario schema

```json
{
  "location": { "name": "Washington D.C.", "lat": 38.8951, "lon": -77.0364 },
  "weapon":   { "yield_kt": 500, "burst": "surface", "fission_fraction": 0.5 },
  "wind":     { "surface": { "speed_kts": 15, "direction_deg": 270 } },
  "grid":     { "n": 256, "cell_km": 1.0, "ref_time_hr": 1.0 }
}
```

All fields are optional (defaults applied). Instead of `location`/`weapon` you
may reference the catalogs:

```json
{ "target": "Raven Rock", "weapon_preset": "w87",
  "wind": { "surface": { "speed_kts": 20, "direction_deg": 240 } } }
```

`target` accepts a numeric id or a name substring; `weapon_preset` is a weapon
id (`damaskino weapons`). Winds may be a single `surface` layer (winds aloft are
estimated) or an explicit `layers` array. Invalid inputs (yield ≤ 0, ref_time
≤ 0, fission fraction outside [0,1], out-of-range lat/lon) are rejected.

## Models

| Effect | Model | Reference |
|---|---|---|
| Air blast | cube-root-scaled peak-overpressure curve + surface/HOB factor; Rankine-Hugoniot winds | Glasstone & Dolan; Kingery-Bulmash |
| Thermal | radiant fluence with meteorological-visibility transmittance; burn thresholds | Glasstone & Dolan |
| Prompt radiation | neutron+gamma, dual exponential attenuation, 1/R² | Glasstone; Fetter et al. 1990 |
| Fallout | WSEG-10 transport + Gaussian deposition (Lagrangian rewrite in Phase 3) | WSEG Report #10 (1959) |
| Casualties | probit/LD50 for blast, thermal, radiation; sheltering; Way-Wigner dose | Glasstone; open lethality literature |
| Terrain | global DEM; line-of-sight masking (thermal/prompt) w/ Earth curvature; valley/ridge fallout | SRTM15+ / Copernicus |
| Cratering | apparent crater radius/depth, surface bursts (~W^0.3) | Glasstone & Dolan |
| HEMP | high-altitude E1 tangent-horizon footprint | Karzas-Latter |
| Activation | soil neutron-activation induced dose near GZ | — |

Provide terrain with `--dem tile.asc`; build a tile from a global DEM with
`tools/prepare_dem.py` (SRTM15+ / Copernicus).

## Data pipeline

`tools/build_targets.py` cleans and unifies the source aimpoint lists into
`data/targets.json` (1087 targets: fixes the `Militaryj` typo, parses embedded
yields/burst types, flags multi-aimpoint cities). `data/weapons.json` holds
public weapon presets.

## Repository layout

```
include/            public engine API + units
src/engine/         physics, fallout, effects, casualties
src/io/             JSON, scenario, catalog, output (JSON/GeoJSON/teletype)
src/json/           dependency-free JSON parser + writer
src/cli/            full-profile command-line driver
src/univac/         UNIVAC-lite teletype driver
tests/              engine / effects / casualty test suites
data/, tools/       target & weapon catalogs and their build script
```
