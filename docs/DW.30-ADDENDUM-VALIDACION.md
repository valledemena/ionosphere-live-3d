# DW.30 — v3 addendum: validez de la corrida y fallback de micro-medición

Estado: ADDENDUM al pre-registro v3 (`5596cf7e`). No cambia el alcance
ni los flags. Se escribe **antes** de ejecutar la feature.

## 1. Criterio de validez (condición de @ARQUITECTO)

Además de la regla `max(dt) > 100 ms -> DISCARDED`:

- Se reporta la **tasa de descarte** por brazo: `descartados / bloques`.
- Si un brazo tiene **menos de 3 bloques retenidos de 4**, o si la tasa
  de descarte de ON supera en más de 1 bloque a la de OFF, la corrida
  se declara **NO MEDIBLE** y se repite. No se permite PASS por subset.
- Los bloques se emparejan por índice: `arm0[0]` vs `arm1[0]`, etc.

## 2. Halazgo de la medición v3 ya ejecutada

`dw30_baseline_v3.err`: 12 bloques, **12 DESCARTADOS** (tasa 100 %).
`p50` 15,5–16,4 ms en todos, pero `max` de 985 a 28 549 ms. Los
WATCHDOG muestran que el stall está en `update` (consumo de
`History preload: 391 cached + 38 fetched (90,8 MB)`), no en `render`.
Con 0 bloques retenidos, el gate v3 es **NO MEDIBLE** según la regla 1.

## 3. Fallback propuesto (si la inestabilidad ambiental persiste)

Micro-medición GPU del coste **añadido por los draws nuevos**,
independiente de `update`/red:

- En la sonda, alrededor de cada llamada a `renderD/renderEs/renderToi`
  se hace `glFinish()` + `QueryPerformanceCounter` antes y después;
  se acumula el tiempo de GPU+driver de 300 frames.
- Se reportan p50/p95/min/max de esa medida para OFF y ON con el
  mismo binario, cámara y datos fijos.
- Gate de este fallback: `ON_p50_render <= OFF_p50_render + 2,0 ms`.
- La métrica de frame completo se sigue reportando, pero se declara
  **NO MEDIBLE** si no se cumplen 3 bloques retenidos por brazo.
- Si tampoco estabiliza, se aplica el fallback de tres slices de la
  condición 4 de @ARQUITECTO.

## 4. Sin cambios

Tres flags OFF independientes, un draw aditivo c/u, sin FBOs nuevos,
capas anteriores intactas, `ctest` 65 y tests sin diff, salvedad visual.

@IMPLEMENTADOR
