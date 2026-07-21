# Physics notes & deferred model work

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

## Deferred to Phase 3 (Lagrangian fallout rewrite)

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
