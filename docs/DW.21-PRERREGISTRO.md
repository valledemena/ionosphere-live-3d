# DW.21 - V2: líneas de campo gruesas antialiasadas con IGRF-14 completo

Estado: PRE-REGISTRO (sin codigo). Base `origin/master` =
`618b456d5e7d1e6a0b9be4f41991701ed765259d`. Worktree
`ionosphere-dw21-cpp`, rama `dw.21-thick-field-lines`.

Decision de `@ARQUITECTO`: **V2 ya**; V1 queda reencuadrado/diferido
(como slice propio de solo acumulacion temporal), no cancelado.

## 1. Coste MEDIDO (no estimado)

Sonda temporal `PROBE-D21` (App::update, `dt` acumulado cada 300 frames,
magnetosfera visible) sobre el arbol vigente, worktree limpio:
```
[BENCH-D21] avg_dt_ms=24.619 frames=300   (arranque)
[BENCH-D21] avg_dt_ms=17.056 frames=300
[BENCH-D21] avg_dt_ms=16.997 frames=300
[BENCH-D21] avg_dt_ms=16.562 frames=300
[BENCH-D21] avg_dt_ms=16.847 frames=300
[BENCH-D21] avg_dt_ms=16.387 frames=300
```
Baseline en regimen: **16,4–17,1 ms/frame** (~58–61 fps), etiquetado
como **flag OFF** (estado actual: 32 lineas analiticas, sin traza RK2).
La sonda fue
**restaurada** (grep `PROBE-D21` = 0) y se reconstruyo `--clean-first`.
El coste del estimado 2 de ARQUITECTO se sustituye por este baseline:
el objetivo es que V2 con flag ON no supere **19,0 ms/frame** medido
con la misma sonda (incremento <= ~2 ms sobre 17 ms).

## 2. Estado del código (medido)

- `CMakeLists.txt:63` compila `src/Ionosphere/MagnetosphereLayer.cpp`
  (ruta exacta; el texto "src/Render" era inexacto, sin impacto).
- `MagnetosphereLayer.cpp:264-328`: 32 lineas dipolares analíticas
  (`r = L·cos²λ`, L=1.5/2.5/4/6, 8 meridianos), `GL_LINES` con un
  shader de posicion+color; `Framebuffer` RGBA16F con depth como
  renderbuffer; GLAD GL 4.0 core (geometry shader disponible,
  compute/SSBO no).
- `src/Data/IrtamIgrfCoeffs.inc`: `kIgrf2015[195]` (NMAX=13) y
  `kIgrf2015s[80]`; `IrtamGridEval.cpp` ya implementa `buildModel(year)`
  y `feldg(glat, glon, alt, Bnorth, Beast, Bdown, Babs)` con esos
  coeficientes, pero `feldg` **no esta expuesto** en el header.
- `tests/test_volume_jitter.cpp` y `Utils/VolumeJitter.h` son el jitter
  V1 ya existente: **no se tocan**.

## 3. Cambio propuesto

### 3.1 IGRF-14 completo ya presente (condicion 2)

- Exponer en `IrtamGridEval.h` una funcion pura:
  `bool igrfFull(double decimalYear, double latDeg, double lonDeg,
                 double altKm, double& bn, double& be, double& bd,
                 double& babs)` que llame a `buildModel(year)` + `feldg`.
- `MagnetosphereLayer` tracea las lineas integrando la direccion de B
  (paso fijo, RK2, max N pasos, corte al cruzar la superficie o el
  limite superior). Semillas en las mismas cascas L actuales para
  conservar la comparabilidad; el campo se evalua en marco
  **Tierra/geodetico (IGRF)**. La magnetopausa Shue sigue en **GSM**:
  se declara que **no se mezclan marcos**; no se rota el dipolo con el
  Sol.

### 3.2 Lineas gruesas antialiasadas (flag OFF)

- Shaders nuevos `shaders/fieldline.vert/.geom/.frag` (con fallback
  inline en el .cpp como el resto de capas): expansion en geometry
  shader de cada segmento `GL_LINES` a una cinta orientada a camara,
  semiancho constante en pixeles, coverage analitica en el fragment.
- `bool thickFieldLines = false` en `App::Impl`, persistido en
  `settings.cfg` con **default OFF**; con OFF se usa el shader y VBO
  actuales (`GL_LINES`), byte-identicos a hoy.
- Con ON, el mismo VBO de segmentos se dibuja con el shader nuevo;
  sin changes en `test_volume_jitter.cpp`.

## 4. Criterio binario

1. **Unitarios**: `igrfFull` devuelve B finito y no cero en un punto de
   prueba concreta `lat=40.0`, `lon=-75.0`, `alt=300 km`, `year=2025.0`;
   `ctest` rc=0 con el total de la base
   + tests nuevos.
2. **Build**: `cmake --build` rc=0; uniforms/atributos del shader
   fallback y del fichero sincronizados.
3. **Coste medido**: misma sonda `PROBE-D21`, con etiqueta explicita
   `flag=OFF` y `flag=ON` en cada linea; ON debe mantenerse <= 19,0
   ms/frame en regimen. La traza RK2 puede exigir rebuild de la
   geometria: se mide tambien el coste del rebuild (primer bloque
   tras activar el flag) y se reporta separado del regimen; el limite
   de 19 ms aplica al regimen, el rebuild se declara con su valor.
4. **Visual**: capturas antes/despues con el mismo epoch y camara,
   lineas gruesas continuas sin escalones; `test_volume_jitter.cpp`
   intacto (git diff vacio para ese fichero).
5. **Alcance**: no tocar TEC, HF, viento, cinturones DW.19/DW.20,
   jitter V1, ni Framebuffer (para V1 temporal se abrira otro slice).

## 5. No-goals

- No cambiar el marco de la magnetopausa (GSM) ni inventar rotacion
  solar del dipolo.
- No activar compute/SSBO (GLAD 4.0).
- No merge sin veredicto del VERIFICADOR (condicion 3 de ARQUITECTO).
- No tocar `test_volume_jitter.cpp` (condicion 4).

## 6. Evidencia

`git diff --numstat`, grep de `thickFieldLines`/`igrfFull`, salida de
`ctest`, dos bloques `[BENCH-D21]` (OFF/ON), capturas, `handoff.py check`.

## 7. Riesgos

- Integrar B completo puede cambiar la forma de las lineas respecto al
  dipolo: se conservan las semillas y se documenta la diferencia.
- El geometry shader aumenta vertices por segmento: el limite de 19 ms
  es falsable y puede forzar reducir semillas/anchos antes de merge.

## Adendum — notas del veredicto v1 (incorporadas)

`VERIFICADOR`: **CONFORME CON NOTAS**, sin GO de ejecucion ni merge.

1. El bench de 6 valores queda etiquetado como **OFF**. En la ejecucion
   cada bloque `[BENCH-D21]` llevara `flag=OFF` o `flag=ON` con la
   misma sonda para que `ON <= 19.0 ms` sea falsable.
2. RK2 no esta en el baseline: se mide por separado el **rebuild** de
   la traza y el regimen; limite de 19 ms solo para regimen.
3. Punto de prueba de `igrfFull` declarado: `lat=40.0`, `lon=-75.0`,
   `alt=300 km`, `year=2025.0`.
4. `tests/test_volume_jitter.cpp` y `src/Utils/VolumeJitter.h` deben
   quedar con `git diff` vacio; `thickFieldLines` se persiste en
   `settings.cfg` con **default OFF** (se comprueba tras cerrar).
5. Ruta corregida: `CMakeLists.txt:63` -> `src/Ionosphere/MagnetosphereLayer.cpp`.

