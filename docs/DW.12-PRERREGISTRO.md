# DW.12 PRERREGISTRO - C++: GloTEC transporte absoluto (backfill URL relativa -> CURLE_URL_MALFORMAT)

Materia: corregir el fallo determinista de transporte que impide que
`GloTecAdapter::backfillFrom` descargue frames GloTEC nuevos. Causa
raiz aceptada por el auditor (acta `0499`, `N3`; `P4 rev-0499`, `N4`;
slice `DW.12 glotec-transporte-absoluto`, `N6`). **Solo pre-registro:
sin codigo tocado.** Baseline `master = ea185d7` (`origin/master`),
rama `dw.12-glotec-transporte-absoluto`, worktree limpio
`ionosphere-dw12-cpp`. DW.11 esta cerrada; esto es slice nuevo.

## 0. Gobernanza y origen del alcance

- Fuente: acta `0499` `N3` (mecanismo H-transport aceptado) + `N4`
  (P4 rev-0499) + `N6` (endoso + requisitos (i)-(vi)). `0497-VERIF`/
  `0498-VERIF` y `0499` (transcripcion non-canonical; canonico
  pendiente) sin conflicto con este contenido.
- GO de David; GLM audita este pre-registro (esperado acta `0500`).
  **Sin codigo antes de acta + GO de ejecucion.** No GO de codigo por
  GLM.
- Protocolo sin cambio: pre-registro -> acta GLM -> GO de ejecucion ->
  ejecucion -> acta -> GO de merge. Sin amend/force-push.
- Anclaje dual: local `ctest` y CI (61+1 + ThreadSanitizer), como DW.11.
  Anclajes de la baseline se re-miden en la ejecucion, no heredados.

## 1. Sintoma y causa (verificado, no heredado)

Sintomas de produccion (David): `PROV_TEC` en **`stale`** y **replay
no anima la capa TEC**. Causa verificada + reproducida:

- `GloTecAdapter::fetchTEC` (`src/Data/GloTecAdapter.cpp`, master
  `ea185d7`, 395 lineas) usa `backfillFrom`, **no** `fetchFrame`.
- `backfillFrom` `pure` guarda **`f.url` RELATIVO** (l.291;
  `GloTecFrame.url` = `/products/glotec/geojson_2d_urt/glotec_icao_*.geojson`
  del indice). 
- `transport` (l.297-304): **l.300 `fetchURL(url, body);`** sin
  prefijar la base. `transportOk = (lastCurl_ == 0)`.
- El prefijo de base **solo** esta en `fetchFrame`
  (l.274 `"https://services.swpc.noaa.gov" + url`); y `fetchFrame`
  **no tiene callers** en master.
- libcurl rechaza URL sin host: **`CURLE_URL_MALFORMAT` (3)**. 
  Reproducido con curl (UA `IonosphereLive3D/1.0`, `--max-time 15`):
  relativa `curl(3) No host part` http=000; absoluta
  `https://services.swpc.noaa.gov` + path http=200 ~2,5 MB.
- `consecTransport >= opt.stopAfter404` -> `stoppedOnTransport` ->
  `error_ = "backfill: N fallos de transporte"`; `store` nunca
  llamado; `backfillFrom=false`; `fetchTEC` cae a
  `teccache::loadCached().frames.back()` (newest real 2026-11-26
  12:34Z); `pushHistoryFrame` re-guarda epoch vieja; dedup por
  timestamp identico; **ningun frame nuevo**. `dataEpoch` viejo >
  1200 s -> `stale` (DW.11-C honesto); `tecHist` sin frames nuevos +
  ventana -> replay estatico (P4).
- Indice SI funciona: `GLOTEC_INDEX_URL` absoluta l.12 ->
  `https://services.swpc.noaa.gov/products/glotec/geojson_2d_urt.json`,
  http=200, 4465, `time_tag max = hoy`. Red NOAA sana; no WAF; no
  429/404/5xx; no `CURLOPT_TIMEOUT` en esta reproduccion (timeout 15 s
  = obs F marginal separada).

## 2. Cambio (mecanismo unico, menor blast radius)

