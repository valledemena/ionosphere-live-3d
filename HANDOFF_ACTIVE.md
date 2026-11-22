# HANDOFF ACTIVE - ionosphere-live-3d / R4

Ultima actualizacion: 2026-11-21 (hora local). **R4.5, R4.6.2, R4.6.6, la
numeracion de R4.6, R4.6.7 (iso-bands) y R4.6.3 (density + chapman) estan
cerradas.** Los PR **#56, #57 y #58** estan **fusionados** con doble GO.
**R4.6.5: GATE VISUAL PASADO (acta `0436` FAVORABLE) y PR #58 mergeado**
como `234eab4` y **cerrada formalmente** por el acta `0437` de GLM.
**R4.6.5 CERRADA**. Ultimo turno: **OpenCode (DeepSeek V4.1 Flash)**.

Estado medido (no heredado):

- **Android `main` = `origin/main` =
  `234eab4f7af3b32da1af6e78e41707cc137d6be7`** (merge commit del PR #58,
  `2026-11-21T15:54:40Z`; padres `1646d56` + `f3079d9`; tree
  `d0f77ef6ed8e54150d1bd4a5471f30d2c644280f`). CI del push: run
  **`44921675310`** 3/3, job `unitTest` `136898863209` con
  `RESUMEN-TESTS ficheros=55 tests=356 fallos=0` a las `15:55:44Z`. Rama
  `fix/r4.6.5-volume-attribute` **conservada** en `f3079d9`.
- Historico (PR #57): `1646d56127f5ea81335d6eebc018385b537c4c3a` (merge commit del PR #57,
  `2026-11-20T23:03:46Z`; padres `3a44ac5...` + `9b801b4...`; tree
  **`6df04fb7048c0fdec9b8f476166694f19a5a18ee`** identico al tree del head:
  `git diff 1646d56 9b801b4` vacio; delta contra main anterior: 12 ficheros,
  +1439/-3). `git ls-remote`: rama
  `feat/r4.6.3-density-chapman` **conservada** en `9b801b4`.
- **CI del push a `main`**: run **`44833939080`** (push de `1646d56`,
  `2026-11-20T23:03:49Z`): completed success, **3/3** — `assembleDebug`
  (`136625491033`), `unitTest` (`136625490970`), `lint` (`136625490802`).
  Log del job `unitTest` `136625490970`:
  `RESUMEN-TESTS tarea=testDebugUnitTest ficheros=54 tests=353 fallos=0
  errores=0 omitidos=0` a las `23:04:58Z` + `BUILD SUCCESSFUL in 52s`.
- **Gate R4.6.3**: acta `0429` condicionada (head `f306730`, archivada en
  `f111b00`) -> fix R1 pushado (`9b801b4`) -> acta **`0430` FAVORABLE** de GLM
  (archivada en el relay `f41dfe5`, blob `03f7163a632e6daee07ff0601844654af17c921e`,
  **4 593 B / 85 l / CR=0 / ASCII**, sha256
  `48b5b60eda713daa670cb57f6fbecebc5eaac9b2082db279aa6f4117d572256d`;
  ferry `files/0430/` con 10 ficheros, `sha256sum -c` 8/8 OK, cmp exit 0) ->
  GO de David -> merge. DoD R1: `innerRadius = volumeInnerRadius(VOL_ALT_MIN_KM)
  = PLANET_R + 60/6371 = 1.00942`, espejo C++ `VolumeRenderer.cpp:256/257`
  (master real 256/257, confirmado por clon local GLM). Pines G7c_10/11/12.
  Errata de job id: `136531605890` pertenece al run anterior `44806450830`;
  jobs reales del head `9b801b4`:
  `136589457552`/`136589457838`/`136589458062`; re-poll del job corregido da
  `RESUMEN-TESTS ... ficheros=54 tests=353 fallos=0` a las `21:47:49Z`.
- Tree del merge = **`9aea9a892b14a16c6e4a998cd227ff438a16d6d8`**, **identico** al
  tree del head `866f196`: `git diff 3a44ac5 866f196` sale **vacio**. Delta
  contra `main` anterior: **5 ficheros, +86/-1**.
- **CI de `main`**: run **`44763826045`** (`push` de `3a44ac5`,
  `2026-11-20T15:29:47Z`): **3/3 success** (`assembleDebug` 15:29:50->15:31:24,
  `unitTest` 15:29:51->15:31:34, `lint` 15:29:51->15:32:05). El job `unitTest`
  (**`136383076401`**) publica a las `15:30:56Z` la linea
  `RESUMEN-TESTS tarea=testDebugUnitTest ficheros=51 tests=326 fallos=0 errores=0 omitidos=0`.
- Gate de merge registrado **en el PR antes de fusionar**:
  `https://github.com/valledemena/ionosphere-live-3d-android/pull/56#issuecomment-7043448129`
  (acta `0427` con blob/sha256/paquete + GO escrito de David + CI del head +
  forma del merge).
- Rama `feat/r4.6.7-iso-bands` **conservada** en `866f196` (`ls-remote` la
  sigue listando); sin borrar (hace falta GO).
- **Acta `from-glm/0427` archivada verbatim antes de mergear**, en el relay:
  commit **`827b72c`**, blob
  **`41d437e1988e5089f6390f735e2b9c7066ee1a7a`**, **12 297 B / 253 lineas /
  CR=0 / ASCII puro**, sha256
  `8b6eef7af4f14c5fccdd4742172de1e098f56318516a47634f6ade26b76a7490`. Paquete
  **218 427 B**, sha256
  `62e4d154f93bdb8cee755738768ffbf3e6dd05646ddc5b5adff3cdcba72b952e`, **19
  miembros**, `sha256sum -c` **17/17 OK**, `cmp` identico (exit 0); miembros bajo
  `from-glm/files/0427/`.
- **Acuse `to-glm/0435` publicado** (`721f364` + normalizacion ASCII en
  `d30f6d7`, blob `1e7b3d204e31dd8c9d0f1ad91a194baa5c13ec77`, **4 286 B / 86
  lineas / CR=0 / ASCII**): post-merge con merge commit, arbol, CI 3/3 con
  linea, tu 0427 archivada y rama conservada.
- **Acta `from-glm/0428` archivada verbatim** (acuse y **conformidad plena** de
  GLM; **R4.6.7 cerrada tambien por su lado**), en el relay: commit
  **`5dcd661`**, blob **`27995249035e95c9015a83c0d7cfdecce2ba3ec4`**,
  **6 543 B / 128 lineas / CR=0 / ASCII puro**, sha256
  `537e862ae68955b43a4cbefd9d6101d27b598021f4dc8972c7023ed14469db82`; paquete
  **241 370 B**, sha256
  `587a7b485f72e6671a0459bcecf953eadf8e8eb98b131c2ed04b2020b033fcb8`, **16
  miembros**, `sha256sum -c` **14/14 OK (0 fallos)**, `cmp` **identico (exit
  0)**; 16 miembros bajo `from-glm/files/0428/`.
- **Acuse de archivo `to-glm/0436` publicado** (`f3f33e9` + normalizacion ASCII
  en `8db6be4`, blob `8437243bb7ed18506f6d977416912ad501996ee8`, **3 776 B / 76
  lineas / CR=0 / ASCII**): contrato del 0428, contadores y siguiente hito.
- Relay `glmrelay`: `HEAD = origin/main = d6c6e80`, limpio. Arbol **1097**;
  `from-glm` nivel 1 **257** (recursivo **489**, `files/` **232**), `to-glm`
  nivel 1 **272** (recursivo **607**, `files/` **335**). Libres: `from-glm`
  **0435**, `to-glm` **0443**. Gate v3 relayado en
  `INTERCAMBIO/work/GLM-MESSAGE-20261121-143548.md` (2 697 B / 56 l /
  CR=0 / ASCII, sha256 `0f0b7ccefb4be9799b0e5b843a0072b4111c84ade7a1741d4fb1e393b1ab0dff`). Actas R4.6.5: `0432` (condicionada), `0433`
  (NO FAVORABLE / geometria nocturna) y `0434` (NO FAVORABLE definitivo /
  causa `aPosition`/`"aPos"`), con acuses `to-glm/0440`, `0441` y `0442`.
- Archivos de cierre de R4.6.3 en el relay: `from-glm/0429-...` (condicionada,
  `f111b00`), `from-glm/0430-dictamen-favorable-r463-pr57-9b801b4-r1-innerradius.md`
  (`f41dfe5`, blob `03f7163a632e6daee07ff0601844654af17c921e`),
  `from-glm/files/0430/` (10 ficheros), `to-glm/0437` (acuse de archivo del
  0429) y `to-glm/0438-ack-archivo-0430-r463-pr57-merge-1646d56.md`
  (`e1c4e20`, blob `91163471ccfe201151db7cd0dc1fea7f6714b0ce`,
  **4 422 B / 83 l / CR=0 / ASCII**, sha256
  `f244c6c2b761334e3cff67a70dcd6df1ffa614cca632c40a6b823303f3d8cdb2`).
- Relay a GLM por canal: `GLM_REVIEW.md` ->
  `INTERCAMBIO/work/GLM-MESSAGE-20261120-163450.md` (**5 300 B / 106 lineas /
  CR=0 / ASCII**, tras normalizar el CRLF que escribe `glm_notify.py`).

El historial detallado sigue en `HANDOFF_LOG.md` (append-only). Este fichero es
el estado vigente.

---

## Objetivo actual

**R4.6.3 (density + chapman) CERRADA**: PR #57 fusionado en `main` como merge
commit **`1646d56127f5ea81335d6eebc018385b537c4c3a`** (tree
`6df04fb7048c0fdec9b8f476166694f19a5a18ee` identico al head `9b801b4`), rama
`feat/r4.6.3-density-chapman` conservada, acta `0430` FAVORABLE de GLM
archivada, CI del push a `main` 3/3 con `RESUMEN-TESTS`. **R4.6.5: gate visual
EJECUTADO en Mi 10**, **v2 con hemisferio diurno** (`5e525a8`); v1 fue NO
 FAVORABLE por geometria nocturna (`sunShadow`), v2 NO FAVORABLE por causa
 `aPosition`/`aPos`. Corregido en PR #58 (`f3079d9`) con GO de David;
 **v3 ejecutada: PASS visual** (acta `0436` FAVORABLE) y **cerrada** por
 el acta `0437`; PR #58 mergeado en `234eab4`.

Siguiente bloque de trabajo:

1. **R4.7 / E7 CERRADA**: NO NDK + licencia N/A, dictamen `0438`, firma
   `to-glm/0446`, verificacion `0439`, ADR publicada en PR #59 y fusionada
   en `a09f57e`. No queda firma pendiente. Evidencia: actas 0430/0436/0437,
   banda 7.85%, steps_=64 en ambos. ADR **publicada**:
   destino `docs/r4/ADR-NDK-MSIS.md` del repo Android via PR docs-only,
   solo con GO. **Consulta relayada a GLM** en
   `INTERCAMBIO/work/GLM-MESSAGE-20261121-191655.md` (3 744 B / 77 l /
   CR=0 / ASCII / sha256 `fb9c7e42...`) pidiendo dictamen E7 condicionado
   a la decision de David (una sola ronda, sin PR ni repo Android tocado).
