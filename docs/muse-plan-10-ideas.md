# Plan de implementación — 10 ideas de la demo «Ionosphere Live 3D · Web Explorer» → motor C++/OpenGL

**Destinatario:** MUSE (agente revisor/implementador)
**Base:** `ionosphere-live-3d` @ **16b6fa5** (ZIP «repo al día» recibido 2026-09-07)
**Procedencia:** revisión en profundidad del motor + demo web de referencia (Next.js/Three.js) que valida las 10 ideas en vivo con datos reales
**Autor del plan:** Super Z (asistente de David)
**Estado:** listo para revisión de MUSE antes de tocar código

---

## 0. Qué es este documento y qué pide el usuario

David validó un catálogo de 21 ideas de mejora para su motor de simulación
ionosférica 3D. Una demo web construida al efecto demuestra **las 10 primeras
en vivo** (marcadas `status: demo` en el catálogo). El usuario ha decidido:

> «Vamos a implementar las 10 primeras que son las que has puesto en la demo».

Este es el plan de portado de esas 10 ideas al motor **C++17 / OpenGL 4.0 core**
original. Es **autocontenido**: toda fórmula, constante, endpoint, geometría y
parámetro validado en la demo está transcrito aquí — no hace falta la demo web
para implementar (ni siquiera para revisar).

Las 10 ideas, en el orden en que el catálogo las lista:

| # | id | Idea | Categoría | Impacto | Esfuerzo |
|---|----|------|-----------|---------|----------|
| 1 | `pipe` | Pipeline lineal: MSAA 4× HDR + bloom + ACES | visual | 4 | 2 |
| 2 | `atm`  | Atmósfera Rayleigh/Mie real (single scattering) | visual | 5 | 3 |
| 3 | `aur`  | Aurora volumétrica con cortinas instanciadas | visual | 5 | 4 |
| 4 | `hf`   | Ray-tracing HF multi-salto (secante real) | radio | 5 | 3 |
| 5 | `rep`  | Replay temporal coherente (el sol sigue al dato) | ux | 3 | 1 |
| 6 | `sdo`  | Disco solar SDO real + regiones activas | datos | 3 | 2 |
| 7 | `grd`  | Grid GIRO + fondo climatológico ponderado | física | 4 | 2 |
| 8 | `abs`  | Absorción D (Sauer-Wilkinson) integrada en LUF | radio | 4 | 2 |
| 9 | `giro` | GIRO por getbest GET (cortés) con historia | datos | 3 | 1 |
| 10 | `city` | Estrellas con Vía Láctea realista | visual | 2 | 1 |

**Fuera de alcance:** las otras 11 ideas del catálogo (god rays, ionogramas
sintetizados, Faraday IGRF, Es, alertas, predicción 24-48 h, exportación,
tour, volumen blue-noise, sombra del planeta en el volumen, airglow). No
implementar nada de eso en esta pasada, aunque algún punto de contacto se
mencione como «futuro».

**Orden de implementación recomendado (dependencias + riesgo, no orden de
catálogo):** Fase 0 (higiene) → M1 `pipe` → M2 `rep` → M3 `city` → M4 `giro`
→ M5 `grd` → M6 `atm` → M7 `abs` → M8 `hf` → M9 `aur` → M10 `sdo`. El
pipeline va primero porque atm/aur/hf se benefician de él; `giro` antes de
`grd`/`hf` porque ambas consumen los grids GIRO; `sdo` al final porque es
independiente y de menor riesgo.

---

## 1. Contexto esencial del motor (verificado sobre 16b6fa5)

> Todo lo de esta sección se ha leído del código del ZIP 16b6fa5 con números
> de línea reales. Si MUSE encuentra divergencia, lo prioritario es el código,
> no este documento.

### 1.1 Stack y arquitectura

- C++17, OpenGL **4.0 core** (sin extensiones), GLFW, Dear ImGui, stb
  (`libs/stb`), libcurl, Glad (`libs/glad`), CMake.
- `App::Impl` (`src/App.cpp`, ~2992 líneas) es el god-object: posee shaders,
  capas, adaptadores, estado de UI y persistencia. Todo feature nuevo
  terminará viviendo ahí o en una clase `Layer`/`Adapter` propia.
- **Red ↔ GL separados**: los adaptadores (`src/Data/*`) corren en hilos
  worker y publican *bundles inmutables* que el hilo GL consume. Jamás curl
  desde el hilo de render.
- Refresco de datos: timer estático en `App::update()` cada **300 s** (no vía
  `DataManager`, que es stub).
- Capas: `GridLayer` (cáscara 2D desplazada por valor — TEC, shell, aurora
  plana, viento solar…), `IonosondeLayer` (puntos+tooltips+hitTest),
  `SliceLayer` (cortina CPU), `RefShells` (anillos), `VolumeRenderer`
  (raymarch 3D, 72×72×48, 60–700 km), `FaradayLayer`, `Terminator`.
- Replay: `tecHist` (36 frames) + `sampleHistoryAt()` + `blendGrids()`
  (App.cpp:1073-1156). `replayEpoch` gobierna GIRO/grids/volumen, **pero no al
  sol** (bug que corrige M2).

### 1.2 Pipeline de render actual (App.cpp:1809–1892)

```
[1] sceneFBO (RGBA16F, TEXTURA, sin MSAA)
      Tierra + estrellas → atmósfera (cáscara Fresnel falsa) → TEC/shell/
      peak/model → volumen (volumeMode==0) → slice → terminador → refshells
      → puntos ionosonda → viento solar → Faraday → aurora (cáscara plana)
[2] bloomFBO[0], bloomFBO[1] (½ resolución): bright-pass + blur H, blur V
      — MISMO shader en H y V → u_intensity se multiplica DOS veces (intensity²)
      — umbral DURo por canal: max(c - threshold, 0)
[3] Composite a pantalla: ACES Narkowicz + exposure + bloom aditivo
      — SIN encode sRGB final (colores lineales a un framebuffer no-sRGB)
[4] NUEVO en 16b6fa5: volumen categórico (volumeMode==1) DESPUÉS del
    composite, colores exactos, oclusión terrestre analítica en shader
[5] ImGui
```

Nota de limpieza: el comentario App.cpp:~1785 «Overlays: se dibujan DESPUÉS
del bloom» está **desactualizado** — en 16b6fa5 todo se dibuja dentro de
`sceneFBO` antes del bloom. Eliminarlo en Fase 0.

### 1.3 Convención esférica — LA TRAMPA

Cualquier shader/geométría nueva que convierta dirección ↔ lat/lon **DEBE**
usar la convención del motor (verificada por 3 vías en
`docs/volumetric-density-spec.md`):

```
phi   = pi/2 - latRad                 // 0 = norte
theta = pi - lonRad                   // lon este-positivo
pos   = (sin(phi)·cos(theta), cos(phi), sin(phi)·sin(theta))

// Inversa (canónica en IonosondeLayer.cpp:249-250):
lat   = asin(clamp(n.y, -1, 1))
lon   = pi - atan2(n.z, n.x)          // plegar a [-pi, pi]

// Altura (motor 1:1, sin exageración):
r     = 1.0 + altKm / 6371.0
```

- `GridData` 72×72: `values[latIdx*72 + lonIdx]`, fila 0 = SUR, col 0 = lon
  −180 (GloTecAdapter.h, AuroraAdapter.h, `giroInterpGrid`).
- Subida de textura 2D de grid: fila 0 = NORTE (`texRow = H-1-latIdx`,
  GridLayer::updateTexture). El volumen 3D NO voltea (`buildDensityVolume`
  escribe sur primero) — ver spec §1 «CORRECCION».
- Helpers existentes: `CoordinateSystem.h` — `latLonToUnit(lat,lon)`,
  `latLonAltToPos(lat,lon,altKm)`, `posToLatLon(pos)`.
- La forma `atan2(n.z, n.x)` a secas produce **longitudes espejadas** en este
  motor. Prohibido.

### 1.4 Reglas de la casa (docs/visual-life-plan.md, del propio David)

1. **Nada toca `evalNeTotal`/`DensityVolume`** salvo añadir un campo. Ninguna
   de las 10 ideas lo necesita (aurora y HF son capas nuevas; `grd` toca
   `interpLayerProfiles`, que es interpolación de entrada, no física).
2. Todo lo nuevo: **checkbox + tooltip EN + persistencia + arranca apagado**.
3. Nada altera datos, solo representación. *Excepción honesta y aprobada por
   el usuario:* `giro` (M4) cambia CÓMO se descargan los mismos datos (GET
   cortés en lugar de POST masivo) y añade historia de 6 h.
4. GL 4.0 core estricto, `#version 400 core` en todo GLSL nuevo.
5. Patrón de carga de shaders: `shader.load("shaders/x.vert","shaders/x.frag")`
   con fallback `loadFromSource()` inline (App.cpp:717-743). Los features
   nuevos con shader propio siguen ese patrón (los archivos de shaders se
   cargan si existen; el fallback garantiza arranque en builds sin `shaders/`).
6. Convención de estado persistente: pares clave-valor estilo
   `fprintf(f, "nombreValor %d\n", ...)` (App.cpp:1252-1283) y parseo
   tolerante en load (App.cpp:1330-1360). Claves nuevas = ignoradas por
   binarios viejos: compatibles.

