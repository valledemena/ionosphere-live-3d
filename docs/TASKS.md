# TASKS

## R0 â€” cerrado (2026-11-02)

Gate SHA256 **8/8**: los cinco documentos de baseline mÃ¡s `GLM_R0_RESPONSE`, el
comentario del Issue #1 y el `relay-primitive` estÃ¡n publicados y verificados
byte a byte. Tabla completa y mÃ©todo de verificaciÃ³n: `docs/r0/README.md`.

NingÃºn artefacto de este repo declara autorÃ­a de GLM sin coincidir con su hash.
Ese es el criterio, y no hay otro.

## Fases (fuente: `docs/r0/IMPLEMENTATION_SLICES.md`, GLM)

Este fichero no resume el contenido de ese documento: lo referencia. El detalle
de alcance y DoD de cada fase vive allÃ­, y cualquier discrepancia la gana el
documento canÃ³nico.

| Fase | TÃ­tulo canÃ³nico | Estado |
|---|---|---|
| R0 | Baseline | **cerrado** |
| R1 | Esqueleto + CI | siguiente â€” Issue #2 |
| R2 | Nucleo de datos (contrato v1) | **cerrado** (issue #4 closed 2026-11-06, SHA ac31886) |
| R3 | Escena v1 (lenguaje visual) | en progreso (R3.0 web 8cdc334, R3.1 Android 72612b9, R3.2 pendiente) |
| R4 | Tiempo y profundidad | pendiente |
| R5 | Cielo vivo | pendiente |
| R6 | Release | pendiente |

MÃ­nimo usable declarado por GLM: **R1 + R2** (globo + foF2/Kp/Dst reales con
degradaciÃ³n honesta). Replay, raymarch, satÃ©lites, WSPR y magnetosfera esperan a
R3â€“R5.

## Regla transversal (GLM, `IMPLEMENTATION_SLICES.md`)

Cada slice = un PR pequeÃ±o, revisado hunk-a-hunk por GLM, con CI verde y gate de
dispositivo de David antes del merge. Merge: solo David.

## Gate de dispositivo

Xiaomi Mi 10 Lite 5G â€” Android 11 (API 30), Snapdragon 765G,
Adreno 620 (GLES 3.2), 6 GB RAM. Detalle en `docs/r0/TEST_GATES.md` v1.1.
