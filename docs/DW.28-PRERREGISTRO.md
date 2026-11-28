# DW.28 — V7: cortinas de aurora 100–250 km (557,7 / 630 / N₂⁺)

Estado: PRE-REGISTRO (sin código de la feature). Base `origin/master` =
`fa8be31437ca7a0b54ea489d96fd8be73f4db431` (V5a+V5b ya mergeadas).
Worktree `ionosphere-dw28-cpp`, rama `dw.28-aurora-curtains`.

`@ARQUITECTO`: V5b se cerró **antes** de V7 para estabilizar el criterio
visual. Por tanto la comparación de colores de este slice se hace ya a
través de la cadena V5a (grano/viñeta/CA) + V5b (LUT 3D / bloom mip),
con ambos flags nuevos en su estado por defecto (OFF) salvo declaración.

## 1. Coste MEDIDO (baseline)

Sonda temporal `PROBE-D28`, vista por defecto, `auroraVisible=1`,
`dt` de frame completo:
```
32,307 (arranque), 20,169, 20,075, 20,201, 19,675, 20,794, 19,521 ms
```
Baseline steady: **19,5–20,8 ms/frame**. Sonda restaurada (grep 0) y
rebuild `--clean-first` rc=0.

## 2. Estado del código (anclas verificadas)

- `AuroraAdapter`: Ovation NOAA 72×72, `values` 0–100 (medido max=23),
  `min=0 max=23` en el log real; `GridData` con `timestamp` y `valid`.
- `App.cpp`: `auroraLayer` es un `GridLayer` con colormap "aurora" a
  110 km (`setAltitude(110/6371)`), una **mancha ovalada plana**, no
  cortinas. Se consume en `App.cpp:3861` y se dibuja en `:4641`.
- `IrtamGridEval`/`IrtamIgrfCoeffs.inc`: IGRF‑14 NMAX=13 ya embebido
  (F1/DW.21); el eje dipolar se deriva del coeficiente grado 1.
- Sin controlador de aurora 3D propio.

## 3. Cambio propuesto (V7)

- Clase nueva `AuroraCurtains` (`src/Ionosphere/AuroraCurtains.h/.cpp`),
  flag `auroraCurtainsEnabled` **OFF por defecto**, persistido y con UI.
- `updateFromGrid(const GridData&)`: una cortina por celda con valor
  > umbral 8 (numericamente falsable: se registra el número de cortinas).
  Cada cortina es un quad (6 vértices) con atributos: posición base en
  esfera unitaria, `aHeight` 0/1, valor normalizado y longitud de celda.
- Shader: altura 100→250 km (`r = 1 + hKm/6371`), inclinación siguiendo
  la dirección del campo dipolar derivada del eje IGRF‑14
  (`b = 3·(r·axis)·r − axis`), **marco Tierra**, declarado frente al GSM
  de la magnetopausa.
- Tres emisiones por altura: **557,7 nm** verde (100–150 km),
  **630,0 nm** rojo (200–250 km) y **N₂⁺** violeta en el borde inferior;
  mezcla suavizada en el shader, intensidad del valor Ovation vivo.
- Un único `glDrawArrays(GL_TRIANGLES, ...)` aditivo con depth test y
  depth mask off. **Sin FBOs nuevos**. La capa ovalada actual queda
  intacta y puede coexistir.

## 4. Criterio binario (falsable)

1. **Build/tests**: `cmake --build` rc=0; `ctest` rc=0 (65 tests); sin
   diff en tests existentes.
2. **OFF byte-idéntico**: el path actual de `auroraLayer` no se toca; la
   clase nueva sólo corre con el flag ON.
3. **Coste** con la misma sonda, mismo binario, flag etiquetado
   `CURTAINS OFF/ON`, cámara/epoch/replay/estado de datos intactos:
   `ON <= OFF + 2,0 ms` y techo 26 ms. Peor ON menos mejor OFF.
4. **Falsabilidad física**: grep de altitudes 100/250, longitudes de
   onda 557,7/630/N₂⁺, eje IGRF y número de cortinas > 0 con datos reales.
5. **Visual**: comparación OFF/ON con la misma cámara/epoch/replay, a
   través de la cadena V5a+V5b (flags por defecto). Si la captura falla,
   **NO VERIFICADA**; no afirmar percepción de colores.

## 5. No-goals

- No tocar `AuroraAdapter`, `GridLayer` ni el ovalo actual.
- No Tsyganenko, no emisión física radiativa: es una parametrisación
  visual declarada de las tres bandas.
- No F1/F2/F3, no V1–V5b, no Android.
- No merge sin veredicto del VERIFICADOR.

## 6. Evidencia

`git diff --numstat`, grep de símbolos y constantes, `ctest`, bloques
`[BENCH-D28] flag=CURTAINS OFF/ON`, número de cortinas, capturas (o
salvedad), `handoff.py check`.
