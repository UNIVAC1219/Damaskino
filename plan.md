# Damaskino — Full-Fidelity Rewrite Plan

Turning the WSEG-10 teletype fallout calculator into a modern, globally-applicable
nuclear-effects simulator for civil-defense, preparedness, and education — while
keeping the original UNIVAC 1219B build alive as a first-class citizen.

This document records **what we're building, what we're deliberately not building,
why, and in what order.** Each item is tagged **[CORE]** (on the critical path) or
**[OPTIONAL]** (self-contained module that can land any time), with its data source
noted so nothing depends on data we can't actually get worldwide.

---

## 1. Guiding principle

> An item is in scope only if the data it needs exists **globally** at a fidelity
> that matches the physics.

Weather (~13–31 km), terrain (30–450 m), and population (~100 m–1 km) all clear that
bar worldwide. Building geometry/material and per-facility operational data do not —
which is the line that defines our exclusions.

---

## 2. Scope decisions (the ledger)

### 2.1 Excluded — and why

| Excluded | Reason |
|---|---|
| Urban building-stock damage (construction class per building) | Global building-material data is inconsistent and unreliable. Damage stays ring / protection-factor based. |
| Building line-of-sight & blast channeling through street canyons | Requires global building **heights** (DC → Bali → Singapore) — does not exist at usable fidelity. |
| Hospital surge-capacity modeling (beds/staff vs. casualty load) | Facility *locations* exist (OSM), but bed counts and staffing do not, globally. We keep the location overlay and drop the capacity math. |
| Nuclear winter / climate response | Moonshot — out of scope. |
| All-hazards generalization (chemical / reactor / RDD) | Moonshot — out of scope. |

### 2.2 Borderline calls — resolved

- **Terrain-only line-of-sight / shadowing — INCLUDED.** Thermal shadows behind hills
  and prompt-radiation masking behind ridges use **only the DEM** we already load. No
  building data involved. *(Distinct from the excluded building-based LOS.)*
- **Time-of-day (day/night) population — INCLUDED, with fidelity caveat.** Static
  population is solid globally; the day/night split is excellent in the US (LandScan)
  and coarser elsewhere. Included, with the caveat surfaced in output/provenance.

### 2.3 Included — summary

Full instant-effects physics (blast, thermal, prompt radiation, cratering, EMP,
activation), modern Lagrangian fallout driven by real weather, global terrain,
casualties over gridded population, generic sheltering, uncertainty ensembles,
validation mode, and a web frontend. Detailed below.

---

## 3. Branch & build strategy

- **`main` stays the UNIVAC 1219B build.** It keeps compiling for the machine; not
  touched destructively.
- **Full-fidelity work** lives on the development branch and becomes the `full` line.
- **Implementation approach:** the full codebase is the *superset*; the UNIVAC-lite
  build is a **compile profile** (`-DUNIVAC`) inside it — small grid, `float`, 4-bit
  embedded terrain, teletype I/O, hand-entered wind. This keeps the two builds from
  rotting apart: a shared-logic fix touches one codebase, not two diverging forks.
  "main" can track a pinned lite profile / tag rather than a hard fork.

---

## 4. Engine architecture

- **Engine core** takes a scenario (**JSON in**) and emits results
  (**GeoJSON + JSON out**). The CLI drives it now; the web frontend and an optional
  WASM build drive the *same* engine later. No throwaway work.
- **Precision:** `double` in the full profile, `float` in UNIVAC-lite.
- **Grid:** heap-allocated, runtime-configurable size / resolution / range
  (replacing the compile-time `GRID_SIZE` / fixed-size embedded grid).
- **Deposition optimization:** each release point deposits within a ±4σ bounding box
  instead of over the whole grid, so cost scales with plume size, not grid size —
  required for large/fine grids.
- **Provenance:** every run emits its full parameter set, model versions, and data
  source identifiers so results are reproducible and citable.

---

## 5. Physics & models (modern science)

"Not stopping at WSEG-10." Current state-of-the-art per effect. All analytic effects
need **no external data**.

### 5.1 Instant effects — [CORE]

- **Blast** — Kingery–Bulmash airblast polynomials (CONWEP/ATP standard) with
  height-of-burst overpressure surfaces (Mach stem), scaled to nuclear via
  Glasstone–Dolan; Brode's equations as cross-check.
- **Thermal** — thermal **fluence with atmospheric transmittance** (visibility- and
  HOB-dependent), with 1st/2nd/3rd-degree burn and ignition thresholds (cal/cm²).
