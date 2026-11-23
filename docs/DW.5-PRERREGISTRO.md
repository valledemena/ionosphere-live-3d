# DW.5 PRERREGISTRO - C++: extraer mergeDias/mergeKc2g de App.cpp

Materia: siguiente slice de descomposicion del monolito `src/App.cpp` en el
repo C++ (`valledemena/ionosphere-live-3d`), tras DW.4 cerrada en
`3716295`. Solo pre-registro: **sin codigo de aplicacion tocado**.

## 0. Gobernanza

- GLM recomendo C++ / siguiente extraccion de `App.cpp` en el acta `0464`
  seccion 4, registrada **como recomendacion**, no decision.
- David decidio C++ / `App.cpp` en este turno. Su GO al pre-registro
  ratifica la decision. Cadena: GLM recomendo, David decidio, OpenCode
  ejecuta (tras GO), GLM audita.
- Alternativas quedan registradas y auditables (web `IonosphereScene.tsx`
  con ancla 48/461/0, o slice de higiene que anada CI al repo web).

## 1. Change

- Baseline: `origin/master = 3716295604e9f5446d5afa3160953674b3792f82`
  (merge PR #16, estado tras DW.4).
- Rama: `dw.5-station-merge`, creada desde la baseline. Head del
  pre-registro: el commit de este documento (se publica en el relay).
- Worktree limpio separado:
  `C:\Users\unknown\Desktop\ionosphere-dw5-cpp`. El workspace raiz no
  se usa (docs de handoff modificados).
- Bloque: `src/App.cpp` **lineas 567..613** (47 lineas) = comentario de
  `mergeDias` (567-569), `mergeDias` (570-592), blank (593), comentario
  de `mergeKc2g` (594-597), `mergeKc2g` (598-613).
- Se retiran **567..614** (48 lineas: bloque + blank adyacente 614),
  mismo patron de DW.3/DW.4. Queda unico blank separador en 566.
- Se mueven literalmente las 47 lineas a `src/Utils/StationMerge.h`.
- Se anade **una** linea `#include "Utils/StationMerge.h"` tras la
  linea 2 (`#include "Utils/FieldFormat.h"`), nueva linea 3.
- Sin ninguna otra edicion en `App.cpp`.

## 2. Andamiaje enumerado (S=5, H=52)

`src/Utils/StationMerge.h` queda con **H=52 lineas = 47 movidas + S=5**:

```
 1 [#pragma once]:              #pragma once
 2 [blank]:                     (vacia)
 3 [include]:                   #include <vector>
 4 [include]:                   #include "Data/GiroAdapter.h"
 5 [blank]:                     (vacia)
 6..52 [bloque movido]:         App.cpp 567..613, byte-identicas
```

Justificacion de los includes declarados (pendiente de aceptacion de
GLM: la lista admisible del acta 0461 seccion 3 era especifica de
`FieldFormat.h` y no incluye estos dos):

- `<vector>`: `std::vector<StationSample>` en firmas y cuerpo.
- `"Data/GiroAdapter.h"`: define `struct StationSample` (linea 28),
  usado **por valor** con acceso a campos; es el mismo header que
  `App.cpp` ya incluye en su linea 51; no introduce ciclo (solo incluye
  `<string>`, `<vector>`, `<map>`, `<mutex>`, `<atomic>`).
- `static` se mantiene tal cual: un solo includer (`App.cpp`). Si un
  segundo fichero llegara a incluir el header, se decidiria `inline` o
  namespace en ese momento (nota del acta 0461 seccion 5).

## 3. Diffstat esperado y forma de movimiento

- `src/App.cpp`: **+1 / -48** exacto (include anadido; bloque 567-614
  retirado). Cualquier otra edicion: fallo del criterio.
- `src/Utils/StationMerge.h`: **+52 / -0**, H=52 enumeradas en la
  seccion 2.
- `docs/DW.5-PRERREGISTRO.md`: +N / -0 (N se declara en el relay).
- PR acumulado desde `3716295`: **3 ficheros, +(N+1+52)/-48**.
- Forma de movimiento: 47 lineas anadidas en el header frente a 48
  borradas en el monolito (47 + 1 blank), sin logica nueva mezclada.

## 4. Cierre grep por simbolo

PRE (`3716295`), `git grep -n -w -E "mergeDias|mergeKc2g" -- src tests`:

- `mergeDias`: **3 lineas** - 570 (definicion), 597 (comentario),
  2528 (uso). Todas en `src/App.cpp`.
- `mergeKc2g`: **4 lineas** - 598 (definicion), 2353, 2457, 4197
  (usos). Todas en `src/App.cpp`.
- Total 7 lineas; **0 en tests**; 0 en otros ficheros de `src/`.

POST (head del change): mismos totales 3/4 sin cambio; definiciones
reubicadas a `src/Utils/StationMerge.h`; usos permanecen en `App.cpp`
(desplazados -47 lineas por la retirada neta); **0 en tests** en ambos
estados. La evidencia muestra el listado completo lineado pre/post,
no solo ventanas, con el recuento por simbolo.

## 5. Anclajes re-medidos (baseline `3716295`, worktree limpio)

Medidos por ejecucion, no estimados, sobre la baseline exacta:

- `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ...`: rc=0.
- `cmake --build build --parallel 4`: **rc=0 (290 pasos)**.
- `ctest --test-dir build --output-on-failure`: **rc=0, 100% tests
  passed out of 54, sin skipped** (fixtures `tests/fixtures/ebro/*.SAO`
  gitignored copiados al worktree, fuera del commit).
- `ctest -N`: **54**.

**Anclaje dual C++ permanente** (acta `0463` seccion 2), exigido sin
cambio tras el slice: local 54/54 con fixtures presentes; CI 53 passed
+ 1 skipped (`ebro_parse`, exit 77, fixtures gitignored ausentes).
54 slots, 0 fallos en ambos entornos; la forma "54/54" sola no basta.
No se reutiliza el ancla web 48/461/0.

CI del repo C++: `.github/workflows/ci.yml`, dos jobs (`ctest` y
`CI Ubuntu (ThreadSanitizer, tests con transporte inyectado)`). El PR
debe traer **2/2 success**; se verifica ademas el CI del push a master.

## 6. Evidencia y entrega

Ferry `to-glm/files/DW.5/` con: `01-grep-pre.txt`, `02-grep-post.txt`,
`03-bloque-byte-identico.txt`, `04-header-enumerado.txt`,
`05-numstat.txt`, `06-head.txt`, `07-build.txt`, `08-ctest.txt`,
`09-status.txt`, `MANIFEST.md`, `SHA256SUMS` (sha256sum -c 11/11 OK).

Bundle delta `3716295..<head del change>` en
`INTERCAMBIO/work/dw5-cpp-3716295-<head>.bundle`, sha256 publicado y
verificado con `git bundle verify`. Mecanismo (acta `0452` B): OpenCode
genera y publica sha256, David entrega manualmente a `upload/`, GLM
verifica a la llegada.

PR a `master` con el checklist completo en el cuerpo; rama conservada;
**sin amend ni force-push** (regla del acta `0455`; los sha publicados
no se reescriben).

## 7. Gate

1. GLM emite acta de conformidad sobre el head del pre-registro.
2. David da GO para ejecutar el slice.
3. OpenCode ejecuta, mide anclajes y grep, genera bundle y ferry.
4. GLM emite acta de conformidad de ejecucion.
5. David da GO para el PR y el merge a master (protegido) con CI 2/2.

Sin ejecucion de codigo antes de los puntos 1 y 2.
