# LICENSE-TEXTURES.md
R3.0 (2026) - licenced replacement of the scene textures.
Tainted assets replaced by fresh NASA downloads; verified hashes, single transform per asset.
## 1. earth-day.jpg
- Verified source: NASA Blue Marble Next Generation (base/topography+bathymetry).
  - Direct URL used (January example):
    https://assets.science.nasa.gov/content/dam/science/esd/eo/images/bmng/bmng-base/january/world.200401.3x5400x2700.jpg
  - Month pattern: .../bmng-base/MONTH/world.2004MM.3x5400x2700.jpg
  - Month picked: January 2004.
- Licence: NASA content is not copyrighted in the US. Credit: NASA Visible Earth / NASA GSFC.
- Original dimensions: 5400 x 2700 px.
- Committed weight: 1.80 MB (1840 KB as delivered).
- Original SHA-256: 99F5FAAD74EFE985FBF1714C8BE7296CA9999759A1215B65F99B7F1DF278DDE5.
- Transformations: none (month choice documented).
## 2. earth-night.png
- Verified source: NASA SVS 30003 Earth's City Lights (DMSP + BMNG).
  - Direct URL used:
    https://svs.gsfc.nasa.gov/vis/a030000/a030000/a030003/dmsp_4096.png
- Licence: NASA / NASA GSFC, public domain (no licence embedded).
  Credit: NASA GSFC Visualization Studio / NASA SVS 30003.
- Original dimensions: 4096 x 2048 px.
- Committed weight: 26.67 MB (27308 KB PNG as delivered).
- Original SHA-256: BFC05C5DE429E52DE8F6AB92FE0022DF2041F648A6983CB4FEDE68CCF05CE795.
- Transformations: none (single pass; future transforms documented).
- Download date: 2026-11-06 (GLM sourcing).
## 3. Provenance rules (R3.0 D1)
- No EXIF / no verifiable metadata -> replacement by verified sources, not detection.
- One transformation from the original; never chained.
- If an obscure asset is found: report before touching main.
- Hires 8192 retired (not licenced and outside sec 5). Substitute: dmsp_4096.png (4096 x 2048).
## 4. Future transformation (if R3.1 stumbles on device memory)
- Downscale 5400 x 2700 to <=4096 x 2048 (sec 5), documented weight/hash, single pass.

