# DW.15 - HF Circuit: estado honesto cuando GIRO no alimenta el epoch

Estado: PRE-REGISTRO (sin codigo). Base `origin/master` =
`dbbb4a4725c7de76d1050e72a97f66cc06cfec30`. Worktree
`ionosphere-dw15-cpp`, rama `dw.15-hf-circuit-sin-datos`.

Decision de David: opcion 1 (estado explicito "sin datos" / borde). No se
alimenta el circuito desde IRTAM ni desde TEC en este slice.

Veredicto del pre-registro v1: **CONFORME CON NOTAS CONDICIONANTES**
(`VEREDICTO_PRERREGISTRO.md`). Esta v2 incorpora las cuatro condiciones:
(1) `samplerBusy` se checkea explicitamente en `circuitHasFreshData`
porque `layerDataTime` no lo aplica a vars 1-7; (2) texto alternativo
cuando `gapSec <= 0`; (3) `staleGapText` se usa verbatim admitiendo
`+N min` y `+A.B h`; (4) el gate de frescura se anade al ribbon,
marcadores y etiquetas (`App.cpp:6047+`) sin tocar `hfTraceVisible`.
Respuestas del verificador: ocultar el ribbon es aceptable, el texto
basta con (2) y (3), y `layerDataTime(1,...)` sirve sin helper para hmF2.

## 1. Problema medido / leido (no inferido)

En replay con TEC animando 72 h, el panel `Circuit` y el ribbon de hops
cambian de color y altura **solo en las ultimas ~24 h**. Causa leida:

- `App.cpp:2200`: en replay `gsAct = impl->giroReplayCache`, construida
  con `GiroAdapter::sampleHistoryAt(replayEpoch)` (`App.cpp:2152`).
- `GiroAdapter.cpp:478-483`: `sampleHistoryAt` **clampea** a
  `h.front()` / `h.back()` fuera del rango historico. No hay senal de
  "sin dato" en el resultado.
- `GiroNetStatus::histHours` (`GiroAdapter.cpp:441-442`) = alcance hacia
  atras del historial, limitado por la peor estacion (~24 h medidos).
- `App.cpp:2338-2340`: los grids que alimentan el circuito son
  `giroInterpGrid(gsAct, 0, ...)` = foF2, `giroInterpGrid(gsAct, 2, ...)`
  = hmF2 y `radioInterpGrid(..., hfEpoch, ...)` = LUF (M7). **TEC no es
  entrada del circuito.**
- `HFTraceLayer.cpp:185-191`: `MUF = min(MUF por salto)`, `FOT = 0.85·MUF`.
  `:222-223`: color por salto = `hopViable(frec, MUF_salto)` (verde/ambar/rojo).
  `:229-240`: altura = `legAltitudeKm(clamp(hmF2,140,420), t, exag, off)`.
  `HFCircuitResult.valid` sigue `true` con muestras clampeadas.
- `App.cpp:6016-6046`: el panel muestra MUF/FOT/LUF/h' y la matriz de
  bandas sin indicar que el epoch esta fuera del historial GIRO; solo
  anade `replay: circuit follows the epoch`.

Ya existe la maquinaria honesta, sin usar aqui: `layerDataTime()`
(`IrtamState.cpp:208-226`) devuelve `stale`, `clamped` y `gapSec` para
vars 1-7 con `LayerRanges` reales; `staleSuffix()` y `staleGapText()`
formatean `(stale, +X h)` y estan testados. `App.cpp:6371-6373` ya llena
`lr.giroOldest = nowUtcR - gs.histHours*3600`.

## 2. Cambio propuesto (minimo)

### 2.1 Estado fresco del circuito (nuevo, puro)

En `src/Data/IrtamState.*` anadir un helper puro y testeable, por ejemplo:

```cpp
// Fresco = hay dato GIRO real en el epoch del cursor (no clampeado).
// Reutiliza layerDataTime(1, ...) + r.giroOldest/giroNewest.
bool circuitHasFreshData(double epoch, double nowUtc,
                         const LayerRanges& r, bool samplerBusy);
```

Semantica (condicion 1 del veredicto): **primero**
`if (samplerBusy) return false;` explicitamente. `layerDataTime` solo
aplica `samplerBusy` a las vars 8-11 (IRTAM), **no** a 1-7 (GIRO), asi
que no basta `ldt.stale`. Despues: `false` si
`layerDataTime(1,...).dataEpoch < 0` o `.stale` (epoch fuera de
`[giroOldest, giroNewest]`); `true` en el interior. Sin red, sin reloj,
sin GL.

### 2.2 Cableado en `App.cpp`

- Calcular el estado con el **mismo `hfEpoch`** usado para los grids
  (`App.cpp:2324`) y las `LayerRanges` ya disponibles (hoist o recalculo
  de `gs.histHours`; no duplicar logica de muestreo).
