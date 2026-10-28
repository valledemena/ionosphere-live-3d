# Bonus extras

Everything in this document is **not ionospheric data**. It is space physics,
heliophysics or historical hardware that came along with the web demo port (or
was already in the app) and now lives in the same 3D scene.

The rules are the ones the rest of the app already follows:

- every extra starts **off** and its state is persisted in `settings.cfg`;
- each one shows its real source, age and scale in a tooltip or label;
- a failed or empty source draws **nothing** — there is no synthetic
  placeholder, no "demo mode" and no invented number;
- anything that is a model, a reconstruction or a visual exaggeration says so
  in the same place where it is shown.

The camera zoom is clamped at **45 R_E** so the true-scale extras fit. Three of
them pull the camera back automatically the first time they are enabled
(magnetopause → 20 R_E, river → 38 R_E, Van Allen → 8 R_E) and each has a
`Fit` button.

---

## Near-Earth space (true R_E scale)

### Magnetopause + IGRF field lines
`Geospace > Magnetopause`

- **What it draws**: the Shue-1998 magnetopause wireframe (dayside cavity,
  θ ≤ 105°) plus IGRF-14 degree-1 dipole field lines at L = 1.5, 2.5, 4.0 and
  6.0.
- **Model**: `r0 = [10.22 + 1.29·tanh(0.184·(Bz + 8.14))] / Dp^(1/6.6)`, with
  `Dp = 1.6726e-6 · n · v²` (n in cm⁻³, v in km/s → nPa). Shared with the river
  through `src/Data/ShueMath.h`, so both layers always agree.
- **Field lines**: IGRF-14 degree-1 coefficients at epoch 2025.0, 9.2° tilt,
  rotated with the live sun direction.
- **Declared limits**: the surface stops at θ = 105° (edge of the dayside
  cavity); the night-side stretch is a v1 parameterisation, **not**
  Tsyganenko — the real tail extends well beyond 20 R_E and is out of scope.
- **Data**: live NOAA RTSW wind. No sample yet → the layer keeps the module
  defaults (400 km/s, 5 cm⁻³, Bz 0), same as the arrows.

### Solar-wind river
`Geospace > Solar wind river (particles)` · `Fit##river`

- **What it draws**: 400 particles (two additive passes: core 0.17 R_E at
  α 0.9, glow 0.38 R_E at α 0.16) streaming antisunward, deflected around the
  magnetopause and recycled at the tail.
- **Rules** (identical to `IonosphereScene.tsx`, "W-4 #4"):
  - birth: `x = 18 − rand·43` → `x ∈ (−25, 18]`, `(y, z)` on a disc of radius
    12 R_E;
  - drift: `x −= (v / 6371) · 60 · dt`;
  - obstacle: inside `0.98 · r_Shue(θ)` the `(y, z)` pair is scaled out; `x`
    is preserved, so the particle slides along the flow and opens sideways
    around the cavity (web behaviour, **not** a port bug);
  - recycle: `x < −28` → respawn at `x = 18`.
- **Declared exaggeration**: the `×60` factor. At a real 400 km/s the flow
  would cross 43 R_E in ~11 minutes; without the factor the river would look
  frozen. The physics lives in the Shue surface (deflection) and the colour
  (real speed, Bz, density).
- **Determinism**: the web used `Math.random`; the port uses a seeded
  `mulberry32` (`0x5EED1963`), so the same wind sequence produces the same
  river. Covered by `tests/test_solar_wind_river.cpp` (46 checks).
- **Independent toggle**: the river is its own layer. It does not require the
  magnetopause layer, and the arrows layer stays separate.

### Van Allen belts
`Geospace > Van Allen belts` · `Fit##va`

- **What it draws**: two wireframe tori — inner protons at L = 1.5 (silver)
  and outer electrons at L = 4.2 + 0.08·Kp (amber) — with brightness knots
  drifting **west** for protons and **east** for electrons (0.05 rad/s).
- **Declared limits**: torus axis is the geographic equator (dipole tilt
  ignored); the outer belt reaches 4.2–4.9 R_E, hence the Fit button.
- **No live RBE glow**: DONKI RBE is an event catalogue, not a flux
  measurement, so the outer-belt glow term stays at 0 instead of faking
  intensity from event counts.

### West Ford 1963 ring
`Geospace > West Ford 1963 ring`

- **What it draws**: the May 1963 copper-dipole belt as 4096 deterministic
  point samples in a single orbital plane: 87.4° inclination, 3600–3680 km
  altitude, declared ±0.6° thickness.
- **Status**: illustrative reconstruction. The node is a fixed declared value;
  there is **no propagation, no simulated rotation and no live data**.

---

## Heliophysics & hardware

### Satellites (TLE)
`Geospace > Satellites (TLE)`

- **Data**: CelesTrak active catalogue → SatNOGS → on-disk cache. The status
  line names the source actually drawn (`1 h` fetch, `6 h` catalogue TTL).
- **Propagation**: TLE mean elements plus J2 secular drift, GMST to ECEF.
  **No SGP4/SDP4** — positions are visual-grade and **not for tracking**.
