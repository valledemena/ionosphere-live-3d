# DW.6 PRERREGISTRO - C++: extraer giroPointsFrom/giroValueOf de App.cpp

Materia: siguiente slice de descomposicion del monolito `src/App.cpp` en el
repo C++ (`valledemena/ionosphere-live-3d`), tras DW.5 cerrada en
`b392e68`. Solo pre-registro: **sin codigo de aplicacion tocado**.

## 0. Gobernanza

- David decidio C++ / `App.cpp` como direccion de DW.6. No pidio
  recomendacion a GLM (acta 0470 seccion 5: la emitira solo a peticion).
  Cadena: David decidio, OpenCode pre-registra sin tocar codigo, GLM
  audita, OpenCode ejecuta tras acta + GO, GLM audita la ejecucion.
- Alternativas quedan registradas y auditables (web
  `IonosphereScene.tsx` con ancla 48/461/0).

## 1. Change

- Baseline: `origin/master = b392e68d8987b0a200f7efb08f78856ef678c069`
  (merge PR #17, estado tras DW.5).
- Rama: `dw.6-appcpp-grid`, creada desde la baseline. Head del
  pre-registro: el commit de este documento (se publica en el relay).
- Worktree limpio separado:
  `C:\Users\unknown\Desktop\ionosphere-dw6-cpp`. El workspace raiz no
  se usa (docs de handoff modificados).
- Bloque: `src/App.cpp` **lineas 568..608** (41 lineas) = comentario de
  `giroPointsFrom` (568), `giroPointsFrom` (569-598), blank (599),
  comentario de `giroValueOf` (600), `giroValueOf` (601-608).
- Se retiran **568..609** (42 lineas: bloque + blank adyacente 609),
  mismo patron de DW.3/DW.4/DW.5. Queda unico blank separador en 567.
- Se mueven literalmente las 41 lineas a `src/Utils/GiroHelpers.h`.
- Se anade **una** linea `#include "Utils/GiroHelpers.h"` tras la
  linea 2 (`#include "Utils/FieldFormat.h"`), nueva linea 3.
- Sin ninguna otra edicion en `App.cpp`.

## 2. Andamiaje enumerado (S=6, H=47), regla del acta 0465 seccion 2

`src/Utils/GiroHelpers.h` queda con **H=47 lineas = 41 movidas + S=6**:

```
 1 [guarda]:         #pragma once
 2 [blank]:          (vacia)
 3 [include directo]: #include <vector>
 4 [include directo]: #include "Data/GiroAdapter.h"
 5 [include directo]: #include "Ionosphere/IonosondeLayer.h"
 6 [blank]:          (vacia)
 7..47 [bloque]:     App.cpp 568..608, byte-identicas
```

Justificacion de cada dependencia directa:

- `<vector>`: `std::vector<StationPoint>` / `std::vector<StationSample>`
  en firmas y cuerpo de `giroPointsFrom`.
- `"Data/GiroAdapter.h"`: define `struct StationSample` (linea 28),
  usado por valor con acceso a campos en `giroValueOf`.
- `"Ionosphere/IonosondeLayer.h"`: define `struct StationPoint`
  (linea 9), usado por valor y con acceso a campos en `giroPointsFrom`.
  App.cpp ya incluye los tres (lineas 18, 45 y 52); la doble inclusion
  es inofensiva por las guardas. Sin ciclo declarado.
- `static` se conserva con un solo includer (`App.cpp`). Si un segundo
  fichero llega a incluir el header, decidir `inline` o namespace
  (nota permanente del acta 0461 seccion 5).

## 3. Diffstat esperado y forma de movimiento

- `src/App.cpp`: **+1 / -42** exacto (include anadido; bloque 568-609
  retirado). Cualquier otra edicion: fallo del criterio.
- `src/Utils/GiroHelpers.h`: **+47 / -0**, H=47 enumeradas en la
  seccion 2.
- `docs/DW.6-PRERREGISTRO.md`: +N / -0 (N se declara en el relay).
- PR acumulado desde `b392e68`: **3 ficheros, +(N+1+47)/-42**.
- Forma de movimiento: 41 lineas anadidas en el header frente a 42
  borradas en el monolito (41 + 1 blank), sin logica nueva.

## 4. Cierre grep por simbolo y predicciones del POST

PRE (`b392e68`), `git grep -n -w -E "giroPointsFrom|giroValueOf" -- src tests`:

- `giroPointsFrom`: **4 lineas** - 569 (definicion), 2245, 2478, 2481
  (usos). Todas en `src/App.cpp`.
- `giroValueOf`: **3 lineas** - 601 (definicion), 637 y 659 (usos
  dentro de `giroInterpGrid`, que permanece en App.cpp). Todas en
  `src/App.cpp`.
- Total 7 lineas; **0 en tests**; **0 en otros ficheros de src/**.

Predicciones lineadas del POST, fijadas ANTES de ver la evidencia:

- Mapeo al header `nueva = vieja - 561`:
  - comentario de `giroPointsFrom` (vieja 568) -> `GiroHelpers.h:7`.
  - `giroPointsFrom` def (vieja 569) -> `GiroHelpers.h:8`.
  - comentario de `giroValueOf` (vieja 600) -> `GiroHelpers.h:39`.
  - `giroValueOf` def (vieja 601) -> `GiroHelpers.h:40`.
  - fin del bloque (vieja 608) -> `GiroHelpers.h:47`.
- Desplazamiento en `App.cpp`: **-41 neto** (+1 include, -42 retirada)
  para todo lo posterior a 609:
  - usos de `giroValueOf`: 637 -> **596**, 659 -> **618**.
  - usos de `giroPointsFrom`: 2245 -> **2204**, 2478 -> **2437**,
    2481 -> **2440**.
- Totales por simbolo sin cambio: 4 / 3; 0 en tests en ambos estados.
  La evidencia muestra el listado completo lineado pre/post y una
  tabla prediccion/observado.

## 5. Anclajes re-medidos (baseline `b392e68`, worktree limpio)

- Configure rc=0; `cmake --build build --parallel 4`: **rc=0 (290
  pasos)**.
- `ctest --test-dir build --output-on-failure`: **rc=0, 100% passed
  out of 54, sin skipped** (fixtures `tests/fixtures/ebro/*.SAO`
  gitignored copiados al worktree, fuera del commit).
- `ctest -N`: **54**.
- Anclaje dual C++ permanente (acta `0463` seccion 2), exigido sin
  cambio: local 54/54 con fixtures; CI 53 passed + 1 skipped
  (`ebro_parse`, exit 77). 54 slots / 0 fallos en ambos. No se
  reutiliza el 48/461/0 web.
- CI del repo C++ (`ci.yml`): dos jobs (`ctest` y ThreadSanitizer).
  PR con 2/2 success y ademas CI del push a master.

## 6. Evidencia y entrega

Ferry `to-glm/files/DW.6/` con: `01-grep-pre.txt`, `02-grep-post.txt`,
`03-bloque-byte-identico.txt`, `04-header-enumerado.txt`,
`05-numstat.txt`, `06-head.txt`, `07-predicciones-post.txt`,
`08-build.txt`, `09-ctest.txt`, `10-status.txt`, `MANIFEST.md`,
`SHA256SUMS` (11 entradas, 11/11 OK; convencion del acta `0466` §3).

Bundle delta `b392e68..<head del change>` en
`INTERCAMBIO/work/dw6-cpp-b392e68-<head>.bundle`, sha256 publicado y
verificado con `git bundle verify`. Entrega en `upload/` por OpenCode
**por delegacion explicita de David** (cambio de actor registrado en
`to-glm/0486`), sin credenciales.

PR a `master` con el checklist completo en el cuerpo; rama conservada;
**sin amend ni force-push** (regla del acta `0455`).

## 7. Gate

1. GLM emite acta de conformidad sobre el head del pre-registro.
2. David da GO para ejecutar el slice.
3. OpenCode ejecuta, mide anclajes, cierra grep, genera bundle y ferry.
4. GLM emite acta de conformidad de ejecucion.
5. David da GO para el PR y el merge a master (protegido) con CI 2/2.

Sin ejecucion de codigo antes de los puntos 1 y 2.
