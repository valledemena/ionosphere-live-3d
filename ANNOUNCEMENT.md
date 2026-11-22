# Ionosphere Live 3D — anuncios para difusión educativa y científica

Aplicación educativa y científica de código abierto: visor 3D en tiempo real de la
ionosfera y el clima espacial, construido únicamente a partir de **datos públicos**
(NOAA SWPC, GIRO, Ebre Observatory, OMNI, etc.). Sin fallbacks sintéticos:
cada fuente publica su estado (ok / stale / degraded / rate-limited / failed) y,
cuando no hay dato, la capa se vacía en lugar de inventar valores.

Repositorio: https://github.com/valledemena/ionosphere-live-3d
Companion web (prototipo): https://github.com/valledemena/ionosphere-live-3d-web
Licencia: GPL-3.0 (ver LICENSE y CREDITS.md).

---

## Variante corta (X/Twitter / Mastodon / Bluesky)

> Ionosphere Live 3D — visor en tiempo real de la ionosfera y el clima espacial,
> solo con datos públicos. TEC, foF2/MUF/hmF2, IRTAM, aurora, viento solar,
> magnetosfera, Van Allen, satélites TLE, espectro completo. C++17/OpenGL,
> código abierto (GPL-3). Cada capa dice si sus datos son frescos; sin sintéticos.
> Repo: https://github.com/valledemena/ionosphere-live-3d

Hashtags sugeridos: #spaceweather #ionosphere #openscience #hamradio #HF

---

## Variante educativa (LinkedIn / comunidades docentes)

**Ionosphere Live 3D: enseñar la ionosfera con datos reales**

Ionosphere Live 3D es un visor 3D de código abierto para docencia y divulgación
científica. Muestra la ionosfera (TEC, foF2/MUF/hmF2/NmF2), el clima espacial
(Kp, Dst, F10.7, viento solar, aurora, rayos X GOES), la propagación HF
(trace, LUF/FOT, Spread-F) y extras de física espacial (magnetopausa Shue-1998,
líneas de campo IGRF, cinturones Van Allen, anillo West Ford 1963), todo sobre
un globo terráqueo con terminador diurno/nocturno, luces de ciudades y nubes.

Principios pedagógicos clave:
- **Cada capa publica su estado**: fresco, stale, degradado, limitado por tasa
  o fallido, con la edad real del dato.
- **No hay datos sintéticos**: si una fuente falla, la capa queda vacía y el UI
  explica por qué.
- **Cada fuente está etiquetada** con su origen (NOAA, GIRO, OMNI…) y sus
  licencias en CREDITS.md.

Ideal para cursos de radio, geofísica, física del espacio y meteorología espacial.
Compilación nativa (CMake, C++17), sin GPU de gama alta; tests sin GPU.

---

## Variante técnica (Reddit r/amateurradio, r/space, foros HF / ionosfera)

**[Open source] Ionosphere Live 3D — real-time ionospheric & space-weather
visualiser (C++17/OpenGL), data-public only**

- TEC (NOAA GloTEC, 10-min) + replay/historia; foF2/MUF/hmF2 IDW desde ~129
  estaciones GIRO; IRTAM (Jones-Gallet coeffs, ~3 d de retardo declarado).
- Propagación HF: trace layer con ribbon cámara-facing (hop abierto/marginal/cerrado),
  LUF/FOT/Spread-F, perfil SAO real de Ebre (EB040, FTP), rotación de Faraday.
- Clima espacial: Kp/Dst/F10.7/Ap/SSN, SW, aurora Ovation, AE electrojets,
  anillo Dst, grey line, shock CME (WSA-ENLIL).
- Extras: magnetopausa Shue-1998 + líneas IGRF, belts Van Allen, West Ford 1963,
  catálogo de satélites TLE (CelesTrak → SatNOGS → caché), Voyager, Parker spiral.
- Transparencia: Off/degraded/failed honesto, sin sintéticos.

Build: cmake -B build -G Ninja && cmake --build build -j, tests con ctest.
Contribuciones bienvenidas.

---

## Especificaciones técnicas rápidas

| | |
|---|---|
| Plataforma | Nativo C++17 / OpenGL 4.0 (MSYS2 UCRT64; Windows/Linux/macOS via GLFW) |
| Dependencias | glfw3, libcurl, glm (vendored), glad, imgui (vendored) |
| Datos | Todos públicos (NOAA SWPC, GIRO, Ebre, CCMC, Kyoto, JPL Horizons, NWS, CelesTrak/SatNOGS) |
| Caché | cache/ local; configurable vía cacheDir en settings.cfg |
| Tests | 43+ suites headless (sin GPU) |

## Sugerencias de medios

- Screenshots incluidas en el repo: screenshot-globe.png, screenshot-irtam.png,
  screenshot-sun.png, screenshot-replay.png, screenshot-satellites.png,
  screenshot-volume.png, screenshot-night-closeup.png.
- Vídeo corto recomendado: rotación del globo (cinema mode C) mostrando IRTAM
  y la capa de volumen Ne.
- Citar siempre las fuentes de datos (ver CREDITS.md) en publicaciones.