- **Prompt (initial) radiation** — neutron + gamma dose vs. range (DNA/Glasstone
  transport tables). Often the true lethal radius for small modern warheads.

### 5.2 Additional effects — [OPTIONAL]

- **Cratering / ejecta / base surge** (surface bursts) — scaling laws, no data.
- **EMP** — HEMP **E1** (Karzas–Latter) and **E3** (geomagnetic, via the global
  **IGRF** field model); source-region EMP for surface bursts.
- **Neutron activation** near GZ — induced activity as a distinct early-time dose
  source from fission-product fallout.

### 5.3 Terrain-only line-of-sight / masking — [OPTIONAL]

- Thermal shadowing behind hills; prompt-radiation masking behind ridges.
- **Data:** the global DEM (Section 6.3) only. No building data.

### 5.4 Fallout — modern Lagrangian — [CORE]

- Replace WSEG-10 with a **Lagrangian particle dispersion** engine
  (HYSPLIT / FLEXPART-class).
- **Freiling fractionation** (refractory vs. volatile partitioning by particle size).
- Real fission-product **decay inventory** (beyond bare t⁻¹·² Way–Wigner).
- **Atmospheric stability** (Pasquill–Gifford classes, inversions) derived from the
  weather fields.
- **Wet deposition / rainout** — precipitation-driven scavenging that produces the
  concentrated hotspots simple dry models miss (Castle Bravo / Chernobyl lesson).
  Feasible because the weather feeds already carry precipitation.
- **Time-integrated dose**, hotspot weathering, and the civil-defense **7/10 rule**.
- **Fallback:** the hand-entered `SPEED@DIRECTION` profile remains for offline /
  UNIVAC runs.

---

## 6. Data ingestion

### 6.1 Weather — [CORE]

Dedicated ingestion module (not an afterthought):

- **Sources:**
  - **ERA5 reanalysis** via the **Copernicus Climate Data Store (CDS) API** —
    NetCDF/GRIB — for historical / "what if it happened that day" runs.
  - **GFS forecast + GDAS analysis** from **NOAA NCEP** — GRIB2 — for current and
    forward-looking runs.
- **Decode:** GRIB2 via eccodes / wgrib2; NetCDF via standard libraries.
- **Variables:** u/v wind on pressure levels (→ the multi-altitude wind column the
  transport model needs), **total precipitation** (rainout), temperature/humidity
  (stability).
- **Interpolation:** in space and time to the burst column and along the evolving
  plume.
- **Fallback:** hand-entered wind profile (offline / UNIVAC path).

### 6.2 Population — [CORE for casualties]

- **WorldPop** or **GHS-POP** — global, ~100 m–1 km — for casualty estimation.
- **Day/night split** — LandScan-class where available (US strong, global coarser;
  caveat carried in output).

### 6.3 Terrain — [CORE]

- **SRTM15+** (~450 m, global incl. bathymetry) and/or **Copernicus GLO-30** (30 m
  over land), tiled (Cloud-Optimized GeoTIFF) with runtime extraction for **any**
  lat/lon on Earth.
- Replaces the current US-only SRTM raster and the 4-bit embedded header (which
  remains only as the UNIVAC-lite terrain path).

### 6.4 Infrastructure locations — [OPTIONAL]

- **OpenStreetMap** points (hospitals, power, water) for a
  "critical-infrastructure-in-damage-zone" overlay — **locations only** (capacity
  modeling excluded, see 2.1).

### 6.5 Data source summary

| Layer | Source | Coverage / fidelity |
|---|---|---|
| Reanalysis weather | ERA5 (Copernicus CDS) | Global, ~31 km |
| Forecast/analysis weather | GFS / GDAS (NOAA NCEP) | Global, ~13–25 km |
| Population | WorldPop / GHS-POP | Global, ~100 m–1 km |
| Day/night population | LandScan-class | US strong, global coarse |
| Terrain | SRTM15+ / Copernicus GLO-30 | Global, 30–450 m |
| Infrastructure (points) | OpenStreetMap | Global, variable completeness |
| Weapon presets | FAS Nuclear Notebook / NRDC | Public |
| Validation data | Bravo, Trinity, Hiroshima DS02 | Published |

---

## 7. Casualties & actionable output — [CORE unless noted]

- **Probit / LD50 casualty functions** for blast, thermal, and acute radiation,
  overlaid on gridded population.
- **Generic sheltering / protection-factor model** — tabulated PF values (basement,
  mid-rise, …), not per-building data, so it's global.
- **Time-of-day population** applied to casualties (with fidelity caveat).
- **Personal dose calculator** — "at this location, in this shelter, what's my dose
  over 48 h and when is it safe to leave?"