### 1.5 Bugs confirmados de los que parte este plan (con ancla)

| Bug | Ancla | Fase que lo arregla |
|-----|-------|---------------------|
| resize roto (FBOs/cámara no se recrean) | App.cpp:594 callback sin efecto | M0 |
| bloom `u_intensity` aplicado en H y V (intensity²) | bloom.frag:22 | M1 |
| bright-pass con umbral duro por canal | bloom.frag:17-21 | M1 |
| composite sin encode sRGB | composite.frag:33 | M1 |
| FBO sin MSAA | Framebuffer.h:20 | M1 |
| hilos detached → use-after-free en shutdown | App.cpp:800-838 | M0 |
| CMake copia `data/` inexistente → POST_BUILD falla | CMakeLists.txt:97 | M0 |
| GIRO: pool 16 hilos POST masivo sin backoff | GiroAdapter.cpp:246-277 | M4 |
| sol alimentado por wall-clock (incoherente en replay) | App.cpp:248-253 | M2 |
| distancia plana en interpolación de perfiles | App.cpp:474+ | M5 |
| LUF heurístico sin flare X-ray | RadioPropagationAdapter.cpp:92-93 | M7 |
| estrellas: 1600 puntos aleatorios (seed 42) | Stars.cpp | M3 |
| aurora = cáscara plana de color (sin volumen) | App.cpp:119 `GridLayer auroraLayer` | M9 |
| atmósfera = Fresnel falso sin física | atmosphere.frag completo | M6 |

---

## 2. Reglas transversales de implementación (aplican a las 10 fases)

- **Commits atómicos** por sub-fase con prefijo `M<id>:` (p. ej.
  `M6: atmósfera raymarch Rayleigh/Mie`). Cada fase debe quedar revertible de
  forma independiente.
- **Regresión cero con toggles OFF:** con todos los features nuevos apagados,
  el binario debe comportarse (y renderizar) como 16b6fa5. Verificar con
  captura idéntica (misma cámara, mismo dato congelado) antes/después de cada
  fase.
- **Presupuesto de rendimiento objetivo:** 1920×1080 en iGPU (Iris Xe / Vega
  8), ≥ 45 FPS con volumen ON + aurora + trazado HF simultáneos. Si una fase
  lo compromete, su toggle de calidad la salva (cada fase define el suyo).
- **Capturas antes/después** obligatorias por fase (misma cámara: congelar
  cine-mode o cámara fija en init) — se usan en la validación.
- **Validación numérica** cuando exista valor de referencia (cada fase lo
  declara: elevación de salto, MUF del circuito de prueba, contaje de celdas
  OVATION, etc.).
- **Sin nuevos endpoints ni assets binarios** salvo los declarados
  explícitamente en M4 (getbest), M9 (nada: reusa OVATION) y M10 (SDO JPG).
- **Idioma de UI:** tooltips EN (regla de la casa), comentarios de código
  ES/EN como el resto del repo.

---

## 3. Fase 0 — Higiene del motor (prerrequisito, ~0.5 día)

Nada de las 10 ideas es visible si la ventana no puede redimensionarse o el
proceso revienta al cerrar. Esta fase NO es una de las 10: es la base.

### M0.1 — Resize funcional
- `glfwSetFramebufferSizeCallback` ya está registrado (App.cpp:594) pero el
  callback no reconstruye nada.
- Diseño: el callback solo encola `pendingW/pendingH` atómicos. En el frame:
  si cambian → `impl->sceneFBO.init(w, h)` (y `bloomFBO[0/1].init(w/2, h/2)`,
  el quad de post-proceso si depende del tamaño) + `camera.setAspect(h ? w/h
  : 1)`. Con M1 (MSAA) el FBO de escena llevará además el resolve — mantener
  toda la recreación en un único `recreateTargets(w,h)`.
- Aceptación: redimensionar en vivo (ventana 800×600 → 1920×1080 → 640×480)
  sin corruptelas ni crash; el limbo y el ImGui reescalan; el replay sigue.

### M0.2 — Shutdown limpio
- Zona: hilos de fetch (App.cpp:800-838) y pool GIRO (GiroAdapter.cpp:246-277)
  usan `detach()`.
- Diseño: guardar `std::thread` como miembros con `stopFlag` atómico + condición
  de salida del bucle de refresco; `join()` en el destructor de `Impl` ANTES de
  destruir los objetos GL que los hilos pudieran estar tocando vía punteros.
  GiroAdapter: el pool debe unirse en su propio destructor (los workers solo
  tocan su estado interno, no GL).
- Aceptación: cerrar la app justo durante un refresco (repetir 10×) sin ASAN
  ni crash. `valgrind --tool=helgrind` opcional pero recomendado en el pool.

### M0.3 — CMake usable desde checkout limpio
- `CMakeLists.txt:97` copia `${CMAKE_SOURCE_DIR}/data` (directorio que no
  existe en el ZIP) → POST_BUILD falla.
- Diseño: envolver en `if(EXISTS ${CMAKE_SOURCE_DIR}/data)`. De paso: añadir
  los fuentes nuevos de las fases siguientes a la lista explícita (NO
  `file(GLOB)`, mantener lista explícita como ahora).
- Aceptación: `cmake -S . -B build && cmake --build build` en un checkout
  limpio del ZIP verde.

### M0.4 — Limpieza de comentario stale
- Eliminar/reescribir el comentario «Overlays: se dibujan DESPUÉS del bloom»
  (App.cpp:~1785) que ya no describe el pipeline real (ver §1.2).

---

## 4. Fase M1 — Pipeline lineal: MSAA 4× HDR + bloom correcto + ACES/sRGB (idea `pipe`) (~1 día)

### Defecto actual
1. `sceneFBO` es una textura RGBA16F sin multisampling → aliasing en bordes
   de cáscaras, terminador y puntos.
2. `bloom.frag` se usa en las pasadas H **y** V: `result * u_intensity`
   (línea 22) se aplica dos veces → intensidad efectiva = `intensity²`.
3. Bright-pass duro: `max(c − threshold, 0)` por canal, sin soft-knee →
   halos con bordes duros y popping cuando el umbral cruza valores.
4. `composite.frag` aplica ACES y escribe lineal a un framebuffer no-sRGB →
   gamma incorrecta (imagen o oscura o lavada según drivers; ImGui compensa
   a ojo, la escena no).

### Diseño

**4.1 MSAA 4× en la escena (Framebuffer.h/.cpp):**
```cpp
// Framebuffer::init(int w, int h, int samples = 0)
//   samples > 0:
//     - color: glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples,
//                GL_RGBA16F, w, h, GL_FALSE)  — NO filtrable, NO necesario
//     - depth: glRenderbufferStorageMultisample(GL_DEPTH_COMPONENT24,
//                samples, w, h)
//     - resolve FBO hermano (textura RGBA16F normal) mismo tamaño
//   render(): glBindFramebuffer(GL_FRAMEBUFFER, msaaFbo)
//   resolve(): glBlitFramebuffer(0,0,w,h, 0,0,w,h, GL_COLOR_BUFFER_BIT,
//               GL_NEAREST)  → deja el resultado en .colorTexture
```
- `App::render`: tras `sceneFBO.unbind()` → `sceneFBO.resolve()` ANTES del
  bloom (el bloom y el composite siguen leyendo `colorTexture` — cero cambios
  en las pasadas 2 y 3).
- Uniforme de calidad: `int msaaSamples` persistible (0/2/4, default 4);
  slider «MSAA» en el panel de render existente (donde están bloom/exposure,
  App.cpp:1948). 0 = path actual (útil para iGPU apurados y para A/B).
- GL 4.0 core: multisample textures + `glBlitFramebuffer` son core 3.2+. OK.

**4.2 Bloom correcto (shaders/bloom.frag):**
- Añadir `uniform int u_pass;` (0 = H, 1 = V). `u_intensity` SOLO en
  `u_pass == 1` (vertical, la última). El resto idéntico.
- Soft-knee en el bright-pass (sustituye la resta dura):
```glsl
vec3 c = texture(u_texture, vUV).rgb;
float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
float knee = 0.6;                                  // radio de rodilla
float w  = smoothstep(u_threshold, u_threshold + knee, l);
vec3 bright = c * w;                                // no c - threshold
```
- Renderer/App: al dibujar la pasada V, subir `u_pass=1`; en H, `u_pass=0`.
- **Migración de settings:** la intensidad efectiva vieja era
  `bloomIntensity²`. Al cargar estado persistido con clave vieja, si
  `bloomIntensity > 1.0`, mostrar `sqrt` en el slider una vez (o documentar en
  tooltip). Simplest: dejar el rango 0-3 y avisar en tooltip que ahora es
  lineal; retune por defecto: threshold 0.35, knee 0.6, intensity 1.0,
  strength 0.6.

**4.3 sRGB en composite (shaders/composite.frag):**
```glsl
// al final, tras ACES y SOLO si u_toneMap (bypass exacto sigue siendo bypass):
col = pow(clamp(col, 0.0, 1.0), vec3(1.0/2.2));
fragColor = vec4(col, 1.0);
```
- Manual pow (NO `GL_FRAMEBUFFER_SRGB`): el pase categórico 3b y ImGui
  dibujan DESPUÉS del composite a pantalla con colores ya pensados en sRGB —
  activar el flag global los rompería. El pow local solo afecta a la escena.
