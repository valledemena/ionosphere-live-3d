# Ionosphere Live 3D

Real-time 3D ionosphere and space-weather visualizer (C++17 / OpenGL 4.0).
A textured Earth with day/night terminator, city lights and animated clouds,
wrapped in live data shells: TEC, GIRO ionosondes, HF propagation, solar wind,
auroral oval and more — all switchable, with a time-travel replay mode.

![Screenshot](screenshot-globe.png)

| ![UI + IRTAM](screenshot-irtam.png) | ![Sun + limb](screenshot-sun.png) |
|---|---|
| Full UI with honest-state badge | SDO Sun panel + limb zoom |
| ![Replay + shells](screenshot-replay.png) | ![Night close-up](screenshot-night.png) |
| Timeline replay with E/F1/F2 reference shells | Black Marble HiRes city lights, close-up |

## Features

**Globe & realism**
- Day/night Earth with soft twilight band, ocean sun glitter (Blinn-Phong),
  NASA Black Marble city lights on the night side (HiRes 13K where the GPU
  allows, anisotropic filtering), procedural animated clouds.
- Milky-Way starfield (1600 tinted stars), fresnel atmosphere, bloom
  post-processing (threshold/intensity/strength), ACES tone mapping with
  bypass toggle + manual exposure, real-sun terminator line (toggleable).
- 3D relief: the data shell deforms radially with the local value and
  "breathes" with Kp (storm swell), plus a nested outer shell with its own
  opacity/colormap controls.
- Reference altitude rings E/F1/F2 (E fixed, F1/F2 follow measured hmF2).
  Cinema mode (auto-rotate, `C`) and full UI hide (`H`).

**Ionosphere layers** (spherical color shell, 12 variables)
- **TEC** (TECU, NOAA GloTEC, 10-min cadence) with session replay + history preload.
- **foF2 / MUF / hmF2 / NmF2** interpolated (IDW) from ~129 GIRO stations,
  with median background (no ocean cliffs).
- **LUF / FOT / Spread-F** derived per ITU practice.
- **IRTAM foF2 / hmF2 / B0 / B1** (retrospective, UML LGDC gambit-coeffs,
  ~3-day lag by design): data-assimilated 46×45 grids evaluated from
  Jones-Gallet coefficients (no resampling). Honest states — no data shows
  the reason (`backfill…` / `429… passive retry`), never silence; in replay
  the layer freezes with a declared out-of-window note. Badge shows walking
  DATA-age from the message TOV.
- Per-variable colormaps (viridis/inferno/plasma/turbo) + manual override,
  altitude/opacity sliders, live legend with units, range and data timestamp.
- **Volumetric density**: real raymarched Ne(lat, lon, h) rebuilt from station
  profiles (Chapman + Epstein) every ~5 s, with day/night D-layer and GOES
  flare boost; nested outer shell + storm-swell relief.
- **Path slice**: vertical Ne(h) curtain between pinned (A) and hovered (B)
  station with peak log-density readout.
- **F2 peak shell**: floats at the real measured hmF2 per column, colored by
  peak log-density (derived from the volume, always consistent with it).
- **Model TEC**: TEC integrated from the volume (compare vs measured GloTEC).

**Stations & radio**
- GIRO points colored by metric, hover tooltips with foF2 sparkline (~2 h),
  click-to-pin; per-station MUF/LUF/FOT/S4/Faraday/Spread-F panel.
- Direct Ebro Observatory SAO profile for EB040 (FTP, full 16-parameter
  decode: foF1/foE/foEs/fmin/fxI, virtual heights, hmF2/B0/B1) with GIRO cross-check.
- Faraday rotation 3D vectors driven by local TEC; NOAA Ovation auroral oval.

**Space weather HUD**
- Kp/Dst/F10.7/Ap/SSN (all live NOAA SWPC, per-field mock fallback), GOES
  X-ray flux/class for flares, solar-wind arrows colored by speed, Bz and
  density (web ramp) + 24 h plots, Carrington rotation + sun disk.
- **Geospace overlays (ported from the web demo)**: live **Dst ring** whose
  radius closes with the storm (true 3.5–6 R_E, compressed on screen to
  1.25–1.80 R_E so it stays visible), **electrojet loops** at ±67° colored by
  AE, and a **±7.5° grey-line ribbon** around the live terminator. All off by
  default and persisted in `settings.cfg`.
- **Batch 2 of the same port (true R_E scale)**: **Shue-1998 magnetopause**
  wireframe driven by the live solar wind (Bz + dynamic pressure) with
  **IGRF-14 degree-1 dipole field lines** at L = 1.5/2.5/4.0/6.0, **Van Allen
  belts** as two tori (inner protons L = 1.5, outer electrons
  L = 4.2 + 0.08·Kp, drift knots west/east), and a static **West Ford 1963**
  ring reconstruction (4096 deterministic samples, single orbital plane).
  Off by default, persisted, with `Fit` buttons because the cavity reaches
  6-12 R_E.