- **Evacuation-timing & KI-zone overlays** from fallout arrival times.
- **Critical-infrastructure-in-damage-zone overlay** — [OPTIONAL], locations only.

---

## 8. Rigor & scientific credibility

- **Monte Carlo ensembles — [CORE].** Propagate input uncertainty (yield, wind,
  particle size, HOB) and draw **probabilistic P50/P90 contours** instead of single
  deterministic lines. Designed in from the start (cheap now, expensive to bolt on).
- **Validation mode — [OPTIONAL but high value].** Overlay model vs. measured for
  declassified events (Castle Bravo fallout pattern, Trinity, Hiroshima DS02).
- **Sensitivity analysis — [OPTIONAL].** Surface the dominant parameter for a run.
- **Provenance / reproducibility — [CORE].** See Section 4.

---

## 9. Product & frontend — [OPTIONAL, phased last]

- **Web frontend:** MapLibre GL over the engine (server API, or WASM-compiled core
  for the lighter models in-browser) — effect rings, fallout plume, casualty heatmap,
  fully configurable.
- Full target list (1087 aimpoints) with search + arbitrary lat/lon entry.
- Real weapon catalog presets.
- Time-lapse plume animation; side-by-side scenario compare; shareable permalinks.
- GIS export (GeoJSON / KML / Shapefile); public API.
- Units toggles (SI / imperial, rem / Sv, R/hr / Gy); inline citations; explain-the-
  physics mode.

---

## 10. Phased roadmap

Front-loaded so something impressive is usable after Phase 1.

### Phase 0 — Engine split + branch profile  *(~2–3 days)*
- Carve the C into an engine core (JSON in → GeoJSON/JSON out).
- `double` precision, heap grid, runtime-configurable size/resolution/range.
- Bounding-box deposition.
- UNIVAC-lite compile profile; keep `main` building for the 1219B.
- Provenance emission.
- Fix the existing grid-span vs. sector-report range mismatch.

### Phase 1 — Full targets + instant effects + casualties  *(~1 week)*
- All 1087 aimpoints; schema reconcile + `Militaryj`/BOM/duplicate cleanup.
- Search + arbitrary lat/lon entry; real weapon catalog presets.
- **Blast, thermal, prompt radiation** (modern models) — no external data.
- Casualties over gridded population (WorldPop/GHS-POP) with generic sheltering and
  time-of-day.
- GeoJSON export. **Reaches NUKEMAP-plus-casualties parity. Shippable.**

### Phase 2 — Global topography  *(~1 week)*
- Tiled global DEM (SRTM15+ / Copernicus GLO-30) with runtime extraction anywhere.
- Wire terrain into effect models; enable **terrain-only line-of-sight / masking**.
- Cratering/ejecta, EMP, activation can land here or opportunistically.

### Phase 3 — Modern fallout + weather ingestion  *(~2 weeks — centerpiece)*
- Lagrangian particle dispersion replacing WSEG-10.
- **Weather module: ERA5 (CDS) + GFS/GDAS (NCEP)**, GRIB2/NetCDF decode, wind column
  + precipitation + stability, interpolated to the plume.
- Wet deposition / rainout; time-integrated dose; personal dose calculator;
  evacuation/KI overlays.

### Phase 3.5 — Uncertainty & validation  *(~1 week)*
- Monte Carlo ensembles → probabilistic contours (wraps the physics, not new physics).
- Validation mode vs. declassified events; sensitivity analysis.

### Phase 4 — Web frontend  *(~1–2 weeks)*
- MapLibre UI (server API or WASM) — rings, plume, casualty heatmap, time-lapse,
  compare, permalinks, GIS export, public API, pedagogy/units.

**Rough total:** ~6–8 weeks end-to-end, usable after Phase 1.

---

## 11. Key risks / open questions

- **Global DEM delivery** (Phase 2) is the scariest data-engineering unknown —
  multi-GB worldwide raster; tiling/hosting strategy needs an early spike.
- **GRIB2/NetCDF ingestion** (Phase 3) pulls a heavier dependency stack; keep it
  isolated behind the weather module so the UNIVAC-lite path stays dependency-free.
- **Validation fidelity** — declassified datasets are sparse and noisy; validation
  mode is a credibility tool, not a guarantee of accuracy.
- **CDS API access** requires registration/credentials; document setup and keep the
  hand-entered wind fallback always available.

---

*Framing: this is a civil-defense / preparedness / educational effects simulator
(NUKEMAP-class). It models consequences from public science and public datasets. It
contains no weapon-design content.*
