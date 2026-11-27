# DW.17 - TEC: quitar el selector de profundidad y pedir siempre el maximo

Estado: PRE-REGISTRO (sin codigo). Base `origin/master` =
`6cadad24f895b159cfc0781266b87e507eb060aa`. Worktree
`ionosphere-dw17-cpp`, rama `dw.17-tec-depth-max`.

Decision de David: "el selector no lo necesitamos, siempre deberia
reclamar el maximo que ofrezca el proveedor (72h o concretamente 69,6
reales en este caso)".

## 1. Estado actual

- `App.cpp:564`: `std::atomic<int> tecDepthSlots{36}` (6 h por defecto).
- `App.cpp:3090-3092`: carga de `settings.cfg` acepta 36/144/432.
- `App.cpp:2948`: guarda `tecDepthSlots`.
- `App.cpp:6376-6385`: `ImGui::Combo("##depth", ...)` con `6h/24h/72h`;
  al cambiar guarda el atomico y `depthCv.notify_all()`.
- `App.cpp:6389`: HelpMarker "Select how far back to download...".
- `App.cpp:1215` y `:1276`: `depth = clamp(tecDepthSlots, 36, 432)` en
  precarga inicial y backfill periodico.
- `glotbf::kDepth72h = 432` en `src/Data/GloTecBackfill.h`;
  `kCapFrames = 432` en `TecCache.h`.

Medido: con `tecDepthSlots 432`, cache `n=432`, span real **69,6 h**
(5 huecos de 10-13 min). El proveedor ofrece 744 h de indice, pero el
horizonte de la app es 72 h (432 frames a 10 min).

## 2. Cambio propuesto

- Fijar la profundidad a **`glotbf::kDepth72h` (432)** en los dos
  puntos de uso (`:1215`, `:1276`): `const int depth = glotbf::kDepth72h;`
  (o dejar el atomico pero inicializado a 432 y **no editable**).
- **Eliminar** el Combo y sus `depthLabels`/`newSlots`/`notify_all`
  (`:6376-6385`).
- En la carga de settings (`:3090-3092`), **ignorar** cualquier valor
  antiguo 36/144 y dejar 432; en el guardado (`:2948`) escribir 432.
- HelpMarker (`:6389`): texto nuevo, p. ej. "GloTEC history: always the
  maximum 72 h (432 frames) supported by the app. Frames on disk are
  kept up to 72 h."

No se toca el timeline: en modo capa sigue siendo
`[tecHist.front(), tecHist.back()]`; en union, 168 h fijos. El rango
real de 69,6 h sigue viniendo de los frames descargados y sus huecos.

## 3. Criterio binario

1. **Grep**: 0 coincidencias de `depthLabels`, `##depth` y Combo de
   profundidad en `src/App.cpp`; `kDepth72h` presente en los dos usos.
2. **Settings**: con un `settings.cfg` que contenga `tecDepthSlots 36`
   o `144`, tras arrancar y cerrar, queda `tecDepthSlots 432`.
3. **Build/tests**: `cmake --build` rc=0; `ctest` rc=0 con el mismo
   total que la base.
4. **Runtime**: stdout de la app con
   `[App] History preload: ... (... planned, depth 432, ...)` y
   backfill periodico `depth 432`; captura del panel Timeline sin el
   selector "History depth".

## 4. No-goals

- No ampliar el horizonte mas alla de 72 h (mil frames > `kCapFrames`).
- No cambiar `kCapFrames`, `kWindowSec`, `zoneForAge`, DW.14/DW.15/DW.16.
- No borrar frames de la cache ni alterar el timeline.
- No merge sin veredicto.

## 5. Evidencia

`git diff --numstat`, grep pre/post, salida de `ctest`, stdout de la
corrida con `depth 432`, captura del panel, `handoff.py check` limpio.

## 6. Riesgos

- Usuarios con `tecDepthSlots 36` en settings: se ignora (es lo pedido).
- El periodicwait usaba el atomico como "cambio" para despertar; al
  dejarlo constante el ciclo horario sigue activo por su plazo de 1 h
  (`PeriodicWait.h`), sin depender del notify del Combo.