- **E7 DICTAMINADO FAVORABLE** (acta `0438`, relay `ebbdd05`): NO NDK,
  licencia N/A, **condicionado unicamente a la firma explicita de David**.
  Con esa firma el texto deviene acta E7 sin ratificacion. Consulta
  archivada como `to-glm/0445` (3 744 B / 77 l / CR=0 / NA=0, sha256
  `fb9c7e42...`; GLM vio 3 NA en el chat, el canon del relay es NA=0).
  Acta `0438`: 10 238 B / 200 l / CR=0 / NA=0 / sha256 `044a6634...`.
  Errata tipografica del emisor `HUEGO` -> `HUECO` corregida al archivar
  segun instruccion de GLM, registrada aqui.
- **Pendientes locales de GLM cerrados por mi lado**: (1) sha256 del acta
  0436 archivada = 10 559 B / `3fe00e1e...` (ya cerrado en `bdaf623`);
  (2) grep MSIS del arbol C++ `d9fb809` = **0 hits** en fuentes, fondo del
  modelo es `DensityVolume` -> `u_density` en `VolumeRenderer.cpp`; el pin
  web `8cdc334` tiene un port TS de NRLMSIS 2.1 con licencia
  academic/non-profit y GPL-3.0 propio, ajeno al puerto Android (refuerza
  el descarte). Sin cambio en el ADR.
- **Firma de David** «firmo» archivada como `to-glm/0446` (`ce0b617`,
  1 537 B / 30 l / NA=0 / sha256 `51f69c81...`): NO NDK + licencia N/A.
  Con acta `0438` + firma, **E7 queda cerrada** sin ratificacion.
- **PR #59 docs-only** `docs/r4.7-adr-ndk-msis` @ head
  `04f69c36301b04c8cf0e4fd16bd6c1ede7f9a7ee`, OPEN/MERGEABLE,
  **1 fichero +113/-0** (`docs/r4/ADR-NDK-MSIS.md`, 6 172 B / 113 l).
  CI run `44939864097` 3/3 success (`assembleDebug` `136954476609`,
  `unitTest` `136954476786` — `FROM-CACHE`, sin linea RESUMEN-TESTS nueva,
  batería 356/0 de `234eab4` —, `lint` `136954476814`).
- **Verificacion mecanica CONFORME** (acta `0439`, relay `4673424`,
  `from-glm/0439-verif-mecanica-adr-pr59-conforme.md`, 12 624 B / 243 l /
  CR=0 / NA=0 / sha256 `a7c178ce...`; el emisor declaro 12 622 B / 242 l,
  diferencia de transcripcion de 2 B / 1 l, contenido intacto). 5 puntos
  conformes, **GO de merge emitido por GLM** (con o sin enmienda). E7 sigue
  cerrada. Dos discrepancias de **estado** no bloqueantes en el ADR
  publicado: cabecera «PROPUESTA / no publicado» y 3 filas del DoD en
  «Pendiente»; GLM recomienda enmendar,.o fusionar tal cual.
- **Hallazgo del grep diferido**: el original tiene `src/lib/iono/msis/`
  (7 ficheros, NRLMSIS 2.1 TS, licencia academic/non-profit) pero **cero
  referencias fuera del directorio**; no cableado. Refuerza el descarte.
  Todos los pendientes heredados 0437/0438 saldados por GLM.
- **Enmienda APLICADA y merge hechos** con GO de David: commit `084d9e8`
  (+13/-11; cabecera ACEPTADA, DoD a Cumplido, «idéntico al oráculo»
  suavizado, atribución de `0438` corregida, NA=171 conservado), CI del
  head `44944702811` 3/3, **PR #59 MERGED** `a09f57edb2351d83d40aed83cd42365b9324e24c`
  (`20:11:32Z`), rama conservada en `084d9e8`, tree `ed5b0c17...`,
  CI main run `44944956902` 3/3. **R4.7 / E7 CERRADA**.
- **Acuse de cierre de GLM** (`from-glm/0440-acuse-cierre-r47-e7-pr59.md`,
  relay `d58fc65`, 6 424 B / 130 l / CR=0 / NA=0 / sha256 `9a65ff09...`):
  verifica merge `a09f57e`, enmienda `084d9e8`, ambos CI 3/3, diff total
  `234eab4..main` = solo el ADR +115/−0. **R4.7 / E7 CERRADA formalmente**.
  R4.6.5 intacta. GLM registró que su acta `0439` archivada difiere del
  texto emitido solo en un re-wrap (+2 B/+1 l, contenido idéntico).
- Relay `d58fc65`, arbol 1131, `from-glm` 263/495/232, `to-glm` 278/635/357.
  Libres: `from-glm` 0441, `to-glm` 0449.
- **R4.8 ABIERTA (pre-registro)**: `docs/R4.8-PRERREGISTRO.md` en repo raiz
  con Q3 (paridad replay vs pin `8cdc334`: solo `replaySpeed`/`replayLoop`
  sobreviven; `live/0/false` al restaurar; 100 % de casos, PASS binario) y
  Q5 (sesión 30 min REPLAY en Mi 10 desenchufado, %/h informado sin umbral,
  PASS si termina sin crash/ANR/throttling). Relayado a GLM en
  `to-glm/0449` (`b1a5217`, 3 190 B / 61 l / CR=0 / NA=6 por tres `§`
  no normalizados / sha256 `b8fbd91d...`). **Sin código tocado**.
- Relay `b1a5217`, arbol 1132, `from-glm` 263/495/232, `to-glm` 279/636/357.
  Libres: `from-glm` 0441, `to-glm` 0450.
- **Dictamen FAVORABLE** (acta `0441`, relay `0089ee0`, 11 386 B / 219 l /
  NA=0 / sha256 `df9c8653...`) con 4 fijos a-d y matriz M1-M18
  pre-registrada. Fijos registrados en el anexo A del pre-registro: matriz
  cerrada, alcance de «sin claves nuevas» (escritor Android =
  `r34-settings` con `pointSize`/`replaySpeed`/`replayLoop`), instrumentos
  de Q5 (batería `dumpsys battery`, temperatura, brillo, ahorro MIUI,
  timeout) y primera sesión a HIGH=64 con repetición solo por GO de David.
- **GO de David ejecutado**: auditoría Q3 sobre el head exacto `a09f57e`
  (worktree limpio `ionosphere-r48-audit`): `testDebugUnitTest` BUILD
  SUCCESSFUL, 55 ficheros / 356 tests / 0 fallos; `ReplayTimeTest` 22/0/0,
  `ReplayTickerTest` 11/0/0. Informe `work/r48-q3-audit.md`: **14 PASS
  directos + 4 N-A** (M2/M3/M9/M12: el puerto usa SharedPreferences
  tipadas, no JSON; equivalentes en M1/M5/M10/M11). **No se declara 18/18**:
  relayado a GLM en `to-glm/0450` (`c0fed0a`, 2 694 B / 53 l / NA=0 /
  sha256 `10018417...`) pidiendo que acepte N-A como PASS-equivalente o
  exija test de inyección de tipos (código nuevo, GO y acta).
- Relay `c0fed0a`, arbol 1134, `from-glm` 264/496/232, `to-glm` 280/637/357.
  Libres: `from-glm` 0442, `to-glm` 0451.
- **Q3 CERRADA**: GLM aceptó los 4 N-A como PASS-equivalente
  (`from-glm/0442-criterio-q3-na-aceptados.md`, relay `4523a76`,
  4 193 B / 83 l / NA=0 / sha256 `d512aa71...`). Registro obligatorio:
  **14 PASS directos + 4 N-A aceptados, nunca 18/18**; base evidencial
  M14 + M1/M5/M10/M11; M14 queda como guarda de regresión del escritor;
  el test de inyección de tipos queda como hardening opcional sin acta.
- **Q5 PREPARADA, NO EJECUTADA**: `docs/R4.8-Q5-PROTOCOLO.md` con
  instrumentos del fijo A.c (nivel `dumpsys battery`, temperatura,
  brillo fijado, ahorro MIUI, timeout, desenchufado), primera corrida a
  HIGH=64 (A.d), PASS binario verbatim del plan §7.2 (%/h informado sin
  umbral), evidencia y repetición solo con GO de David.
- Relay `4523a76`, arbol 1135, `from-glm` 265/497/232, `to-glm` 280/637/357.
  Libres: `from-glm` 0443, `to-glm` 0451.
- **Q5 EJECUTADA: PASS**. 7 muestras cada 5 min, 1838,4 s (30,6 min),
  batería **56 % → 46 % (20,0 %/h informado, sin umbral)**, reloj replay
  **00:57 → 01:28 UTC** (+31 min, seguía avanzando), temperatura
  28,2 → 31,8 °C (+3,6, sin apagado térmico), **0 FATAL / 0 ANR**,
  desenchufado y `Awake` en todas las muestras. 25 `QueueBuffer time out`
  aislados (47–63 ms) y 6 `Slow main thread`, sin crash ni caida sostenida.
  **Salvedad declarada**: captura FPS in-session falló por comillas en el
  layer (`#0` interpretado como comentario); referencia post-sesión
  21,80/21,80 fps, no usada en el veredicto (Q5 no exige umbral de FPS).
- Informe `work/r48-q5-informe.md`; ferry `to-glm/files/R4.8-Q5/` con
  11 ficheros y SHA256SUMS 11/11 OK; relay `to-glm/0451` en `f1b9cda`
  (3 510 B / 73 l / NA=7 por 2 `°` y 1 guion no separable — higiene de
  canal menor) pidiendo revisión final de E8.
- Relay `f1b9cda`, arbol 1149, `from-glm` 265/497/232, `to-glm` 281/651/357.
  Libres: `from-glm` 0443, `to-glm` 0452.
- **Acta `0443`** (revisión final de E8 FAVORABLE con condiciones, relay
  `9b3c07f`, 5 149 B / 99 l / NA=0) archivada. Condiciones registradas:
  tasa **19,6 %/h** (10 pp / 1833 s reales, base `dumpsys battery`),
  velocidad UI 4x / efectiva ~1x, rebuild reproducible del APK, Q3
  14/18+4/18 nunca 18/18, FPS post-sesión no usado, hardening diferido.
- **PR #60 docs-only** `docs/r4.8-cierre` @ head
  `00c4c81499b6cd8eb79ee244bece89ae3d3e8bff`, OPEN/MERGEABLE,
  **4 ficheros +503/−0** (`docs/r4/R4.8-{PRERREGISTRO,Q3-AUDITORIA,
  Q5-INFORME,INFORME-E8}.md`), CI run `44967717332` **3/3**, cuerpo con
  el SHA256SUMS de 11 líneas del ferry. Relay de conformidad final en
  `to-glm/0452` (`a9f0ff7`, 2 917 B / 56 l / NA=4 por dos `×`).
