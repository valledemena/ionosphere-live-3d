# DW.9 PRERREGISTRO - C++: extraer el par IRTAM del bracket de App.cpp

Materia: siguiente (y probablemente ltima) cosecha mecanica de
`src/App.cpp` en el repo C++ (`valledemena/ionosphere-live-3d`), tras
DW.8 cerrada en `87313c5`. **Solo pre-registro: sin codigo de
aplicacion tocado.**

## 0. Gobernanza

- GLM recomendo la opcion (a) en el acta `0480` a peticion del hub;
  David decidio aceptarla y abrir este pre-registro. Cadena: GLM
  recomienda a peticion, David decide, OpenCode pre-registra sin tocar
  codigo, GLM audita, OpenCode ejecuta tras acta + GO, GLM audita la
  ejecucion.
- Checklist del acta `0473` seccion 9 aplicado. **Leccion DW.8
  aplicada**: la nota de lectura del acumulado se calcula sobre el
  PLAN REAL del bloque, no sobre un literal pre-errata.

## 1. Change

- Baseline: `origin/master = 87313c5325d2a046da359061c8c259b0664a9245`.
- Rama: `dw.9-irtam-extract`, creada desde la baseline. Head del
  pre-registro: el commit de este documento (se publica en el relay).
- Worktree limpio separado:
  `C:\Users\unknown\Desktop\ionosphere-dw9-cpp`.
- Bloque: `src/App.cpp`, **dos segmentos no contiguos**, 35 lineas:
  - S1 `585..602` (18 lineas): 4 lineas de comentario (585-588) +
    `evalTovGrid` (589-602, 14 lineas).
  - S2 `720..736` (17 lineas): 3 lineas de comentario (720-722) +
    `lerpBracketGrid` (723-736, 14 lineas).
  - Entre S1 y S2 queda intacto `sampleIrtamBracketWorker` (604-718).
- Se retiran `585..603` (19) y `720..737` (18) = **37 lineas**
  (bloque + blank adyacente en cada caso); blanks unicos entre vecinos.
- Se mueven literalmente las 35 lineas a `src/Utils/IrtamBracket.h`.
- Se anade **una** linea `#include "Utils/IrtamBracket.h"` tras la
  linea 2, nueva linea 3. Sin otra edicion en `App.cpp`.

## 2. Andamiaje enumerado (regla del acta 0465 seccion 2)

`src/Utils/IrtamBracket.h` con **H = 47 = 35 lineas movidas + 12 de
andamiaje (S=12)**: 1 guarda + 1 blank + 8 includes directos + 1
blank separador entre segmentos = 11 lineas de esqueleto antes de S1
(mas 1 blank entre S1 y S2). Esquema:

```
  1 [guarda]:  #pragma once
  2 [blank]
  3 [include]: #include <cmath>            (std::floor)
  4 [include]: #include <string>           (std::string)
  5 [include]: #include <vector>           (std::vector<float>&)
  6 [include]: "Data/IrtamCoeffCache.h"    (irtamccache::Bucket/loadBucket)
  7 [include]: "Data/IrtamCoeffParse.h"    (irtamcoeff::Message/parseMessage/
                                          isStandardBasis)
  8 [include]: "Data/IrtamGridEval.h"      (irtameval::evalGrid/kGridW/kGridH)
  9 [include]: "Data/IrtamState.h"         (irtamstate::lerpValues)
 10 [include]: "Ionosphere/GridLayer.h"    (struct GridData)
 11 [blank]
 12..29 [S1 18 lineas = App.cpp 585..602, byte-identicas]
 30 [blank separador]
 31..47 [S2 17 lineas = App.cpp 720..736, byte-identicas]
```

H = 11 (esqueleto) + 18 (S1) + 1 (separador) + 17 (S2) = **47**.
Cada include es dependencia DIRECTA verificada por `git grep -w`:
`std::floor` (<cmath>), `std::string` (<string>), `std::vector`
(<vector>), `irtamccache::Bucket/loadBucket` (IrtamCoeffCache.h),
`irtamcoeff::Message/parseMessage/isStandardBasis` (IrtamCoeffParse.h),
`irtameval::evalGrid/kGridW/kGridH` (IrtamGridEval.h),
`irtamstate::lerpValues` (IrtamState.h), `GridData` (GridLayer.h).
App.cpp ya incluye los ocho. `static` se conserva con un solo
includer; nota permanente del acta 0461 seccion 5 si un segundo
fichero incluyera el header.

