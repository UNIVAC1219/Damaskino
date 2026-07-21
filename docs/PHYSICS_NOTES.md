# Physics notes & deferred model work

## Phase 5 — fidelity hardening (drove down the deferred list)

Resolved:
- **Thermal transmittance** now includes forward-scatter build-up
  `T = exp(-tau)(1 + 0.9 tau)` instead of raw beam extinction; 1 Mt 3rd-degree
  burns 8.3 -> 10.9 km, matching Glasstone ~11 km (was ~20-40% short).
- **Cloud rise** recalibrated to Glasstone Ch. 9 (top `21.5 W_MT^0.2`, ~20 km at
  1 Mt vs. the old 12.5 km). Release spans the stem (0.1*top) to top so the
  near-GZ coarse hotspot is retained. This lengthened the fallout pattern toward
  the Glasstone benchmark.
- **Settling velocity** is a continuous implicit terminal-velocity solve
  (Clift-Gauvin Cd(Re)) -- no Stokes/Newton regime discontinuity.
- **Particle size** is a log-normal in MASS with the correct activity-median
  (~200 um for local fallout) and Delta-ln(d) bin weighting (the d^3 form
  implied ~1500 um, far too coarse).
- **Anisotropic plume**: deposition is an oriented sigma_along/sigma_cross
  Gaussian with a scale-dependent crosswind term, so the plume is a realistic
  ~6:1 cigar even under a unidirectional column (was a thin line). Pasquill-
  Gifford stability (A..F) scales crosswind spread.
- **Wet scavenging** confined to the precipitating layer (below ~6 km) rather
  than the whole fall column.
- **Terrain fallout modifier** is now a mass-conserving redistribution (grid
  rescaled so valleys collect exactly what ridges shed), applied as a shared
  post-pass to BOTH the WSEG and Lagrangian grids (was WSEG-only).
- **Performance**: the Lagrangian settling velocity is precomputed as a per-
  class altitude table (the implicit Clift-Gauvin solve is a pure function) and
  the timestep is adaptive (fixed vertical dz), so fine particles no longer take
  millions of steps. Single 500 kt run 10.8 s -> 0.04 s; weather-driven
  Lagrangian ensemble (100 members) ~18 min -> ~5 s -- with identical results
  (1 Mt 1000 R/hr still 40 km).
- **Ensemble priors**: logit-normal fission fraction (no boundary pile-up) and
  independent per-layer wind decorrelation atop the shared synoptic term.
- **Prompt radiation** uses slant geometry `sqrt(ground^2 + HOB^2)` for both
  casualty dose and the ground rings.
- **Fallout validation** now exercises the modern Lagrangian model: 1 Mt
  1000 R/hr H+1 downwind extent 40 km (Glasstone Fig 9.85 ~50-65 km at 15 mph;
  the wider anisotropic plume sits at the lower end) -- a graded check.

Honestly remaining (research-scale, disclosed):
- **Full fractionated fission-product inventory.** Decay still uses Way-Wigner
  t^-1.2 (which reproduces the standard 7/10 rule exactly and is the accepted
  engineering model) with the Glasstone 1.6e6 R/hr H+1 activity anchor. A real
  per-nuclide inventory with fractionation-dependent decay (near-in refractory
  vs. far volatile) is the ultimate refinement; spatial patterns and arrival
  times are more trustworthy than absolute R/hr.
- **True 4-D weather**: a single wind column is advected (now with per-layer
  shear and stability); full space/time-varying gridded winds + a spatial
  precip field (for localized rainout hotspots) remain future work.
- **Global DEM pyramid** (single regional tile today); blast is not terrain-
  masked (diffraction).



Living record of model fidelity decisions and items raised by the expert review
panel (nuclear physicist + nuclear event modeler). Blocking items are fixed in
the phase they were raised; deferred items are scheduled to the phase that owns
the relevant rewrite.

## Phase 0 — resolved

