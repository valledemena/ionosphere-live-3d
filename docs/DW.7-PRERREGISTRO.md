# DW.7 PRERREGISTRO - C++: extraer blendGrids de App.cpp

Materia: siguiente slice de descomposicion del monolito `src/App.cpp` en el
repo C++ (`valledemena/ionosphere-live-3d`), tras DW.6 cerrada en
`ba8a4c1`. Solo pre-registro: **sin codigo de aplicacion tocado**.

## 0. Gobernanza

- GLM recomendo C++ / continuar la dieta de `App.cpp` en el acta `0473`
  seccion 9, a peticion expresa de David. David decidio C++ y abrir
  este pre-registro. Cadena: GLM recomendo (a peticion), David decidio,
  OpenCode pre-registra sin tocar codigo, GLM audita, OpenCode ejecuta
  tras acta + GO, GLM audita la ejecucion.
- Checklist consolidado del acta `0473` seccion 9 aplicado: NA=0 en el
  documento, bloque literal anatomizado, andamiaje justificado,
  grep PRE lineado, predicciones del POST congeladas aqui, anclajes
  re-medidos, anclaje dual, ferry, bundle y CI 2/2.

## 1. Change

- Baseline: `origin/master = ba8a4c1a89a970fb0a8084162f650089a09ac437`
  (merge PR #18, estado tras DW.6).
- Rama: `dw.7-appcpp-grid`, creada desde la baseline. Head del
  pre-registro: el commit de este documento (se publica en el relay).
- Worktree limpio separado:
  `C:\Users\unknown\Desktop\ionosphere-dw7-cpp`. El workspace raiz no
  se usa (docs de handoff modificados).
- Bloque: `src/App.cpp` **lineas 808..823** (16 lineas) = comentario de
  `blendGrids` (808) y la funcion `blendGrids` (809-823).
- Se retiran **808..824** (17 lineas: bloque + blank adyacente 824),
  mismo patron de DW.3 a DW.6. Queda unico blank separador en 807.
- Se mueven literalmente las 16 lineas a `src/Utils/GridBlend.h`.
- Se anade **una** linea `#include "Utils/GridBlend.h"` tras la
  linea 2 (`#include "Utils/FieldFormat.h"`), nueva linea 3.
- Sin ninguna otra edicion en `App.cpp`.

## 2. Andamiaje enumerado (S=5, H=21), regla del acta 0465 seccion 2

`src/Utils/GridBlend.h` queda con **H=21 lineas = 16 movidas + S=5**:

```
 1 [guarda]:         #pragma once
 2 [blank]:          (vacia)
 3 [include directo]: #include <glm/glm.hpp>
 4 [include directo]: #include "Ionosphere/GridLayer.h"
 5 [blank]:          (vacia)
 6..21 [bloque]:     App.cpp 808..823, byte-identicas
```

Justificacion de cada dependencia directa:

- `<glm/glm.hpp>`: `glm::clamp(f, 0.0, 1.0)` en el cuerpo (linea 817).
- `"Ionosphere/GridLayer.h"`: define `struct GridData` (linea 7), usado
  por valor y con acceso a campos en todo el cuerpo; es el mismo header
  que `App.cpp` ya incluye (linea 41). Sin ciclo declarado.
- `static` se conserva con un solo includer (`App.cpp`). Si un segundo
  fichero llega a incluir el header, decidir `inline` o namespace
  (nota permanente del acta 0461 seccion 5).

## 3. Diffstat esperado y forma de movimiento

- `src/App.cpp`: **+1 / -17** exacto (include anadido; bloque 808-824
  retirado). Cualquier otra edicion: fallo del criterio.
- `src/Utils/GridBlend.h`: **+21 / -0**, H=21 enumeradas en la
  seccion 2.
- `docs/DW.7-PRERREGISTRO.md`: +N / -0 (N se declara en el relay).
- PR acumulado desde `ba8a4c1`: **3 ficheros, +(N+1+21)/-17**.
- Forma de movimiento: 16 lineas anadidas en el header frente a 17
  borradas en el monolito (16 + 1 blank), sin logica nueva.

## 4. Cierre grep por simbolo y predicciones del POST

PRE (`ba8a4c1`), `git grep -n -w "blendGrids" -- src tests`:

- **2 lineas** - 808 (comentario, contiene el simbolo), 809 (definicion)
  y 2362 (uso). Total **3 lineas** (el comentario 808 tambien mencion
  el nombre); todas en `src/App.cpp`; **0 en tests**; 0 en otros
  ficheros de `src/`.

Predicciones lineadas del POST, fijadas ANTES de ver la evidencia:

- Mapeo al header `nueva = vieja - 802`:
  - comentario (vieja 808) -> `GridBlend.h:6`.
  - definicion (vieja 809) -> `GridBlend.h:7`.
  - fin del bloque (vieja 823) -> `GridBlend.h:21`.
- Desplazamiento en `App.cpp`: **-16 neto** (+1 include, -17 retirada)
  para todo lo posterior a 824: uso en 2362 -> **2346**.
- Totales por simbolo sin cambio: 3 (comentario + def + uso); 0 en
  tests. La evidencia muestra el listado completo lineado pre/post y
  tabla prediccion/observado.

## 5. Anclajes re-medidos (baseline `ba8a4c1`, worktree limpio)

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

Ferry `to-glm/files/DW.7/` con: `01-grep-pre.txt`, `02-grep-post.txt`,
`03-bloque-byte-identico.txt`, `04-header-enumerado.txt`,
`05-numstat.txt`, `06-head.txt`, `07-predicciones-post.txt`,
`08-build.txt`, `09-ctest.txt`, `10-status.txt`, `MANIFEST.md`,
`SHA256SUMS` (11 entradas, 11/11 OK; convencion del acta `0466` sec.3).

Bundle delta `ba8a4c1..<head del change>` en
`INTERCAMBIO/work/dw7-cpp-ba8a4c1-<head>.bundle`, sha256 publicado y
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