- **Batch 3 - live data layers (each fetches its own source, while the layer
  is visible, the Geospace panel is open, or the Provider status section is
  expanded)**: **WSPR arcs** from the
  wsprnet.org volunteer network (one great-circle arc per spot, coloured by
  band, 10 min TTL, 40-arc cap); **CME shock front** from the newest
  WSA-ENLIL simulation with an Earth arrival time (spherical cap at the
  ENLIL half-angle, radius a declared linear compression of the transit
  progress); **storm sprites + whistlers** from NWS Severe Thunderstorm
  Warning polygons (sprite on the warning centroid plus the real IGRF dipole
  duct through it). No data means nothing is drawn - there are no synthetic
  fallbacks, and no flash counts are claimed (the GLM granules are NetCDF and
  out of scope).
- **Batch 4 - satellites, solar imagery and flares**: **satellite layer**
  from the live TLE catalogue (CelesTrak active objects → SatNOGS → on-disk
  cache, the source label says which one is drawn) with a declared
  **visual-grade** propagation (TLE mean elements + J2 secular drift, GMST to
  ECEF; no SGP4/SDP4, not for tracking), regime colours (LEO cyan, MEO
  yellow, GEO magenta, HEO orange) and instantaneous orbit rings for a
  deterministic subset (max 150); **LASCO C2** coronagraph and **NOAA SWPC
  D-RAP** absorption map in a dedicated `Windows > Solar imagery` window (own
  GL textures, live/cached label, age and pixel size, disk cache); **DONKI
  FLR** flares (72 h, class C/M/X, peak time, active region) listed in the
  Geospace panel. No data means nothing is drawn and no image is ever
  synthetic.
- **Batch 5 - real AE, Voyager, radiation-belt events and the Parker spiral**:
  **AE** from the Kyoto WDC quicklook files (real auroral electrojet index,
  used by the electrojet overlay; Kyoto publishes with a lag of days to ~3
  weeks by design, so the adapter
  walks back up to 35 days, keeps 24 h of minute values and declares the day
  served; the Kp-derived proxy is only used while there is no real file);
  **Voyager 1/2** range from JPL Horizons (172.8 AU and 144.5 AU at the last
  check) with light time and a declared Friis link budget in a `Windows >
  Voyager` window (`No range, no claim - no synthetic distance`);
  **DONKI RBE** radiation-belt enhancement events (58-day window, newest
  first) listed in the Geospace panel; **Parker spiral** in a 2D top-down
  `Windows > Parker spiral` window: Parker 1958 geometry driven by the live
  solar wind, sector polarity from the measured Bx GSE (declared convention:
  +X = toward), and the last real sector crossing within 6 h - no crossing, no
  notice. The 1 AU scale does not fit the 3D view in Earth radii, so Parker is
  a schematic panel, not a 3D layer.
- **Geospace panel** (`Windows > Geospace`, off by default, position and
  size persisted): the data view for the three batch-3 layers above - WSPR
  spots per band with mean SNR and a 60 s-sampled sparkline of the spot count,
  the live ENLIL event (countdown, transit progress, speed, half-angle, Kp,
  drawn front radius) plus the bulletin history, the active NWS storm cells,
  the DONKI flare list (class-coloured, peak time, AR/location) and the DONKI
  RBE events (time + instrument, newest first). It keeps
  fetching while open even with the 3D layers off, and shows the adapter's own
  note when a source is empty or offline.

**Time & UX**
- Timeline: LIVE vs REPLAY (TEC frames + GIRO history scrub, loop, 0.5–8 fps);
  union window 168 h (IRTAM [T−168,T−72] + TEC [T−72,T]) with per-layer loop
  mode; DATA line reports staleness with delay magnitude when clamped.
