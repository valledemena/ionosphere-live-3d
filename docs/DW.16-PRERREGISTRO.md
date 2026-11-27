# DW.16 - HF Circuit: ruta visible en gris cuando no hay datos frescos

Estado: PRE-REGISTRO (sin codigo). Base `origin/master` =
`504af0d278577be7eaa28583ef09a02a41a9a96e` (DW.15 mergeada y
desplegada). Worktree `ionosphere-dw16-cpp`, rama
`dw.16-hf-ruta-sin-datos`.

Decision de David: **no ocultar la ruta**. El usuario puede querer ver
el trayecto aunque no haya valores frescos que modifiquen sus
atributos. La ruta debe verse siempre, pero con un estilo gris
inequivoco, distinto de verde/ambar/rojo.

## 1. Estado actual (DW.15)

- `App.cpp:4132`: `if (impl->hfTraceVisible && impl->hfCircuitFresh)`
  -> ribbon **no se dibuja** sin dato fresco.
- `App.cpp:6121`: mismo gate para marcadores TX/RX y etiquetas.
- `App.cpp:6066`: panel muestra `HF circuit: sin datos <fuente> ...`
  y matriz `sin datos` (esto se conserva).
- `HFTraceLayer::Input` no tiene flag de "sin datos"; el color sale
  del fragment shader (`shaders/hftrace.frag` y fallback inline en
  `HFTraceLayer.cpp`) segun `vViable`: verde/ambar/rojo, con pulso
  viajero y neon (`u_time`, `pulse`, `spill`, `add`).

## 2. Cambio propuesto

### 2.1 Estilo "sin datos" (nuevo, testeable)

En `hfc` (`src/Ionosphere/HFTraceLayer.h`) anadir:

```cpp
inline glm::vec3 traceColorNoData() { return glm::vec3(0.55f, 0.58f, 0.62f); }
```

Gris neutro, suficientemente lejano de los tres colores de banda
(verde `0.10/0.95/0.55`, ambar `0.98/0.72/0.10`, rojo
`0.95/0.16/0.16`). Tests: diferencia por canal > 0.3 frente a cada
color de banda; channels casi iguales entre si (neutral).

### 2.2 Capa GL

- `HFTraceLayer::Input` anade `bool noData = false`.
- `HFTraceLayer` guarda `noData_` y en `render()` envia dos uniforms
  nuevos: `u_noData` (float 0/1) y `u_noDataColor` (vec3 desde
  `hfc::traceColorNoData()`), en **ambos** shaders:
  `shaders/hftrace.frag` y el fallback inline `kTraceFragSrc`.
- Fragment shader: si `u_noData > 0.5`:
  - `col = u_noDataColor`;
  - `pulse = 0.0`, `add = 0`, `spill = 0` (sin pulso ni neon: el
    trayecto es estatico y explicitamente inerte);
  - alfa del tubo `0.65 * prof` en vez de `0.9 * prof` (aspecto
    "fantasma").
  Si `u_noData <= 0.5`, **byte-identico** al shader actual.

### 2.3 App.cpp

- `hfIn.noData = !impl->hfCircuitFresh` al construir el Input.
- `App.cpp:4132`: gate passa a `if (impl->hfTraceVisible)` (la capa
  siempre se dibuja si el usuario la activo; el estilo gris lo decide
  el shader).
- `App.cpp:6121`: gate pasa a `if (impl->hfTraceVisible && hfR2.valid)`
  y los colores de marcadores TX/RX se eligen grises cuando
  `!hfCircuitFresh` (mismo `hfc::traceColorNoData()` convertido a
  `ImVec4`). Las etiquetas "TX"/"RX" siguen visibles.
- Panel y matriz: **sin cambios** (texto `sin datos <fuente>`).
- Tooltip del checkbox: anadir "sin dato: la ruta se muestra en gris,
  sin pulso; atributos no calculados".

## 3. Criterio binario (falsable)

