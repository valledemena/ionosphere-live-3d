# DW.11 PRERREGISTRO - C++: honestidad de la fila GloTEC y del tiempo del dato

Materia: corregir la publicacion del estado del proveedor GloTEC y el
etiquetado temporal del frame TEC, en el alcance consensuado con el
auditor (actas `0492`, `0493`, `0494`) y aprobado por David. **Solo
pre-registro: sin codigo tocado** (los dos ficheros de produccion
siguen byte-identicos a `master`).

## 0. Gobernanza y origen del alcance

- Sintoma reportado por David: en "Provider status", GloTEC aparecia
  `off` en arranque con cache valida en disco, y mas tarde `ok` con
  razon `backfill: 3 fallos de transporte` sirviendo una grilla de
  hacia ~61 dias. El replay animaba el sol pero no la capa TEC.
- Investigacion y dictamenes: actas `0491` (alcance minimo A+B y E2),
  `0492` (WAF, E5, E6, sub-caso ii), `0493` (WAF retirado como causa
  universal; E6: binario no reproducible), `0494` (R1..R4, alcance
  final). Archivos en `from-glm/` del relay (`fab9757`, `5f2e095`,
  `eae18c6`).
- Alcance final consensuado (acta `0494` R4): **A + B + C + D + E'**.
  La ventana de replay de 168 h queda **fuera** de este slice (slice
  propio, con pre-registro aparte). El hilo de precarga que se detiene
  a los 180 s sin reintento sigue como deuda declarada (era el antiguo
  "C" de `0491`; aqui se renombra **D2** para no colisionar con la C
  de `dataEpoch`).
- David dio **GO** el 2026-11-26 para traer este pre-registro y para
  sustituir el binario obsoleto de su maquina. Cadena: GLM audita ->
  David GO de ejecucion -> OpenCode ejecuta -> GLM audita ejecucion ->
  David GO de merge.
- **E5(iii) retirada** por el acta `0494` R3 y por bytes: el
  cruzamiento completo de los 88 ficheros `cache/tec_*.bin` (13 de
  hoy, 75 de septiembre, 13+75 hashes de contenido distintos,
  interseccion **0**) refuta el re-estampado persistente. Las
  consecuencias que se mantienen (HUD que miente, dedup de 240 s,
  desajuste de replay) dependen de la sobreescritura **en memoria** y
  son las que cubre D.
- **E6**: el binario en uso (`4b1ada55`) no se reproduce desde ningun
  punto publico de la historia; el rebuild limpio desde `cd65a7b` da
  `2a4568b2...` (316 pasos). La sustitucion del binario es despliegue,
  no cambio de codigo, y va con este turno.

## 1. Change

