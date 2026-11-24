# DW.8 PRERREGISTRO - C++: extraer la familia de interpolacion IDW de App.cpp

Materia: siguiente slice de descomposicion del monolito `src/App.cpp`
en el repo C++ (`valledemena/ionosphere-live-3d`), tras DW.7 cerrada en
`38ef6e3`. **Solo pre-registro: sin codigo de aplicacion tocado.**

## 0. Gobernanza

- David pidio expresamente recomendacion; GLM la emitio en el acta
  `0476`: opcion (a) C++ `App.cpp`, familia de interpolacion IDW de
  estaciones, registrada **como recomendacion**. David decidio aceptarla
  y abrir este pre-registro. Cadena: GLM recomienda a peticion, David
  decide, OpenCode pre-registra sin tocar codigo, GLM audita, OpenCode
  ejecuta tras acta + GO, GLM audita la ejecucion.
- Checklist consolidado del acta `0473` seccion 9 aplicado: NA=0 en el
  documento, bloque literal anatomizado, andamiaje justificado,
  grep PRE lineado, predicciones del POST congeladas aqui, anclajes
  re-medidos, anclaje dual, ferry, bundle y CI 2/2.

## 1. Change

- Baseline: `origin/master = 38ef6e345bdf25711f177c3ec0d6b5c0dd93c90b`
  (merge PR #19, estado tras DW.7).
- Rama: `dw.8-idw-extract`, creada desde la baseline. Head del
  pre-registro: el commit de este documento (se publica en el relay).
- Worktree limpio separado:
  `C:\Users\unknown\Desktop\ionosphere-dw8-cpp`. El workspace raiz no
  se usa (docs de handoff modificados).
- Familia IDW = **223 lineas en TRES segmentos no contiguos** de
  `src/App.cpp`:
  - S1 `570..631` (62 lineas): comentario + `giroInterpGrid` (575-631).
  - S2 `646..705` (60 lineas): comentario + `radioInterpGrid` (650-705).
  - S3 `707..807` (101 lineas): comentario + `interpLayerProfiles`
    (714-807), incluye la lambda local `medOf` (726-735).
- **Entre S1 y S2 queda intacto** `HelpMarker` (633-644, UI ImGui): no
  es IDW y no se toca. `interpLayerProfiles` empieza en 707 (el blank
  706 se conserva separando).
- Se retiran `570..632`, `646..706` y `707..808` (63+61+102 = 226
  lineas: cada bloque mas su blank adyacente). Queda un unico blank
  entre cada vecino.
- Se mueven literalmente las 223 lineas a `src/Utils/IdwInterp.h`
  (los tres segmentos, en ese orden, separados por un blank).
- Se anade **una** linea `#include "Utils/IdwInterp.h"` tras la linea 2
  (`#include "Utils/FieldFormat.h"`), nueva linea 3.
- Sin ninguna otra edicion en `App.cpp`.

## 2. Andamiaje enumerado (regla del acta 0465 seccion 2)

`src/Utils/IdwInterp.h` queda con **H = 223 + S**, con S=12:

```
  1 [guarda]:  #pragma once
  2 [blank]
  3 [include]: #include <algorithm>      (std::sort)
  4 [include]: #include <cmath>          (sqrtf, isfinite, sqrt)
  5 [include]: #include <vector>
  6 [include]: #include <glm/glm.hpp>    (glm::vec3)
  7 [include]: "Data/GiroAdapter.h"      (struct StationSample)
  8 [include]: "Data/RadioPropagationAdapter.h"  (stationValue, linea 38)
  9 [include]: "Ionosphere/GridLayer.h"  (struct GridData)
 10 [include]: "Ionosphere/LayerProfile.h" (LayerProfile, sanitizeB0/B1,
                                          usableFo, havDeg, smoothstep,
                                          dayOfYear, modelFoF2/HmF2)
 11 [include]: "Ionosphere/DensityVolume.h" (densityDir, linea 60)
 12 [blank]
 13..  [S1 62 lineas = App.cpp 570..631, byte-identicas]
  +1 blank
  [S2 60 lineas = App.cpp 646..705, byte-identicas]
  +1 blank
  [S3 101 lineas = App.cpp 707..807, byte-identicas]
```

Justificacion: cada include es dependencia DIRECTA del bloque movido,
verificada por ejecucion (`git grep -w`): las funciones auxiliares
`sanitizeB0/B1`, `usableFo`, `havDeg`, `smoothstep`, `dayOfYear`,
`modelFoF2/HmF2` son `inline` en `LayerProfile.h`; `densityDir` es
`inline` en `DensityVolume.h`; `stationValue` es metodo de
`RadioPropagationAdapter` (header); `giroValueOf` ya vive en
`Utils/GiroHelpers.h`, **que se incluye explicitamente ademas** como
dependencia directa. `static` se conserva: un solo includer (`App.cpp`).
Nota de higiene permanente (acta 0461 seccion 5): si un segundo fichero
llega a incluir el header, decidir `inline` o namespace.

## 3. Diffstat esperado

- `src/App.cpp`: **+1 / -226** exacto (include anadido; 63+61+102
  lineas retiradas). Cualquier otra edicion: fallo.
- `src/Utils/IdwInterp.h`: **+H / -0**, con H = 223 + 12 + 2 blanks
  separadores entre segmentos = **237 lineas** (si GLM admite los dos
  blanks separadores; en caso contrario se declara la desviacion y se
  enumera). Cada linea enumerada en `04-header-enumerado.txt`.
- `docs/DW.8-PRERREGISTRO.md`: +N / -0 (N se declara en el relay).
- PR acumulado desde `38ef6e3`: **3 ficheros, +(N+1+H)/-226**.

## 4. Cierre grep por simbolo y predicciones del POST

PRE (`38ef6e3`), `git grep -n -w <simbolo> -- src tests`:

- `giroInterpGrid`: 11 lineas (def 575; comentarios 373 y 707; usos
  2439, 2545, 2546, 4108 en App.cpp; 4 lineas en HFTraceLayer.h
  que son comentarios).
- `radioInterpGrid`: 6 lineas (def 650; usos 2449, 2547, 4125 en
  App.cpp; 2 en HFTraceLayer.h, comentarios).
- `interpLayerProfiles`: 4 lineas (def 714; uso 2619 en App.cpp;
  1 en VolumeBuildKey.h y 1 en tests/test_model_foF2.cpp, ambos
  comentarios).

Predicciones lineadas del POST fijadas ANTES de ver la evidencia: el
cuerpo de los tres segmentos se mueve byte-identico al header; las
**definiciones** desaparecen de `App.cpp` y aparecen en `IdwInterp.h`;
los **comentarios y usos** de `App.cpp` se desplazan: -63 para todo lo
posterior a 632, -61 adicional para lo posterior a 706. En particular:

- Uso `giroInterpGrid` 2439 -> **2315**; 2545 -> **2421**; 2546 ->
  **2422**; 4108 -> **3984**.
- Uso `radioInterpGrid` 2449 -> **2325**; 2547 -> **2423**; 4125 ->
  **4001**.
- Uso `interpLayerProfiles` 2619 -> **2495**.
- Totales por simbolo sin cambio (11/6/4), 0 usos nuevos en tests.

La evidencia muestra el listado completo lineado pre/post con tabla
prediccion/observado y el desglose de comentarios frente a usos.

## 5. Anclajes re-medidos (baseline `38ef6e3`, worktree limpio)

- Configure rc=0; `cmake --build build --parallel 4`: **rc=0 (290
  pasos)**.
- `ctest --test-dir build --output-on-failure`: **rc=0, 100% passed
  out of 54, sin skipped** (fixtures ebro gitignored copiados al
  worktree, fuera del commit).
- `ctest -N`: **54**.
- Anclaje dual C++ (acta `0463` seccion 2), exigido sin cambio: local
  54/54 con fixtures; CI 53 passed + 1 skipped (`ebro_parse`, exit
  77). 54 slots / 0 fallos. No se reutiliza el 48/461/0 web.
- CI del repo C++ (`ci.yml`): dos jobs (`ctest` y ThreadSanitizer). PR
  con 2/2 success y ademas CI del push a master.

## 6. Evidencia y entrega

Ferry `to-glm/files/DW.8/` con: `01-grep-pre.txt`, `02-grep-post.txt`,
`03-bloque-byte-identico.txt`, `04-header-enumerado.txt`,
`05-numstat.txt`, `06-head.txt`, `07-predicciones-post.txt`,
`08-build.txt`, `09-ctest.txt`, `10-status.txt`, `MANIFEST.md`,
`SHA256SUMS` (11 entradas, 11/11 OK, convencion acta `0466` seccion 3).

Bundle delta `38ef6e3..<head del change>` con sha256 publicado,
verificado con `git bundle verify`, entregado en `upload/` **y en el
relay** `to-glm/files/DW-BUNDLES/` (delegacion de David, to-glm/0486).

PR a `master` con el checklist completo; rama conservada; **sin amend
ni force-push** (regla del acta `0455`).

## 7. Gate

1. GLM emite acta de conformidad sobre el head del pre-registro.
2. David da GO para ejecutar el slice.
3. OpenCode ejecuta, mide anclajes, cierra grep, genera bundle y ferry.
4. GLM emite acta de conformidad de ejecucion.
5. David da GO para el PR y el merge a master (protegido) con CI 2/2.

Sin ejecucion de codigo antes de los puntos 1 y 2.
