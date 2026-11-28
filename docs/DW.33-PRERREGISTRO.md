# DW.33 — V11a: vista heliosférica y espirales de Parker

Estado: PRE-REGISTRO (sin código de la feature). Base `origin/master` =
`fa261045f73bd2a2b4f96a414ff6a0da10b1bd74`. Worktree `ionosphere-dw33-cpp`,
rama `dw.33-heliosphere`.

V11 tiene tres mecanismos (tubos Parker, billboards de sondas, Luna en
magnetosfera). Se divide como V3/V5/V10: **V11a** = cámara extendida +
espirales Parker; **V11b** = sondas + Luna, pre-registro aparte.

## 1. Coste

- La feature añade **1 draw** (espirales). Aplica `DRAW_GATE`
  (`glFinish` + reloj de alta resolución envolviendo **sólo** el draw
  nuevo; 300 muestras, p50/p95/min/max, pares por índice), con
  `ON_draw_p50 <= OFF_draw_p50 + 2,0 ms`. OFF ejecuta **0 draws**.
- **Frame completo NO MEDIBLE** por los stalls de 90,8 MB en `update`
  documentados en DW.30 (bug abierto); se declara de nuevo. Coste de
  frame real NO VERIFICADO; veredicto PARCIAL.

## 2. Estado verificado

- `camera.distance` se limita a `[1,5 , 45]` R_E en `App.cpp:2165` y
  al cargar settings en `:3462`; el tooltip de Fit dice
  "zoom limit is 45 R_E".
- `SolarWindRiver` vive a 18–28 R_E; la magnetopausa Shue a 6–12 R_E.
- Sin líneas Parker ni vista heliosférica.

## 3. Cambio propuesto

- Flag `heliosphereView` **OFF por defecto**, persistido y con checkbox.
- Con ON, el límite superior de cámara pasa de **45 a 200 R_E** (sólo
  mientras el flag está activo; al apagarse vuelve a 45 y se reclampa
  la distancia actual).
- Espirales de Parker analíticas: 12 líneas en el plano ecuatorial
  del marco solar, `r(phi) = r0 + (v / omega) * phi`, con **v = 400
  km/s declarado** y `omega` solar; muestreadas en 64 segmentos,
  dibujadas con un único `glDrawArrays(GL_LINE_STRIP, ...)` por línea
  o un draw indexado conjunto. Color por radio, blend aditivo, depth
  mask off. Un solo VAO/VBO.
- **Declarado**: es una parametrisación visual de la espiral de
  Parker, no un campo MHD ni una trayectoria de partícula medida; el
  marco es el mismo del río (sol). Sin tocar el río ni la magnetopausa.

## 4. Criterio binario

1. `cmake --build` rc=0; `ctest` rc=0 (65); sin diff en tests.
2. OFF byte-idéntico: clamp 45 y ningún draw nuevo (grep).
3. `DRAW_GATE` sobre el draw nuevo (OFF=0), p50/p95/min/max.
4. Grep de 45→200, `Parker`, v=400 y del draw añadido.
5. Visual PARCIALMENTE NO VERIFICADO si falla la captura.

## 5. No-goals

- No sondas billboard ni Luna (V11b).
- No tocar V1–V13 ni física F4/F5/F6.
- No merge sin veredicto.

@IMPLEMENTADOR