- Relay `a9f0ff7`, arbol 1152, `from-glm` 266/498/232, `to-glm` 282/639/357.
  Libres: `from-glm` 0444, `to-glm` 0453.
- **Conformidad final de GLM** (acta `0444`, relay `c8720f6`, 5 079 B /
  104 l / NA=0) sobre `00c4c81`, con observación no bloqueante 4x vs 1x.
- **Enmienda opcional aplicada** con GO de David: UI dump `t0` mostraba
  `play` (pausado) + `4x`: velocidad configurada 4x pero reproducción
  pausada, `replayAdvance` no ticka y el reloj sigue tiempo de pared
  (~1x, 31 min/1833 s). Reconciliado en `R4.8-Q5-INFORME.md` y
  `R4.8-INFORME-E8.md` (commit `7bed1c7`, +20/−8 en 2 docs), CI
  `44969078446` 3/3. PR #60: **4 ficheros +515/−0**, rama conservada.
  Re-relay a GLM en `to-glm/0453` (`3464e80`, 2 592 B / 53 l / NA=0).
- Relay `3464e80`, arbol 1154, `from-glm` 267/499/232, `to-glm` 283/641/357.
  Libres: `from-glm` 0445, `to-glm` 0454.
- **Acta `0445`** (enmienda aceptada, conformidad trasladada a `7bed1c7`,
  relay `669714b`, 5 746 B / 106 l / NA=0). Cualificación registrada:
  19,6 %/h = REPLAY **pausado** + volumen ON + HIGH=64; criterio «reloj
  avanzando» no discrimina activo/pausado. David eligió **cerrar tal
  cual** (sin corrida en reproducción activa).
- **PR #60 MERGED** como `6cbb796287f39e3bec73b20db12a5cf1cab9a882`
  (`2026-11-22T01:21:40Z`), rama conservada en `7bed1c7`, tree
  `07ec371a5048b4e8c3612a2520953ee0e2cbcd96`. CI main run
  `44971098915` **3/3** (unitTest `137050835977`, lint `137050836237`,
  assembleDebug `137050836293`). Doble GO: acta `0445` + GO de David.
- Post-merge `to-glm/0454` en `52de915` (2 481 B / 58 l / NA=0 /
  sha256 `d659474d...`) pidiendo acta de cierre de R4 con 4 ítems
  abiertos: (1) coste en reproducción activa no medido, (2) hardening
  diferido, (3) bug de comillas del script FPS, (4) contexto obligatorio
  de la cifra 19,6 %/h.
- Relay `52de915`, arbol 1156, `from-glm` 268/500/232, `to-glm` 284/642/357.
  Libres: `from-glm` 0446, `to-glm` 0455.
- **R4 CERRADA FORMALMENTE** por acta `0446` (`from-glm/0446-acta-cierre-r4.md`,
  relay `bd5f8bc`, 5 558 B / 117 l / CR=0 / NA=0 / sha256
  `da30742fb2c8cf2a974836d20564d8c005521f8a522d3b66801f23c33b859078`).
  Doble GO verificado, cadena `0437 -> 0440 -> 0441..0445 -> merge 6cbb796`,
  CI main `44971098915` 3/3. Q3 queda registrada como **14/18 + 4/18,
  nunca 18/18**; tasa **19,6 %/h** con contexto obligatorio (REPLAY
  pausado, volumen ON, HIGH=64).
- **4 ítems abiertos a registro** (no trabajo pendiente, canal normal
  pre-registro + acta + GO para cualquiera): (1) sesión en reproducción
  activa no ejecutada (coste en activo y aplicación de velocidad sin
  medir en sesión; `ReplayTickerTest` 11/0/0 a nivel unitario);
  (2) hardening de `0442` (test de inyección de tipos, `try/catch`
  del lector); (3) bug de comillas del script de muestreo FPS;
  (4) contexto obligatorio al citar 19,6 %/h.
- Relay `bd5f8bc`, arbol 1156, `from-glm` 269/501/232, `to-glm` 284/654/357.
  Libres: `from-glm` 0447, `to-glm` 0455.
- **Sin trabajo de código ni gates abiertos. R4 terminada.**
- **NUEVA DECISION DE DAVID (2026-11-22)**: **descartar R5** del plan y
  continuar con modificaciones en la **app desktop C++** y la **app web**;
  pide consultar a GLM antes de cualquier cambio. Texto literal:
  "Quiero descartar R5 del plan, puesto que quiero hacer modificaciones en
  la app desktop de C++ y en la del web. Quiero continuar sin R5, consultar
  a GLM."
- Consulta relayada en `to-glm/0455` (`24c19d2`, 3 174 B / 60 l / NA=0 /
  sha256 `35cf1e9a...`) con 5 preguntas: vehículo para formalizar el
  descarte (TASKS.md R4/R5, plan R4, IMPLEMENTATION_SLICES), suerte de R6,
  track/gate para C++ y web, alcance de los 2 ficheros web modificados
  (`next-env.d.ts`, `IonosphereScene.tsx`, trabajo de David sin tocar) y
  régimen de revisión de diffs fuera del repo Android.
- **Estado verificado**: C++ `d9fb809` con untracked preexistentes; web
  `8cdc334` con 2 ficheros modificados sin commitear. Relayer `24c19d2`,
  arbol 1157, `from-glm` 269/501/232, `to-glm` 285/643/357; libres
  `from-glm 0447`, `to-glm 0456`.
- **Dictamen recibido** (acta `0447`, relay `8180bda`, 6 363 B / 132 l /
  NA=0 / sha256 `dd4ab56f...`): (1) descarte de R5 en **PR docs-only**
  con contenido pre-fijado por el acta, circuito PR → conformidad breve
  → GO → merge, sin acta de cierre adicional; (2) R6 = decisión de David
  (mantener como release —recomendación no vinculante—, diferir o
  descartar; nunca «pendiente» en silencio); (3) track nuevo **DW**
  (sugerido, nombre a elección de David) con pre-registro por slice,
  criterio binario, mismo gate y Android congelado en `6cbb796`;
  (4) los 2 ficheros web no entran en silencio (baseline de David,
  slice DW.1 o descarte); (5) mismo régimen de revisión en C++/web, con
  auditoría por relay y sin ground truth propio. Condiciones: ningún
  cambio hasta el GO del PR docs-only; primer DW tras ese merge;
  untracked de C++ al baseline o descartados, nunca absorbidos.
- **Diffs web verificados para la decisión**: `next-env.d.ts` = mera
  regeneración de Next (`.next/types/` → `.next/dev/types/`, 2 líneas,
  archivo marcado «should not be edited»); `IonosphereScene.tsx` =
  dos toggles temporales `?bloom=off` / `?atmo=off` marcados
  «TEMP W11 (measurement only, revert)» (2 bloques, +4/−2).
- **Untracked C++ (`d9fb809`)**: lista extensa (docs, worktrees clonados,
  `download/`, `upload/`, `work/`, `scripts/`, `NUL`, `=`, `temp.*`):
  recommended declararlos fuera de la baseline DW, no commitearlos.
- Relay `8180bda`, arbol 1158, `from-glm` 270/502/232, `to-glm` 285/643/357.
  Libres: `from-glm` 0448, `to-glm` 0456.
- **DAVID REVIERTE la decision (2026-11-22)**: mantiene **R5** en el plan,
  aparca **Android** (no se abren slices Android; `main` congelado en
  `6cbb796`) y reforma primero la **app desktop C++ y la web**; el
  desarrollo Android se reanuda tras esa reforma. Documentación al día.
  El PR docs-only de descarte pre-fijado por acta `0447` **no se abre**;
  esa acta queda sin efecto como base operativa (R4 no se reabre).
- Errata de David: "el desarrollo Android, no web, errata mia". La clausula
  correcta es **dejar el desarrollo ANDROID hasta reformar desktop y web**.
- Consulta y errata relayadas: `to-glm/0456` (`6d0a6eb`, 3 248 B / 66 l /
  NA=0 / sha256 `87144a99...`) y `to-glm/0457` (2 886 B / 62 l / NA=0 /
  sha256 `de9a1d97...`), con preguntas sobre registro de la inversion,
  track de reforma C++/web, R6 y baseline de los 2 ficheros web /
  untracked C++.
- **Sin codigo, docs ni merge ejecutados** desde el cierre de R4.
- Relay `6d0a6eb`, arbol 1160, `from-glm` 270/502/232, `to-glm` 287/645/357.
  Libres: `from-glm` 0448, `to-glm` 0458.
- **Dictamen recibido** (acta `0448`, relay `a400abc`, 8 050 B / 160 l /
  NA=0 / sha256 `da87f72f...`): anula la sección 1 del acta `0447` (PR
  de descarte, no se abre), subsisten las demás adaptadas; forma
  corregida aceptada por coherente. Registro en **PR docs-only "estado
  del plan"** en el hub Android con contenido pre-fijado: TASKS.md
  (R4 cerrada con refs, R5 mantenida/diferida citando su contenido y
  anti-trampa `paridad-R5`, R6 = decisión de David o «en espera de
  decisión (2026-11-22)», Android aparcado con `main` congelado en
  `6cbb796` y **carve-out** para PRs docs-only de estado, track activo
  = reforma C++/web); PLAN-R4 sin cambios de cuerpo; IMPLEMENTATION_SLICES
  solo si referencia R5 u orden. Circuito: PR → conformidad breve →
  GO de David → merge, sin acta de cierre adicional.
- **Track DW** (nombre sugerido, a confirmar por David): pre-registro
  por slice con head exacto, criterio binario e instrumentos (C++:
  CMake + suite de tests; web: build/lint Next o capturas); mismo gate
  acta + GO, repo por repo; hub documental en el repo Android mientras
  dure el aparcar; 4 ítems abiertos de `0446` adscritos a la línea
  Android y sin acción.
- **Baselines** antes del primer pre-registro: web `8cdc334` =
  `next-env.d.ts` como higiene + `IonosphereScene.tsx` tal cual con
  **DW.1 = revert de los toggles** (criterio: ausentes y build/lint
  verde) o reclamados por David como feature; C++ `d9fb809` =
  untracked al baseline con inventario y clasificación por fichero
  (higiene / reforma / obsoleto).
- **Sin código ni docs cambiados** desde el cierre de R4.
- Relay `a400abc`, arbol 1161, `from-glm` 271/503/232, `to-glm` 287/645/357.
  Libres: `from-glm` 0449, `to-glm` 0458.
- **Decisiones de David recibidas**: R6 = **release del estado verificado
  desde `6cbb796`** ahora; track **DW** confirmado (`DW.n/cpp`, `DW.n/web`).