- **Newton-regime settling drag coefficient.** Terminal velocity for Re ≳ 1000
  now includes C_d ≈ 0.44 for a sphere: `v = sqrt(4 g d Δρ / (3 C_d ρ_air))`.
  Previously C_d was implicitly 1, underestimating the fall speed of the
  mass-dominant coarse tail by ~1.5×. (`src/engine/physics.c`)
- **Gaussian deposition normalization.** The 2-D isotropic kernel integrates to
  `2πσ²`; normalization corrected from `πσ²` so each release conserves its
  assigned activity (was depositing 2×). (`src/engine/fallout.c`)
- **Input validation.** `yield_kt>0`, `ref_time_hr>0`, `0≤fission_fraction≤1`,
  grid bounds, lat/lon range — rejected with a message (parser) and an rc=-3
  guard (engine), so pathological input can no longer produce silent NaN/inf.
- **Off-grid activity accounting.** Each release's on-grid fraction is computed
  analytically (product of 1-D erf integrals over the domain); the result is
  reported as `domain.off_grid_fraction` / `plume_clipped`, and each contour
  carries `grid_limited`. Domain clipping is no longer silent.
- **Geographic projection clamping.** `dmk_offset_to_latlon` clamps latitude to
  [-90,90] and wraps longitude to [-180,180], so output is always valid GeoJSON.

## Phase 1 — panel items

Resolved in Phase 1 (post-panel):
- **Thermal exposure is now a population split**, not a fluence multiplier:
  the exposed fraction sees full fluence, the rest ~none, combined at the
  probit step. Removes the ~24% bias both panels flagged.
- **Separate protection factors** for prompt (`pf_prompt`) vs fallout
  (`pf_fallout`) dose — prompt neutron+gamma is far harder to shield.
- **Injuries** now use continuous per-mechanism dose-response probits (blast
  ~2 psi, thermal ~3.5 cal/cm^2, radiation ~150 rem) among survivors, replacing
  the flat 0.6x heuristic.
- **Uniform population caveat** emitted (stderr note + JSON `population_caveat`
  + `population_mode`), since uniform density fills the whole domain.

Deferred (raised by the panel):
- **Mechanism independence** (`surv = product of (1-P)`) slightly over-counts:
  people near GZ receive all mechanisms (positively correlated exposures), so
  combined fatalities are a mild upper bound. Acceptable for this tool class.
- **Prompt slant range = ground range** (ignores HOB geometry); fold burst
  height in when terrain/LOS lands in Phase 2.
- **Thermal transmittance is conservative.** `tau = exp(-3.912 R/V)` uses the
  raw Koschmieder visible-contrast extinction as a beam transmittance, ignoring
  the large forward-scattered contribution to thermal *fluence*. Burn radii /
  thermal casualties run ~20-40% short at moderate visibility (1 Mt 3rd-degree
  8.3 km here vs. ~11-13 km with scattering build-up). Replace with an
  atmospheric-transmission model with build-up in **Phase 3.5**.
- **Thermal exposure as population partition, not fluence multiplier.**
  `thermal_exposed_frac` should split the population (exposed at full fluence
  vs. shielded at ~0) rather than halving everyone's fluence, which biases the
  nonlinear probit. *(Fixing in Phase 1 once modeler concurs.)*
- **Prompt-radiation linear yield scaling** is simplified (real transport is
  sub-linear at high yield). Immaterial because for yields >~50 kt the prompt
  lethal radius lies inside the blast lethal radius; anchor placed at 15 kt
  where prompt dominates. Revisit with a transport-based table if needed.
- **HOB slant-range geometry** ignored in Phase 1 thermal/blast (ground range
  used). Full height-of-burst Mach-stem surface scoped for Phase 3.5.
- **Blast LD50 = 10 psi** is a building-collapse-dominated consequence anchor;
  revisit against ~5 psi urban-collapse studies with the sheltering model.

## Phase 2 — panel items

Resolved (post-panel):
- **Earth-curvature sign fixed** in terrain LOS: occlusion is `terrain + drop >
  sightline` (the chord dips below the sphere by the sagitta). The prior sign
  made curvature *improve* visibility; negligible at Phase 2 ranges but wrong
  beyond ~30 km. Guarded by a flat-sea horizon test.