- Default `exposure = 1.12` (valor calibrado en la demo; persistible, rango
  0.2-2.5 como ahora).

### Archivos
- `src/Render/Framebuffer.{h,cpp}` (MSAA + resolve)
- `shaders/bloom.frag`, `shaders/composite.frag` (+ fallbacks inline en
  App.cpp:717-743, que DEBEN actualizarse en paralelo)
- `src/App.cpp` (init de FBOs con samples, `resolve()`, uniforms `u_pass`,
  defaults, persistencia `msaaSamples`)

### Aceptación
1. Zoom 200% en captura: borde del terminador y anillos sin «escalera».
2. Bloom: doble de brillo de glow con la MISMA `bloomIntensity` (antes
   cuadrática); halo suave alrededor de luces urbanas sin popping al variar
   threshold ±0.05.
3. Escena sin tonemap-bypass: colores visiblemente correctos (azules de la
   Tierra diurna no lavados); ImGui sin cambios.
4. `msaaSamples=0` reproduce bit-a-bit la imagen de 16b6fa5 (regresión).
5. FPS: MSAA 4× en 1080p cuesta < 15% en iGPU (blit barato); si cuesta más,
   bajar default a 2× con nota en tooltip.

---

## 5. Fase M2 — Replay temporal coherente: el sol sigue al dato (idea `rep`) (~0.25 día)

### Defecto actual
`Impl::updateSunDirection()` (App.cpp:248-253) usa `std::time(nullptr)`
(wall-clock). En replay, `replayEpoch` (App.cpp:1073-1102) desplaza los datos
GIRO/grids/volumen al pasado, pero el terminador, el día/noche de la Tierra y
la `applyDayNight` del volumen quedan en el AHORA → «datos nocturnos con sol
de mediodía».

### Diseño (quirúrgico)
```cpp
// antes: void updateSunDirection() { double utc = (double)std::time(nullptr); ... }
void updateSunDirection(double utc) { sunDirection = SolarPosition::getDirection(utc); }
```
- Llamarla **cada frame** desde el bucle de render, con el epoch efectivo:
```cpp
double effEpoch = (impl->replayEpoch > 0.0) ? impl->replayEpoch
                                            : (double)std::time(nullptr);
impl->updateSunDirection(effEpoch);
```
- Consumidores ya cableados a `impl->sunDirection` (no tocar): Terminator
  (App.cpp:1043), volumen + `applyDayNight` (App.cpp:1149-1150), shader de la
  Tierra, atmósfera. Un solo productor, muchos consumidores — coherencia
  inmediata y total.
- Coste: `SolarPosition::getDirection` (Meeus bajo) es ~µs; por frame es
  ruido. Si se prefiere, cachear por epoch (actualizar solo si |Δ| > 60 s).
- Con M4 (getbest) la historia por estación llega a 6 h desde el arranque →
  el replay gana rango útil sin esperar a que `tecHist` se llene (36 frames ×
  refresco). Mencionar en el tooltip del TimeBar: «Replay range: min(history)».

### Archivos
- `src/App.cpp` (3 líneas + call-site).

### Aceptación
1. LIVE: sin cambios visibles (regresión cero).
2. REPLAY −3 h: el terminador retrocede ~45° hacia el este (15°/h); la cara
   nocturna de la Tierra coincide con el hemisferio de foF2 deprimido del
   grid GIRO; el volumen D/E se atenúa del lado nocturno correcto.
3. REPLAY y vuelta a LIVE: el sol vuelve suavemente (sin salto brusco de
   más de 1 frame).

---

## 6. Fase M3 — Estrellas con Vía Láctea realista (idea `city`) (~0.5 día)

### Defecto actual
`Stars.cpp` (71 líneas): 1600 puntos uniformes con `srand(42)`, sin catálogo,
sin banda galáctica, sin colores por temperatura.

### Diseño (parámetros exactos de la demo, ya validados visualmente)
- **Total 6300 estrellas** en 3 poblaciones (radio de la esfera estelar: el
  que ya use el motor para estrellas, escala relativa idéntica):
  1. **3400 uniformes:** dirección aleatoria isótropa
     (`u = 2·rand−1`, `φ = 2π·rand`, `s = sqrt(1−u²)`,
     `dir = (s·cosφ, u, s·sinφ)`), magnitud `2.2 + rand^2.2 · 3.6`.
  2. **2900 banda galáctica:** normal del plano galáctico
     `n = normalize(0.48, 0.82, 0.31)`; base ortonormal
     `t1 = normalize(cross((1,0,0), n))`, `t2 = cross(n, t1)`;
     ángulo `θ = 2π·rand`; dispersión gaussiana aproximada
     `g = (r1+r2+r3 − 1.5)/1.5` (suma de 3 uniforms ≈ gauss);
     `dir = normalize(t1·cosθ + t2·sinθ + n·(g·0.16))`
     (ancho de banda σ≈0.16 rad ≈ 9°); magnitud `2.6 + rand^2.0 · 3.4`.
  3. **60 brillantes con halo:** dirección isótropa, magnitud `rand·1.4`.
- **Colores por temperatura** (prob. acumulada):
  `12% → (1.0, 0.72, 0.52)` gigantes rojas · `18% → (1.0, 0.9, 0.75)` cálidas
  · `42% → (1.0, 0.98, 0.95)` blancas · `20% → (0.78, 0.86, 1.0)` azul-blanco
  · `8% → (0.62, 0.74, 1.0)` azules.
- Atributos por vértice: `pos (vec3)`, `aMag (float)`, `aColor (vec3)`.
- **Shader (star.vert/star.frag, reemplaza el actual):**
  - `gl_PointSize = mix(3.5, 1.0, (aMag − 0.0)/6.0) · uPixelRatio` (tamaño
    inverso a magnitud; brillantes ~3.5 px, débiles ~1 px).
  - Twinkle sutil: `float tw = 0.85 + 0.15·sin(uTime·(1.5 + fract(aPos.x)·2.0) + aMag·7.0);`
  - Frag: disco suave `alpha = smoothstep(0.5, 0.15, length(gl_PointCoord − 0.5))`,
    color `aColor · brillo`, blending aditivo, `depthWrite off`.
- Un solo `glDrawArrays(GL_POINTS, 0, 6300)` — coste despreciable.
- Toggle «Milky Way band» (ON por defecto en ESTA capa porque las estrellas
  ya existen — es un reemplazo visual de un feature existente; si se quiere
  ser estricto con la regla de la casa, toggle «Legacy stars» OFF por defecto
  que restaura el field viejo. Decisión menor para MUSE).
- Futuro anotado (NO implementar): catálogo Hipparcos ~9k en textura de
  atributos + picos de difracción en <2 mag.

### Archivos
- `src/Globe/Stars.{h,cpp}` (regeneración de buffers + atributos nuevos)
- `shaders/star.{vert,frag}` (+ fallback inline)

### Aceptación
1. Banda galáctica visible como arco de densidad (captura nocturna lado
   anti-sol); 60 estrellas dominan por brillo.
2. Sin patrón repetitivo ni alineación con el ecuador terrestre (la normal
   galáctica está fija en el mundo: correcto, la esfera estelar no rota con
   la Tierra — así lo hace también la demo).
3. FPS sin cambios medibles (< 0.5 ms).
4. Twinkle perceptible solo en pausa/observación (amplitud 15%).

---

## 7. Fase M4 — GIRO por getbest GET (cortés) con historia (idea `giro`) (~1 día)

### Defecto actual
`GiroAdapter` lanza un pool de hasta 16 hilos que **POSTean**
`https://giro.uml.edu/didbase/scaled.php` para 129 estaciones cada 5 min, sin
backoff y sin control de 429 (GiroAdapter.cpp:246-277). Riesgo real de
rate-limit/baneo del servicio (público y académico). Además la respuesta de
scaled.php solo trae el valor instantáneo: la historia para replay se
construye esperando refrescos.

### Descubrimiento clave (verificado en vivo durante la revisión)
El endpoint **GET** `https://lgdc.uml.edu/fastchar/getbest` devuelve, en UNA
llamada por estación, la **serie temporal completa** del carácter pedido
(mismas columnas de texto de ancho fijo que scaled.php — el parseo existente
sirve). Sustituye al POST masivo y trae 6 h de historia de regalo.

### Diseño

**7.1 Endpoint y formato:**
```
GET https://lgdc.uml.edu/fastchar/getbest?ursiCode=EB040&charName=foF2&fromDate=2026.09.07T09:55:00&toDate=2026.09.07T15:55:00
```
- Fecha: `YYYY.MM.DDTHH:MM:SS` (UTC). `fromDate = now − 6 h`, `toDate = now`.
- `User-Agent` propio identificable (p. ej. `ionosphere-live-3d/1.0 (educational)`).
- Timeout: 12 s por estación.
- Respuesta: texto con líneas de columnas — mismas posiciones que
  scaled.php (`test_giro_cols` ya valida `B0=[26], B1=[28], fmin=[30]`);
  `toks[0]` = timestamp, `toks[2]` = valor del carácter. Si el cuerpo contiene
  `STATUS: ERROR` → estación inválida esta ronda (silencio, no error global).