- **PR #61 docs-only «estado del plan»** `docs/estado-plan-20261122` @
  head `513137aa3e60bb97e79c23dace0c10e20c4c15db`, OPEN/MERGEABLE,
  **2 ficheros +46/−5** (`docs/TASKS.md` +34/−5,
  `docs/r0/IMPLEMENTATION_SLICES.md` +12/−0), CI `45014044367` **3/3**.
  Contenido = acta `0448` §3: R4 cerrada, R5 mantenida/diferida con
  contenido y anti-trampa, R6 release, Android aparcado con carve-out
  docs-only, track DW, baselines. PLAN-R4 sin cambios de cuerpo.
- Confirmadad pedida a GLM en `to-glm/0458` (`3969f30`, 2 778 B / 56 l /
  NA=0 / sha256 `f1564336...`).
- Relay `3969f30`, arbol 1162, `from-glm` 271/503/232, `to-glm` 288/646/357.
  Libres: `from-glm` 0449, `to-glm` 0459.
- **Conformidad breve recibida** (acta `0449`, relay `0c2a6b0`,
  3 912 B / 84 l / NA=0 / sha256 `40a03f94...`) sobre el head exacto
  `513137a`: 8 ítems pre-fijados presentes, aritmética 34+12=46 y 5+0=5,
  cronología de runs/jobs coherente, carve-out docs-only ejercido.
  Alcance honesto: coincidencia auditada contra el mapeo relayado, no
  contra el diff real (esa es la revisión de David en el GO).
- Relay `0c2a6b0`, arbol 1163, `from-glm` 272/504/232, `to-glm` 288/646/357.
  Libres: `from-glm` 0450, `to-glm` 0459.
- **GO de David ejecutado**: PR #61 **MERGED** como
  `59f539b5e5456f2ae0ac2b2eaf81a40164406920` (`10:57:35Z`), rama
  conservada en `513137a`; CI main run `45014928975` **3/3** (unitTest
  `137185115855`, lint `137185115849`, assembleDebug `137185115630`).
- **R6 ejecutada**: tag anotado **`v0.1.0-android`** sobre
  `6cbb796287f39e3bec73b20db12a5cf1cab9a882`, objeto tag `fbcd6b0c...`,
  pusheado y verificado (`ls-remote`). `main = 59f539b` (docs-only sobre
  el head verificado). Post-merge relayado en `to-glm/0459` (`57d7913`,
  2 069 B / 43 l / NA=0 / sha256 `53ceef91...`).
- **Pendiente**: clasificación por fichero de los untracked de C++
  (`d9fb809`) y de los dos ficheros web (`8cdc334`) para las baselines
  del acta `0448` §6; después pre-registro `DW.1` con acta de GLM y GO
  de David antes de tocar código.
2. **R4.8 (cierre de fase) pendiente**: paridad de replay contra el pin web
   e informe de bateria de 30 min (sesion de David con el Mi 10), revision
   final de GLM. Sin features nuevas.
3. **Deudas documentales (a)(c)(e) SALDADAS** en `docs/R4.6.5-POSTGATE.md`:
   coste ON medido (18.74 vs 27.75 fps, `VolumeQuality.HIGH`=64, errata de
   GLM reconocida), correccion de la frase del lado nocturno (banda debil
   pero visible, +95..+166 vs +226 diurno) y convencion de hashes de ferry
   sobre artefactos normalizados. Citas C++ 256/257 sin cambio (errata
   231/232 retractada en acta 0431); informe del emulador solo si GLM lo pide.
4. **R4.6.5 v3: EJECUTADA**. 2 ciclos ON/OFF en Mi 10 con APK `f3079d9`
   (`d3c97413`), franja viridis en [1.018R,1.078R] = 7.85 % del radio,
   ON/OFF 10.4x/9.1x el control OFF/OFF, 0 FATAL. Ferry 20/20 OK y mensaje
   `to-glm/0443` relayados. FPS ON ~18.7 vs OFF ~27.7 (~32 % caida,
   declarada a GLM). Gate pasado (acta `0436`) y mergeado en `234eab4`.

## R4.6.3: que se hizo y como se midio

Worktree `C:\Users\unknown\Desktop\ionosphere-r463-density` (rama
`feat/r4.6.3-density-chapman` en `ionosphere-live-3d-android`), head
**`9b801b4d7db14b48fc1102f36305587ef67d10ef`**,
**PR #57 (`valledemena/ionosphere-live-3d-android`) fusionado** como merge
commit `1646d56127f5ea81335d6eebc018385b537c4c3a` (23:03:46Z), tree
`6df04fb7048c0fdec9b8f476166694f19a5a18ee` = tree del head, +1439/-3 en 12
ficheros (al head `f306730`: +1398/-3, el fix de R1 añade +49/-8).
Rama conservada por `ls-remote`. Doble GO: acta `0430` FAVORABLE + GO de David.

**Fix de R1 fijado (commit `9b801b4`, con GO de David)**: `VolumeLayerRenderer.innerRadius`
ahora `= volumeInnerRadius(VOL_ALT_MIN_KM) = PLANET_R + 60/6371 = 1.00942`,
espejo C++ `VolumeRenderer.cpp:256/257` (confirmado por clon local de GLM;
`explode_ = 1.0`); el shader mapea `altNorm = (r - uInnerR)/(uOuterR - uInnerR)`,
asi el volumen cubre alt 60..500 km sin desplazamiento vertical.
Pines: `G7c_10` (radios, 1.00942/1.07848), `G7c_11`, `G7c_12`.
Cruce Python de GLM como referencia: sweep 72x72 sobre la formulacion real del
renderer Android; dev=0.12 con `uInnerR=PLANET_R` (sin fijar, per acta 0429),
dev=0 con `uInnerR = 1 + 60/6371` (formulacion que ahora pasa el renderer).

**Deudas de forma pendientes**: informe del emulador si GLM lo pide;
**NO cambiar** las citas C++ de los comentarios Kotlin (256/257 son correctas;
GLM retracto su errata 231/232 en el acta 0431);
FPS OFF/ON del volumen en dispositivo cuando se disponga.

- **Bateria medida** (local, con `gradlew -p C:\Users\unknown\Desktop\ionosphere-r463-density`):
  - `testDebugUnitTest`: **353 tests, 0 fallos, 0 omitidos** (54 ficheros);
    `RESUMEN-TESTS tarea=testDebugUnitTest ficheros=54 tests=353 fallos=0
    errores=0 omitidos=0`.
  - `assembleDebug`: **BUILD SUCCESSFUL**; `app-debug.apk` **42 087 632 B**.
  - `lint`: **BUILD SUCCESSFUL** (sin warnings nuevos en ficheros del slice).
  - CI del PR #57 (run **`44823314882`**, head `9b801b4`): **3/3 success**
    (`assembleDebug` `136589457552`, `unitTest` `136589457838`, `lint`
    `136589458062`); log del job `unitTest` **`136589457838`** con
    `RESUMEN-TESTS tarea=testDebugUnitTest ficheros=54 tests=353 fallos=0
    errores=0 omitidos=0` a las `21:47:49Z` (errata: el id `136531605890`
    pertenece al run anterior `44806450830`, no a este).
  - APK `app-debug.apk` del worktree (f306730+fix, 22:18Z): **42 133 475 B**.
  - **CI del push a `main`** (run `44833939080`, merge `1646d56`): **3/3
    success** (`assembleDebug` `136625491033`, `unitTest` `136625490970`,
    `lint` `136625490802`); log del job `unitTest` `136625490970`:
    `RESUMEN-TESTS tarea=testDebugUnitTest ficheros=54 tests=353 fallos=0
    errores=0 omitidos=0` a las `23:04:58Z` + `BUILD SUCCESSFUL in 52s`.
  - Run **`44806450830`** (head f306730): **3/3 success**
    (`assembleDebug`, `unitTest` `136531605890`, `lint`); log del job
    `unitTest` `136531605890` con
    `RESUMEN-TESTS ... ficheros=54 tests=353 fallos=0 errores=0
    omitidos=0` a las `19:55:04Z` + `BUILD SUCCESSFUL in 35s`.

**Diseño pre-registrado** (`docs/r4/R4.6.3-VOLUMEN-DENSITY-CHAPMAN.md`, en PR
#57): pase volumetrico Ne(h) de espejo del oraculo
(`src/Ionosphere/VolumeRenderer.cpp`), con:

- Textura 3D R8 `72x72x48` (layout de `DensityVolume`, truncado P3-184) +
  textura de capa (D/E/F1/F2, `GL_NEAREST`); colormap viridis 256x1.
- Esfera proxy (radio exterior 500 km sobre el planeta), entrada por cara
  interior (`GL_CULL_FACE` + `GL_FRONT`); raymarch con jitter, sombra dura,
  acumulacion front-to-back, `discard` por alpha.
- **Mapeo de marco del motor**: `u = fract(-atan(n.z, n.x)/2pi)`,
  `v = asin(n.y)/pi+0.5`, `altNorm` de `raySphere` (vs `sampleVolumeAt`;
  test G7c_24).
- **Modos runtime sin recompilar**: `uMode` 0 (`density`: colormap) / 1
  (`chapman`: colores Chapman byte-iguales, espejo `VOL_FRAG`/`u_colorMode==1`).
- **Cero coste OFF**: `VolumeFramePlan` sin OFF devuelve `upload/draw = null`;
  sin `glTexImage3D` ni draw (test G7c_01).
- El volumen se sube una vez por volumen (huella de celdas, no timestamp).

## R4.6.5: gate visual (dispositivo real)

Ejecutado el 2026-11-21 con GO escrito de David: "preparar y medir, solo
registrar FPS sin umbral". Dispositivo **Xiaomi Mi 10** (`M2002J9G`, Android
12, MIUI `V13.0.4.0.SJIEUXM`/V130, 1080x2400, `adb 1140a489`). APK construido
desde worktree limpio en detached HEAD **`1646d56`** (= `main`), `assembleDebug`
BUILD SUCCESSFUL, `app-debug.apk` **42 087 632 B**, `adb install -r` Success,
`versionName 0.1.0-r1`. **MIUI bloquea INJECT_EVENTS**: todos los toques los
hizo David manualmente.

- FPS (`dumpsys SurfaceFlinger --latency` sobre
  `SurfaceView[...MainActivity](BLAST)#0`, 126 frames validos por muestra,
  dos muestras separadas 10 s): **OFF 29.93 / 29.81 fps**; **ON 29.81 /
  29.93 fps**. Muestras crudas `work/r465-fps-{off,on}-{a,b}.txt`.
- Estados: OFF inicial (`r465-off-base.png`); ON ciclo 1 (`r465-on-c1.png` +
  `r465-ui-on.xml`: `checked=true`, `volumen on`); OFF ciclo 2
  (`r465-off-c2.png` + `r465-ui-now.xml`: switch gris, `checked=false`); ON
  ciclo 2 (`r465-on-c2.png` + `r465-ui-c2.xml`: `checked=true`); OFF ciclo 3
  (`r465-off-c3.png` + `r465-ui-final.xml`: `checked=false`); estado final medido
  **ON** (`r465-final-off.png` + `r465-ui-final2.xml`: `checked=true`) aunque
  David escribio "lo puse en off" — discrepancia declarada, prevalece la
  medicion. `r465-cycle-01..16.png` son capturas de background anteriores a
  los toques (todas ON), no evidencia de ciclo.
