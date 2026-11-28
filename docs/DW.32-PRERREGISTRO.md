# DW.32 — V13: honestidad de nubes decorativas e hito Voyager 1 día‑luz

Estado: PRE-REGISTRO (sin código de la feature). Base `origin/master` =
`f87882573c804b378b25dd4bba84d83b7ce5dd63`. Worktree `ionosphere-dw32-cpp`,
rama `dw.32-honesty-hud`.

## 1. Coste

Solo texto/HUD: **0 draws nuevos**. Según DW.30/DW.31:
`DRAW_GATE NO APLICABLE` (0 draws) y **frame completo NO MEDIBLE** por
los stalls de 90,8 MB en `update` (bug abierto). Coste real de V13
**NO VERIFICADO**; veredicto PARCIAL. El criterio es de honestidad,
no de rendimiento.

## 2. Estado verificado

- `App.cpp:5321-5324`: slider `Clouds` con tooltip *"Procedural animated
  cloud cover (seamless noise, drifts with time). 0 = clear sky"*; no
  declara que es decorativo ni que no es dato meteorológico.
- `VoyagerPanel.cpp:96-110`: muestra `light time %.2f h one way`,
  `%.2e W received` (link budget, no telemetría) y `sweep x N over 10 s`
  con compresión declarada. **No** explicita el hito de ~1 día‑luz.

## 3. Cambio propuesto

1. Tooltip de `Clouds`: declarar **DECORATIVO**, ruido procedural
   animado, **no producto meteorológico ni imagen satelital**, sin dato
   real; el slider sigue igual.
2. Panel Voyager: línea nueva bajo el tiempo‑luz con el hito
   `≈ N light-day(s)` calculado de `probe.lightHours / 24`, con tooltip
   que declara que es el retardo de ida real (range/c) y que la
   animación lo comprime por el factor `xN` ya mostrado. Si no hay
   rango válido, no se muestra ninguna cifra (doctrina "no range, no
   claim").
3. Sin tocar `earth.frag`, shaders, adaptadores ni tests. Sin draws.

## 4. Criterio binario

1. `cmake --build` rc=0; `ctest` rc=0 (65); sin diff en tests/shaders.
2. Grep de `DECORATIVE`/`light-day` y de que no hay `glDraw*` nuevo.
3. Off/byte-idéntico en render (no hay cambio de render).
4. Visual PARCIALMENTE NO VERIFICADO si falla la captura.

@IMPLEMENTADOR