- Se necesita valor instantáneo para los 8 campos IDW de perfiles
  (foF2/hmF2/B0/B1/foE/foEs/…): la demo llama getbest con `charName=foF2`
  para la HISTORIA y usa el parseo multi-columna del último snapshot para el
  resto. Para el C++: **opción A (recomendada):** mantener 1 llamada
  getbest/estación con `charName=foF2` (historia + último valor) y conservar
  el valor instantáneo de las demás columnas de la ÚLTIMA ronda ya parseada
  (el motor ya lo tiene en StationSample); **opción B:** getbest por cada
  carácter (8× llamadas — no recomendado, multiplica carga 8×).

**7.2 Cortesía (el corazón de la idea):**
- Concurrency máx **6** hilos (no 16), pausa **250 ms** entre lanzamientos.
- Caché 10 min por estación (timestamp por código URSI).
- Backoff ante **429** o fallo: duplicar el intervalo de refresco de esa
  estación hasta 30 min (decaimiento exponencial, reset al éxito). Flag
  `degraded` visible en UI («GIRO: rate-limited, retry 12 min»).
- Estaciones: priorizar el subconjunto **51 verificado en vivo** (Apéndice B)
  antes que el catálogo completo de 129; el catálogo (ya se descarga) sirve
  para el dropdown de estaciones. Con 51 × 1 GET cada 10 min el servicio
  apenas nota la carga.

**7.3 Integración de la historia:**
- Parsear todas las líneas de la respuesta → pares (timestamp, foF2),
  quedarse con las últimas 72 (6 h @ 5 min).
- Alimentar el buffer de historia por estación que ya usa
  `sampleHistoryAt()`/`tecHist` (GiroAdapter mantiene historia interna:
  insertar deduplicando por timestamp y en orden). Resultado: el replay
  tiene 6 h de rango desde el primer minuto de ejecución.
- En replay, `gsAct` ya interpola por `replayEpoch` (App.cpp:1101-1102) —
  cero cambios en el consumidor.

**7.4 Eliminación del POST:**
- Borrar el pool POST de scaled.php (o dejarlo tras `#ifdef GIRO_LEGACY_POST`
  como fallback si getbest falla 24 h — decisión para MUSE; recomendación:
  borrarlo, el fallback añade superficie de baneo).

### Archivos
- `src/Data/GiroAdapter.{h,cpp}` (endpoint, parseo, historia, backoff,
  concurrencia 6 + pausa 250 ms)
- `src/App.cpp` (mensaje de estado GIRO en UI; rango de replay dinámico en
  TimeBar)

### Aceptación
1. Sniff de red: **cero POSTs** a didbase; ~51 GETs/10 min con pausas.
2. 24 h de ejecución: ningún 429 (o recuperado con backoff sin crash).
3. Replay usable a −6 h inmediatamente tras el arranque (grid foF2 coherente,
   no plano).
4. EB040 (Roquetes) siempre en la lista prioritaria — es la estación del
   usuario.
5. Con GIRO caído (cortar red en pruebas): la app sigue con
   `simulated`/último dato, como hoy.

---

## 8. Fase M5 — Grid GIRO + fondo climatológico ponderado (idea `grd`) (~1 día)

### Defecto actual
`interpLayerProfiles()` (App.cpp:474-542) hace IDW de 8 campos con
**distancia plana** (error grande en latitudes altas) y fondo = **mediana
global** de estaciones. Consecuencia física visible: los océanos (sin
estaciones) heredan la mediana «de otra longitud» — de noche, el Pacífico
muestra foF2 diurnos de otro hemisferio.

### Diseño

**8.1 Distancia gran círculo (haversine, sustituye la plana):**
```cpp
// en grados (motor ya usa glm::radians; R no importa, solo el ángulo):
double havDeg(double la1, double lo1, double la2, double lo2) {
    double dla = radians(la2-la1), dlo = radians(lo2-lo1);
    double a = sin(dla/2)*sin(dla/2) +
               cos(radians(la1))*cos(radians(la2))*sin(dlo/2)*sin(dlo/2);
    return degrees(2.0*asin(min(1.0, sqrt(a))));
}
```

**8.2 Modelo climatológico de fondo (validado en la demo; port directo):**
```cpp
// foF2 climatológico (MHz) — CCIR-like simplificado con F10.7 vivo
double modelFoF2(double latDeg, double lonDeg, double epoch, double f107) {
    double zen  = solarZenithDeg(latDeg, lonDeg, epoch);   // ya existe vía SolarPosition
    double cosChi = cos(radians(zen));
    double lat  = fabs(latDeg);
    // actividad: F10.7 70 → ×1.0, 200 → ×1.55
    double solar = 0.62 + 0.0046 * clamp(f107, 60.0, 220.0);
    // base diurna + reserva nocturna (recarga del vientre ecuatorial)
    double dayBase   = 3.2 + 7.8 * pow(max(cosChi, 0.0), 0.7);
    double nightBase = 1.6 + 1.8 * exp(-lat / 40.0);
    double t = smoothstep(-0.12, 0.15, cosChi);           // transición crepuscular
    double fo = (1.0-t)*nightBase + t*dayBase;
    // anomalía ecuatorial: cresta ±15-16°, valle en ecuador geomagnético
    double eq = exp(-pow((lat-16.0)/12.0, 2.0)) - 0.55*exp(-pow(lat/20.0, 2.0));
    fo *= 1.0 + 0.32*eq*t;
    // depresión invernal (N en invierno, S en verano) por inclinación F2
    int doy = dayOfYear(epoch);
    double seasonal = cos(((doy-172)/365.0)*2.0*M_PI) * (latDeg > 0 ? 1.0 : -1.0);
    fo *= 1.0 + 0.06*seasonal*(lat/90.0);
    return fo * solar;
}
// hmF2 climatológico (km): sube de noche, más alto a bajas latitudes
double modelHmF2(double latDeg, double lonDeg, double epoch) {
    double zen = solarZenithDeg(latDeg, lonDeg, epoch);
    double cosChi = cos(radians(zen)); double lat = fabs(latDeg);
    double dayH = 250.0 - 0.35*lat, nightH = 300.0 + 0.2*lat;
    double t = smoothstep(-0.12, 0.15, cosChi);
    return (1.0-t)*nightH + t*dayH;
}
```
- `smoothstep(a,b,x)` = `t=clamp((x−a)/(b−a),0,1); t·t·(3−2t)`.
- F10.7: ya llega (SolarIndicesAdapter, 45-day forecast). `solarZenithDeg`:
  derivable de `impl->sunDirection` y la convención esférica (zenit = ángulo
  entre normal del punto y sol).

**8.3 Mezcla medición↔modelo por cercanía:**
```cpp
double nearDeg = distancia gran-círculo al puesto de estación MÁS CERCANO;
double wG = smoothstep(55.0, 25.0, nearDeg);   // 1 cerca de estaciones, 0 lejos
foF2_cell = wG * foF2_IDW + (1.0 - wG) * modelFoF2(...);   // ídem hmF2
```
- Campos restantes del IDW de 8 columnas (B0/B1/foE/foEs/…): solo heredan la
  corrección haversine (mismo fondo de mediana que hoy, pero con peso
  correcto). El modelo solo sustituye fondo en **foF2 y hmF2** (los que
  gobiernan el volumen y la MUF).
- `nearestStationDist`: min sobre estaciones activas del grid — 72×72×51 =
  264k haversines por rebuild: trivial (< 5 ms) si se cachea por estación
  (recalcular solo cuando cambian las estaciones activas).

### Archivos
- `src/App.cpp` (`interpLayerProfiles` ~40 líneas modificadas; helper
  `modelFoF2/modelHmF2` junto a las demás estáticas)
- (opcional) `src/Ionosphere/LayerProfile.h` si se prefiere exponer para tests

### Aceptación
1. Pacífico nocturno (replay a una hora en que sea de noche allí): foF2
   2-4.5 MHz (no la mediana diurna 7-9).
2. Junto a EB040/DB049 (wG≈1): valores = medición GIRO, sin cambio vs 16b6fa5.
3. Cresta ecuatorial visible como banda de foF2 elevado a ±15° lat
   geomagnética en el día.
4. Los tests de perfil existentes (si se restauran los test_*.cpp, ver
   Cuestiones §15.4) siguen pasando: la física de `evalNeTotal` no se toca.
5. Captura A/B del volumen: el hemisferio nocturno oceánico se deprime de
   forma creíble.

---

## 9. Fase M6 — Atmósfera Rayleigh/Mie real (idea `atm`) (~2 días)

### Defecto actual
`shaders/atmosphere.frag` (entero, 26 líneas): Fresnel `pow(3)` + dos colores
constantes (dayColor/sunsetColor) mezclados por `sunAlign`. Sin profundidad
óptica, sin dependencia de altura, sin vista. Es LA cáscara falsa que el
propio David señala. Nunca recibe bloom coherente ni alimenta el halo.

### Diseño

**9.1 Modelo (single scattering con sombra dura — idéntico al validado en la
demo; unidades normalizadas a radio terrestre = 1.0):**
```glsl
// constantes (UNIDADES R⊕ — no convertir):
const float PLANET_R = 1.0;
const float ATM_R    = 1.0255;                     // tope ~162 km
const float H_R      = 8.5e-3;                     // escala Rayleigh (8.5 km)
const float H_M      = 1.2e-2;                     // escala Mie (12 km)
const vec3  BETA_R   = vec3(5.8, 13.5, 33.1) * 0.0105;   // m⁻¹ → R⊕
const float BETA_M   = 21.0 * 0.0105;
const float G_MIE    = 0.76;

float raySphere(vec3 ro, vec3 rd, float r) {       // t de entrada o -1
    float b = dot(ro, rd);
    float c = dot(ro, ro) - r*r;
    float h = b*b - c;
    if (h < 0.0) return -1.0;
    return -b - sqrt(h);
}
```
- Marcha: **14 muestras de vista × 5 de sol**, densidades
  `exp(-h/H_R)`, `exp(-h/H_M)` con `h = length(p) - 1.0`.