- logcat: **0 `FATAL EXCEPTION`**; solo `miui.drm.DrmManager.exportFatalLog`
  (sistema MIUI) y `RuntimeInit`/`uiautomator` por los dumps. Sin crash de la
  app.
- **ERRATA VISUAL (GLM acta 0433)**: mi atribucion de "parches cian/lila
  del volumen" era incorrecta: eran terminador + datos en vivo, presentes
  identicos en OFF. Cuatro metodos de GLM dan **cero pixeles ON-correlacionados
  en el globo**. Causa: disco 100 % nocturno (Americas de noche, 0 % dia),
  `sunShadow = 0` -> `discard` por diseno en el shader Kotlin y en el C++.
  No es defecto del port: la prueba no ejercito el camino visual.
- Relay del gate: `GLM_REVIEW.md` -> `INTERCAMBIO/work/GLM-MESSAGE-20261121-011536.md`
  (**3 812 B / 72 l / CR=0 / ASCII**, sha256 `93150b4ae3bbacafbd1aeb6d6ebef374ba311aa3ee362cc7c7d03c3e6b8a8d95`).
- **Acta `0432` de GLM (CONDICIONADA)** archivada en el relay `637d52f`:
  `from-glm/0432-dictamen-condicionado-r465-inspeccion-visual.md` (blob
  `784f293d786701489ee62fa5ad8766c006dd4255`, **3 713 B / 73 l / CR=0 /
  ASCII**, sha256 `6d5c2c48258b96946487f28ef730532d5bfa2e64b5f5611fc25fbcaab694962e`;
  contrato **parcial** declarado: transcrita del mensaje, sin zip de GLM).
  Condicion unica: inspeccion visual de las capturas (a) capa visible en el
  limbo, (b) separada del planeta, sin banda pegada ni hueco anormal, (c)
  globo visible, (d) sin artefactos groseros.
- **Capturas subidas al relay para inspeccion**: `to-glm/files/R4.6.5/capturas/`
  con las 6 PNG (`r465-off-base.png`, `r465-on-c1.png`, `r465-off-c2.png`,
  `r465-on-c2.png`, `r465-off-c3.png`, `r465-final-off.png`); ademas 5 UI XML,
  4 muestras FPS crudas, `logcat-resumen.txt` (0 FATAL), `build/provenance.txt`
  con sha256 del APK `aa24448890cd9b93ea2232d01e3c78020097b281ff87e82e71555a28a2a313a6`
  y `GLM-MESSAGE-20261121-011536.md` pusheado. SHA256SUMS 18/18 OK en
  `to-glm/files/R4.6.5/` y 19/19 OK en `from-glm/files/0432/`.
- **Ack `to-glm/0440`** publicado en `0766408` (blob
  `6d1b947b231e32be92bae722be9a9a5694be234b`, **3 788 B / 72 l / CR=0 /
  ASCII**, sha256 `9789addda3e99e93b41e2d74a7e0f64c9c228782efad341cdc11d5aecba2503a`):
  rutas de las capturas, hashes, contrato parcial declarado y peticion de
  acta `0433` NO FAVORABLE (`8ad2f82`) y ack `to-glm/0441` (`3af0ac7`).
  Numero libre de acuse: `to-glm/0442`.

## R4.6.5 v2: re-test con hemisferio diurno

Tras el acta `0433` (NO FAVORABLE por geometria nocturna), David giro el
globo manualmente ~180 grados (MIUI bloquea INJECT_EVENTS). Captura
`r465-v2-on-c1.png`: **Australia/Asia iluminados**, terminador a la izquierda;
por fin hay porcion de dia visible. Mismo APK (`main 1646d56`, sha256
`aa244488...`), sin cambio de codigo.

- 2 ciclos verificados por UI dump + screenshot: ON c1 (`checked=true`) ->
  OFF c1 (`checked=false`) -> ON c2 (`checked=true`) -> OFF c2
  (`checked=false`). `volumen on` solo en los ON; `cortina` OFF siempre.
- FPS v2: ON 29.93/29.93 (126 frames), OFF 29.93/29.93 (127 frames),
  muestras crudas `work/r465-v2-fps-*`. `FATAL EXCEPTION` = 0.
- Ferry v2 en el relay `5e525a8`: `to-glm/files/R4.6.5-v2/` con 4 capturas
  (`r465-v2-{on,off}-c{1,2}.png`), 4 UI XML, 4 FPS, `logcat-resumen.txt`,
  `build/provenance.txt` y `GLM-MESSAGE-20261121-022419.md` (2 880 B / 56 l /
  ASCII, sha256 `53d4a6f077cb4d617fbf85ed60bab62988c5b6f8405b2ffc0eb3425ab4e4823b`);
  SHA256SUMS 15/15 OK + MANIFEST.
- Cierre: acta `0434` NO FAVORABLE definitiva + ack `to-glm/0442`;
  corregido en PR #58 (`f3079d9`); v3 ejecutada PASS y cerrada por `0437`.

## R4.6.5: causa raiz cerrada (acta 0434)

Tras el re-test v2 con hemisferio diurno (GLM confirmo ON = OFF por tres
metodos), GLM encontro la causa con lineas locales: **`VolumeShaderSource.kt:32`**
declara `in vec3 aPosition` y **`VolumeLayerRenderer.kt:105`** consulta
`glGetAttribLocation(program, "aPos")`. Devuelve `-1`; los
`glEnableVertexAttribArray`/`glVertexAttribPointer` de `L124-125` son no-ops
silenciosos; el atributo queda a (0,0,0) -> triangulos degenerados -> cero
fragmentos rasterizados, sin crash. **Congenito de `f306730`**, aislado a
esta capa (cortina `aPos`/`aPos` coincide; Tierra/atmosfera/estaciones
`aPosition`/`aPosition`). Tuve verificado en mi clon local con `git log -S`.
Descartados `sunShadow`, textura a ceros, depth test y "neblina tenue"
(simulacion de GLM: `accumAlpha 0.84-0.98` = franja opaca).

- Acta `0434` archivada `a1432b7` (from-glm/0434, blob `486c7dd7...`,
  10 179 B / 171 l / CR=0 / ASCII, sha256
  `a6d4ec4d5847e455ddebc6790f5fd0f43dd5f41487355eeb0cc2ee99a5f339d0`),
  ferry con `sha256sum -c` 1/1 + MANIFEST, contrato parcial declarado
  (zip GLM `glm-acta-0434.zip` sha256 `993441df...`, cmp exit 0).
- Ack `to-glm/0442` publicado `d6c6e80` (3 993 B / 77 l / ASCII).
- **GO de David ejecutado**: slice de correccion en PR #58 (`f3079d9`):
  lookup `aPosition` + test estatico de acuerdo (356/0). Criterio v3
  pre-registrado: franja viridis ~7.85 % del radio sobre el limbo iluminado.
  Observacion no bloqueante: roll de -23.4 grados de la Tierra no lo aplica
  el volumen; declarado, no cambiado.

## R4.6.5 v3: correccion del binding (PR #58)

Con GO de David ("GO"), worktree nuevo `C:\Users\unknown\Desktop\ionosphere-r465-fix`
desde `main 1646d56`, commit **`f3079d9647c338118ca41811610fcd407b2075c9`**,
PR **#58** OPEN/MERGEABLE, **2 ficheros +79/-4**:

- `VolumeLayerRenderer.kt`: `aPos` -> `aPosition`, lookup `"aPosition"` en
  `glGetAttribLocation`, y `glEnableVertexAttribArray`/`glVertexAttribPointer`
  con el nuevo nombre. El vertex shader no cambio (`in vec3 aPosition`).
- `VolumeAttributeBindingTest.kt` (nuevo): 3 tests JVM que comparan el nombre
  declarado en `VOLUME_VERT` con el lookup del renderer; la cortina es el
  control sano. Caza el defecto sin GL.
- Local: `testDebugUnitTest` **356 tests / 0 fallos / 55 ficheros** (`RESUMEN-TESTS
  ... ficheros=55 tests=356 fallos=0`), `assembleDebug` BUILD SUCCESSFUL,
  `lint` BUILD SUCCESSFUL.
- CI del head: run `44909702060` **3/3 success**; job `unitTest`
  `136862679225` con `RESUMEN-TESTS ... ficheros=55 tests=356 fallos=0
  errores=0 omitidos=0` a las `13:37:20Z`.
- Limitacion declarada: roll de -23.4 grados de la Tierra no lo aplica el
  volumen; queda ~23 grados desplazado respecto a la geografia (no bloqueante
  segun acta 0434; declarado, no cambiado).
- **v3 EJECUTADA (PASS)**: acta `0435` de GLM (fase codigo CONFORME),
  ferry `to-glm/files/R4.6.5-v3/` (20/20 OK) y mensaje `to-glm/0443`.
  Centro globo (539,1237), R=498; pixeles viridis en banda ON 10 748/13 944
  vs OFF 21/660; diff ON/OFF 152 477/133 147 (49 %/43 %) vs control
  OFF/OFF 14 672 (4.7 %). Perfil: anillo en [1.018R,1.078R], dG +203..+220.
  0 FATAL en 18 864 lineas. FPS ON 18.76/18.67 vs OFF 27.71/27.63 (caida
  ~32 % declarada). Gate pasado (acta `0436`) y mergeado en `234eab4`.

## R4.6.5 v3: gate visual EJECUTADO (PASS)

Tras el acta `0435` de GLM (fase de codigo CONFORME, archivada en relay
`5cc9550`, 11 604 B / 224 l / ASCII / sha256 `a992d508...`), se ejecuto la v3
en el Mi 10 (`1140a489`, M2002J9G) con el APK construido desde `f3079d9`
(`d3c9741338e36c618b57772d6eabc777a3afa78825a0f41da5dca7f8a1cebda7`,
42 087 632 B, distinto del v2 `aa244488`, `cmp exit 1`).

- 2 ciclos ON/OFF completos; UI dumps `volumen checked=true/false` segun
  estado; estado final OFF. David hizo todos los toques y la rotacion
  manualmente (MIUI bloquea INJECT_EVENTS).
- Geometria detectada desde los pixeles del anillo: centro (539,1237), R=498.
  Pixeles viridis en la banda [0.93R,1.13R]: ON 10 748 (3.45 %) y 13 944
  (4.48 %) frente a OFF 21 (0.007 %) y 660 (0.21 %). Diff sum|RGB|>30:
  ON/OFF 152 477 (48.98 %) y 133 147 (42.77 %) frente al control OFF/OFF
  14 672 (4.71 %): **10.4x y 9.1x el control**.
- Perfil radial en el limbo diurno: anillo en [1.018R,1.078R] con dG
  +203..+220; a 1.098R ya es fondo. **Banda del 7.85 % del radio**,
  fina y nitida (sin bloom), exactamente el criterio pre-registrado.
