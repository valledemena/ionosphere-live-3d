# DW.4 — Pre-registro: extraer formateo de campos de `App.cpp` (C++)

> Estado: **PRE-REGISTRO** — pendiente de acta de GLM y GO de David antes
> de tocar código.
> Repo: `valledemena/ionosphere-live-3d`. Head exacto:
> **`e680d018e15975f7434d86c2fc17cc4389b0a671`**, rama
> `baseline/dw-cpp-20261122` (worktree limpio
> `C:\Users\unknown\Desktop\ionosphere-dw4-cpp`).

## 1. Contexto y elección del bloque

`src/App.cpp` tiene **6 703 líneas** en `e680d01` (medido). Es el
monolito que el README describe como composición, timeline, replay y
todos los paneles. Siguiendo el acta `0460` §4.1 (paso pequeño, no
big-bang), DW.4 mueve **un** bloque cohesivo: las utilidades puras de
formateo de campos `ebFmt` y `giroFmt`.

Bloque exacto en `e680d01`:

- Línea 719: comentario `// Formatea un campo del perfil Ebro ("---" si NaN).`
- Líneas 720–727: `static std::string ebFmt(float v, int dec)` (8 líneas).
- Línea 728: en blanco.
- Línea 729: comentario `// Igual para GIRO (ausente = 0).`
- Líneas 730–733: `static std::string giroFmt(float v, int dec)` (4 líneas).
- Línea 734: en blanco (se retira junto con el bloque para no dejar
  doble blanco; equivalente al blank adyacente de DW.3).

**Total: líneas 719–734 = 16 líneas retiradas.**

Usos verificados con `git grep -n -E "ebFmt|giroFmt" e680d01 -- src tests`:
todos dentro de `src/App.cpp` (definiciones + llamadas en 5965–5984 y
6191–6193). **Ningún test ni otro fichero las referencia.**

## 2. Cambio pre-registrado

1. **Nuevo** `src/Utils/FieldFormat.h`: contiene el bloque de las 15
   líneas 719–733 **literalmente** (mismo cuerpo, mismo orden, mismos
   calificadores `static`), más un andamiaje declarado:
   `#pragma once` y los includes `<cmath>`, `<cstdio>`, `<string>`.
2. **`src/App.cpp`**: se retiran las líneas 719–734 (16) y se añade
   `#include "Utils/FieldFormat.h"` junto a los includes locales. Las
   llamadas en 5965–5984 y 6191–6193 quedan intactas.

Superficie pública: las dos funciones siguen siendo `static` y el
nuevo header solo se incluye desde `App.cpp`; no hay cambio de API,
no se exporta nada nuevo y los tests no se tocan.

## 3. Anclajes de comportamiento (medidos, no estimados)

Medidos por ejecución sobre `e680d01` en el clon local:

- `cmake --build build --parallel 4` → **rc=0** (24 pasos, enlaza
  `ionosphere-live-3d.exe` y los executables de test).
- `ctest --test-dir build --output-on-failure` → **rc=0**,
  **100% tests passed, 54/54**, 20,62 s.
- `ctest -N` → **Total Tests: 54**.

Tras el cambio se exigen los mismos tres: build rc=0, ctest 54/54
rc=0 y `ctest -N` 54. El ancla web 48/461/0 **no se reutiliza**.

## 4. Criterio binario (PASS solo si todo se cumple)

1. `cmake --build build --parallel 4` → rc=0.
2. `ctest --test-dir build --output-on-failure` → rc=0, 54/54.
3. Bloque extraído: las 15 líneas 719–733 del header son
   **byte-idénticas** a las del original; único añadido permitido el
   andamiaje declarado en §2 (`#pragma once` + 3 includes).
4. Diffstat con forma de movimiento: `-16` en `App.cpp`, `+21` en el
   header nuevo (± k pequeño declarado: 5 líneas de andamiaje);
   `+1` en `App.cpp` por el include. Sin lógica nueva ni ediciones
   fuera del bloque.
5. `git grep -n -E "ebFmt|giroFmt" <head> -- src tests` → definiciones
   y usos solo donde corresponde; 0 referencias en tests; 0 cambios
   de comportamiento en tests.
6. PR a `master` (protegido), con CI del repo C++ verde.

## 5. CI y evidencia

- `.github/workflows/ci.yml` **existe en `e680d01`**: workflow
  `CI Ubuntu (Release + ctest)`, `ubuntu-latest`, cmake Release + GLFW
  3.4 + ctest. Último run en `master`: `41373456157` **success**
  (2026-11-02). Sin runs aún en `baseline/dw-cpp-20261122`.
  Base de evidencia: **CI verde en el PR** + anclajes locales medidos
  (a diferencia del repo web, sin CI).
- Evidencia: diff completo, logs de build/ctest, bloque byte-idéntico,
  diffstat, `git status`, head con padre `e680d01`, y bundle delta.
- Bundle: `e680d01..<head del slice>` con sha256 publicado en
  INTERCAMBIO, como en la era DW.
- Sha del head del slice publicado **antes del PR**; sin amend ni
  force-push (regla del acta `0455`).

## 6. Nota de alcance

El snapshot estructural de GLM (master `aea88e1`) es anterior a la
cadena registrada (`d9fb809 → e680d01`): sirvió para ubicar
`src/App.cpp`, `tests/`, `CMakeLists.txt` y `.github/workflows`, pero
todos los números de este pre-registro están medidos en `e680d01`.

## 7. Referencias

- Acta `0460` §4.1 (comunes) y §4.3 (C++).
- Acta `0459` §3 (anclajes C++), acta `0455` §2 (sha inmutable).
- Acta `0454` §6 (slices pequeños, anclajes, diffstat de movimiento).