- Baseline: `master = cd65a7b7fb96776ace58162097c4aadd49c104c0`
  (merge del PR #22). Worktree limpio separado:
  `C:\Users\unknown\Desktop\ionosphere-dw11-cpp`.
- Rama: `dw.11-glotec-honestidad`, creada desde la baseline. Head del
  pre-registro: el commit de este documento.
- Ficheros de produccion tocados: **2** (`src/App.cpp`,
  `src/Data/GloTecAdapter.cpp`). Ficheros de test: **0 nuevos**; se
  reutilizan `provider_status` y `glo_backfill` como guardas.
- Posiciones verificadas sobre `cd65a7b` (no heredadas): App.cpp
  3165-3233 (bloque de publicacion del snapshot), 3223
  (`st.dataEpoch = epoch`), 3215-3216 (`b->grid = g` +
  `b->grid.timestamp = epoch`), 3440 (`pushHistoryFrame`), 3575-3618
  (`restoreTecCache`), 3649-3676 (`pushHistoryFrame`), 5065-5100
  (HUD), `src/Utils/Exporter.cpp:73`; GloTecAdapter.cpp 228-265
  (`fetchIndex`), 292-304 (transporte, `lastHttp_`), 337-351 (mapeo de
  `error_`), 354-384 (`fetchTEC`).

## 2. Alcance exacto (5 correcciones)

### A. Publicar la fila `PROV_TEC` al restaurar cache fresca

`restoreTecCache` instala el bundle `fromCache` (3606) pero no publica
snapshot; la fila nace `Off` y solo cambia tras el primer pase del
worker. Se anade, dentro del `if (isFresh(...))` y tras 3606 (9 lineas
nuevas, 0 retiradas):

```cpp
        provstatus::Snapshot st;   // DW.11-A: fila GloTEC desde cache fresca
        st.id = provstatus::PROV_TEC;
        st.hasData = true;
        st.dataEpoch = newest;
        char sdb[32];
        std::snprintf(sdb, sizeof(sdb), "%dx%d", b->grid.width, b->grid.height);
        st.data = sdb;
        st.error = "cache";
        { std::lock_guard<std::mutex> lk(impl->bundleMutex); impl->provBoard[provstatus::PROV_TEC] = st; }
```

`<cstdio>` ya esta incluido (App.cpp:19) y `b->grid.*` esta disponible
en ese punto. La rama de cache **no fresca** no publica: ahi no hay
dato actual que anunciar; el primer pase del worker publica `ok` o
`failed` con su razon. Se declara como no-goal (no hay ventana
silenciosa: el worker corre en segundos).

### B. Indice HTTP 200 con `[]` deja de ser exito silencioso

`fetchIndex` termina en `return !out.empty()` (265): un 200 con array
vacio devuelve `false` **sin fijar `error_`**, y el log dice "Index
OK: 0 frames". Consecuencia: la fila cae a `off` sin motivo y la
guarda `"indice vacio"` de `fetchTEC:359` es inalcanzable. Se
sustituye el final de `fetchIndex` (264-265) por (+5/-1):

```cpp
    if (out.empty()) {   // DW.11-B: HTTP 200 con [] deja de ser exito silencioso
        error_ = "indice vacio (0 frames)";
        fprintf(stderr, "[GloTecAdapter] Index empty: HTTP 200, 0 frames\n");
        return false;
    }
    printf("[GloTecAdapter] Index OK: %d frames\n", (int)out.size());
    return true;
```

La guarda de `fetchTEC:359` se conserva como defensa (ya inalcanzable
por construccion); no se toca.

### C. `dataEpoch` refleja la edad del dato, no la hora del fetch

En el bloque de publicacion del worker, `st.dataEpoch = epoch`
(App.cpp:3223) aplasta la edad real: un frame de septiembre servido
desde disco se clasifica `Ok` porque `classify` (ProviderStatus.cpp:
94-98) compara contra un `dataEpoch` = ahora y el umbral TEC es 1200 s.
Se cambia esa unica linea por:

```cpp
                    st.dataEpoch = g.timestamp;   // DW.11-C: edad del dato, no del fetch
```

(`g` conserva el timestamp real: 3216 modifica `b->grid`, no `g`; y
con D ya no se modifica ninguno). Resultado mecanico con el detector
existente: la fila pasa a `stale 72x72 | 61 d | <razon>` sin maquina
de estados nueva. El caso cubre tambien el `skip` de backfill en curso
(3196), que republica en el siguiente pase.

### D. No sobrescribir el timestamp del frame TEC

`b->grid.timestamp = epoch` (App.cpp:3216) reetiqueta el frame con la
hora del fetch. Se **retira** esa linea (0 nuevas, -1). El bundle
conserva el epoch real que ya trae `g.timestamp` (vivo: `f.timeUtc` del
indice; servido de disco: `nf.epoch`; mock ESA: `epoch`, ya explicito
en 3202). Consumidores enumerados y revisados (ver seccion 3).

### E'. Los runs con respuestas no-200 no son "limpios"

`glotbf::run` clasifica un HTTP 202 (desafio WAF) o cualquier no-200
distinto de 404/429/5xx en `++httpErrors` y **no** para (GloTecBackfill.
cpp:140). Al no haber flag de stop, `backfillFrom` limpia `error_` en
el `else` final (GloTecAdapter.cpp:349) y la fila queda `ok` **sin
razon** aunque no se haya guardado nada. Se sustituye esa linea por
(+6/-1):

```cpp
    else if (res.httpErrors > 0) {   // DW.11-E': 202/no-200 no es un run limpio
        char eb[64];
        std::snprintf(eb, sizeof(eb), "backfill: %d respuestas no-200 (ultima %ld)",
                      res.httpErrors, lastHttp_);
        error_ = eb;
    } else error_.clear();
```

`lastHttp_` ya existe (fetchURL:129-130) y el ultimo codigo visto es el
de la ultima peticion del run. Un 202 aislado entre frames buenos deja
razon visible sin degradar el estado por si mismo (la clasificacion la
decide `dataEpoch` con C).

## 3. Blast radius de D (enumeracion exigida)

Consumidores de `b->grid.timestamp` / `tec->grid.timestamp` (grep
exhaustivo sobre `src/`, 4 sitios):

1. **HUD** (App.cpp:5070, 5089-5099): pasa a mostrar la hora real del
   frame y una edad `+Ns` real. Es el objetivo: hoy imprime la hora del
   fetch y `+0s` sobre datos viejos.
2. **Persistencia/historia** (3440 -> `pushHistoryFrame`): el dedup de
   240 s (3652) y el `saveFrame` (3660-3666) usan el epoch real. Efecto:
   el mismo frame de disco ya no se reinserta cada pase como si fuera
   nuevo, y no se escribe con epoch actual.
3. **Exportador** (`src/Utils/Exporter.cpp:73`, `tov`): exporta el
   epoch real del frame. Sin cambio de firma.
4. **Replay** (2084-2086, `blendGrids(fa,fb,f)` via `tecHist`): los
   pares se construyen de `tecHist`, cuyos timestamps ya eran reales
   (3586 restauracion y 3655 `fr.timestamp = ts`). D no cambia el
   replay en este slice; el desajuste de ventana de 168 h es el slice
   aparte.

No se localizan otros consumidores. La rama de cache no fresca
(3602 `b->grid.timestamp = newest`) ya era correcta y no se toca.

## 4. No-goals (declarados)

- **Ventana de replay 168 h** (`kIrtamReplayWindowSec = 604800`,
  2008-2009 y 2046): slice propio. Hoy, con solo 2 frames dentro de la
  ventana, `pos` se clava en el extremo y el blend es estatico; el sol
  si avanza. Mitigacion sin codigo: desmarcar "Full 168 h window"
  (`replayLayerLoop`).
- **D2**: el hilo de precarga se detiene a los 180 s sin reintento
  (deuda de `0491`).
- **Tiempo de espera de 15 s vs frames de ~2,5 MB** (observacion F del
  acta `0493`): no fue la causa del sintoma de David; no se toca.
- **WAF**: fenomeno vantage-dependiente (acta `0493`); no se toca la
  app por ello. E' solo hace visible el no-200 si ocurre.
- **Cache no fresca al arranque**: la fila no se publica hasta el
  primer pase (ver A).
- E5(iii) **no** se verifica: retirada por no medible (acta `0494`).

## 5. Criterio binario

1. `cmake --build build --parallel 4` rc=0.
2. `ctest --test-dir build --output-on-failure` rc=0 con **62/62**
   (`ctest -N` = 62) en local, fixtures ebro copiados fuera del commit.
   Anclaje dual (acta `0463`): CI **61 passed + 1 skipped**
   (`ebro_parse`), dos jobs.
3. Estatico (grep sobre `src/` tras el cambio): 0 ocurrencias de
   `b->grid.timestamp = epoch`; 0 de `st.dataEpoch = epoch;`;
   exactamente 1 de `indice vacio (0 frames)`; exactamente 1 de
   `respuestas no-200 (ultima`.
4. `App.cpp` y `GloTecAdapter.cpp` cambian **solo** en los cinco puntos
   anteriores: `git diff --stat` de produccion espera
   `App.cpp +10/-2` y `src/Data/GloTecAdapter.cpp +11/-2` (neto
   **+21/-4**). El acumulado real se mide en la ejecucion y se audita
   contra el plan, no contra un literal pre-errata (leccion `0482`
   N2 / `0484` E2).
5. Runtime (con el binario de `cd65a7b` reconstruido, despliegue de
   este turno): en una sesion con red correcta, la fila GloTEC muestra
   `ok` con edad **distinta de 0** (tipicamente 150-600 s = ahora menos
   `time_tag` del frame) y el HUD `TEC: 72x72 GloTEC HH:MM (+Ns)` con
   `Ns` > 0; si el backfill falla y se sirve el frame de septiembre, la
   fila dice `stale 72x72 | <dias> | <razon>` y no `ok | 0s`.
   Evidencia: log + captura del panel y del HUD, con hora de captura.
6. `provider_status` test #7 (TEC 1300 s > 1200 -> stale) queda como
   guarda del detector usado por C; `glo_backfill` sigue verde (E' no
   altera `glotbf::run`).

## 6. Anclajes re-medidos en la baseline (worktree limpio)

- `git rev-parse HEAD` = `cd65a7b7fb96776ace58162097c4aadd49c104c0`;
  `git status` limpio antes de copiar fixtures (gitignored).
- Configure rc=0; `cmake --build build --parallel 4` rc=0 (**316
  pasos**, binario 4 192 404 B).
- `ctest --test-dir build --output-on-failure` rc=0, **62/62** en
  34,66 s; `ctest -N` = 62.
- Binario de referencia de la baseline (build limpio previo en el repo
  raiz, `build-master-cd65a7b`): sha256
  `2a4568b278ba7617caa8b0007b4c33186bd6a5726cbaf8d2eead75f3e8c258db`.

## 7. Evidencia y entrega (en la ejecucion, no antes)

- Ferry `to-glm/files/DW.11/`: `01-numstat.txt`, `02-head.txt`,
  `03-grep-post.txt` (los cuatro greps del criterio 3),
  `04-build.txt`, `05-ctest.txt`, `06-status.txt`, `07-runtime.log`,
  `08-captura-*.png`, `MANIFEST.md` y `SHA256SUMS` (hashes sobre bytes
  post-normalizacion, leccion `0479`).
- Bundle delta `cd65a7b..<head del change>` con sha256 publicado y
  `git bundle verify`; entregado en `upload/` y en el relay con la
  ejecucion (leccion `0484` E4: no se declara entregado lo que no esta).
- PR a `master` con este checklist; rama conservada; **sin amend ni
  force-push** (regla `0455`).

## 8. Gate

1. GLM emite acta de conformidad sobre el head de este pre-registro.
2. David da GO para ejecutar el slice.
3. OpenCode aplica A/B/C/D/E', mide anclajes, genera bundle y ferry,
   y hace la recaptura runtime con el binario de `cd65a7b`.
4. GLM emite acta de conformidad de ejecucion.
5. David da GO para el PR y el merge a `master` (protegido) con CI 2/2.

Sin codigo tocado antes de 1 y 2. El despliegue del binario
(`4b1ada55` -> `2a4568b2`) es independiente del slice y se ejecuta con
el GO ya dado, dejando copia de seguridad del anterior.
