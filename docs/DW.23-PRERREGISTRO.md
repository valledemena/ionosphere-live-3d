# DW.23 - V3a: partículas del río solar como quads instanciados

Estado: PRE-REGISTRO (sin codigo). Base `origin/master` =
`22b1643c12da38241212f4a010731331fa8edc8b`. Worktree
`ionosphere-dw23-cpp`, rama `dw.23-particles-tf` (nombre heredado;
el slice es solo V3a).

Decision de `@ARQUITECTO`: V3 se divide. **V3a** = quads instanciados
con flag OFF; **V3b** = transform feedback, pre-registro propio. No
mezclar, no tocar `sim_`.

## 1. Coste MEDIDO

Sonda temporal `PROBE-D23` con `swRiverVisible=true`, `camera.distance=20`,
400 particulas, `dt` de frame completo:
```
29,294 (arranque), 19,373, 20,028, 19,235, 19,612, 19,050 ms
```
Baseline steady (**flag=POINTS**, path actual): **19,0–20,0 ms/frame**.
Sonda restaurada (grep 0) y rebuild `--clean-first`.

## 2. Estado del código

- `SolarWindRiver.h/.cpp`: 400 `GL_POINTS`, `swriver::RiverSim sim_`
  en CPU, `upload()` hace `glBufferSubData` cada frame; dos pasadas
  aditivas (core 0,17 R_E / alpha 0,90; halo 0,38 / 0,16).
- Criterio de ARQUITECTO: **no tocar `sim_`** ni el path `GL_POINTS`
  cuando el flag esta OFF; mantener el factor **×60** declarado y la
  edad del dato (doctrina DW.11) en el HUD/tooltip.

## 3. Cambio propuesto (V3a)

- `bool instancedParticles = false` en `SolarWindRiver`, persistido en
  `settings.cfg` y con checkbox en UI, **default OFF**.
- Con flag OFF: codigo actual byte-identico (`GL_POINTS` + VBO
  pos/color, mismas dos pasadas y tamaños).
- Con flag ON: mismo VBO (stride 6 floats, divisor 1) y un shader de
  quads instanciados (`glDrawArraysInstanced(GL_TRIANGLE_STRIP,0,4,400)`),
  esquinas por `gl_VertexID`, billboard en espacio de vista con los
  mismos tamaños 0,17/0,38 R_E. Se conserva `upload()` tal cual:
  solo cambia la primitiva de dibujo, no la simulacion.

## 4. Criterio binario

1. **Build/tests**: `cmake --build` rc=0; `ctest` rc=0 con el total de
   la base (65); `test_solar_wind_river.cpp` sin diff.
2. **Coste** con la misma sonda, mismo binario, flag etiquetado
   `POINTS`/`INSTANCED`, camara/epoch/replay intactos:
   `INSTANCED <= POINTS + 2,0 ms` y techo absoluto 26 ms.
3. **Visual**: capturas antes/despues con el mismo epoch: puntos
   redondos suaves, mismos tamaños y colores, sin cuadrados duros;
   captura fallida se declara NO VERIFICADA (no se inventa).
4. **Alcance**: sin `sim_`, sin ×60, sin edad del dato, sin T-F, sin
   TEC/HF/lineas/temporal; OFF byte-identico.

## 5. No-goals

- No transform feedback (V3b, pre-registro aparte).
- No cambiar el numero de particulas ni la exageracion ×60.
- No tocar `test_solar_wind_river.cpp`.
- No merge sin veredicto del VERIFICADOR.

## 6. Evidencia

`git diff --numstat`, grep de `instancedParticles`, `ctest`, bloques
`[BENCH-D23] flag=POINTS/INSTANCED`, capturas, `handoff.py check`.
