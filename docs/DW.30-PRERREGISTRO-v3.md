# DW.30 — V10b v3: regla de filtrado declarada antes de medir

Estado: PRE-REGISTRO v3 (sin código de la feature). Base `origin/master` =
`354149bcc51c445ba148ca7026649be0fb4fb131`. Worktree `ionosphere-dw30-cpp`,
rama `dw.30-mid-layers`.

`@ARQUITECTO` rechazó el gate por mínimo de v2: elegir la mejor muestra
puede hacer pasar cualquier ON y repite el error corregido en V2. Sus
condiciones se incorporan **antes de cualquier medición nueva**.

## 1. Regla de filtrado (pre-registrada, no negociable después)

Sonda por **bloques de 300 frames**, guardando el `dt` de cada frame:

1. Por bloque se calculan **p50, p95, min, max** de los 300 `dt`.
2. Un bloque se marca **`DISCARDED`** si y sólo si `max(dt) > 100 ms`.
   Ese umbral se declara ahora y significa "hubo un stall de red/fetch,
   no es régimen steady". No se descarta nada más.
3. Se reportan **todos** los bloques, incluidos los `DISCARDED`, con
   sus cuatro estadísticos. No hay cherry-picking.
4. Las estadísticas de la corrida se calculan sobre los bloques
   **retenidos**; se reporta además cuántos se descartaron.

## 2. Gate (reemplaza al de v1 y v2)

- `ON_p50 <= OFF_p50 + 2,0 ms` (emparejado, mismo binario).
- `ON_p95 <= 26 ms` (techo absoluto sobre p95, no sobre un frame aislado).
- Cámara, epoch y replay fijos. Se prefiere `replayMode=true` con epoch
  fijo; si no es posible, se declara la deriva del epoch vivo.
- Si no se puede estabilizar, V10b se divide en tres slices
  (plasmasfera / anomalía / TOI) con gates propios, como V3/V5/V10a.

## 3. Alcance de la feature (sin cambios)

`MidIonoLayers` con tres flags **OFF** independientes y persistidos:
plasmasfera (escala L con Kp vivo), anomalía ±15° magnética (eje IGRF‑14,
modulada por TEC), TOI (onda `sin(m·lon − ω·t)` con Kp/AE). Un draw
aditivo por capa, sin FBOs nuevos; capas anteriores intactas.

## 4. Falsabilidad

- grep de `L_pp`, `±15`, `sin(`, `currentKp()`, eje IGRF.
- `ctest` rc=0 (65), sin diff en tests, OFF byte-idéntico por ramas.
- Bloques `[BENCH-D30] arm=N p50= p95= min= max= discarded=`.
- Visual con los seis flags declarados; NO VERIFICADA si falla captura.

@IMPLEMENTADOR