- Provider status matrix (`System > Data sources & attribution > Provider
  status (live)`): one row per source (24: GIRO, kc2g, IRTAM, GloTEC, indices,
  solar wind, aurora, Kp/HF, X-ray, SDO, DIAS, Ebro, ESA, NOAA stub, WSPRnet,
  ENLIL, NWS, satellites, LASCO C2, D-RAP, DONKI flares, AE (Kyoto), Voyager,
  DONKI RBE). State is `off` until
  the source is actually used (so an unused
  failover like kc2g stays off), then `ok` / `stale` (2x its nominal cadence)
  / `degraded` (declared wait: retry or LGDC courtesy gate) / `rate-limited` /
  `failed` (with the adapter's own error), always read from the real fetch
  outcome - failures never overwrite the last good data or its age. The Data
  column shows the last fetch age; Reason shows error + pending retry, with a
  tooltip when it is clipped. Expanding the section counts as interest, so it
  also refreshes the batch-3/4/5 sources.
- Every variable has an English tooltip (`(?)` markers); UTC clock + subsolar
  readout; GUI values persist to `settings.cfg` (window layout in `imgui.ini`).

## Data sources

| Data | Source | Refresh |
|---|---|---|
| TEC | NOAA SWPC GloTEC (GeoJSON 72×72) | 5 min + 3 h preload |
| foF2/MUF/hmF2/… | GIRO DIDBase `scaled.php` (129 stations) | 5 min |
| foF2/MUF/hmF2/… (failover) | prop.kc2g.com redistribution (GIRO data with permission, WWROF-supported, plus eSWua/INGV and NOAA feeds) | on GIRO failure only, 1 req/10 min |
| EB040 profile | Ebre Observatory FTP (`EB040_*.SAO`) | 5 min, background |
| IRTAM foF2/hmF2/B0/B1 | UML LGDC `gambit-coeffs` (Jones-Gallet coeffs, 46×45 evaluated) | 15-min slots, ~3 d lag; cold backfill 384/param, steady ≤4/cycle, 1 req/15 s shared LGDC gate |
| Kp/Dst/F10.7/Ap/SSN | NOAA SWPC JSON | 5 min |
| Solar wind | NOAA RTSW 1-min | 5 min |
| Aurora | NOAA Ovation | 5 min |
| X-ray flux (D-layer/flares) | NOAA GOES primary | 5 min |
| WSPR spots | wsprnet.org (band · SNR · grid) | 5 min, 10 min spot TTL |
| CME / shock arrival | CCMC WSA-ENLIL (newest run) | 30 min |
| Storm cells (sprites/whistlers) | NWS Severe Thunderstorm Warnings | 5 min |
| Satellites (TLE) | CelesTrak active catalogue → SatNOGS → disk cache | 1 h fetch, 6 h catalogue TTL |
| LASCO C2 coronagraph | SOHO/NASA-ESA realtime JPEG (1024×1024) | 15 min, disk cache |
| D-RAP absorption map | NOAA SWPC `images/d-rap/global.png` (model output, 850×475) | 15 min, disk cache |
| Solar flares | CCMC DONKI FLR (72 h, GOES classes) | 30 min |
| AE (auroral electrojet) | Kyoto WDC quicklook `ae_realtime/data_dir/YYYY/MM/DD/aeYYMMDD` | 30 min, walk-back ≤35 d (source lags days to ~3 weeks), disk cache |
| Voyager 1/2 range | JPL Horizons `ssd.jpl.nasa.gov/api/horizons.api` (COMMAND -31/-32, QUANTITIES 20) | 6 h, disk cache |
| Radiation belt enhancements | CCMC DONKI RBE (58-day window) | 30 min, disk cache |
| Night lights | NASA Black Marble 2016 | static texture |

Any unreachable source degrades gracefully (per-field mock/offline badges,
stale-data warnings). GIRO data: CC BY-NC-SA 4.0 (non-commercial).
Please cite: Reinisch, B. W., and I. A. Galkin, Global ionospheric radio
observatory (GIRO), Earth, Planets and Space, 63, 377-381,
doi:10.5047/eps.2011.03.001, 2011 — SPASE: http://spase.info/SMWG/Observatory/GIRO.
Access policy: polite cadence and mirrors lower the risk; they don't remove
it — we have measured 429/504 under load, which is why the kc2g failover exists.
LGDC traffic (catalog/getbest/gambit) shares one 15 s pacing gate with
timestamped `stderr` trace lines (`[LGDC …] launch / gate-consult / result`).

## Controls

- Drag: orbit · wheel / `+`–`-`: zoom · `Esc`: quit
- `C`: cinema mode · `H`: hide UI
- Panels: Layers · Space Weather · Radio Propagation · Legend · Timeline

## Build (MSYS2 UCRT64)

```powershell
cmake -B build -G Ninja
cmake --build build -j
.\build\ionosphere-live-3d.exe
```

Requires: `glfw`, `curl` (MSYS2), vendored glad/imgui/glm/stb in `libs/`.
Textures and shaders are copied next to the binary automatically.

## Layout

```
src/App.cpp          # composition, timeline, replay, all panels, persistence
src/Data/            # one adapter per source (fetch + parse + fallback)
src/Ionosphere/      # GridLayer, IonosondeLayer, SolarWindLayer,
                     #   FaradayLayer, RefShells, VolumeRenderer, SliceLayer,
                     #   LayerProfile/DensityVolume (pure profile math)
src/Globe/           # Earth (+night HiRes), Stars, Atmosphere, Terminator
src/Render/  src/UI/  src/Utils/   # camera/shaders/FBO, stubs, sun math, colormaps
shaders/  textures/  data/  docs/
```
