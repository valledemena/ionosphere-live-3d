# DW.11 EJECUCION - mediciones (no acta)

Registro de lo **ejecutado y medido** para el slice DW.11, con el GO de
David y bajo el dictamen `0495` del auditor (conformidad del
pre-registro + errata E-1 vinculante + E-2 no vinculante). Las actas
son del auditor; este documento solo lleva numeros, comandos y rutas.

Baseline: `master = cd65a7b7fb96776ace58162097c4aadd49c104c0`.
Rama: `dw.11-glotec-honestidad`. Worktree:
`C:\Users\unknown\Desktop\ionosphere-dw11-cpp`.

## 1. Cadena de commits (sin amend, sin force-push)

| commit | contenido |
|---|---|
| `a2a0c68` | pre-registro `docs/DW.11-PRERREGISTRO.md` |
| `b0d3a5b` | codigo A/B/C/D/E' (2 ficheros de produccion) |
| `db0800b` | errata de ejecucion (contabilidad, E-1, enlace no determinista) |
| head final | este documento |

## 2. Diff real contra el plan

`git diff --numstat master -- src`:

```
10      2       src/App.cpp
12      2       src/Data/GloTecAdapter.cpp
```

- Plan del pre-registro: `App.cpp +10/-2`, `GloTecAdapter.cpp +11/-2`
  (neto +21/-4).
- Real: `App.cpp +10/-2`, `GloTecAdapter.cpp +12/-2` (neto **+22/-4**).
- Desvio **+1/-0 localizado y explicado**: el pre-registro conto el
  `return true` de B como linea no modificada; es una sustitucion
  (`return !out.empty()` -> `return true`), luego B es +6/-1 y no +5/-1.
  Ninguna linea de codigo cambia por la errata: es contabilidad. Ver
  `docs/DW.11-PRERREGISTRO.md` seccion 9.

## 3. Criterio binario

| # | criterio | resultado medido |
|---|---|---|
| 1 | build rc=0 | rc=0 (5/5 objetos, enlace OK) |
| 2 | ctest 62/62 local | `100% tests passed out of 62`, rc=0, 23,68 s |
| 3 | greps post 0/0/1/1 | 0 `b->grid.timestamp = epoch`; 0 `st.dataEpoch = epoch;`; 1 `indice vacio (0 frames)`; 1 `respuestas no-200 (ultima` |
| 4 | diff vs plan | +22/-4 (desvio +1 documentado) |
| 5 | runtime honesto | ver seccion 4 |
| 6 | guardas | `provider_status` y `glo_backfill` verdes dentro de los 62 |
| 7 | CI 2/2 | **pendiente**: requiere el PR (gate 5, GO de David) |

`ctest -N` = 62. `git status` del worktree limpio tras los commits.

## 4. Runtime (binario construido de este arbol, en su destino real)

Instalado en el destino del acceso directo
(`ionosphere-dw10-cpp\build\ionosphere-live-3d.exe`), con `WorkingDir`
= repositorio. Backups conservados: `*.bak-4b1ada55` (binario opaco
previo) y `*.bak-2a4568b2` (master limpio).

Arranque (log `13`/`15`): `TEC cache: 116 restored, newest 12:34 UTC
(pruned 0, dropped 0)`, `Index OK: 4465 frames` x2, **sin**
`Index empty` en estas corridas.

### 4.1 E-1 (errata vinculante del acta `0495`): la fila no nace OFF

Sonda de instrumentacion sobre la **misma ruta** (no commiteada; el
arbol entregado no la contiene), impresa antes de restaurar y en el
primer pase del worker:

```
[PROBE-A] t=12:30:22 hasData=1 data=72x72 err=cache      dataEpoch=12:28:25 age=117
[PROBE-A] t=12:30:25 hasData=1 data=72x72 err=backfill: 3 fallos de transporte age=120
```

La primera linea prueba A: con cache fresca y **antes** del primer
`Index OK`, `PROV_TEC` ya tiene `hasData=1`, `data=72x72`,
`error="cache"` y edad real (117 s). No hay ventana `OFF`.

### 4.2 C + D: el HUD y la fila dicen la edad del dato

Cadena exacta que el HUD dibuja (mismo `snprintf` que App.cpp:5106),
con la fila clasificada por el detector existente:

```
[PROBE-STATE] HUD: TEC: 72x72 GloTEC 12:34 (+1104s) | fila: ok    72x72 age=1104 err=cache
[PROBE-STATE] HUD: TEC: 72x72 GloTEC 12:34 (+1184s) | fila: ok    72x72 age=1184 err=backfill: 3 fallos de transporte
[PROBE-STATE] HUD: TEC: 72x72 GloTEC 12:34 (+1204s) | fila: stale 72x72 age=1204 err=backfill: 3 fallos de transporte
[PROBE-STATE] HUD: TEC: 72x72 GloTEC 12:34 (+1244s) | fila: stale 72x72 age=1244 err=backfill: 3 fallos de transporte
```

- D: el HUD muestra `12:34` (hora real del frame, la del cache) y
  `+Ns` creciente, no `+0s` con hora de captura.
- C: la fila clasifica con `dataEpoch` real; cruza a `stale`
  exactamente al superar `kStaleSec[PROV_TEC] = 1200`, sin maquina de
  estados nueva.
- Razon visible del backfill (`3 fallos de transporte`) mientras el
  estado sigue `ok`: la degradacion no se disfraza de exito limpio.

## 5. Enlace no determinista (declarado, no es del slice)

Seis enlaces consecutivos del **mismo arbol** dan sha256 distintos:
`2f860a71`, `4e87b5d1`, `2cb83e30`, `d0f420a7`, `e5712b4c`,
`794d809b`. Por tanto el sha256 de un `.exe` identifica UNA
construccion y no sirve de ancla reproducible; la identidad del slice
es arbol + numstat + tests. El binario desplegado en esta corrida es
`794d809b` (build del arbol entregado), no un valor normativo.

## 6. No verificado en esta ejecucion (declarado)

1. **CI 2/2**: no hay PR todavia (gate 5: GO de David para PR y merge).
2. **P2 del acta `0495`** (indice 200 con `[]`): no ocurrio en estas
   corridas; la guarda esta en el codigo y su grep da 1, pero la
   firma en vivo no se reprodujo hoy. Queda como prediccion abierta.
3. **E-2** no adoptada, con razon de bytes (`docs/DW.11-PRERREGISTRO.md`
   seccion 9.2): `out.empty()` solo es alcanzable tras `CURLE_OK` +
   HTTP 200 exacto, y no hay `error_` previo que pisar en esa ruta.
4. **Ventana de replay 168 h** y **D2** (hilo de precarga 180 s):
   fuera de alcance, con pre-registro propio.

## 7. Observacion sobre el cache (para el auditor)

`TEC cache: 116 restored` (75 de septiembre + 41 de noviembre; el
`newest` es 12:34). El recuento anterior era 81/88 porque la sesion
que corria el binario **previo** (arrancada 13:22, terminada en este
turno) siguio descargando frames con el re-estampado antiguo. Con D en
tierra, los frames que guarde este arbol llevan su epoch real.
