# DW.22 - V1 (slice propio): acumulación temporal sin confundir ghosting con dato stale

Estado: PRE-REGISTRO (sin codigo). Base `origin/master` =
`97066b15e39a05f2fa8d26a9e86fbff7110d845f`. Worktree
`ionosphere-dw22-cpp`, rama `dw.22-temporal-accum`.

Decision de `@ARQUITECTO`: V1 quedo como slice propio tras V2; el jitter
IGN ya existente es la entrada, no se duplica.

## 1. Estado medido

- `VolumeRenderer.cpp:78` ya usa IGN espacial determinista
  `fract(52.9829189*fract(dot(gl_FragCoord.xy, vec2(0.06711056,
  0.00583715))))`, con espejo CPU `Utils/VolumeJitter.h` y
  `tests/test_volume_jitter.cpp` con pines. **No se tocan**.
- `Framebuffer.h:90` usa `GL_DEPTH24_STENCIL8` como **renderbuffer**
  (no textura); por eso no hay reproyeccion ni TAA hoy.
- Baseline V2 (`dw21_bench_*`): OFF steady 20,7–22,2 ms; con
  magnetosfera. Coste del estimado 3 de ARQUITECTO sigue sin medir.

## 2. Riesgo de honestidad (el motivo del slice separado)

Una acumulacion temporal sin reset puede dejar un ghosting que el
usuario confunda con **edad del dato** (doctrina DW.11: la edad siempre
visible y nunca un valor silencioso). Por eso:

- El buffer de historia se **resetea** cuando cambia `dataEpoch` de cualquier
  fuente publicada en `provBoard`, o cuando el cursor de replay salta
  (`|epoch - lastEpoch| > 30 s`, el mismo umbral que usa HF), o al
  cambiar de modo live/replay, o al redimensionar la ventana.
- `alpha` de acumulacion y si el reset ocurrio se muestran en el HUD:
  `TAA: ON alpha=0.90 reset=N` (o `TAA: OFF` con flag desactivado).
- El flag `temporalAccum` es **OFF por defecto** y se persiste en
  `settings.cfg`; OFF deja el pipeline actual byte-identico.

## 3. Cambio propuesto

- `Framebuffer.h`: anadir textura de profundidad opcional
  (`GL_DEPTH_COMPONENT24`) solo si `temporalAccum` esta activo; el
  renderbuffer queda para el path clasico. Dos FBO de historia RGBA16F
  ping-pong con el mismo tamano que `sceneFBO`.
- Shader de acumulacion (nuevo `shaders/temporal.vert/.frag` con
  fallback inline): mezcla `mix(current, historyReprojected, alpha)`
  con clamp vecinal por color para evitar ghosting; la reproyeccion
  usa view-projection anterior y la depth texture.
- `App.cpp`: resolver el composite actual hacia la historia, y el
  pase final desde la historia; `provBoard` y `replayEpoch` provocan
  reset; HUD con alpha y contador de resets.
- `temporalAccum` default OFF, save/load en settings, checkbox en UI.

## 4. Criterio binario

1. **Build/tests**: `cmake --build` rc=0; `ctest` rc=0 con el total de
   la base; `tests/test_volume_jitter.cpp` y `Utils/VolumeJitter.h`
   sin diff.
2. **Coste medido** con la misma sonda etiquetada: `flag=OFF` y
   `flag=ON`, mismo binario, camara/epoch/dataset/replay intactos,
   regimen tras warm-up; gate `ON <= OFF + 2,0 ms` y techo 26 ms.
3. **Reset falsable**: con ON, cambiar `dataEpoch` de una fuente
   (o saltar el replay) debe incrementar el contador de resets y el
   HUD debe mostrarlo; sin cambio de fuente, resets=0 en regimen.
4. **Visual**: captura antes/despues; sin banding nuevo en el volumen
   y sin rastro del frame anterior tras un reset.
5. **Alcance**: no tocar V2 (lineas/IGRF), TEC, HF, viento, Shue ni
   `test_volume_jitter.cpp`.

## 5. No-goals

- No duplicar el jitter IGN.
- No activar compute/SSBO (GLAD 4.0).
- No introducir historia sin reset por cambio de dato.
- No merge sin veredicto del VERIFICADOR.

## 6. Evidencia

`git diff --numstat`, grep de `temporalAccum`/resets, `ctest`, bloques
`[BENCH-TAA] flag=OFF/ON`, captura HUD con resets, `handoff.py check`.

## 7. Riesgos

- La depth texture puede romper el path MSAA: se mide OFF y ON con
  MSAA en los dos estados; si falla, se desactiva temporalAccum y se
  documenta.
- El limite de 2 ms puede ser optimista; como en V2, se reporta el
  valor medido y se pide re-veredicto antes de merge.