- Fase Rayleigh `(3/16π)(1+cos²θ)`; Mie Henyey-Greenstein
  `(1-g²)/(4π (1+g²-2g cosθ)^1.5)`.
- **Sombra dura del planeta** (el detalle que vende el atardecer): en cada
  muestra, si `raySphere(p, sunDir, PLANET_R) >= 0` → sin luz directa.
- Profundidad óptica acumulada por el segmento cámara→muestra
  (`odR/odM` acumulados con exponente) → extinción
  `exp(-(BETA_R·odR + BETA_M·1.1·odM))`.
- Color final: `(ΣR·BETA_R·phaseR + ΣM·BETA_M·phaseM)·uIntensity·20.0`
  (factor 20 calibrado en la demo), alpha = luminancia — blending aditivo
  como ya usa la capa.

**9.2 Render state:**
- Reusar la esfera existente de `Atmosphere` (32×64) escalada a `ATM_R`
  (hoy ya se escala a su radio; verificar el model/uniform de escala).
- **Cull FRONT faces** (dibujar el hemisferio trasero de la cáscara): el
  raymarch necesita la salida lejana; la entrada `t0` se recomputa
  analíticamente en el shader desde `u_cameraPos` (exacta, no interpolada).
  `vPos` solo aporta dirección: `rd = normalize(vPos - u_cameraPos)`.
- Orden: dibujar DESPUÉS de la Tierra con depth test ON y depth write OFF
  (la parte tras el planeta queda oculta por el depth → limbo correcto).
  Posición en la secuencia actual (atmósfera tras tierra/estrellas, App.cpp
  1815) ya sirve.
- `tExit = raySphere(ro, rd, ATM_R)` (salida); `tEnter = raySphere(ro, rd,
  ATM_R)` entrada (cámara fuera SIEMPRE: la cámara orbita a ~2-4 R⊕);
  `tPlanet = raySphere(ro, rd, PLANET_R)` → `tExit = min(tExit, tPlanet)`.
  Marcha entre max(tEnter, 0) y tExit.

**9.3 Aerial perspective en `earth.frag` (la mejora que une Tierra y cielo):**
- Añadir al frag de la Tierra: atenuación del color de superficie por la
  profundidad óptica cámara→superficie (`exp(-tau)` con las MISMAS
  constantes, 1 muestra por píxel — el camino es corto) + inscatter
  aproximado `(1 - exp(-tau))·skyTint` con `skyTint` dependiente de
  `dot(normal, sunDir)` (azul de día, anaranjado a ras del terminador).
- La demo además añade **sombra de nubes** en earth.frag usando la textura
  diurna como proxy de cobertura (`cov = luminancia canales ~nubes`):
  `col *= 1 - 0.22·cov·shadow·dayAmount`. Copiar tal cual (2 líneas, gran
  efecto).

**9.4 Calidad y rendimiento:**
- Presupuesto: la cáscara cubre ~25-40% de pantalla; 14×5 = 70
  texturas-no/maths por píxel → OK en iGPU (la demo corre 60fps a 1080p con
  el mismo coste en WebGL).
- Toggle de calidad «Atmosphere: High (raymarch) / LUT»:
  - LUT (fallback): transmittance 256×64 horneada UNA vez en init (CPU
    o pase offscreen) con la misma física; el shader consulta la LUT en vez
    de marchar al sol. Muestras de vista 12.
  - Default: High. Persistir.
- Uniformes nuevos: `u_cameraPos` (ya existe), `u_intensity` (slider
  «Atmosphere intensity», default 1.0, rango 0.25-2).

### Archivos
- `shaders/atmosphere.frag` (reescritura total) + fallback `loadFromSource`
  actualizado en App.cpp:717-743
- `shaders/earth.frag` (aerial perspective + sombra de nubes)
- `src/Globe/Atmosphere.{h,cpp}` (cull mode, uniforms, escala a ATM_R)
- `src/App.cpp` (estado render: depth write off en su bloque, slider,
  persistencia)

### Aceptación
1. Halo azul en el limbo diurno que transiciona a rojo/naranja SOLO en el
   anillo del terminador (captura al atardecer sobre el Atlántico).
2. La cara nocturna NO gana halo azul (sombra dura del planeta funcionando).
3. Aerial perspective: el horizonte de la Tierra diurna ligeramente azulado,
   los continentes mantienen contraste (sin lavado).
4. Sombra de nubes visible sobre el Amazono/Pacífico diurno.
5. FPS ≥ 45 en iGPU con High; con LUT, ≥ 55.
6. Con «Atmosphere OFF» (toggle existente): imagen = 16b6fa5 sin la cáscara
   vieja (la cáscara falsa se elimina — es reemplazo, no adición).

---

## 10. Fase M7 — Absorción D (Sauer-Wilkinson) integrada en LUF (idea `abs`) (~0.5 día)

### Defecto actual
`RadioPropagationAdapter.cpp:92-93`:
```cpp
float absorb = 1.2f + 2.8f * dayF * fmaxf(0.f, cosf(latDeg·π/180));  // heurística plana
float luf = 0.35f * muf + 0.5f * absorb;
```
Sin flare: un evento X-ray M/C (SID, sudden ionospheric disturbance) realza la
capa D y absorbe 2-20 m de día — el LUF actual no se entera y el panel miente
durante los fadeouts.

### Diseño
Sustitución con el modelo de la demo (Sauer-Wilkinson simplificado; datos que
YA llegan: `impl->lastXray->latest.flux` vía XrayAdapter — el `flareFactor`
ya existe y el volumen ya reacciona; aquí se conecta el lado radio):

```cpp
// dayFactor = max(0, cosZenit) en el punto MEDIO del circuito (ya disponible)
// F = flujo X-ray GOES 0.1-0.8nm en W/m² (XrayAdapter::latest.flux)
float absorb      = 1.2f + 2.8f * dayFactor * 0.7f;               // base diurna
float flareAbsorb = (F > 1e-7f)
    ? 2.5f * powf(F / 1e-6f, 0.75f) * (0.3f + 0.7f * dayFactor)   // A ∝ F^0.75
    : 0.0f;
float luf = 0.35f * muf + 0.5f * (absorb + flareAbsorb);
luf = clamp(luf, 1.5f, 20.0f);
```

- Absorción D como magnitud **mostrable** (dB, mismo modelo que la demo usa
  para el aviso):
```cpp
float dRegionAbsorptionDb(float cosChi, float F, float kp) {
    float dayFactor = fmaxf(0.f, cosChi);
    float sid = (F >= 1e-7f)
        ? 4.5f * powf(fmaxf(F, 1e-7f) / 1e-6f, 0.75f) * dayFactor : 0.f;
    float auroral = 0.35f * powf(fmaxf(kp, 0.f), 2.1f);   // capa D polar
    return sid + auroral;
}
```
- `currentKp()` ya existe en App.cpp (usado en App.cpp:1141 para
  `radioInterpGrid`). Conectarlo.
- **Aviso en UI:** si `flareAbsorb > 1.0` (≈ M1+) en el circuito actual →
  texto ámbar «D-region fadeout: +X.X dB» en el panel de radio existente (el
  que muestra LUF/FOT) + tooltip explicativo EN.
- La variable de mapa 5-7 (LUF) hereda el cambio automáticamente vía
  `radioInterpGrid` (App.cpp:1138-1141) — verificar que el rango del colormap
  del LUF se re-autocalibra (o subir su max a 20).

### Archivos
- `src/Data/RadioPropagationAdapter.cpp` (lufEstimate + helper dB)
- `src/App.cpp` (aviso en panel radio, kp ya disponible)

### Aceptación
1. B4.1 en calma (F≈4e-7, como el día de la verificación): LUF idéntico a
   16b6fa5 (flareAbsorb≈0.5 → dentro del ruido).
2. Inyectar en pruebas un F=1e-5 (M4.7 ficticio): LUF sube ~4-5 MHz y el
   aviso salta; restaurar al volver a B-class.
3. Kp≥6: absorción auroral visible en el valor dB (componente 0.35·Kp^2.1).
4. Sin X-ray data (adapter caído): degradación a la heurística vieja
   (flareAbsorb=0), sin crash.

---

## 11. Fase M8 — Ray-tracing HF multi-salto (idea `hf`) (~2.5 días)

### Defecto actual
No existe modelo de saltos: `SliceLayer` muestra Ne(h) vertical pero no
refracta; el «MUF» de GIRO es el de las estaciones (M3000), no del circuito.
No hay geometría de circuito, ni elevación, ni bandas por salto.

### Diseño — capa nueva `HFTraceLayer` + panel «Circuit»