1. **Unitarios**: `traceColorNoData()` neutral (max-min <= 0.10; el
   gris propuesto da 0.07) y **distinto por distancia euclidea** de
   los 3 colores de banda (distancia > 0.5: verde ~0.59, ambar ~0.69,
   rojo ~0.74). La comparacion "diferencia por canal > 0.3" es falsa
   (azul gris 0.62 vs verde 0.55 = 0.07); se usa euclidea, no por
   canal. `ctest` rc=0 con el total de la base + tests.
2. **Build**: `cmake --build` rc=0; diff de shaders y fallback
   sincronizados (mismo `u_noData`/`u_noDataColor`).
3. **Runtime** (sonda de replay con failover kc2g, reutilizando el
   metodo de DW.15):
   - sin dato fresco: ribbon **visible en gris**, sin pulso (dos
     capturas separadas 2 s con el mismo aspecto), marcadores grises,
     panel `sin datos kc2g ...`. **El cursor del replay debe estar
     congelado** (`replayPlaying=false` en la sonda) en esas dos
     capturas, para que el aspecto identico no dependa del epoch;
     se declara el epoch en cada captura;
   - con dato fresco: mismos colores y pulso que DW.15 (captura de
     control);
   - capturas con `PrintWindow` del propio proceso, con timestamp.
4. **Alcance**: diff no toca frescura (`circuitHasFreshData*`),
   `hfc::` de fisica (MUF/FOT/LUF/hopViable/legAltitudeKm), TEC,
   GIRO/kc2g ni DW.14. Solo estilo visual de la ruta.

## 4. No-goals

- No cambiar el texto del panel ni la matriz `sin datos`.
- No fabricar atributos: el gris significa explicitamente "ruta
  geometrica, sin dato fresco".
- No ocultar la ruta bajo ningun concepto.
- No reabrir DW.15 ni cambiar su logica de fuente activa.
- No merge sin veredicto.

## 5. Evidencia a entregar

- `git diff --numstat`, head exacto, grep de uniforms nuevos.
- Salida de `ctest` (total y rc) y `cmake --build` rc.
- Capturas `PrintWindow` con/sin dato, con timestamp y SHA del exe
  usado en la corrida.
- Comandos exactos y resultados; `handoff.py check` limpio.

## 6. Riesgos

- Un gris demasiado oscuro podria confundirse con sombreado; se elige
  gris claro neutro (0.55/0.58/0.62) y alfa 0.65, y se valida en
  captura.
- El pulso desactivado podria parecer "congelado" (es exactamente el
  mensaje deseado: inerte sin dato).
- Los shaders en disco y el fallback inline deben cambiar juntos; se
  verifica con grep de los dos uniforms en ambos ficheros.

## Notas del veredicto v1 (incorporadas, commit adicional sin amend)

1. Distancia euclidea (no por canal) en el test del gris; ver criterio 1.
2. `HFTraceLayer::update` (`HFTraceLayer.cpp:135-150`) tiene un
   early-return por firma que NO incluye `noData`. Se asigna
   `noData_ = in.noData;` **antes** de ese early-return (o se anade a
   la firma), si no el paso de "sin dato" a "con dato" puede dejar el
   trazo gris.
3. Las dos capturas separadas 2 s congelan el epoch del cursor;
   ver criterio 3.
4. El gate de marcadores vuelve a `hfR2.valid` (`App.cpp:6121`). En
   failover kc2g el caso probado en DW.15 tenia `result.valid=true`
   con panel MUF/FOT; se declara y verifica en la corrida, si no los
   marcadores no aparecerian.
5. Alcance del grep: se **permite** el simbolo nuevo
   `traceColorNoData` dentro de `hfc` (es color de estilo, no fisica)
   y se **prohibe** cualquier cambio en `mufForHopMHz`, `fotOfMHz`,
   `hopViable`, `legAltitudeKm` y `bandState`.
