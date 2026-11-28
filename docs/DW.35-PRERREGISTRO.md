# DW.35 — F4: IDW de residuo sobre fondo climatológico + métrica

Estado: PRE-REGISTRO (sin código de la feature). Base `origin/master` =
`3bdb7f3d75242733270cd9754bdf50f9b16ad93d`. Worktree `ionosphere-dw35-cpp`,
rama `dw.35-idw-residual`.

## 1. Estado verificado

- `Utils/IdwInterp.h::giroInterpGrid()` interpola **la magnitud directa**
  (foF2/MUF/hmF2/NmF2) con fondo = mediana global débil (`wBg=0,004`).
- `Data/IrtamGridEval.h::igrfDip()` da MODIP (`xmodip`) en marco Tierra;
  `IrtamIgrfCoeffs.inc` trae NMAX=13.
- No hay zenith helper ni fondo climatológico ni métrica de residuo.
- Tests existentes: `test_hdr_idwinterp.cpp` (sin tocar).

## 2. Cambio propuesto (núcleo, sin render)

Nuevo `src/Utils/IdwResidual.h` (header-only, puro, testeable):

1. `climatologyFoF2(lat, lon, utcSeconds, f107)`:
   fondo **analítico declarado**, no dato medido. Usa `xmodip` de
   `irtameval::igrfDip` y el cenit solar calculado de UTC+lat/lon.
   Parametrización: `foF2_bg = A(f107) * g(|modip|) * max(0, cos(chi))^0.5`,
   con `A` lineal en F10.7 y `g` creciente en |MODIP|; coeficientes
   declarados en el código y aquí.
2. `giroResidualGrid(samples, utc, f107, w, h)`:
   `resi = observado - fondo` por estación válida no stale; IDW del
   **residuo** con radio 50° y peso de fondo débil; resultado final
   `bg + resi_interp`, mismo layout 72×72 que GloTEC.
3. `residualMetrics(samples, utc, f107)`: leave-one-out, reporta bias,
   RMSE y RMSE estratificado por `|MODIP|` bajo/alto y cenit
   día/noche. Declara que es validación interna sobre las mismas
   estaciones, **no** un producto operativo.

## 3. Criterio binario

1. `cmake --build` rc=0; `ctest` rc=0 con **66 tests** (uno nuevo);
   `test_hdr_idwinterp.cpp` y todos los existentes sin diff.
2. Test determinista con datos sintéticos: si las observaciones son
   exactamente el fondo, el residuo es ~0 y la métrica RMSE≈0;
   si se añade un offset conocido, el grid lo recupera.
3. Coste CPU del core (no GPU): `climatologyFoF2` + `giroResidualGrid`
   sobre 72×72 y ~129 estaciones, 1000 repeticiones; se reporta p50
   y se declara que **no sustituye** al gate de frame de DW.30
   (frame completo NO MEDIBLE, bug 90,8 MB abierto). Coste de render
   NO APLICABLE (0 draws).
4. Sin tocar `App.cpp` ni capas visuales: F4 es núcleo puro, flag de UI
   no se añade en este slice. Visual NO APLICABLE. Veredicto PARCIAL.

## 4. No-goals

- No cambiar `giroInterpGrid` existente (path actual intacto).
- No F5/F6, no render, no red nueva.
- No merge sin veredicto.

@IMPLEMENTADOR