**11.1 Física (port exacto de la demo, ya validado contra valores de
libro):**
```cpp
// Geometría del salto (secante clásica, teorema del seno) — dado elevación β
// (deg) y altura virtual h' (km), con R = 6371:
//   i  = asin( R·cos(β) / (R + h') )        // incidencia desde la vertical
//   salto = 2·R·(π/2 − β − i)               // en radianes de arco terrestre
struct HopGeometry { double hopKm; double incidenceDeg; };
HopGeometry hopGeometry(double elevDeg, double hVirtualKm);

// Inversa por bisección (el salto DECRECE al subir la elevación):
//   rango de búsqueda [1°, 88°], 48 iteraciones, si f(1°)<0 → 1°,
//   si f(88°)>0 → 88°.
double elevationForHop(double targetKm, double hVirtualKm);

// Nº de saltos óptimo: n ∈ 1..6 que minimiza |D/n − 3000| con D/n ≤ 4000.
int hopCountForDistance(double totalKm);

// M(D) del circuito (Dudeney + ITU-R P.1240):
//   M3000 = 1490 / (hmF2 + 216)
//   M(D)  = M3000 · (max(D,200)/3000)^0.24
//   MUF(D) = foF2(reflexión) · M(D)
// FOT = 0.85 · MUF (mínimo del circuito = MUF del peor salto).

// Estado de banda (f en MHz):
//   open     si LUF ≤ f ≤ FOT
//   marginal si 0.85·LUF ≤ f ≤ MUF
//   closed   en el resto
```
- **Altura virtual** del circuito: `h' = clamp(hmF2(puntoMedio), 140, 420)`
  (muestreado del grid hmF2 = `giroInterpGrid(gsAct, 2, …)` — variable de
  mapa 3).
- foF2 por punto de reflexión: `giroInterpGrid(gsAct, 0, …)` (variable 1),
  muestreo bilineal en la celda.
- LUF del circuito: el de M7 (absorción incluida) — **la ideas `abs` y `hf`
  se cierran aquí**.

**11.2 Circuito de referencia para validación** (usado en la demo):
TX **DB049** (Dourbes, Bélgica) → RX **EG931** (Fairford, UK), D ≈ 2511 km:
3 saltos, h′≈310 km, elevación ≈ **7.9°**, MUF ≈ **15.3 MHz** con foF2 del
grid del día de la validación. (La versión arccos del catálogo daba 1° y
saltos de 15.000 km — el arcsin de arriba es el correcto; validar i contra
tablas de Davids/ITU para D=3000/h′=300 → i≈74°, salto≈3900 km tangente.)

**11.3 Geometría de render (ribbon camera-facing, patrón de la demo):**
- Por salto: 2 patas × 14 muestras. Pata = interpolación slerp del punto de
  suelo al punto de reflexión con altitud `h'·t` → posición mundo vía
  `CoordinateSystem::latLonAltToPos` (convención del motor, sin dudas).
- Cada vértice: `pos`, `aT` (0..1 longitud de arco total, para el pulso),
  `aViable` (0/0.5/1 según MUF del salto vs frecuencia del panel).
- **Ribbon:** extruir cada vértice perpendicular a la vista
  (`n = normalize(cross(dirSegmento, dirCámara))`, ancho ~0.004 R⊕) en el
  vertex shader — la geometría CPU es una polilínea; el shader la engorda.
  Alternativa más simple: `GL_LINE_STRIP` grueso no existe en core → ribbon
  es el camino.
- Shader: color por `aViable` (verde 0.3/1.0/0.4 · ámbar 1.0/0.75/0.3 · rojo
  1.0/0.35/0.35), **pulso viajero**: brillo `+0.6·exp(−40·fract(aT·3 −
  uTime·0.15)²)` — un «paquete» que recorre el circuito; blending aditivo,
  depth write OFF.
- TX/RX: marcadores (reusar el disco de `IonosondeLayer` o esferitas
  propias) + etiqueta ImGui proyectada (patrón ya existente en tooltips).

**11.4 UI (panel «Circuit», collapsible en SidePanel):**
- TX: estación pineada actual (la que ya existe) o dropdown.
- RX: **modo pick** (botón «Pick RX…» → el próximo click sobre un punto de
  estación vía `IonosondeLayer::hitTest`, IonosondeLayer.h:85 lo fija) o
  dropdown. Default: DB049→EG931 si no hay nada pineado.
- Slider frecuencia 1-30 MHz (default 14.0) — recalcula bandas.
- Salida: D, n saltos, h′, elevación, MUF circuito, FOT, LUF, y **matriz de
  bandas** 10 filas (160/80/60/40/30/20/17/15/12/10 m → 1.8/3.5/5.3/7.0/10.1/
  14.0/18.1/21.0/24.9/28.0 MHz) con estado open/marginal/closed (color
  verde/ámbar/rojo).
- En replay: `gsAct` ya sigue al epoch → el trazado Y la matriz reaccionan
  al scrubbing (demostrativo: ver la MUF caer en la zona nocturna).
- Toggle capa «HF trace» (checkbox, off por defecto, tooltip EN, persist).

**11.5 Rendimiento:** rebuild CPU del circuito solo cuando
(estación RX/TX | freq | grids | replayEpoch) cambie (~60 vértices: trivial).
Pulso = uniform `uTime`, sin rebuild.

### Archivos
- NUEVOS `src/Ionosphere/HFTraceLayer.{h,cpp}` + `shaders/hftrace.{vert,frag}`
  (+ fallbacks inline)
- `src/App.cpp` (instancia, render en el bloque aditivo de overlays, panel
  «Circuit», persistencia, pick RX)
- Física pura en `HFTraceLayer.h` como funciones `static` testeables sin GL.

### Aceptación
1. DB049→EG931: 3 saltos, elevación 7-8°, MUF 13-17 MHz según el día (el
   15.3 exacto depende del grid vivo).
2. Circuitos cortos (<500 km): 1 salto, elevación >60° (near-vertical
   incidence) — sanity check de la bisección.
3. La matriz de bandas es coherente con FOT/MUF/LUF mostrados (open dentro
   de [LUF,FOT]).
4. En replay −3h con noche en el punto medio: MUF cae visiblemente (el
   foF2 nocturno baja) y bandas altas pasan a closed.
5. FPS: coste < 0.5 ms (un draw, ~200 triángulos).

---

## 12. Fase M9 — Aurora volumétrica con cortinas instanciadas (idea `aur`) (~1.5 días)

### Nota honesta (LEER — decisión explícita para MUSE)
El propio plan visual de David (`docs/visual-life-plan.md`) dice: «Cortinas
de aurora verticales: nuestra aurora es una cáscara plana sin dimensión
vertical → solo parpadeo de brillo, no scroll vertical. **Sin cortinas
falsas**», y reserva para el futuro la producción E-region real dentro de
`evalNeTotal`. La demo implementa cortinas 3D **instanciadas** como capa
independiente plantadas en celdas OVATION reales (probabilidad NOAA). No es
«scroll falso sobre cáscara plana»: es geometría 3D con datos debajo. Aún
así, la objeción de David es legítima — el plan propone:

- **Opción A (recomendada — lo que el usuario vio y aprobó en la demo):**
  capa nueva «Aurora curtains» instanciada, OFF por defecto, tooltip EN
  honesto («Illustrative 3D curtains driven by NOAA OVATION probability»).
  La cáscara plana actual (GridLayer aurora, App.cpp:119) SE CONSERVA como
  «Aurora (probability)» — el usuario elige.
- **Opción B (mínima, conforme al plan del repo):** solo añadir shimmer/flicker
  modulado por Kp a la cáscara existente (Fase 3.2 del visual-life-plan).
- **Opción C (futura, fuera de alcance aquí):** producción E-region auroral
  en `evalNeTotal` modulada por Kp/AE → cortinas con física real (tocaría la
  regla nº1 de la casa; David la tiene anotada como futuro).

Implementar **A** salvo indicación contraria de MUSE/David; documentar la
decisión en el commit.

### Diseño (Opción A)

**12.1 Datos (YA llegan — cero endpoints nuevos):**
- `AuroraAdapter` descarga OVATION (~65k puntos `[lon, lat, prob 0-100]`).
- Downsample a celdas de **2°**: `lat2 = round(lat/2)·2`, `lon2 = round(lon/2)·2`;
  media de prob por celda; **descartar p ≤ 8%** → ~2k celdas del anillo.

**12.2 Instancias (parámetros exactos de la demo):**
- Por celda: **2 quads cruzados** (rotación 0 y π/2) → ~4k instancias.
- Quad base: `PlaneGeometry(1,1)` — 4 vértices, 6 índices.
- Atributos por instancia (divisor 1):
  - `iPos (vec3)`: posición en la esfera unidad (convención del motor).
  - `iEast (vec3)`: **vector este local** — orientación E-O de la cortina:
    `phi = (lon+180)·π/180; east = normalize(sin(phi), 0, cos(phi))`.
    (Las cortinas aurorales cuelgan E-O respecto al campo magnético; la
    demo usa este geográfico — suficiente visualmente, nota para C.)
  - `iUp (vec3)`: radial (= iPos normalizado).
  - `iProb (float)`: p/100.
  - `iPhase (float)`: `rand·2π` (flicker descorrelacionado).
  - `iRot (float)`: 0 o π/2.
- GL 4.0 core: `glVertexAttribDivisor(attr, 1)` +
  `glDrawElementsInstanced(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0, nInst)`
  (instanced arrays son core 3.3+). Un solo draw call.

