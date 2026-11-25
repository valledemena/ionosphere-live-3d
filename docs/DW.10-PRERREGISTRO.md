# DW.10 PRERREGISTRO - C++: tests de consolidacion de las seis cabeceras

Materia: cierre de la dieta mecanica con una serie de otra clase: no
extrae codigo, ANADE tests de estructura. `App.cpp` y las seis
cabeceras quedan BYTE-IDENTICOS. **Solo pre-registro: sin codigo
tocado.**

## 0. Gobernanza

- GLM recomendo DW.10 en prosa del chat (sandbox caido, sin triple),
  retransmitida al relay en `to-glm/0508` (`ce4ac2f`). David decidio
  abrir el pre-registro. Cadena: GLM recomienda a peticion, David
  decide, OpenCode pre-registra sin tocar codigo, GLM audita, OpenCode
  ejecuta tras acta + GO, GLM audita la ejecucion.
- **Clase de slice distinta**: anadir tests no es un movimiento de
  bloque. El checklist del acta `0473` seccion 9 se adapta: no hay
  diffstat de movimiento ni andamiaje; hay N tests nuevos con
  proposito estructural, y el criterio es que compilen y pasen sin
  tocar las unidades bajo test.

## 1. Change

- Baseline: `origin/master = 25098f32ca7ac4bfb57cad47ed4978534066be76`.
- Rama: `dw.10-consolidacion`, creada desde la baseline. Head del
  pre-registro: el commit de este documento.
- Worktree limpio separado:
  `C:\Users\unknown\Desktop\ionosphere-dw10-cpp`.
- Seis cabeceras bajo test (todas en `src/Utils/`, byte-identicas a
  master, verificadas por builder aislado con los 7 include dirs de
  `build.ninja`):
  `FieldFormat.h` (548 B), `StationMerge.h` (2 008 B),
  `GiroHelpers.h` (1 347 B), `GridBlend.h` (742 B),
  `IdwInterp.h` (10 842 B), `IrtamBracket.h` (1 943 B).
- Sin cambio en `App.cpp`, en las seis cabeceras ni en ningun fichero
  de produccion. Solo ficheros nuevos de test y `CMakeLists.txt`.

## 2. Tests anadidos (N=8)

1. **A1..A6  autocontencion (6 tests)**: un TU por cabecera que la
   incluya SOLA y no haga nada (`int main(){return 0;}`). Compila
   porque la cabecera es suficiente por si misma. Nombres:
   `test_hdr_fieldformat.cpp`, `test_hdr_stationmerge.cpp`,
   `test_hdr_girohelpers.cpp`, `test_hdr_gridblend.cpp`,
   `test_hdr_idwinterp.cpp`, `test_hdr_irtambracket.cpp`.
2. **B  conjunto (1 test)**: un TU que incluya las SEIS juntas,
   en orden, y compile: verifica guardas de inclusion y ausencia de
   ciclos. `test_hdr_all_six.cpp`.
3. **C  duplicados (1 test)**: script de verificacion que extrae
   las definiciones `static`/`inline` de las seis cabeceras y
   comprueba que ningun simbolo aparece definido en dos de ellas
   (ODR). Se implementa como test CTest que invoca un script de
   shell/Python trivial con `grep`/`sort`/`uniq`, o como regex en
   el propio TU. El criterio binario: **0 duplicados**.

**IWYU burdo (no bloqueante, sin test)**: revision manual de includes
sin uso en las seis; se registra en la evidencia como lista de
hallazgos, sin corregir en este slice.

## 3. Criterio binario

- `cmake --build build --parallel 4` rc=0.
- `ctest --test-dir build --output-on-failure` rc=0.
- Anclaje dual: local **62 passed + 1 skipped** (54 existentes +
  8 nuevos; `ebro_parse` sigue Skip por fixtures gitignored); CI
  **61 passed + 1 skipped** (53+8 y 1 skip). Rango previsto por GLM:
  5961 passed + 1 skipped; el valor exacto se mide en la ejecucion
  y se declara, sin reescribir este documento.
- 0 duplicados de simbolos entre las seis cabeceras.
- `App.cpp` y las seis cabeceras byte-identicos a master: `git diff
  --stat` sobre ellos, vacio.

## 4. Contabilidad y nota de lectura (leccion DW.8/DW.9)

- Ficheros nuevos: 8 `.cpp` de test + entradas en `CMakeLists.txt`.
- **N contable**: +8 tests de estructura, literal congelado aqui.
- PR esperado: **10 ficheros** (8 tests + `CMakeLists.txt` +
  `docs/DW.10-PRERREGISTRO.md`), con N lineas de documentacion y
  M lineas de CMake. El acumulado se mide en la ejecucion y se
  audita contra el plan real, no contra un literal pre-errata:
  la nota de lectura se calculara sobre los ficheros realmente
  anadidos, como en DW.9 (N2 del acta `0482`).
- Correcciones permitidas: solo anadir includes que faltaran en
  los TUs de test; cualquier parche de higiene se registra y no
  reabre la dieta.

## 5. Anclajes re-medidos (baseline `25098f3`, worktree limpio)

- Configure rc=0; `cmake --build build --parallel 4`: rc=0 (290 pasos).
- `ctest --test-dir build --output-on-failure`: rc=0, **54/54** sin
  skipped (fixtures ebro copiados al worktree, fuera del commit);
  `ctest -N` = 54.
- Prueba de viabilidad ya ejecutada: las 6 cabeceras compilan
  aisladas (`g++ -std=c++17 -fsyntax-only`) con los 7 include dirs
  de `build.ninja`.
- Anclaje dual (acta `0463` seccion 2) exigido con los nuevos
  recuentos; CI repo C++ dos jobs (`ctest` + ThreadSanitizer).

## 6. Evidencia y entrega

Ferry `to-glm/files/DW.10/` con: `01-tests.txt` (lista de 8 tests),
`02-numstat.txt`, `03-head.txt`, `04-byte-identico.txt` (App.cpp y
las 6 cabeceras vs master), `05-duplicados.txt` (0), `06-iwyu.txt`,
`07-build.txt`, `08-ctest.txt`, `09-status.txt`, `MANIFEST.md` y
`SHA256SUMS` (11 entradas, 11/11 OK; hashes sobre bytes
post-normalizacion, leccion `0479`).

Bundle delta `25098f3..<head del change>` con sha256 publicado y
`git bundle verify`; entregado en `upload/` y en el relay
`to-glm/files/DW-BUNDLES/` (delegacion de David, to-glm/0486).

PR a `master` con el checklist; rama conservada; **sin amend ni
force-push** (regla `0455`).

## 7. Gate

1. GLM emite acta de conformidad sobre el head del pre-registro.
2. David da GO para ejecutar el slice.
3. OpenCode anade los 8 tests, mide anclajes, genera bundle y ferry.
4. GLM emite acta de conformidad de ejecucion.
5. David da GO para el PR y el merge a master (protegido) con CI 2/2.

Sin codigo tocado antes de los puntos 1 y 2.