- FPS: ON 18.76/18.76 y 18.67/18.75; OFF 27.71/27.73 y 27.63/27.92.
  Caida ~32 % al rasterizar el volumen, **declarada** (GLM estimo ~30;
  queda a su criterio si dispara el FAIL de "caida fuera de lo esperado").
  0 FATAL en 18 864 lineas de logcat; aviso MIUI `QueueBuffer time out 68 ms`.
- Ferry `to-glm/files/R4.6.5-v3/` con 20 ficheros (declarado 20/20 por mi;
  GLM verifico **19/20 directo**: `analisis-pixeles.txt` normalizado a LF por
  git, hash declareado con CRLF; contenido identico, no bloqueante).
- **Dictamen visual final: FAVORABLE** (acta `0436`, relay `a5ee3ba`,
  `fec511b6`, 10 559 B / 200 l / CR=0 / sha256 `3fe00e1e...`). GLM con
  geometria propia: senal/control **46.2x / 45.2x**, OFF=0 px viridis, banda
  **[1.010,1.080]R**, VLM ambos ciclos, 0 FATAL, FPS 18.74/27.75 (errata
  propia del ~30 reconocida; coste fiel `steps_=64`). Aceptadas sus 3
  correcciones: discrepancia EOL, frase del lado nocturno y errata de FPS.
- **Merge con doble GO**: PR #58 -> `234eab4` (`15:54:40Z`), rama conservada;
  CI main run `44921675310` 3/3, `RESUMEN-TESTS ficheros=55 tests=356
  fallos=0`. Post-merge `to-glm/0444` en relay `b182a82` (3 820 B / 71 l /
  sha256 `6fb153a6...`), relay `GLM-MESSAGE-20261121-165859.md`.
- Relay `b182a82`, arbol 1123, `from-glm` 259/491/232, `to-glm` 274/631/357.
  Libres: `from-glm` 0437, `to-glm` 0445.
- **Cierre formal**: acta `0437` de GLM (`bdaf623`, blob `1b2dd4c2`,
  7 224 B / 147 l / CR=0 / NA=6 por tres `caché` acentuadas del original,
  sha256 `ba088b96...`) -> **R4.6.5 CERRADA**. Cadena: 0432 -> 0433 ->
  0434 -> 0435 -> 0436 -> merge `234eab4` -> 0437.
- **Pendientes menores de GLM CERRADOS localmente**: (1) blob del acta 0436
  en el relay = 10 559 B / sha256 `3fe00e1e...` (coincide con lo declarado);
  (2) `to-glm/0444` presente en `b182a82`, blob `7dae8fb5...`. Ambos quedaron
  pendientes solo por la caché obsoleta de los endpoints de listado del
  sandbox de GLM.
- Roll de -23.4 grados y recomendaciones (a)-(e) del acta 0436 quedan como
  no bloqueantes (backlog).

## R4.6.5: CIERRE (gate pasado + merge)

- Acta `0436` de GLM: **DICTAMEN VISUAL FINAL FAVORABLE** sobre `f3079d9`,
  archivada verbatim en relay `a5ee3ba` (blob `fec511b6`, 10 559 B / 200 l /
  CR=0 / NA=4 por dos acentos del original conservados, sha256
  `3fe00e1eb32f712c9db3fbbb89f349693d4beb72d2338400451a339644fca871`).
  Senal/control 46.2x y 45.2x con geometria propia, OFF=0 pixeles viridis,
  banda [1.010,1.080]R, VLM ambos ciclos, 0 FATAL, FPS 18.74/27.75.
  Incluye errata propia (su "~30 fps despreciable" del 0435 estaba mal),
  discrepancia EOL del ferry declarada (19/20 directo, 20/20 con CRLF) y
  correccion de la frase "lado nocturno apagado" (banda debil nocturna,
  fisicamente correcta).
- Doble GO verificado: acta sobre el head exacto `f3079d9` + GO de David.
- PR #58 **MERGED** `234eab4f7af3b32da1af6e78e41707cc137d6be7`
  (`15:54:40Z`), rama conservada en `f3079d9`. CI push main run
  `44921675310` 3/3, `RESUMEN-TESTS ficheros=55 tests=356 fallos=0`.
- Post-merge `to-glm/0444` en `b182a82`; acuse de cierre `0437` en `bdaf623`.
  Relay `bdaf623`, arbol 1124, `from-glm` 260/492/232, `to-glm` 274/631/357;
  libres `from-glm` 0438, `to-glm` 0445.

## R4.6.5: cierre formal (acta 0437)

GLM emitio el acuse de cierre verificando por vias frescas (API del repo
Android + endpoint raw del relay, con los endpoints de listado sirviendo
cache obsoleta por la caida de su sandbox): PR #58 merged true a las
`15:54:40Z`, commit `234eab4` con padres `1646d56` + `f3079d9`, CI push
run `44921675310` 3/3 con los IDs exactos, fix presente en `main` por raw.
Acta archivada `from-glm/0437-acuse-cierre-r465.md` en relay `bdaf623`
(blob `1b2dd4c2`, 7 224 B / 147 l / CR=0 / NA=6 por tres `caché` acentuadas
del original conservadas verbatim, sha256
`ba088b96c4bd8ff64394be90cc03485e63b21959b5b3d6bc1d25ee895fa10810`).
Los dos pendientes menores que GLM dejo por su sandbox se cerraron aqui
por via local: blob del acta 0436 = 10 559 B / sha256 `3fe00e1e...`
(coincide) y `to-glm/0444` presente en `b182a82` (blob `7dae8fb5...`).
Cadena completa: 0432 condicionado -> 0433/0434 NO FAVORABLE -> 0435
codigo CONFORME -> 0436 VISUAL FAVORABLE -> merge `234eab4` -> 0437 cierre.
**R4.6.5 CERRADA**. Backlog no bloqueante: recomendaciones (a)-(e).

## R4.6.7: que se hizo y como se midió

Rama `feat/r4.6.7-iso-bands`, commits `96c52d1` + `9c86d76` + `866f196` sobre
`8611ee6`. Head fusionado: **`866f19607ca566fbe25781ecf1d19f2162428dae`**.

- **R1 cerrada** (dictamen GLM `0424`): `if (uShowIso) isoSum += isoB * dt;` en
  `GlobeRenderer.kt` L121, integral por paso como `dR`/`dM`; antes el brillo
  escalaba con `N` (~2x de 8 a 16 pasos). Las posiciones de las bandas no se
  mueven (`odIso` converge).
- **R2 cerrada**: tres `assertTrue(frag.contains(...))` nuevos que pinean la
  escalera completa `1.0` / `0.75` / `0.5` contra `ISO_BAND_3/2/1`. El dictamen
  permitia pinnar solo los extremos: se pinnan los tres peldanos **desviacion
  declarada**; GLM acepto el pin del medio en su `0427` (ya preautorizado en su
  `0425` seccion 5).
- Delta `9c86d76..866f196` = **2 ficheros, +7/-2**; acumulado `8611ee6..866f196`
  = **5 ficheros, +86/-1** (`numstat`: GlobeRenderer +26/-0,
  AtmoShaderConstants +9/-0, AtmoShaderSource +22/-1,
  AtmoShaderConstantsTest +11/-0, AtmoShaderSourceTest +18/-0).
- **CI del head**: run **`44728416840`**, 3/3 (`assembleDebug` 58 s, `unitTest`
  55 s, `lint` 1 m 34 s); job `unitTest` `136261624532` con
  `RESUMEN-TESTS ... ficheros=51 tests=326 fallos=0 errores=0 omitidos=0` a las
  `11:33:13Z`. Refrendo local del mismo arbol: misma linea, BUILD SUCCESSFUL
  1 m 24 s.
- **Cuerpo del PR #56 reescrito** (`gh pr edit --body-file work/pr-r467-body.md`):
  delta real +86/-1, cierre R1/R2, las tres anotaciones del latente (proxy
  provisional `odIso = odR + odM` con des-normalizacion `8 + 4.5 * (odIso /
  0.49)`, rango medido max 0.494; anillos de limbo y no cascarones 3D;
  `ISO_STRENGTH` como multiplicador escalar sin clamp, golden 0.75, acotacion en
  R4.6.5) y la atribucion correcta: **el acta favorable es la `0424` de GLM**.
- **Sitios protegidos intactos** en el arbol fusionado (GLM lo verifico linea por
  linea en el `0427`): `logNe`, `ISO_BAND_*` y el sitio
  `isoSum * uIsoColor * uIsoStrength`; el unico cambio funcional fue el `* dt`.

### Estado del doble GO (CERRADO)

- **Mitad de David: dada** por escrito el 2026-11-20 en el canal («GO para todo
  por mi parte»), y registrada en el gate del PR.
- **Mitad de GLM: `0427` FAVORABLE** sobre el head final `866f196`: R1/R2
  cerrados, delta minimo verificado linea por linea, CI 3/3, cuerpo del PR con
  las tres anotaciones y la atribucion correcta. Archivada en `827b72c`.
- **Consecuencia**: merge ejecutado como **merge commit** (sin squash, sin
  rebase), gate registrado antes y rama conservada. **Hecho.**
- **Cierre del lado de GLM**: acta **`0428`** (acuse del `to-glm/0435` y
  **conformidad plena, sin reparos**: verifico de primera mano su 0427
  byte-exacta en `827b72c`, la forma del merge, los sitios protegidos, el gate
  7 s antes del merge y el CI 3/3 de `main`). **R4.6.7 cerrada en ambos lados.**
- **Verificacion de campo de GLM sobre el `to-glm/0436` (ronda 7, 2026-11-20)**:
  sandbox con red tras seis rondas sin arranque; clones frescos de relay,
  android y web. **Cero discrepancias**: su 0428 en `5dcd661` byte-exacta
  (`cmp` exit 0 contra su canonica local), ferry 16 miembros 14/14 OK,
  `to-glm/0436` blob `8437243b` / 3 776 B / 76 l / CR=0 / ASCII, normalizacion
  `8db6be4` de solo 2 lineas, contadores 989 / 251-424-173 / 266-564-298 y
  continuidad historica en 7 anclajes (`0837077` -> `8db6be4`, `diff d30f6d7..8db6be4`
  = 18 Added / 0 modificados / 0 borrados). Android en quietud: `main` `3a44ac5`,
  rama conservada. **No emite acta**: el `0429` queda reservado para el dictamen
  de **R4.6.3**, segun la cadencia pactada. Evidencia de su ronda en
  `work/r0429-verificacion/evidencia/`.

### Precision sobre el handoff (declarada a GLM en la puerta de R4.6.3)

GLM declaro el `handoff.py check` y la tabla del ferry como **no medibles desde
fuera** y lo dejo como artefacto interno. Precision nuestra, para que conste:
**existen** en el repo publico **`valledemena/ionosphere-live-3d`**
(`HANDOFF_ACTIVE.md`, `HANDOFF_LOG.md`, `scripts/handoff.py`), pero estan
**sin versionar** (`git status`: `??`), asi que **no estan en GitHub**: por eso
no son medibles. No es un fichero inexistente ni un repo privado. Publicarlos
requiere GO de David (no se commitea sin el).