- **Rendering**: colour by regime (LEO cyan, MEO yellow, GEO magenta, HEO
  orange); instantaneous orbit rings for a deterministic subset, max 150.
- **Failure**: no catalogue → nothing drawn, reason in the status line.

### Parker spiral
`Windows > Parker spiral`

- **Why a panel and not a layer**: the geometry is astronomical (1 AU ≈ 215 R_E)
  and does not fit a view that works in Earth radii, so it is a 2D top-down
  schematic in the ecliptic plane with the Sun at the origin and Earth fixed at
  (1, 0) AU.
- **Model**: Parker 1958, `φ(r) = φ0 − Ω·(r − r0)/v_sw`, with the sidereal
  rotation pinned at 25.38 d and the **live** solar-wind speed. At 1 AU and
  400 km/s the spiral lags 61.4°.
- **Sector polarity**: declared convention, `Bx ≥ 0` → *toward* (+X GSE),
  `Bx < 0` → *away*. It is a convention applied to a measurement, not a
  sector classification product.
- **Sector crossings**: computed from sign changes between adjacent samples;
  gaps larger than 2 h are ignored (a boundary could have passed inside the
  gap, so nothing is claimed) and no crossing is ever synthesised.

### Voyager 1/2
`Windows > Voyager`

- **Data**: JPL Horizons, COMMAND −31/−32, QUANTITIES 20, 6 h disk cache.
  Last verified ranges: **172.8 AU** (V1) and **144.5 AU** (V2).
- **What it shows**: range in AU, the row date served, one-way light time and
  a declared Friis link budget — 8.42 GHz X-band downlink, 23 W TWTA, 3.7 m
  spacecraft dish, 70 m DSN dish, η_tx 0.55, η_rx 0.65 — plus the received
  power and the 10 s sweep factor.
- **Failure**: *no range, no claim*. Without a Horizons row there is no
  distance, no light time and no budget.

### LASCO C2 + D-RAP
`Windows > Solar imagery`

- **What it draws**: the SOHO/NASA-ESA LASCO C2 coronagraph (1024×1024) next
  to the NOAA SWPC D-RAP absorption map (model output, 850×475), each with its
  own GL texture, live/cached label, age and pixel size.
- **Data**: 15 min refresh, disk cache. D-RAP is model output, not a
  measurement, and the panel does not pretend otherwise.
- **Failure**: no image → no texture, no synthetic frame.

### Storm sprites + whistlers
`Geospace > Storm sprites + whistlers`

- **What it draws**: a sprite marker on the centroid of each active NWS Severe
  Thunderstorm Warning (CONUS), plus the **real IGRF dipole field line**
  through that point (Helliwell 1965 ducting) as the whistler path.
- **Honest scope**: these are *warned storms*, not measured lightning. No flash
  counts are claimed: the GLM granules are NetCDF and out of scope here.

---

## Geospace / space-weather extras

### Dst ring
`Geospace > Dst ring`

- Radius closes as Dst drops (web formula `5.5 + Dst/100`, clamped to
  3.5–6 R_E); colour goes blue → amber → red with the storm band.
- The **true** radius is compressed to 1.25–1.80 R_E for display so it stays
  on screen at the default camera distance. The ordering is preserved and the
  panel prints both numbers (`true r` / `drawn`).

### Electrojets
`Geospace > Electrojets`

- Two loops at ±67° latitude (~110 km) standing in for the auroral electrojet
  ovals, coloured by AE (≤100 blue, 100–500 amber, 500–1500 towards white),
  with radial ticks suggesting the current filaments.
- AE is the **real Kyoto** quicklook value when a file is available; the label
  says `(Kyoto)` or `(Kp proxy)` otherwise.

### Grey line
`Geospace > Grey line`

- Soft ±7.5° band around the live terminator (solar zenith 82.5–97.5°),
  rebuilt from the solar ephemeris.
- Purely geometric: the Earth shader is untouched. It marks the twilight
  highway HF paths can ride.

### CME shock front
`Geospace > CME front (ENLIL)`

- Spherical cap at the ENLIL half-angle around the Sun–Earth axis for the
  newest WSA-ENLIL run that publishes an Earth arrival time.
- **Declared compression**: the real transit is ~215 R_E and cannot fit the
  camera, so the radius is a linear mapping of the model progress
  (1.8 → 19.5 R_E). **Position is not physical; the ETA and the speed are.**
- ENLIL forecasts an arrival, not a Dp. No event → nothing drawn.

---

## Quick check: is it honest?

| Claim you might see | Where the truth is |
|---|---|
| Particles cross the whole cavity in seconds | `×60` declared in the tooltip and in `SolarWindRiverSim.h` (real wind speed is still printed) |
| Satellite dot next to your city | visual-grade (J2, no SGP4), source + age in the status line |
| Voyager "172.8 AU" | Horizons row date shown next to it; no row → no claim |
| CME front already at 10 R_E, arrival in two days | display radius is compressed; the ETA is the model value |
| Belt "glow" | disabled: RBE is a catalogue, not a flux |
| Dst ring inside the Earth | on-screen compression 1.25–1.80 R_E, true radius printed |
| West Ford "orbit" | fixed declared node, static reconstruction |