- **Crater radius recalibrated** to ~150 m / 45 m at 1 Mt (dry soil, contact
  burst) from ~200 m; was ~30% high vs. Glasstone.
- **LOS raycast gated** on `r_km <= max(thermal, prompt range)` so cost is
  independent of grid span (was a raycast per populated cell).

Deferred:
- **Blast is not terrain-masked** (only thermal/prompt LOS). Blast waves
  diffract/reflect around terrain; explicit note so this isn't assumed modeled.
- **Terrain fallout modifier is non-conserving** (local valley ×1.3 / ridge
  ×0.8 multiplier, not a redistribution), and the off-grid activity diagnostic
  is accumulated before it, so `off_grid_fraction` is blind to terrain. Phase 3
  Lagrangian deposition should make this mass-conserving.
- **Partial-tile LOS inconsistency**: if GZ is outside the DEM tile but some
  receivers are inside, the emitter elevation falls back to 0. Require the tile
  to contain GZ when the global pyramid lands.
- **Neutron activation** is a separate diagnostic, not added to the casualty
  dose field; fold into near-GZ dose for enhanced-radiation / low-fallout air
  bursts.
- **HEMP E1 amplitude** is a flat nominal 50 kV/m; footprint geometry is exact.
  A Karzas-Latter field(altitude, geomagnetic latitude) with the off-nadir
  "smile" is the eventual upgrade (IGRF already scoped for E3).

## Phase 3 — implemented

- **Lagrangian particle dispersion** replaces WSEG single-column transport when
  a real wind column is supplied: size-resolved parcels released across the
  stabilized cloud disk (base→top), advected through the multi-level wind with
  size-dependent settling, deposited terrain-aware with a turbulent-diffusion
  sigma ~ sqrt(2 K t).
- **Wet deposition / rainout**: precipitation drives a scavenging coefficient
  Λ = a·P^0.8; activity is removed en route and deposited under the rain,
  concentrating the near-in pattern (verified: 10 R/hr extent 178→122 km at
  8 mm/hr).
- **Freiling fractionation** (first-order): per-class activity biased toward
  larger/earlier-falling particles vs. the mass fraction.
- **Weather ingestion**: ERA5 (CDS) / GFS-GDAS (NCEP) → `fetch_weather.py`
  JSON interchange → `src/weather` wind column (u/v vs. altitude + precip).
  Hand-entered profile remains the offline/UNIVAC fallback.
- **Protective actions**: 48 h integrated (unsheltered) dose zones (50/150/450
  rem) for shelter/evacuation, plus fallout first/last arrival timing.
- **Personal dose calculator**: `dmk_fallout_dose_rem` (Way-Wigner integral,
  arrival→window, protection factor).

Resolved (post-panel):
- **Release altitude is ASL** (terrain height under GZ + in-cloud height), so
  wind lookups and the ground sink are consistent over elevated terrain.
- **Fine-parcel step cap** raised to 200k and, on exceedance, the parcel is
  counted as off-grid loss rather than deposited at a spurious location
  (10 Mt runs finite, off_grid 0.03).
- **Wind interpolation guards** duplicate/equal-altitude levels (no div-by-zero
  -> no NaN plume).
- **Deposition cutoff widened to 4 sigma** (cleaner conservation bookkeeping).
- **ERA5 precip unit fix** in fetch_weather.py (accumulated metres -> mm/hr;
  --accum-hours), which previously would have been ~1000x off for tp.
- **Personal dose CLI**: `damaskino dose --rate --arrival --window --pf` reports
  accumulated dose, an effect assessment, and time-to-50-rem.

Deferred (Phase 3.5 / later):
- **Full fission-product decay inventory** + gamma dose-rate conversion, to
  replace the repurposed 1.6e6 R/hr point constant (still the activity anchor).
- **Scale-dependent crosswind spread**: eddy diffusivity K=40 m^2/s carries
  little width; plume breadth comes from the cloud disk + directional shear
  across release altitudes. Real (veering) ERA5/GFS columns spread more; add a
  travel-distance-dependent sigma_y for unidirectional columns.