## Erratas de la ronda (registradas, sin reescribir lo publicado)

GLM las registro sobre mi mensaje de 2026-11-20. Se aceptan las cuatro y quedan
aqui; el mensaje publicado no se reescribe (doctrina `0422`/`0433`):

1. «PAQUETEs ... **0425 (53 397 B)**»: el zip de 53 397 B es el **0426**
   (`bfbc3edb`); el 0425 es **15 706 B / `2ffe3940`**. La tabla del mensaje
   relayado ya lo tenia bien; el desliz estaba en el resumen del chat.
2. «`0424` ... **`9dc0965` (mia, blob `bdef5bb5`)**»: doble desliz. La `0424` es
   acta de **GLM**, su canonica aterrizo en `files/0424/` en **`0837077`**
   (blob `bdef5bb5`, 18 213 B / 289 l) y nunca existio a nivel 1; `9dc0965` es
   el commit del archivo de la **transcripcion del 0423**.
3. «to-glo»: `to-glm`. La cifra (264) era correcta.
4. «8 bultos» del ferry: eran **9 items** (4 actas sueltas 0422/0423/0425/0426
   + 5 zips).

Cerradas por el byte con el ferry (`0837077`) y con el `0427`: la transcripcion
lossy del `0426` en el relay (`dd08684`, blob `d9f7b722`, 4 704 B / 66 l frente
a su canonica `c5e9a187`, 14 036 B / 227 l) y la transcripcion copiada en
`files/0422/` del `ae5e2d3` (sustituida por la canonica).

## Ferry GLM -> Deepseek: CERRADO (0422-0427)

Los cinco paquetes de GLM llegaron a `INTERCAMBIO` y se archivaron byte-exactos
bajo `from-glm/files/0422/` ... `/0426/` en el commit **`0837077`**, con contrato
de llegada medido (`from-glm/files/FERRY-0422-0426-CONTRATO-LLEGADA-20261120.md`,
blob `a74fe1bf`, 5 324 B / 105 lineas / CR=0):

| paquete | zip | miembros | `sha256sum -c` | acta canonica | blob | sha256 |
|---|---|---|---|---|---|---|
| 0422 | 25 117 B `904c7e27` | 12 | 10 OK / 0 fallos | 17 421 B / 261 l / CR=0 | `f1fa7ddd` | `5c040127...` |
| 0423 | 22 003 B `72d327d5` | 11 | 9 OK / 0 fallos | 15 665 B / 254 l / CR=0 | `041d21b3` | `dbdc636f...` |
| 0424 | 38 798 B `7cd51e2b` | 16 | 14 OK / 0 fallos | 18 213 B / 289 l / CR=0 | `bdef5bb5` | `e17cfc45...` |
| 0425 | 15 706 B `2ffe3940` | 10 | 8 OK / 0 fallos | 14 354 B / 244 l / CR=0 | `c5589255` | `4776d137...` |
| 0426 | 53 397 B `bfbc3edb` | 15 | 13 OK / 0 fallos | 14 036 B / 227 l / CR=0 | `c5e9a187` | `fe1eb620...` |
| 0427 | 218 427 B `62e4d154` | 19 | 17 OK / 0 fallos | 12 297 B / 253 l / CR=0 | `41d437e1` | `8b6eef7a...` |
| 0428 | 241 370 B `587a7b48` | 16 | 14 OK / 0 fallos | 6 543 B / 128 l / CR=0 | `27995249` | `537e862a...` |

`cmp` identico (exit 0) en las seis actas (0422, 0423, 0425, 0426 sueltas;
0427 y 0428 sueltas y sus miembros). Todos los blobs y sha256 de GLM coinciden
sin una sola desviacion; la `0424` solo vino dentro del zip y queda cubierta por
`sha256sum -c` y el sha256 declarado. 64 + 19 + 16 = **99 miembros** archivados.
Commits de archivo: `0837077` (0422-0426), `827b72c` (0427), `5dcd661` (0428).

## FPS de referencia en el emulador (medido, NO es gate)

Medido el 2026-11-20 sobre el head **`866f196`** (el arbol que heredo el merge).
Condiciones declaradas: emulador `meas33` (`emulator-5554`, Android 13,
`ro.kernel.qemu=1`), arrancado con
`-no-snapshot -no-audio -no-boot-anim -no-window -ports 5554,5555`; viewport
`Override size: 610x704` sobre panel 1080x2280 -> **GL por software
(SwiftShader)**; APK `app-debug.apk` **42 054 864 B** de `assembleDebug`,
instalado con `adb install -r` (Success);
`topResumedActivity=com.valledemena.ionosphere/.MainActivity`.

- Instrumento: `dumpsys SurfaceFlinger --latency` sobre la capa
  `SurfaceView[...MainActivity](BLAST)#199`, dos muestras separadas 10 s:
  **4.80 fps** (127 frames / 26.457 s) y **5.52 fps** (127 frames / 22.992 s).
- `dumpsys gfxinfo` **no sirve** aqui (globo por GL en un SurfaceView; la ventana
  HWUI registro 1 frame en 15 s: 100% janky).
- Evidencia: `work/fps-ref-866f196-b.png` (156 898 B, globo + HUD; la primera
  captura `work/fps-ref-866f196.png` pillo el ANR del launcher encima),
  `work/sf-lat-a.txt` y `work/sf-lat-b.txt` con las muestras crudas.
- Tras la medida el emulador se apago (SIGTERM a los 600 s);
  `adb devices` queda vacio. Para re-medir, mismo comando de arranque.
- **Contexto que invalida comparaciones**: tormenta de ANR (`Pixel Launcher`,
  luego `systemui`). Numero de referencia de emulador, **no de dispositivo**; no
  se propone como gate.

## Pendientes, en orden

1. **R4.6.5** (gate visual, dispositivo): **EJECUTADO** en Mi 10 con 3 estados
   OFF y 3 ON verificados, FPS medidos y 0 FATAL; GLM dicto NO FAVORABLE
   definitivo con causa `aPosition`/`aPos`; pendiente GO de correccion.
2. **Deudas de documentacion**: actualizar las citas de linea del espejo C++
   en los comentarios Kotlin (CANCELADA: GLM retracto la errata 231/232,
   los comentarios quedan como estan); informe del
   emulador (version, imagen, comando, logcat, casillas) como documento
   propio si GLM lo pide; FPS OFF/ON ya medido y documentado en
   `docs/R4.6.5-POSTGATE.md`.
3. **Custodia del 0427, 0428, 0429 y 0430**: cerradas (actas + 19, 16, 15 y
   10 miembros en `827b72c`, `5dcd661`, `f111b00` y `f41dfe5`). Los zips de
   GLM viven tambien en `INTERCAMBIO` como custodia, sin borrar sin GO.
4. **Doble GO para la proxima puerta de modificacion de codigo**: R4.6.5 y
   (si aparece) cualquier slice siguiente.

## Bloqueo

- **Ninguno para R4.6.3**: PR #57 fusionado con doble GO, rama conservada,
  CI del push a `main` 3/3 con `RESUMEN-TESTS`.
- **Gate visual (capa ON + FPS en dispositivo): EJECUTADO** en Mi 10; solo
  queda el acta `0434` de GLM sobre el ferry v2. El dispositivo sigue
  conectado (`1140a489`).
- **Gate de shader/arranque: NO PROBADO**: sin `glslangValidator`/`glslc`;
  `ShaderProgram.create` lanza `IllegalStateException` si no enlaza.
- **Altura estable del panel al conmutar**: razonamiento de diseno, no medido en
  dispositivo.
- **Call site sin costura JVM**: probado-por-lectura (GLM lo acepto como Q4);
  Robolectric anotado como candidato, no bloqueo.
- **MIUI prohibe inyectar toques** (`INJECT_EVENTS`): gates de dispositivo
  manuales, por David.
- **Aviso de entorno**: `handoff.py status` informa del clon
  `ionosphere-live-3d-android`, en `feat/r4-e-gates` (`eec186a`) con cambios
  locales y `main` local desfasado. **No se toca**: todo se mide contra
  `origin/main` tras `git fetch`.
- **Limite declarado**: el texto del log de un job no es accesible anonimamente
  ("Sign in to view logs"); se cita desde una sesion `gh` autenticada.

---

## Reglas vigentes

- Solo David decide merges; el acta de GLM del SHA exacto y el GO de David deben
  estar **registrados** en PR o relay antes del merge.
- Un slice = un PR; no tocar `data/**`, `.github/**` ni el checker congelado
  (`343c8104de65cb13086f22436083561ad1c32473`); sin force-push; no commitear
  `work/`, `work-perf-*` ni logs; no borrar ramas sin GO.
- Documentos de GLM: archivo **verbatim**, sin renumerar; ningun documento
  declara su propio hash; nuevos ficheros ASCII, sin BOM, LF.
- **Mutantes**: no editar los ficheros mutados mientras corre el script; exigir
  `RESTAURADO todo=True`.
- **`RESUMEN-TESTS` (ratificado por GLM en el `0420`)**: la linea prueba ejecucion
  real de la tarea; `FROM-CACHE` / `UP-TO-DATE` / `SKIPPED` dan run verde sin linea
  y **no** son fallo. Su ausencia no se cita nunca como evidencia de tests. En
  slices cuyo diff no ejecuta la tarea, el gate es **CI 3/3 + refrendo local con
  `--rerun` declarado como local** (arbol y linea integra citados); en slices de
  codigo la linea de CI sigue siendo exigible y un silencio sin explicacion es
  anomalia a investigar.

---

## Sistema de handoff

```bash
python scripts/handoff.py status
python scripts/handoff.py log --tool opencode --summary "..." --files "..." --verified "..." --next "..."
python scripts/handoff.py check
```

`python` no esta en PATH: usar
`C:\Users\unknown\AppData\Local\Python\pythoncore-3.11-64\python.exe`.

---

## GLM review loop

- Canal real: **David**. Un relay por puerta: `GLM_REVIEW.md`
  (`Status: pending`) -> `python scripts/glm_notify.py` ->
  `INTERCAMBIO/work/GLM-MESSAGE-<timestamp>.md` (`Status: relayed`). El texto
  completo se pega en la respuesta final.
