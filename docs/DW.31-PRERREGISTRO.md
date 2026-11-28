# DW.31 — V12: estado de viento rápido/lento visible

Estado: PRE-REGISTRO (sin código de la feature). Base `origin/master` =
`2a657d5e5ae3a18b535cc5623b67221d66804f36`. Worktree `ionosphere-dw31-cpp`,
rama `dw.31-wind-state`.

## 1. Coste (métrica heredada de DW.30)

Este slice **no añade draws**: reutiliza el VBO de colores existente y
una rama en el HUD. Según el addendum de DW.30 (`14d3ba8`), la métrica
aplicable es `DRAW_GATE` con `glFinish` + reloj de alta resolución:

- OFF ejecuta **0 draws nuevos**; ON ejecuta **0 draws nuevos**.
- `ON_draw_p50 <= OFF_draw_p50 + 2,0 ms` es trivialmente cierto.
- **Frame completo NO MEDIBLE** por los stalls de red documentados en
  DW.30 (12/12 bloques descartados, 90,8 MB en `update`); se declara
  de nuevo y no se afirma que el gate original pase.

## 2. Estado del código (anclas)

- `SolarWindRamp.h`: rampa web portada (violeta→blanco calido),
  `webColor(speed, bz, density, seed)`; la usan `SolarWindLayer` y
  `SolarWindRiver`.
- `SolarWindRiver::refreshColors()` rellena `verts_[i*6+3..5]` cuando
  cambia el viento; las tres variantes de render (POINTS, instanced y
  TF de DW.24) leen ese mismo VBO / su copia `tfColors_`.
- HUD del río en `App.cpp:6001`: `river: %.0f km/s n ... Bz ...`.

## 3. Cambio propuesto

- Flag `windStateMode` **OFF por defecto**, persistido y con checkbox.
- Umbrales declarados y falsables: **FAST** si `v >= 500 km/s`,
  **SLOW** si `v <= 400 km/s`, **TRANSITIONAL** en el intervalo
  (400, 500). Umbranes sólo visuales, no clasificación física.
- Con flag ON, `refreshColors()` aplica un énfasis determinista sobre
  el color de la rampa: FASTA realza el azul/cian, LENTA realza el
  ámbar; TRANSITIONAL deja el color de la web intacto. Sin cambiar
  posiciones, tamaños, alfas ni número de partículas.
- Con flag ON, el HUD muestra el régimen con la velocidad real:
  `river: ... [FAST WIND v>=500]` / `[SLOW WIND v<=400]` /
  `[TRANSITIONAL]`.
- OFF: colores y HUD byte-idénticos al head. Sin shaders nuevos, sin
  FBOs, sin tocar `SolarWindRamp.h` ni `SolarWindRiverSim.h`.

## 4. Criterio binario

1. `cmake --build` rc=0; `ctest` rc=0 (65); sin diff en tests ni en
   `SolarWindRamp.h`/`SolarWindRiverSim.h`.
2. OFF byte-idéntico por construcción (rama nueva dentro de
   `if (windStateMode_)`), sin draws nuevos (`0` en ambos brazos).
3. `DRAW_GATE` con 0 draws en ambos brazos (métrica de DW.30).
4. Grep de umbrales 500/400, `FAST WIND`/`SLOW WIND` y de que no hay
   `glDraw*` nueva.
5. Visual PARCIALMENTE NO VERIFICADO si falla la captura.

## 5. No-goals

- No cambiar la rampa web compartida, ni el ×60, ni la edad del dato.
- No V11/V13 ni física pesada (F4/F5/F6).
- No merge sin veredicto.

@IMPLEMENTADOR
