# DW.14 - Replay por capa: saltar huecos sin datos (no fabricar frames)

Estado: PRE-REGISTRO v2 - incorpora el veredicto `VEREDICTO_DW14.md`
(CONFORME CON NOTAS CONDICIONANTES). Sin codigo, sin commit, sin merge.
Pendiente: GO de David para ejecutar.

Base: `origin/master` = `86a2a34` (merge PR #24, DW.12). Worktree
`ionosphere-dw14-cpp`, rama `dw.14-tec-replay-gaps`, doc **untracked**.

## 0. Cambios respecto a v1 (por el veredicto)

1. `kLoopGapJoinSec` **1800 -> 3600 s** (medido: con 1800 el cluster de
   septiembre se parte en 72+3 por un hueco real de 2010 s, y habria DOS
   saltos por loop).
2. Se declara explicitamente que **union 168 h queda intacta** y que el
   modo por defecto es union (`replayLayerLoop=false`): DW.14 **no**
   promete arreglar la sensacion en el modo por defecto; solo actua al
   activar modo capa.
3. Criterio 2 cambia de "extracto de log" a **contador por loop**
   (`jumps`, `clusters`, `intervalsNoData`) + capturas representativas
   opcionales; se declara la velocidad del test.

## 1. Problema medido (no inferido)

El usuario reporta que "antes el TEC animaba a lo largo de casi todo el
timeline" y ahora no. Medido (read-only):

- `cache/` del workspace raiz, app en ejecucion: `n=320` frames, span
  `1762.0 h`, **dos clusters** con `join=3600`: `2026-09-15 05:55 ->
  18:33` (75 frames, 12.6 h) y `2026-11-26 01:55 -> 2026-11-27 15:55`
  (245 frames, 38.0 h). Salto unico entre ambos: **6 160 905 s
  (1711.4 h)**.
- Huecos > 600 s en la cache: **7**. El mayor intra-cluster es **2010 s
  (33.5 min)**, `09-15 17:45:00 -> 18:18:30` (parte 72+3 con umbral
  1800 s). Los otros cinco son 618-779 s y cualquier umbral >= 1800 s los
  absorbe. El septimo es el salto Sep->Nov.
- `settings.cfg`: `tecDepthSlots 144` (24 h pedidas; los frames viejos son
  heredados de disco).
- `App.cpp:404` `replayLayerLoop = false` (defecto = union 168 h).
- `App.cpp:2036-2046`: el cursor avanza
  `replayEpoch += advDt * replaySpeed * 1200.0` y hace wrap en
  `[loBound, hiBound]`. A 1x, 1200 s de timeline por segundo.
- `App.cpp:6321-6340`: union = `[nowUtc - 604800, nowUtc]` (168 h); en
  modo por capa, `layerLoopRange` (`IrtamState.cpp`) da para TEC
  `[tecHist.front(), tecHist.back()]` = los 1762 h medidos.
- Aritmetica: un loop de 168 h a 1x dura 604800/1200 = **504 s (8.4 min)**;
  un loop de 1762 h dura **5286 s (88 min)**. En ambos, la mayor parte del
  tiempo no hay TEC que pintar. Ahi esta la diferencia de sensacion, no en
  `kWindowSec` ni en `kCapFrames`.

Causa estatica del recuerdo del usuario (verificada con `git show`): antes
de `40a26ea` (2026-09-16) `replayPos` era un **indice sobre `tecHist`**
(`maxPos = size-1`, loop) e interpolaba frames consecutivos, asi que el
hueco Sep->Nov se saltaba en un paso y parecia continuo. Desde `40a26ea`
el muestreo es por epoch real (`muestreo pares al epoch`) + `zoneForAge`,
y los huecos se ven. `kCapFrames 72->432` (`56a41d7`) y
`kIrtamReplayWindowSec 96h->168h` (`40a26ea`) amplian cobertura; no la
recortan.

## 2. Cambio propuesto (minimo, sin datos nuevos)

Objetivo: que el replay **no se detenga en huecos sin datos**, sin
inventar frames ni tocar timestamps. El cursor salta del final de un
cluster al inicio del siguiente cuando el paso cae en un hueco mayor que
un umbral declarado.

### 2.1 Funciones puras nuevas en `src/Data/IrtamState.*`

```cpp
// 3600 s = 60 min. Justificado por el hueco real de 2010 s medido en la
// cache (Sep15 17:45 -> 18:18:30): con 1800 el cluster de septiembre se
// parte en 72+3 y habria dos saltos por loop. 3600 da 2 clusters y 1 salto.
inline constexpr double kLoopGapJoinSec = 3600.0;

struct LoopCluster { double first; double last; };

// Agrupa epochs ordenados asc; une saltos <= joinSec. Puro.
std::vector<LoopCluster> loopClusters(const std::vector<double>& epochsAsc,
                                      double joinSec);

// Avanza el cursor stepSec; si el tramo [epoch, epoch+step] cruza el final
// de un cluster y el siguiente empieza mas alla de joinSec, salta al inicio
// del siguiente cluster. Wrap a lo al pasar hi. Devuelve el epoch nuevo y,
// por puntero, los segundos saltados (0 si no hubo salto). Puro.
double advanceLoopEpoch(double epoch, double stepSec,
                        const std::vector<LoopCluster>& clusters,
                        double lo, double hi, double joinSec,
                        double* skippedSec);
```

Semantica obligatoria:

- Un solo cluster: comportamiento **identico** al actual (avance lineal y
  wrap en `hi`).
- Dos o mas clusters: ningun paso del cursor queda dentro de un hueco
  `> joinSec`; el salto va **exactamente** al `first` del cluster
  siguiente; nunca se salta un cluster entero.
- `skippedSec > 0` solo cuando hubo salto; es el hueco real saltado.
- Sin estado global; sin `std::time`; sin acceso a disco. Testeable en CI.

### 2.2 Call site (`App.cpp`, avance del replay)

- Solo cuando `replayLayerLoop == true` y la capa activa es TEC (0) o GIRO
  (1-7). Clusters desde los epochs reales: TEC = `tecHist` bajo
  `tecHistMutex` (solo timestamps); GIRO = historial existente
  (`historyStats`/muestras, segun lo ya disponible).
- Union 168 h (`replayLayerLoop == false`, **modo por defecto**): **sin
  cambios de comportamiento**, misma rama de codigo actual. Se declara que
  DW.14 no modifica la experiencia por defecto; en los ultimos 168 h no
  hay hueco > 779 s, asi que el salto no dispararia alli de todos modos.
- Contadores por loop (nuevos, en el log al hacer wrap):
  `[Replay] loop stats clusters=%d jumps=%d skippedSec=%.0f
  intervalsNoData=%d speed=%.1f`.
- Una linea por salto, con flush ya garantizado por `main.cpp` (`_IOLBF`):
  `[Replay] skip hueco %.1f h (cluster %s -> %s)`.
- Si no hay clusters validos, se mantiene el avance actual.

### 2.3 UI

- La etiqueta `Zone:`/`DATA` no cambia de semantica: sigue diciendo la
  verdad en el cursor. El salto no falsea ningun timestamp.
- Opcional (mismo slice, 1 linea): mostrar los segundos saltados en el
  tooltip del slider de replay. No obligatorio para el criterio.

## 3. Criterio binario (falsable)

PASS solo si se cumplen los cuatro:

1. **Unitarios**: `loopClusters` y `advanceLoopEpoch` con casos:
   single-cluster identidad; dos clusters separados 1711.4 h; salto exacto
   al `first` del siguiente; wrap en `hi`; frontera del umbral en
   `3599/3600/3601` s (incluye el hueco real de 2010 s dentro de un mismo
   cluster); `skippedSec` correcto; ningun paso dentro de hueco
   `> joinSec`; con la cache real (320 epochs) `loopClusters` devuelve
   **2 clusters de 75 y 245** y **1 salto** de 6 160 905 s.
2. **Runtime (contador, no extracto)**: con la cache de dos clusters y TEC
   en modo capa, un loop completo reporta en `[Replay] loop stats`:
   `clusters=2`, `jumps=1`, `intervalsNoData=0`. `intervalsNoData` cuenta
   los intervalos renderizados en los que la capa activa no tiene dato
   (misma condicion que pinta `DATA no data on layer`). Velocidad del
   test declarada: **8x** (loop de 1762 h = 660.75 s ~ 11 min); si el
   tiempo de CI/pantalla lo impide, se acepta un loop sintetico de dos
   clusters en unitarios + una captura runtime de inicio/salto/fin, y se
   declara cual de las dos vias se uso.
3. **Regresion**: `cmake --build` rc=0 y `ctest` con el mismo total que la
   base (`62/62` hoy); `grep -c` de los simbolos nuevos declarado;
   `numstat` del slice declarado antes de ejecutar.
4. **Alcance**: `git diff` del PR no toca `kWindowSec`, `kCapFrames`,
   `tecDepthSlots`, `zoneForAge`, `kIrtamReplayWindowSec`, timestamps ni
   rutas de descarga; union 168 h sin cambios de comportamiento (mismo
   codigo, sin log nuevo).

Si cualquiera falla: NO conforme, sin merge.

## 4. No-goals (explicitos)

- No subir `kWindowSec`/`kCapFrames` ni adoptar 168 h de descarga
  (1009 frames > 432; ~10.7 GB).
- No descargar historia gruesa de 744 h (~900 MB) ni mezclar cadencias en
  el ring de 432: canibalizaria la cobertura reciente. Queda como slice
  aparte solo si David lo pide.
- No fabricar frames ni re-estampar timestamps (eso es lo que hacia el
  playback por indice anterior a `40a26ea`; DW.11-D lo elimino).
- No reabrir DW.13 ni mergearlo.
- No tocar la extraccion NOAA ni el transporte (DW.12 cerrado).
- No cambiar el modo por defecto (union 168 h) ni prometer que DW.14
  arregla la sensacion en ese modo.

## 5. Evidencia a entregar en la ejecucion

- `git diff --numstat origin/master -- src docs` y head exacto.
- `grep -c` pre/post de `loopClusters`, `advanceLoopEpoch`,
  `kLoopGapJoinSec`.
- Lista de tests nuevos y salida de `ctest` (total y rc).
- Salida de `[Replay] loop stats` de un loop completo con la cache real
  (o la via sintetica declarada), con `clusters`, `jumps`,
  `intervalsNoData`, `skippedSec` y `speed`.
- Capturas opcionales en inicio / salto / fin.
- Medicion de cache (n, clusters, huecos > 600 s) del working dir usado.
- Entrada en `HANDOFF_LOG.md` y `handoff.py check` limpio.

## 6. Riesgos declarados

- Umbral `3600 s`: huecos reales de hasta 1 h se absorben (no se saltan);
  huecos mayores se saltan. El mayor hueco intra-cluster medido es 2010 s,
  asi que ningun dato real se oculta con este umbral en la cache actual.
- GIRO (1-7) puede no exponer una lista de epochs barata; si no la hay, el
  slice se limita a TEC (0) y se declara. No se inventa historial.
- El salto cambia la duracion percibida del loop; se documenta en el
  tooltip y en el acta.

## 7. Estado del gate

- Pre-registro v1 relayado; veredicto `VEREDICTO_DW14.md`: CONFORME CON
  NOTAS CONDICIONANTES (las tres incorporadas en v2).
- **Sin GO de codigo, commit ni merge.** David decide la ejecucion.