- `glm_notify.py` escribe CRLF y deja pasar no-ASCII: normalizar a LF/ASCII.
- **R4.6.3 CERRADA — doble GO completo**: acta `0430` FAVORABLE de GLM sobre
  el head `9b801b4` (por su re-poll de la API publica) + GO escrito de David
  (push y merge del PR #57). Merge ejecutado: `1646d561...`, rama conservada,
  CI del push a `main` 3/3 con linea. Relay del gate:
  `GLM-MESSAGE-20261120-210209.md` (**4 722 B / 97 lineas / CR=0 / ASCII**),
  con las peticiones A-E (DoD, mapeo de marco, HW/compilacion del shader, FPS
  OFF/ON, conmutacion capas sin volumen).
  - **Acta `0429` (condicionada)** archivada verbatim: commit `f111b00`, blob
    `54f1d3fd70f6da68612401f26b57fb73f8db1674`,
    `11 986 B / 218 l / CR=0 / ASCII`, sha256
    `357c4fb2f95ef3baf94e9946203ba40aaa748ceb793f7b97fea6146dca064eca`
    (cmp exit 0; ferry files/0429/ con 15 ficheros, sha256sum -c ferry 13/13
    OK). Ack `to-glm/0437` (blob `7348d8d8...`).
  - **Acta `0430` (FAVORABLE)** archivada: commit `f41dfe5`, blob
    `03f7163a632e6daee07ff0601844654af17c921e`, **4 593 B / 85 l / CR=0 /
    ASCII**, sha256 `48b5b60eda713daa670cb57f6fbecebc5eaac9b2082db279aa6f4117d572256d`
    (transliteracion ASCII declarada: 2 bytes con acento -> sin acento);
    ferry files/0430/ con 10 ficheros, sha256sum -c 8/8 OK, cmp exit 0.
  - **Ack de cierre `to-glm/0438`** (blob
    `91163471ccfe201151db7cd0dc1fea7f6714b0ce`, **4 422 B / 83 l / CR=0 /
    ASCII**, sha256 `f244c6c2b761334e3cff67a70dcd6df1ffa614cca632c40a6b823303f3d8cdb2`):
    merge, CI del push a main, erratas de GLM y contadores.
  - **Auditoria `from-glm/0431`** (CONFORME) archivada en `08bdc41`: blob
    `fd52d83d71151e66c2beb953ba1b2b7291caad05`, **3 848 B / 73 l / CR=0 /
    ASCII**, sha256 `9986df210b4cf68805094fff234975268331569509105eaa5505f403338bdfef`;
    ferry `files/0431/` con 10 ficheros, `sha256sum -c` 8/8 OK. Retracta
    la errata C++ 231/232 (clon local confirma 256/257), cancela la deuda
    de documentacion, consigna `DensityVolume.kt:24` /
    `AtmoShaderConstants.kt:8` y declara las ediciones del acta 0430.
  - **Ack `to-glm/0439`** publicado en `84d22b5` (blob
    `4872269799c78259ebb76af88efda33687c91656`, **3 722 B / 69 l / CR=0 /
    ASCII**, sha256 `defe007cc4f30c21c7a640ac21d3395e4e309587e265f7e1d96853a2913c2ec3`):
    aceptacion de las rectificaciones, errata de mi evidencia 03 (`paul.h`
    -> `VolumeRenderer.h:57`, evidencia publicada NO reescrita) y
    declaracion de ubicacion del handoff (`C:\Users\unknown\Desktop\ionosphere-live-3d\HANDOFF_ACTIVE.md`,
    `HANDOFF_LOG.md`, `scripts/handoff.py`; sin versionar, no pusheados).
  - **Erratas registradas (no bloqueantes)**: job `136531605890` del relay
    pertenece al run anterior `44806450830`; jobs reales del head:
    `136589457552`/`136589457838`/`136589458062`. Deriva de linea del espejo
    C++ (RETRACTADA por GLM: los comentarios Kotlin citan 256/257 correctamente).
    **Errata de lineas en el acta 0430** (consignada aqui, acta archivada
    intacta): el acta cita `VOL_ALT_MIN_KM` en `DensityVolume.kt:15` (real
    **24**) y `AtmoShaderConstants.PLANET_R` en `:9` (real **8**); los
    **literales** si coinciden (`60.0`, `1.0f`) y la aritmetica cuadra.
  - **Sin relay de gate pendiente**: R4.6.5 pasado y mergeado; solo falta el
    cierre formal del acta `0437` recibido y archivado.
- **Relay vigente anterior**: `GLM-MESSAGE-20261120-163450.md` (**5 300 B / 106
  lineas / CR=0 / ASCII**): acuse post-merge de R4.6.7 (merge `3a44ac5`, arbol
  identico, CI 3/3 con linea, `0427` archivada en `827b72c`, rama conservada)
  y peticion de acuse de recepcion + conformidad con la forma del merge.
  Transportado al relay como `to-glm/0435` (`d30f6d7`). **Respondido por GLM con el
  acta `0428`** (conformidad plena; archivada en `5dcd661`), acusada en
  `to-glm/0436` (`8db6be4`) y **verificada de campo por GLM en su ronda 7 con
  cero discrepancias**. **Sin relay pendiente** para R4.6.7.
- Anterior: `GLM-MESSAGE-20261120-125441.md` (acuse de erratas + pin del 0.75 +
  delta literal + FPS), publicado como `to-glm/0434` (`1c2aa15`).
- Acta de review de la implementacion: `from-glm/0422` (blob `b3a32335...`,
  4 158 B, 86 lineas, CR=0) en `c30760e`; canonica en `files/0422/`.

---

## Sesion de OpenCode

- Session ID: `ses_ef515a973ffeXydCVmjLVzEjgR`
- Titulo: `DEEPSEEK V4.1 FLASH`
- Directorio: `C:/Users/unknown/Desktop/ionosphere-live-3d`
- Modelo: `opencode/deepseek-v4.1-flash-free`

---

## Prompt listo para el siguiente agente

> Lee `AGENTS.md` y ejecuta `python scripts/handoff.py status` (ojo: informa del
> clon viejo `ionosphere-live-3d-android`; el estado real esta en este fichero).
> Proyecto `C:/Users/unknown/Desktop/ionosphere-live-3d`.
>
> **Android `main` = `3a44ac5`** (merge commit del PR #56). **R4.6.7 (iso-bands)
> esta FUSIONADA** con doble GO completo: acta `0427` FAVORABLE de GLM (archivada
> verbatim en el relay `827b72c`, blob `41d437e1`, 12 297 B / 253 l / CR=0) + GO
> escrito de David. Tree del merge `9aea9a89` **identico** al head `866f196`; 5
> ficheros +86/-1; CI de `main` run `44763826045` **3/3** con linea
> `RESUMEN-TESTS ficheros=51 tests=326 fallos=0`; rama `feat/r4.6.7-iso-bands`
> conservada; gate en el PR `#issuecomment-7043448129`.
>
> **Acuse `to-glm/0435`** publicado en el relay (`d30f6d7`, 4 286 B / 86 lineas /
> CR=0 / ASCII) con las cifras del merge y del CI. GLM respondio con el acta
> **`0428`** (acuse y **conformidad plena**, verificacion de primera mano:
> `827b72c` antes del gate y del merge, merge impecable, sitios protegidos
> intactos, CI 3/3 de `main`), archivada verbatim en **`5dcd661`** (blob
> `27995249`, 6 543 B / 128 l / CR=0 / ASCII, zip 241 370 B sha256 `587a7b48`,
> 16 miembros, `sha256sum -c` 14/14 OK, `cmp` identico), y acusada en
> `to-glm/0436` (`8db6be4`). **GLM verifico el 0436 de campo en su ronda 7
> (clones frescos, cero discrepancias, 7 anclajes de continuidad) sin emitir
> acta: el `0429` queda reservado para el dictamen de R4.6.3.** **R4.6.7 cerrada
> en ambos lados; sin relay pendiente.**
>
> **R4.6.3 (density + chapman) CERRADA**: PR **#57** fusionado en `main` como
> merge commit **`1646d56127f5ea81335d6eebc018385b537c4c3a`** (tree
> `6df04fb7048c0fdec9b8f476166694f19a5a18ee` identico al head `9b801b4`;
> rama `feat/r4.6.3-density-chapman` conservada). Doble GO: acta `0430`
> FAVORABLE de GLM sobre `9b801b4` + GO de David. CI del push a `main` run
> `44833939080` **3/3** con `RESUMEN-TESTS ficheros=54 tests=353 fallos=0` en
> job `136625490970` (23:04:58Z).
>
> Bateria del head `9b801b4`: tests 353/0/0 (54 ficheros), assemble OK, lint
> OK, CI run `44823314882` 3/3 (jobs `136589457552`/`136589457838`/
> `136589458062`; el id `136531605890` era del run anterior, errata de GLM).
> Fix R1: `innerRadius = PLANET_R + 60/6371 = 1.00942`, espejo C++
> `VolumeRenderer.cpp:256/257` (confirmado por clon local GLM, sin deuda de doc); pines
> G7c_10/11/12.
>
> Relay de cierre: acta `0429` (condicionada, `f111b00`), acta `0430`
> (FAVORABLE, `f41dfe5`, blob `03f7163a632e6daee07ff0601844654af17c921e`,
> 4 593 B / 85 l / ASCII, ferry files/0430/ con 10 ficheros, sha256sum -c
> 8/8 OK), acuse `to-glm/0437` y ack de cierre `to-glm/0438` (`e1c4e20`,
> blob `91163471ccfe201151db7cd0dc1fea7f6714b0ce`, 4 422 B / 83 l / ASCII,
> sha256 `f244c6c2b761334e3cff67a70dcd6df1ffa614cca632c40a6b823303f3d8cdb2`).
>
> Relay `HEAD = origin/main = d6c6e80`, limpio, arbol **1097**, `from-glm`
> **257/489** (`files/` **232**), `to-glm` **272/607** (`files/` **335**).
> Libres: `from-glm` **0435**, `to-glm` **0443**.
>
> **R4.6.5 v3: PASS VISUAL** (`f3079d9`, PR #58). Acta `0435` (codigo
> CONFORME) + ferry 20/20 + `to-glm/0443`. Franja viridis en [1.018R,1.078R]
> = 7.85 % del radio SOLO en ON; ON/OFF 10.4x/9.1x el control OFF/OFF;
> 0 FATAL. Caida de FPS ON ~18.7 vs OFF ~27.7 declarada. Relay `85d856a`.
> **CERRADO**: acta `0436` FAVORABLE + GO de David -> PR #58 mergeado
> (`234eab4`), CI main 3/3; **R4.6.5 CERRADA** por acta `0437` (`bdaf623`).
> **NO cambiar** las citas C++ de los comentarios Kotlin (256/257 correctos;
> GLM retracto su errata 231/232 en el acta 0431).
>
> **FPS de referencia**: medido sobre `866f196` en `emulator-5554` (software GL,
> 610x704): **4.80 y 5.52 fps** por `SurfaceFlinger --latency`; referencia de
> emulador, no gate. El gate de shader link sigue **NO PROBADO** (sin
> `glslangValidator`/`glslc`).
>
> Reglas: sin codigo sin GO; push y merge con GO escrito de David y registrado en
> PR o relay; un slice = un PR; no `data/**`, `.github/**` ni checker
> `343c8104...`; mutantes sin tocar ficheros en caliente y exigiendo
> `RESTAURADO todo=True`; documentos de GLM verbatim y sin renumerar; lo
> publicado no se reescribe (errata = documento nuevo).
