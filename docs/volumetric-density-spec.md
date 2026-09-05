# Ionosphere Live 3D — Perfil vertical Ne(h) y render volumétrico real

Guía de implementación para otro LLM (o para David). Cubre y corrige el
documento previo de análisis ("Perfil vertical Ne(h) y renderizado
volumétrico real", de Claude): la física y el diseño general de aquel doc
son correctos, pero su §4.3 trae una convención esférica que **no es la del
motor** — seguirla al pie de la letra desalinearía el volumen. Este documento
da las convenciones verificadas contra el código real (con citas de archivo
y línea) más el plan de implementación por pasos.

Estado verificado del repo (NO supuesto, leído del código):

- `src/Ionosphere/VolumeRenderer.h` = 4 no-ops; `VolumeRenderer.cpp` = 1 línea
  (`#include`). Cero referencias en `App.cpp`. Hueco confirmado.
- `GridLayer` = cáscara esférica única a `heightOffset_` fijo; `displaceAmp_`
  desplaza por valor normalizado (cosmético, no altura). `shellLayer` en
  `App.cpp` = segunda copia a otro radio. Dos cáscaras 2D, ningún volumen.
- `StationPoint` (`IonosondeLayer.h`) YA trae foF2/muf/hmF2/nmF2/foF1/foE/
  foEs/fxI/hF2/hF/hE/hEs/B0/B1/fmin. Solo colorean puntos y tooltips.
- `EbroAdapter.cpp` decodifica B0/B1/hmF2 del SAO (verificado con test
  `test_ebro_parse` → PASS). OJO: el FTP de Ebre está cortado desde esta
  red (canal de datos filtrado, verificado con curl EPSV+PASV) → la fuente
  práctica de B0/B1 es GIRO `scaled.php` (mismas columnas, verificadas con
  respuesta real EB040: `B0=[26], B1=[28], fmin=[30]`, test `test_giro_cols`
  → PASS).
- `RefShells` = anillos a 110/200/300 fijos, "sin datos" (cierto). PERO además
  tiene un bug real: usa `theta = radians(lon)` (`RefShells.cpp:30`) mientras
  TODO el resto del motor usa `theta = pi - lonRad` → sus meridianos están
  rotados 180° respecto a las capas de datos. Arreglar al tocarlo (1 línea).
- Refresco de datos: timer estático en `App::update()` cada 300 s, NO vía
  `DataManager` (vestigial). El volumen debe engancharse ahí mismo.
- `buildPalette()` es `static` dentro de `GridLayer.cpp` → hay que exponerla
  (p.ej. mover a `Utils/Colormap.h`) para el `sampler1D` del volumen.
- GL 4.0: `GL_R16F` + filtrado lineal + `glTexImage3D` + `texture(sampler3D)`
  todo core. Sin extensiones.

## 1. Convenciones exactas del motor (leer esto primero, es la trampa)

Todos los ficheros de datos usan el layout `values[latIdx*W + lonIdx]`,
fila 0 = sur (lat −90), col 0 = lon −180, 72×72 (ver `GloTecAdapter.h`,
`AuroraAdapter.h`, `giroInterpGrid` en `App.cpp`).

Forward (malla → mundo), idéntico en `GridLayer.cpp:buildPositions`,
`IonosondeLayer`, `SolarWindLayer`, `FaradayLayer`:

```
phi   = pi * stack/stacks            // 0 = norte
theta = 2*pi * slice/slices
pos   = (sin(phi)*cos(theta), cos(phi), sin(phi)*sin(theta))
```

Relación textura↔mundo (verificada de TRES formas independientes: píxeles de
`earth-day.jpg` muestreados —Sahara/Pacífico/Amazonas en su sitio—,
test numérico del sol → 179.8/89.8/−0.3/−90.3, y `hitTest`):

```
theta = pi - lonRad        // lon en radianes, este positivo
phi   = pi/2 - latRad
```

Inversa (la que necesita el fragment shader del raymarching):

```
lat = asin(clamp(n.y, -1, 1))
lon = pi - atan2(n.z, n.x)           // radianes en [0, 2pi]; plegar a [-pi, pi]
s   = lon / (2*pi) + 0.5
t   = lat / pi + 0.5                 // t=0 es el SUR (fila 0 = latIdx 0)
```

CORRECCION (bug real encontrado al implementar): la primera version de este
documento ponia `t = 0.5 - lat/pi` por analogia con `GridLayer::updateTexture`
(que SI voltea filas con `texRow = H-1-latIdx`). Pero `buildDensityVolume`
guarda `latIdx=0 (sur)` en la primera fila SIN voltear, y el upload OpenGL
mapea la primera fila a t=0. Con `0.5 - lat/pi` el shader leia el hemisferio
especular (verificado numericamente: Ebro lat+40.8 leia fila 19.18 en vez de
51.82). `sampleVolume()` (CPU, usado por el slice) siempre leyo bien.
Test ejecutable del invariante en `test_slice.cpp` ("shader-cpu-row-match").

Referencia canónica en el código: `IonosondeLayer.cpp:249-250`
(`lon = pi - atan2(z,x)`). Cualquier shader nuevo que convierta dirección→
lat/lon DEBE usar esta forma. La forma `atan2(n.z, n.x)` a secas del doc
previo produce longitudes espejadas+desplazadas en este motor.

Subida de la textura 3D (ancho×alto×prof = lon×lat×alt):

```
texel(s, t, w): s = (lonIdx+0.5)/W,
                t = (H-1-latIdx+0.5)/H,   // fila 0 = norte (igual que GridLayer)
                w = (altIdx+0.5)/A        // altIdx 0 = 60 km (abajo)
```

Conversión de altura (calibrada: `heightOffset_ = 0.05` ≈ 318 km):

```
r = 1.0 + altKm / 6371.0     // innerR(60km) = 1.00942, outerR(500km) = 1.07848
```

## 2. Física a implementar (validada, con 2 correcciones menores)

Todo lo del doc previo es correcto: `Nm = 1.24e10·fo²`, Chapman-alfa para
D/E/F1, Epstein-modificado de Bilitza&Rawer para bottomside F2, topside
difusivo, combinacion por `max()`, normalizacion log10 [8, 12.5].

CORRECCION Epstein (bug real del doc previo, verificado numericamente): el
minimo de `x^B1/cosh(x)` esta donde `x*tanh(x)=B1` (x*~=1.97 para B1=1.9,
x*~=2.63 para B1=2.6, x*~=3.01 para B1=3.0). Clampear a xmax=3.0 YA resucita
con B1 diurno/nocturno real. Implementado: xmax=1.6 (seguro para B1>=1.7) +
taper `exp(-(x-xmax)/1.2)` mas alla, y saneo `B0->[20,300]`, `B1->[1.7,4.0]`
al ingerir (`sanitizeB0/sanitizeB1` en `LayerProfile.h`, aplicados en
`profileFromStation` y en `interpLayerProfiles`). Tests `mono-B1-*` en
`test_profile.cpp`.

## 3. Implementacion realizada (mapa para el siguiente LLM)

- `Utils/Colormap.h`: paleta unica (verificada bit a bit vs la original antes
  de borrar el duplicado de `GridLayer.cpp`; `IonosondeLayer` conserva la
  suya a proposito porque su plasma difiere ligeramente).
- `Ionosphere/LayerProfile.h`: `profileFromStation` + `evalNeTotal` (+ D con
  `sqrt(cosChi)*flare`, `applyDayNight` atenua E/F1 de noche).
- `Ionosphere/DensityVolume.h`: `buildDensityVolume` (72×72×48, 60-500 km en
  ^1.5), `densityDir` (convencion verificada), `sampleVolume` trilineal.
- `Ionosphere/VolumeRenderer.{h,cpp}`: bounding sphere + front-cull + inner
  analitica, `GL_R8` (suficiente: el dato ya va 0..1), colormap 1D propio,
  64 pasos por defecto. Sin `setDensityRange` a proposito (la normalizacion
  es CPU; el setter mentiria).
- `Data/XrayAdapter.{h,cpp}`: GOES `xrays-6-hour.json` (filtrar
  `energy=="0.1-0.8nm"`, usar `flux`), clases A/B/C/M/X, `flareFactor`
  ilustrativo. 6º paso del worker.
- `Ionosphere/SliceLayer.{h,cpp}`: cortina slerp 64×20 A(pinned)->B(hover)
  muestreando el volumen (usa la ruta CPU, inmune al bug del shader).
- `RefShells`: meridianos corregidos a `theta=pi-lon` + `updateAltitudes`
  (E=110, F1/F2=mediana hmF2, histeresis 0.5 km).
- `GridLayer`: modo `setDisplaceCentered` para hmF2 (alturas reales sobre/bajo
  la media en vez de valor normalizado).
- `App.cpp`: `interpLayerProfiles` (8 campos IDW de una pasada), rebuild del
  volumen cada 5 s (sigue live+replay+sol+flares), UI (checkbox+opacidad,
  linea X-ray, slice), `lastVolume` para el slice, persistencia de
  volumen/slice.
- GOES verificado en vivo (`flux` 4.09e-07 = B4.1 durante el desarrollo).