**Opcion ENDOSADA (N6-1):** construir la URL absoluta dentro del
lambda `transport`, sin tocar `fetchURL`, `glotbf::run`, `fetchFrame`,
`fetchIndex`, `pushHistoryFrame`, cache, replay, DW.11, ni ningun otro
consumidor.

Base: `https://services.swpc.noaa.gov` (mismo literal que l.274).

### 2.1 Antes (master `ea185d7`, `src/Data/GloTecAdapter.cpp:297-304`)

```cpp
    auto transport = [this](const std::string& url) {
        glotbf::Response r;
        std::string body;
        fetchURL(url, body);              // l.300  <-- URL relativa rechazada
        r.http = lastHttp_;
        r.transportOk = (lastCurl_ == 0);
        r.retryAfterSec = lastRetryAfter_;
        r.body = std::move(body);
```

### 2.2 Despues (unico cambio, +3/-1 neto en la linea l.300)

```cpp
    auto transport = [this](const std::string& url) {
        glotbf::Response r;
        std::string body;
        // DW.12: absolutizar URL relativa; mismo prefijo que fetchFrame.
        std::string full = (!url.empty() && url[0] == '/')
                           ? "https://services.swpc.noaa.gov" + url : url;
        fetchURL(full, body);
        r.http = lastHttp_;
        r.transportOk = (lastCurl_ == 0);
        r.retryAfterSec = lastRetryAfter_;
        r.body = std::move(body);
```

(Conserva el guard `!url.empty() && url[0]=='/'`; absolutas intactas.)

### 2.3 Opciones rechazadas (N6)

- Pasar URLs absolutas a `glotbf::run` (cambio de libreria; fuera).
- Conectar `fetchFrame` (cambio de flujo; sin callers; fuera).
- `CURLOPT_TIMEOUT`, E-2, cache, replay: fuera de este slice.

## 3. Blast radius (enumeracion exigida, N6-ii)

- Unico fichero: `src/Data/GloTecAdapter.cpp`, unica linea l.300.
- `transport` solo se usa por `glotbf::run` dentro de `backfillFrom`
  (l.~330). Llamadores de `backfillFrom`: `fetchTEC`; `fetchTEC`
  llamadores: `App.cpp:3189` (`glo.fetchTEC(epoch,3)`),
  `DataManager::fetchTECAsync` -> App TEC; `backfillFrom` tambien usado
  por la precarga de App. Sin cambio de firma.
- `fetchURL` (l.108-139) **no se modifica**; `CURLOPT_TIMEOUT 15`,
  `CURLOPT_FOLLOWLOCATION`, UA intactos; `transportOk` logica intacta;
  `fetchFrame`/`fetchIndex`/`pushHistoryFrame` intactos.

## 4. Auditoria grep (N6-iii): URLs relativas a `fetchURL` - LISTAR, no arreglar

`git grep -n "fetchURL(" origin/master -- src` (GloTecAdapter::fetchURL
+ otros `::fetchURL` por clase; `DiasAdapter`, `IrtamCoeffAdapter`,
`SdoImageAdapter`, etc. son `fetchURL` propios distintos):

- **GloTecAdapter.cpp:228** `fetchURL(GLOTEC_INDEX_URL, ...)` absoluta.
- **GloTecAdapter.cpp:276** `fetchURL(full, ...)` `full` prefijada.
- **GloTecAdapter.cpp:300** `fetchURL(url, body);` **RELATIVA <- el defecto de DW.12.**
- **DiasAdapter.cpp:250** `fetchURL(DIAS_PAGER_URL + ..., js)` absoluta.
- **IrtamCoeffAdapter.cpp:148** `urlFor(...)` absoluta `https://lgdc.uml.edu/...`.
- **SdoImageAdapter.cpp:72** `urlFor(...)` absoluta `https://sdo.gsfc.nasa.gov/...`.
- **Aurora/GIRO/Kc2g/RadioProp/SolarIndices/SolarWind/Storm/Xray**:
  constantes `*_URL`/`kpUrl()`/... absolutas; `fetchURL` distinta por clase.
- No hay `fetchURL("/...` literal en `origin/master`.
- **Auditoria concluye: solo l.300 pasa una URL relativa.** Otros
  candidatos a verificar post-merge **se LISTAN, no se arreglan** aqui.

## 5. Verificacion de replay (N4 / N6-vi; P4 rev-0499)

