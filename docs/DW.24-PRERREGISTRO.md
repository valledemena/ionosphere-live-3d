# DW.24 — V3b: simulación del río en GPU por transform feedback

Estado: PRE-REGISTRO (sin código de la feature). Base `origin/master` =
`e1679f52838be66ab4b823b029bb62a506ff423f` (DW.23/V3a mergeada).
Worktree `ionosphere-dw24-cpp`, rama `dw.24-transform-feedback`.

Decisión de `@ARQUITECTO`: V3b merece su propio pre-registro y bench
ping-pong. V3a queda cerrada; este slice NO la modifica.

## 1. Coste MEDIDO (baseline)

Sonda temporal `PROBE-D24`, río visible, `camera.distance=38`, 400
partículas, `dt` de frame completo, mismo binario, flag etiquetado:
```
POINTS    113,329 (arranque), 19,558 ms steady
INSTANCED 19,754 ms
```
Baseline steady (ambas variantes V3a): **19,5–19,8 ms/frame**.
Sonda restaurada (grep 0) y rebuild `--clean-first` rc=0.

## 2. Estado del código (anclas)

- `SolarWindRiverSim.h`: `RiverSim::step()` en CPU (drift `x -= dist`,
  obstáculo Shue con `obstaclePush`, reciclado `x < -28` con mulberry32),
  `positions()`, `setParticle()` para tests. **No se toca** (misma regla
  que V3a; test `test_solar_wind_river.cpp` pina estos números).
- `SolarWindRiver.cpp::upload()`: rota `rot_ * pos` a mundo, interlea
  pos(3)+color(3) en `verts_[]` y hace `glBufferSubData` cada frame.
- `render()`: dos pasadas (core 0,17/0,90; halo 0,38/0,16).

## 3. Cambio propuesto (V3b)

- `bool tfSimulation = false` en `SolarWindRiver`, persistido y con UI,
  **default OFF**. Flag independiente y compatible con `instancedParticles`.
- Con flag OFF: **byte-idéntico** al path actual (`sim_.step` + `upload`).
- Con flag ON: dos buffers de posiciones (ping-pong) en marco SOLAR y
  un programa de transform feedback que en GPU aplica el mismo
  `x -= dist`, el mismo empuje Shue (coeficientes 10,22 / 1,29 / 0,184 /
  8,14 / 6,6 / 0,58 / -0,007 / 0,024 / 0,05 / 0,98) y el reciclado.
  `glBeginTransformFeedback(GL_POINTS)` / `glEnd`, con
  `GL_RASTERIZER_DISCARD`; se alternan buffers de entrada/salida.
  Las posiciones se rotan a mundo en el vertex shader de render
  (`uniform mat3 u_rot`) para no mezclar marcos en el ping-pong.
  Los colores siguen calculándose en CPU cuando cambia el viento y se
  suben a un buffer aparte solo en ese caso.
- **Declaración de no-identicidad**: el PRNG del recycle en GPU es un
  hash GLSL, no mulberry32; NO se afirma igualdad bit a bit con la
  simulación CPU. Se conservan el rango de nacimiento
  `x en (-25,18]`, `(y,z)` de radio `<=12`, el factor **×60**, el
  reciclado en `x < -28`, el clearance 0,98 y la edad del dato (DW.11).

## 4. Criterio binario (falsable)

1. **Build/tests**: `cmake --build` rc=0; `ctest` rc=0 con los 65 de la
   base; `tests/test_solar_wind_river.cpp` y `SolarWindRiverSim.h` sin diff.
2. **OFF byte-idéntico**: con flag OFF, grep muestra `sim_.step` y
   `upload()` originales y sin `glBeginTransformFeedback` en el path activo.
3. **Coste** con la misma sonda y el mismo binario, flag etiquetado
   `CPU`/`TF`, cámara/epoch/replay intactos: `TF <= CPU + 2,0 ms` y
   techo absoluto 26 ms. Peor TF menos mejor CPU, reportado tal cual.
4. **Falsabilidad mecánica**: grep de `glBeginTransformFeedback`,
   `glTransformFeedbackVaryings`, `vPos`, `glBindBufferBase` y dos buffers
   ping-pong; verificar que el shader replica los coeficientes Shue y
   `kExaggeration=60`.
5. **Visual**: PARCIALMENTE NO VERIFICADO si no hay captura válida; no
   afirmar igualdad visual no medida (lección DW.23).

## 5. No-goals

- No tocar `SolarWindRiverSim.h` ni su test.
- No cambiar ×60, ni la edad del dato, ni colores/alfa/tamaños.
- No transform feedback en el path OFF; no merge sin veredicto.

## 6. Evidencia

`git diff --numstat`, grep de símbolos TF, `ctest`, bloques
`[BENCH-D24] flag=CPU/TF`, capturas (o salvedad), `handoff.py check`.