- **Wet scavenging confined to the precipitating layer** (currently whole
  column) and a **spatial precip field** for true localized rainout hotspots
  (single scalar precip today).
- **off_grid_fraction floor** (~0.1-0.3% from the sigma cutoff) is bookkeeping,
  not real escape; document or subtract the analytic tail.
- **4-D gridded wind**: currently a single representative wind column advects
  all parcels; large plumes crossing strong horizontal wind gradients want a
  space/time-varying field (interpolated per parcel position).
- **Scavenging coefficient calibration** (Λ = a·P^0.8, a=1e-4) against measured
  washout; and dry-deposition velocity by surface type.
- **Activity accounting** for wet+dry is summed from on-grid Gaussian integrals
  (approximate at the domain edge).

## Phase 3.5 — panel items

Resolved (post-panel):
- **Ensemble runs the Lagrangian model** when `--weather` is supplied: the same
  per-member wind speed factor and direction rotation perturb the wind column,
  so the probabilistic bands quantify the modern model's uncertainty, not just
  the WSEG fallback. `used_lagrangian` is reported.
- **Default samples raised to 300**; output notes that P>=90% tail bands need
  more members than P>=50%.
- **Validation honestly labels** [=CONS] model-anchor consistency checks vs.
  [PASS/FAIL] independent graded checks vs. [~INFO] approximate/wind-dependent.
  Only the 3 genuinely independent cases (20 kt blast cube-root, thermal, DS02
  prompt) are graded; a benchmark file with zero gradeable cases now warns.
- **ensemble.h doc aligned** to the implemented perturbations (yield, wind
  speed/direction, fission; not HOB/particle size).

Deferred:
- **Per-layer wind decorrelation**: the ensemble perturbs the column rigidly
  (one speed factor, one direction offset); add a small independent per-layer
  component for fuller directional spread.
- **Beta prior for fission fraction** (currently normal+clamp piles mass at the
  bounds near 0/1). HOB and particle-size perturbation not yet included.

## Deferred to Phase 3.5 (Lagrangian fallout — further refinement)

- **Continuous settling curve.** The piecewise Stokes / Schiller-Naumann /
  Newton branches are discontinuous at the regime boundaries. The Lagrangian
  settling should solve the implicit terminal-velocity balance (Re from the
  actual v, not v_stokes) for a smooth curve.
- **Cloud-rise recalibration.** `H_top = 12.5·W_MT^0.25 km` runs low vs.
  Glasstone & Dolan Ch. 9 (~20 km stabilized top for 1 MT). Recalibrate against
  Glasstone when the modern source term is built.
- **Particle mass distribution semantics.** Clarify whether 120 µm is the
  number- or mass/activity-median; the extra `d³` weighting is only correct for
  a number-median input. Add Δln(d) bin-width weighting (the 12 diameters are
  not equally spaced in log space). Fold into Freiling fractionation.
- **Real dose normalization.** Replace the repurposed 1.6×10⁶ R/hr point
  constant with a fission-product decay inventory + gamma dose-rate conversion
  (R/hr per unit deposited areal activity, R·hr⁻¹ per (fissions·m⁻²)).
- **Anisotropic spread.** Deposition uses isotropic σ; carry independent
  σ_x/σ_y (crosswind vs. downwind) with particle-resolved trajectories.

## Deferred to Phase 1 / Phase 4 (output)

- **Contour polygons.** Emit merged iso-dose polygons (marching squares) per
  level instead of one square Feature per grid cell, for a real GIS deliverable.
- **Post-processing cost.** Contour scans and the air-burst rescale are O(n²);
  track a deposited-region bbox for large fine grids.
- Surface `particle_class_max` (currently computed but not output).

## Validation targets (Phase 3.5)

Absolute R/hr magnitudes must be validated against known WSEG-10 / Glasstone
idealized H+1 patterns and declassified events (Castle Bravo, Trinity) before
any dose figure is presented as authoritative.