`kIrtamReplayWindowSec = 604800` (168 h, `src/Data/IrtamState.h:39`).
Call sites verificados: `App.cpp:2009` (`winStart`, pares TEC en
`tecHist`) y `App.cpp:6321` (`sloB`, ventana replay). 
**Nota de auditoria `0499` N4 cerrada:** `kReplayWindowSec = 86400`
(`IrtamState.h:28`, comentario "kReplayWindowSec (86400, TEC) queda
intacta" l.38) **NO tiene call sites**; el replay TEC real usa
`kIrtamReplayWindowSec`. H-window **no se toca** en DW.12.

- H-transport primero; H-window **solo despues** (`P4 rev-0499`).
- Replay test **solo valido tras ingesta demostrada de frames frescos**
  (`tecHist` crece): con frames frescos entrando, si replay sigue
  estatico -> **H-window promovida** y pre-registrada aparte.
- **No usar mtimes** como evidencia de fetch: `restoreTecCache` ->
  `pushHistoryFrame` -> `teccache::saveFrame` re-saves refrescan mtimes
  con epochs viejas. Usar **epoch/sha de contenido** (metodo 0526).

## 6. No-goals (N6-iv)

- `CURLOPT_TIMEOUT=15 s` vs frames ~2,5 MB (**obs F** de `0493`):
  **observar si aparece `CURLE 28` tras el fix**, no cambiar aqui.
- **E-2** no vinculante (honradez de logs `B`): separado.
- **Ventana replay 168 h / kReplayWindowSec 86400 / H-window**: separado.
- **D2** (hilo de precarga 180 s sin reintento): separado.
- `glotbf::run`, `fetchFrame`, `fetchURL`, cache, replay, DW.11,
  persistencia, export, HUD: no se tocan. 
- **`fetchFrame` sin callers**: no se conecta en este slice.
- No-goals habituales de la serie; sin trabajo extra.

## 7. Criterio binario (N6-v + habituales)

1. `cmake --build build --parallel 4` rc=0.
2. `ctest --test-dir build --output-on-failure` rc=0; `ctest -N`=62
   local 62/62 (fixtures); **CI 61+1 + ThreadSanitizer**.
3. `git diff --numstat master -- src` esperado **`src/Data/GloTecAdapter.cpp`
   +3/-1** (+2 neto; solo l.300). `git diff --name-only master` = 1
   fichero + este doc. **0 codigo fuera del punto.**
4. Greps post: `fetchURL(full, body);` en l.300; 0 `fetchURL(url, body);`
   sin prefijo; `https://services.swpc.noaa.gov` presente; greps DW.11
   0/0/1/1 intactos; `kIrtamReplayWindowSec` sin cambio.
5. **Runtime post-fix (binario de este arbol):** primer ciclo sin
   `fallos de transporte`; **>=1 frame nuevo en cache con epoch del
   dia** (`tec_*.bin` epoch, no mtime); `PROV_TEC` `ok` edad <1200 s;
   `Index OK` + no `3 fallos de transporte`; `tecHist` crece; HUD
   honesto (DW.11). Observar `CURLE 28` (timeout F) post-fix.
6. **P4 rev test:** sesion de replay con frames frescos -> anima =>
   **P4 cerrado**; estatica => H-window promovida (no en este slice).
7. Evidencia como DW.11 (numstat, greps, build, ctest, CI, runtime
   log, sha256, bundle `git bundle verify`, sin amend).

## 8. Anclajes a re-medir en ejecucion (no heredados)

- Build limpio; ctest local; CI; `ctest -N`. 
- Step 0 ya verificado (read-only): solo l.300 relativa; `fetchFrame`
  sin callers; `kIrtamReplayWindowSec` sites; `kReplayWindowSec`
  unused. Re-verificar tras el cambio.
- Binario no determinista: sha256 = una construccion; identidad =
  arbol + numstat + tests.

## 9. Gate

1. GLM acta de conformidad sobre este pre-registro (esperado `0500`).
2. David GO de ejecucion.
3. OpenCode aplica **unico** cambio l.300 + mide + bundle + relay.
4. GLM acta de ejecucion.
5. David GO merge (CI 2/2, blobs, greps, cierre).

Sin codigo antes de 1+2.