## 3. Diffstat esperado y nota de lectura

- `src/App.cpp`: **+1 / -37** exacto.
- `src/Utils/IrtamBracket.h`: **+H / -0** (H se fija en el relay).
- `docs/DW.9-PRERREGISTRO.md`: +N / -0.
- PR acumulado desde `87313c5`: **3 ficheros, +(N+1+H)/-37** (H=47).
- **Nota de lectura (calculada sobre el plan real, ver seccion 0)**:
  el acumulado desde `e680d01` y desde `d9fb809` se audita contra el
  resultado real de la ejecucion, no contra un literal pre-errata.

## 4. Grep PRE y predicciones del POST

PRE (`87313c5`), `git grep -n -w -- src tests`:

- `evalTovGrid`: **2 lineas** - def 589 y uso 657 (dentro de
  `sampleIrtamBracketWorker`, que permanece en App.cpp).
- `lerpBracketGrid`: **5 lineas** - def 723 y usos 2241, 2265, 3920,
  3951.
- Total 7 lineas, todas en `src/App.cpp`; **0 en tests**; 0 en otros
  ficheros de `src/`.

Predicciones del POST (congeladas antes de la evidencia):

- Header S1 (`nueva = vieja - 573`): comentario 585 -> `:12`,
  def 589 -> `:16`, fin 602 -> `:29`.
- Header S2 (`nueva = vieja - 689`): comentario 720 -> `:31`,
  def 723 -> `:34`, fin 736 -> `:47`.
- App.cpp: desplazamiento neto = +1 - 19 (S1) = -18 para lineas
  posteriores a 603, y -18 - 18 = -36 para posteriores a 737.
  Uso `evalTovGrid` 657 -> **639**; usos `lerpBracketGrid`
  2241 -> **2223**, 2265 -> **2247**, 3920 -> **3892**,
  3951 -> **3923**.
- Totales 2/5 sin cambio; 0 usos nuevos en tests.

La evidencia muestra listado lineado completo pre/post y tabla
prediccion/observado.

## 5. Anclajes re-medidos (baseline `87313c5`, worktree limpio)

- Configure rc=0; `cmake --build build --parallel 4`: rc=0 (290 pasos).
- `ctest --test-dir build --output-on-failure`: rc=0, **54/54** sin
  skipped (fixtures ebro gitignored copiados al worktree, fuera del
  commit); `ctest -N` = 54.
- Anclaje dual (acta `0463` seccion 2) exigido sin cambio: local 54/54;
  CI 53 passed + 1 skipped (`ebro_parse`, exit 77). 54 slots / 0 fallos.
- CI repo C++ dos jobs (`ctest` + ThreadSanitizer); PR 2/2 y push 2/2.

## 6. Evidencia y entrega

Ferry `to-glm/files/DW.9/`: 01-grep-pre, 02-grep-post,
03-bloque-byte-identico, 04-header-enumerado, 05-numstat, 06-head,
07-predicciones-post, 08-build, 09-ctest, 10-status, MANIFEST y
SHA256SUMS (11 entradas, 11/11 OK, convencion acta `0466` seccion 3).
**Recomendacion 0479 aplicada**: los ficheros 08/09/10 se hashean
sobre los bytes post-normalizacion (LF) para evitar la dual-track
CRLF recurrente en DW.5-8.

Bundle delta `87313c5..<head del change>` con sha256 publicado y
`git bundle verify`; entregado en `upload/` **y en el relay**
`to-glm/files/DW-BUNDLES/` (delegacion de David, to-glm/0486).

PR a `master` con checklist; rama conservada; **sin amend ni
force-push** (regla `0455`).

## 7. Gate

1. GLM emite acta de conformidad sobre el head del pre-registro.
2. David da GO para ejecutar el slice.
3. OpenCode ejecuta, mide anclajes, cierra grep, genera bundle y ferry.
4. GLM emite acta de conformidad de ejecucion.
5. David da GO para el PR y el merge a master (protegido) con CI 2/2.

Sin ejecucion de codigo antes de los puntos 1 y 2.