**12.3 Shaders:**
```glsl
// VERT (esencia): quad corner c ∈ [−0.5, 0.5]²
vec3 width  = iEast * (c.x * 0.014);                    // ~90 km de ancho
vec3 height = iUp   * ((c.y + 0.5) * H);                // H = altura de la cortina
vec3 p      = iPos + rotate(width + height, iRot sobre iUp);
float wave  = sin(uTime*0.8 + iPhase + c.x*6.0) * 0.004; // ondulación lateral
p += iEast * wave;
// altura H por instancia: 95-250 km escalada por probabilidad:
//   H = (0.015 + 0.024*iProb) * uHeightScale        // (km→R⊕, 95→0.0149, 250→0.0393)
// uHeightScale = slider 1.0-3.0, default 2.0 (exageración declarada, como el resto del motor)
```
```glsl
// FRAG: gradiente físico de emisión
//   base (100-150 km): OI 557.7 nm  → verde (0.14, 1.0, 0.45)
//   alto (>200 km):    OI 630.0 nm  → rojo  (1.0, 0.28, 0.30)
vec3 col = mix(verde, rojo, smoothstep(0.35, 0.8, vHeightNorm));
float flicker = 0.75 + 0.25*sin(uTime*(2.0 + iProb*3.0) + iPhase);
float a = iProb * flicker * (1.0 - smoothstep(0.7, 1.0, vHeightNorm)); // fade superior
fragColor = vec4(col * a, a);        // blending ADITIVO, depthWrite off
```
- **Kp modula la intensidad global:** uniform `uKp` (de `currentKp()`):
  `intensidad *= 0.55 + 0.45·min(kp,8)/8` — a Kp alto el óvalo arde; a Kp 0
  casi no hay anillo (coherente con OVATION: p baja).
- Render: en el bloque aditivo de overlays (tras `auroraLayer` plana, dentro
  de sceneFBO — participa del bloom de M1: las cortinas BLOOMEAN, efecto
  «neón» del que presumen las capturas de la demo).
- Culling: bounding sphere radio 1.6 centrada en origen (frustum cull del
  motor si existe; si no, es un solo draw — da igual).

**12.4 UI:** checkbox «Aurora curtains» + slider «Curtain height ×» +
tooltip EN. OFF por defecto (regla de la casa). Persist.

### Archivos
- NUEVOS `src/Ionosphere/AuroraCurtainLayer.{h,cpp}` +
  `shaders/auroracurtain.{vert,frag}` (+ fallbacks inline)
- `src/App.cpp` (downsample 2°/p>8 al recibir el bundle, instancia de la
  capa, render, UI, persistencia)

### Aceptación
1. Las cortinas cubren SOLO el anillo OVATION real (comparar captura con el
   mapa NOAA del mismo minuto — validación visual con la web de SWPC).
2. Cero cortinas con p≤8; densidad y altura crecen con p.
3. Gradiente verde→rojo vertical visible en pausa (con uHeightScale 2).
4. Kp actual modula el brillo global (inyectar Kp 7 en pruebas: el óvalo
   clearly más intenso).
5. FPS: < 1.5 ms de coste (4k quads instanciados = 1 draw).
6. Con la capa OFF: bit-igual a 16b6fa5 (la cáscara plana intacta).

---

## 13. Fase M11 — Disco solar SDO real + regiones activas (idea `sdo`) (~1 día)

### Defecto actual
No hay imagen solar: el repo no dibuja el sol (solo el terminador). La
demo muestra el disco HMIIF/AIA-304 en vivo con las regiones activas de
`sunspot_report.json` geoposicionadas — el «por qué» de Kp/X-ray visible.

### Diseño

**13.1 Adaptador nuevo `SdoImageAdapter` (patrón de los existentes):**
```
GET https://sdo.gsfc.nasa.gov/assets/img/latest/latest_512_HMIIF.jpg     (fotosfera)
GET https://sdo.gsfc.nasa.gov/assets/img/latest/latest_512_0304.jpg     (cromosfera 304 Å)
GET https://sdo.gsfc.nasa.gov/assets/img/latest/latest_512_0193.jpg     (corona 193 Å, opcional)
```
- Caché **15 min**; solo descargar cuando el panel esté visible (toggle) —
  cortesía con SDO.
- Decode con `stb_image` (ya en `libs/stb`) → textura GL RGBA8, `GL_LINEAR`,
  `GL_CLAMP_TO_EDGE`, 512×512. Re-subida solo al refrescar.
- El hilo worker decodifica y publica el buffer RGBA en el bundle (NADA de
  stb/GL en el hilo de render salvo el `glTexImage2D` en el frame).

**13.2 Regiones activas (`sunspot_report.json` — endpoint NUEVO pero del
mismo NOAA SWPC que ya se consulta):**
```json
[{"time_tag":"2026-09-07 00:30","Region":4441,"Latitude":-12,
  "Longitude":145,"Location":"S12W45", ...}]
```
- Extender `SolarIndicesAdapter` (que ya va a SWPC por F10.7/Kp) o el nuevo
  `SdoImageAdapter` — preferir extender SolarIndicesAdapter: es «índices».
- Mantener por `Region` el último reporte (campo `seen`).

**13.3 Proyección al disco (mejora sobre la demo, que usaba mapeo lineal):**
- Preferir el campo `Location` "S12W45" cuando exista: lat = −12,
  Δlon = −45 (W = oeste del meridiano central, signo negativo en convención
  este-positiva). Si no existe, usar `Latitude`/`Longitude` (Carrington) con
  L0 calculado (abajo) y `Δlon = wrap180(lonCarr − L0)`.
- Visible si `|Δlon| ≤ 90°`.
- **L0 (longitud Carrington del meridiano central):**
```cpp
// CR1 empezó en JD 2399101.5; rotación media 27.2753 d
double CRfrac = fmod(jd - 2399101.5, 27.2753) / 27.2753;
double L0 = CRfrac * 360.0;                      // deg, crece con el tiempo
```
- **B0 (latitud heliográfica del centro del disco):**
```cpp
// nodo ascendente del ecuador solar: Ω ≈ 75.77°; inclinación 7.25°
// λs = longitud eclíptica aparente del Sol (derivable de sunDirection ya
// calculado —SolarPosition— o de Meeus bajo; ya portado en el motor)
double B0deg = degrees(asin(sin(radians(7.25)) * sin(radians(λs - 75.77))));
```
- Ortográfica al disco (radio imagen = 1):
```cpp
double rho = cos(lat);                          // ρ = cos(lat)
double x = rho * sin(Δlon);                     // este → derecha? NO:
double xPix = -rho * sin(Δlon);                 // el Sol se ve E-W invertido
double yPix = sin(lat)*cos(B0) - rho*sin(B0)*cos(Δlon);
// clamp por |xPix|,|yPix| ≤ 1; fuera → no dibujar
```
- La demo usaba `x = 50 − (lon/180)·40` (lineal, sin B0/L0) y quedaba
  razonable; la proyección de arriba es la correcta — validar visualmente
  contra la imagen HMIIF (las manchas DEBEN caer sobre grupos oscuros).

**13.4 UI (ventana ImGui nueva «Sun», toggle en View/Layers):**
- `ImGui::Begin("Sun")` → `ImGui::Image((ImTextureID)(intptr_t)tex, {300,300})`
  (primer uso de imagen en la UI del motor — ImGui_ImplOpenGL3 ya soporta
  texturas nativas; sin cambios de backend).
- Overlays de regiones: dibujar sobre la imagen con `ImDrawList` del window
  (`ImGui::GetWindowDrawList()->AddCircleFilled`) + tooltip al hover:
  «AR4441 · S12W45 · 5 spots» (lo que traiga el report).
- Click en la imagen alterna HMIIF ↔ AIA-304 (título indica cuál).
- Caption: «SDO/HMIIF · updated HH:MM UTC» + X-ray class actual al lado
  (ya llega).
- Posición por defecto: esquina superior derecha, colapsable, persistir
  posición/tamaño + visible.

### Archivos
- NUEVOS `src/Data/SdoImageAdapter.{h,cpp}` (stb decode + bundle RGBA)
- `src/Data/SolarIndicesAdapter.cpp` (sunspot_report fetch+parse)
- NUEVO `src/UI/SunPanel.{h,cpp}` (ventana ImGui + proyección + drawlist)
- `src/App.cpp` (registro, textura GL, toggle, persistencia)
- `CMakeLists.txt` (nuevos fuentes)

### Aceptación
1. Ventana Sun: disco HMIIF nítido 300×300; manchas reales del día visibles.
2. Los marcadores caen sobre grupos de manchas (validar con la imagen del
   mismo minuto; si el parseo de Location falla, con L0 la desviación debe
   ser < 10°).
3. Alternar a AIA-304: plasma/fulguraciones en naranja (mismo tamaño).
4. Tráfico: 1 JPG/15 min SOLO con el panel abierto; cerrado = 0 peticiones.
5. Clic en imagen alterna fuente sin recomputar regiones (mismos markers).

---

## 14. Matriz idea × archivos (resumen de impacto)