- Guardar `impl->hfCircuitFresh` (bool) y `impl->hfCircuitGapSec`.
- Si `hfCircuitFresh == false`:
  - panel: **no mostrar MUF/FOT/LUF/h' como frescos**; mostrar
    `HF circuit: sin datos GIRO en este epoch (hist X.X h; +Y)`, donde
    `+Y` es `staleGapText(gapSec)` **verbatim** (condicion 3: puede ser
    `+N min` si gap < 3600 s o `+A.B h` si gap >= 3600 s; no forzar
    horas). Condicion 2: con epoch < `giroOldest`, `stale=true` pero
    `gapSec=0` y `staleGapText` devuelve vacio; en ese caso mostrar
    `HF circuit: sin datos GIRO en este epoch (antes del historial; hist X.X h)`.
    Si `histHours<=0`: `HF circuit: sin datos GIRO (historial vacio)`;
  - matriz de bandas: mostrar `sin datos` en vez de `open/marginal/closed`;
  - ribbon, **marcadores TX/RX y etiquetas**: **no dibujar** (mismo
    criterio P5: sin dato → sin capa). El gate existente esta en
    `App.cpp:6047+` bajo `hfTraceVisible && hfR2.valid`; anadir
    `hfCircuitFresh` a esa condicion y a marcadores/etiquetas, **sin
    tocar** `hfTraceVisible` (condicion 4).
- Si `hfCircuitFresh == true`: comportamiento **byte-identico** al actual
  (mismos numeros, colores y alturas).

Nota: `HFCircuitResult` y `hfc::` (fisica, paleta, umbrales) **no se
tocan**. Solo se decide si se presenta su resultado como dato fresco.

### 2.3 UI / textos

- Textos exactos a validar (ASCII): con gap > 0,
  `HF circuit: sin datos GIRO en este epoch (hist X.X h; +Y)`, donde
  `+Y` = `staleGapText(gapSec)` sin transformar (`+N min` o `+A.B h`).
  Con `gapSec <= 0` (epoch anterior al inicio del historial):
  `HF circuit: sin datos GIRO en este epoch (antes del historial; hist X.X h)`.
  Sin historial (`histHours<=0`): `HF circuit: sin datos GIRO (historial vacio)`.
- Tooltip del checkbox `HF trace layer` anade una frase: el trazo solo se
  dibuja con dato GIRO fresco en el cursor.

## 3. Criterio binario (falsable)

PASS solo si:

1. **Unitarios**: nuevos checks para `circuitHasFreshData`: borde exacto
   `oldest`/`newest` = fresco; 1 s fuera = stale; sin historial = false;
   worker ocupado = false; gapSec coherente con `staleGapText`. `ctest`
   con el mismo total que la base (+tests nuevos) y rc=0.
2. **Build**: `cmake --build` rc=0.
3. **Runtime con la app en replay**: con cursor **dentro** de las 24 h,
   panel con MUF/FOT/h' y ribbon verde/ambar/rojo (sin cambios); con
   cursor **fuera** (p. ej. ~40 h, donde TEC si tiene frames), panel con
   `sin datos GIRO ...` y **ribbon, marcadores y etiquetas ausentes**;
   ningun MUF/FOT/h' presentado como fresco. Evidencia: capturas con el
   epoch del cursor y grep de los simbolos nuevos.
4. **Alcance**: `git diff` no toca `hfc::` (MUF/FOT/LUF formulas, colores,
   `hopViable`, `legAltitudeKm`, `kPulseRateHz`), no toca TEC ni GIRO
   (polling/historial/sampleHistoryAt), no reabre DW.13.

Si cualquiera falla: NO conforme, sin merge.

## 4. No-goals (explicitos)

- No alimentar el circuito desde IRTAM retrospectivo (queda como opcion
  futura, con pre-registro propio y etiqueta "estimado IRTAM").
- No derivar MUF desde TEC: TEC no da foF2/hmF2 en puntos de reflexion;
  seria fisica inventada.
- No extender el historial GIRO ni subir su frecuencia de sondeo.
- No cambiar el clamp de `sampleHistoryAt` (es honesto en si mismo; el
  defecto es que el panel lo presenta como fresco).
- No cambiar la ventana de replay, `zoneForAge` ni DW.14.
- No merge sin veredicto + GO.

## 5. Evidencia a entregar en la ejecucion

- `git diff --numstat` y head exacto; grep pre/post de
  `circuitHasFreshData` y `hfCircuitFresh`.
- Salida de `ctest` (total y rc) y `cmake --build` rc.
- Capturas: cursor dentro y fuera del historial, con el texto del panel
  y el estado del ribbon.
- Comandos exactos y resultados; entrada en `HANDOFF_LOG.md` y
  `handoff.py check` limpio.

## 6. Riesgos declarados

- Ocultar el ribbon puede parecer "perder la capa" al usuario; se
se mitiga con el mensaje explicito y el tooltip.
- `histHours` es la peor estacion: puede ser conservador incluso cuando
  algunas estaciones tienen mas historia; se documenta como tal.
- El panel `Circuit` puede estar cerrado: el estado se calcula igual
  para el ribbon si `hfTraceVisible` esta activo.

## 7. Gate

- PRE-REGISTRO a veredicto del VERIFICADOR. Sin GO de codigo ni merge.
- Una sola ronda de notas; las Sugerencias se incorporan antes de ejecutar.