| Fase | Archivos NUEVOS | Archivos TOCADOS |
|------|-----------------|------------------|
| M0 | — | App.cpp, CMakeLists.txt, GiroAdapter.cpp (join) |
| M1 pipe | — | Framebuffer.{h,cpp}, bloom.frag, composite.frag, App.cpp |
| M2 rep | — | App.cpp (3 líneas) |
| M3 city | — | Stars.{h,cpp}, star.{vert,frag} |
| M4 giro | — | GiroAdapter.{h,cpp}, App.cpp (TimeBar/UI) |
| M5 grd | — | App.cpp (interpLayerProfiles + helpers) |
| M6 atm | — | atmosphere.frag, earth.frag, Atmosphere.{h,cpp}, App.cpp |
| M7 abs | — | RadioPropagationAdapter.cpp, App.cpp (aviso) |
| M8 hf | HFTraceLayer.{h,cpp}, hftrace.{vert,frag} | App.cpp, CMakeLists.txt |
| M9 aur | AuroraCurtainLayer.{h,cpp}, auroracurtain.{vert,frag} | App.cpp, CMakeLists.txt |
| M10 sdo | SdoImageAdapter.{h,cpp}, SunPanel.{h,cpp} | SolarIndicesAdapter.cpp, App.cpp, CMakeLists.txt |

6 capas/adaptadores nuevos, ~14 archivos tocados, 2 endpoints nuevos
(getbest ya era conocido; sunspot_report), 1 familia de shaders nueva + 2
reescritos. `evalNeTotal`/`DensityVolume`: **intactos**.

## 15. Cuestiones abiertas para MUSE (decisiones de revisión)

1. **M9 opción A/B/C** (§12): cortinas instanciadas (lo que David vio y
   aprobó) vs. la letra del visual-life-plan («sin cortinas falsas»). El
   plan recomienda A con tooltip honesto; ¿confirma?
2. **M4 opción A/B** (§7.1): 1 getbest/estación (foF2+historia, resto de
   columnas del snapshot en vivo) vs. 8 getbest/estación. Recomendada A.
   ¿El pool POST se borra o queda `#ifdef` legacy?
3. **M1 migración de `bloomIntensity`**: el valor persistido cambia de
   significado (era cuadrático). ¿sqrt al cargar una vez, o simplemente
   retune manual documentado en tooltip?
4. **Tests**: los `test_*.cpp` citados en los docs (test_slice, test_profile,
   test_giro_cols, test_ebro_parse) NO están en el ZIP 16b6fa5. ¿Existen en
   local de David? Si no: este plan sugiere crear `tests/` con al menos
   `test_hop_geometry` (elevación 7.9° para D=2511/h′=310 — validation de
   libro), `test_model_foF2` (rango nocturno Pacífico), `test_getbest_parse`
   (fixture de respuesta real) y un target CTest opcional.
5. **M3 reemplazo vs. toggle legado**: el starfield viejo ¿se conserva tras
   un toggle o se sustituye sin vuelta atrás? (Regla de la casa: lo nuevo
   off por defecto — pero reemplazar un feature existente no es «nuevo».)
6. **M10 layout de la ventana Sun** en ImGui: flotante libre vs. dock fijo.
   El motor no usa docking → flotante colapsable, ¿OK?
7. **Presupuesto global**: el plan asume iGPU 1080p ≥45 FPS con todo ON
   (volumen+MSAA 4+atm High+cortinas+HF). Si el objetivo real de David es
   60 FPS duros, bajar defaults: MSAA 2, atm LUT, cortinas ON solo con Kp≥4
   (las demás veces la cáscara plana basta).

## 16. Estrategia de verificación global (integración final)

1. **Build matrix:** Debug + Release, `-Wall -Wextra` sin warnings nuevos,
   clang-tidy si ya está configurado (no introducir).
2. **Smoke 10 min:** arrancar, abrir TODAS las capas nuevas una a una,
   replay −6h → LIVE, redimensionar, cerrar. Sin leak (RSS estable ±5%),
   sin GL errors (`glGetError()` = 0 en debug builds — el motor ya lo
   comprueba? si no: assert en Development).
3. **Regresión dorada:** con todos los toggles OFF + `msaaSamples=0` +
   atmósfera OFF (la vieja cáscara ya no existe — comparar SOLO Tierra/
   estrellas/TEC/puntos): captura ≠ solo por el nuevo sRGB del composite →
   A/B consciente y documentado (el sRGB es INTENCIONAL y global).
4. **Datos en vivo 24 h:** GIRO sin 429, OVATION/SDO/X-ray refrescando,
   `degraded` nunca permanente.
5. **Validación física externa** (mismo día): MUF del circuito vs
   propagation prediction de HWU/voacap online si se quiere oro; mínimo:
   bandas abiertas vs WSPR spots reales de 20/40 m (chequeo cualitativo).

## 17. Cronograma y esfuerzo estimado

| Fase | Días | Acumulado | Dependencias |
|------|------|-----------|--------------|
| M0 higiene | 0.5 | 0.5 | — |
| M1 pipe | 1.0 | 1.5 | M0 |
| M2 rep | 0.25 | 1.75 | — (paralela con M1) |
| M3 city | 0.5 | 2.25 | — (paralela) |
| M4 giro | 1.0 | 3.25 | M0 (shutdown) |
| M5 grd | 1.0 | 4.25 | M4 (grids limpios ayudan) |
| M6 atm | 2.0 | 6.25 | M1 (HDR/bloom correctos) |
| M7 abs | 0.5 | 6.75 | — |
| M8 hf | 2.5 | 9.25 | M5 (hmF2/foF2 con fondo), M7 (LUF) |
| M9 aur | 1.5 | 10.75 | M1 (bloom) |
| M10 sdo | 1.0 | 11.75 | — (independiente) |
| Integración + verificación | 1.0 | 12.75 | todas |

**~13 días de trabajo efectivo.** M3/M2/M7/M10 son «rellenos» paralelizables
si hay varios agentes.

---

## Apéndice A — Constantes y fórmulas canónicas (chuleta)

```
R⊕ = 6371 km · r = 1 + km/6371 · convención: theta = pi − lon, phi = pi/2 − lat
ATM_R = 1.0255 · H_R = 8.5e-3 · H_M = 1.2e-2 · G_MIE = 0.76
BETA_R = (5.8, 13.5, 33.1)·0.0105 · BETA_M = 21·0.0105 · atm gain 20.0
M3000 = 1490/(hmF2+216) · M(D) = M3000·(D/3000)^0.24 · FOT = 0.85·MUF
Salto: i = asin(R·cosβ/(R+h')) · hop = 2R·(π/2−β−i) · bisección [1°,88°]
Absorción: A_x = 4.5·(F/1e-6)^0.75 dB (día) · A_aur = 0.35·Kp^2.1
LUF = 0.35·MUF + 0.5·(1.2 + 1.96·dayF + 2.5·(F/1e-6)^0.75·(0.3+0.7·dayF))
Mezcla GIRO↔modelo: wG = smoothstep(55°, 25°, distEstaciónMásCercana)
Bandas: 1.8 3.5 5.3 7.0 10.1 14.0 18.1 21.0 24.9 28.0 MHz
Estrellas: 3400 uniformes + 2900 banda (n = norm(0.48,0.82,0.31), σ=0.16) + 60 halo
Aurora: celdas 2° p>8 · 2 quads/celda · 95-250 km · OI 557.7 nm (verde) / 630 nm (rojo)
Getbest: lgdc.uml.edu/fastchar/getbest?ursiCode=&charName=foF2&fromDate=YYYY.MM.DDTHH:MM:SS
SDO: sdo.gsfc.nasa.gov/assets/img/latest/latest_512_{HMIIF,0304,0193}.jpg
SWPC: services.swpc.noaa.gov/json/{planetary_k_index_1m, sunspot_report}.json
```

## Apéndice B — 51 estaciones GIRO verificadas en vivo (subconjunto prioritario M4)

```
EB040 EA036 PQ052 JR055 KL154 KI167 FF051 GM037 AT138 VT139 OL246 LD160
MZ152 RV149 TR170 RL052 SO148 TR169 DB049 EG931 CS839 BC840 AC843 EI764
PF765 AU930 BVJ03 SMK29 TNJ20 CAJ2M CXM9B BLJ03 LA42Q DW41K CB53N TO535
IC437 DH224 NI135 XI434 CS31K TV51R MA155 MO155 ML10L GR13L MU12K RM041
NDA81 TR0P2
```
(EB040 = Roquetes, Observatorio del Ebro — estación del autor, primera
siempre. El catálogo completo de 129 sigue usándose para el dropdown.)

## Apéndice C — Qué cambia 16b6fa5 vs. la versión anterior revisada

Solo 2 archivos, relacionados con el volumen:
- `src/App.cpp` (+~16 líneas): `volumeMode` (0 = densidad dentro de la escena
  con tonemap; 1 = **capas categóricas dibujadas DESPUÉS del composite**,
  colores exactos sin bloom, oclusión terrestre analítica) — patrón «Pass 3b»
  (App.cpp:1857-1866) que M1 debe respetar (el pow sRGB NO debe afectarle:
  confirmado, va directo a pantalla tras el composite).
- `src/Ionosphere/VolumeRenderer.cpp`: en `u_colorMode==1`, recorte del
  raymarch contra la esfera del planeta (analítico) para poder dibujar sin
  depth buffer compartido.
Implicación para este plan: M1 (sRGB) y M9 (orden de overlays) deben
convivir con el Pass 3b — ya contemplado en §4.3 y §12.3.

