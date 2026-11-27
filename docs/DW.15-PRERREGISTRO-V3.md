# DW.15 v3 - Frescura del circuito HF segun la FUENTE ACTIVA

Estado: PRE-REGISTRO v3 (enmienda tras veredicto NO CONFORME). Sin codigo
nuevo todavia. El codigo de la v2 sigue sin commitear en el worktree; no
se descarta.

Base: rama `dw.15-hf-circuit-sin-datos`, HEAD `7ca92d9` (pre-registro
v2). Worktree `ionosphere-dw15-cpp`.

## 1. Por que se enmienda

`VEREDICTO_EJECUCION.md`: **NO CONFORME**, criterio 3 incumplido. El
verificador elige la opcion (b): no basta con unitarios para salvar un
criterio binario de runtime, y el gate de la v2 mira **solo**
`giro.getNetStatus().histHours`, mientras en `kc2gMode` la fuente real
es `gsAct = mergeKc2g(...)` con historia propia. Resultado medido:

- `hours=12` y `hours=40` producen el **mismo** texto
  `HF circuit: sin datos GIRO (historial vacio)` porque `histHours=0`
  en arranque limpio (GIRO no persiste su historial).
- En esa misma corrida: `[kc2g] replay: 30 st with history, range
  [22:30..20:45 UTC]` y `[RadioProp] ... (GIRO mock)`: la fuente activa
  tenia historia real, pero el circuito quedo oculto.

Evidencia: `dw15_outside_root.err` y `dw15_outside_root.png`.

## 2. Cambio de la v3 (alcance enmendado)

### 2.1 Rango de la fuente activa

En `App.cpp`, al calcular el estado del circuito con el mismo `hfEpoch`:

- **`kc2gMode == true`**: usar `impl->kc2g.historyStats(nSt, t0, t1)`;
  si `nSt > 0`, rango activo = `[t0, t1]`. Si no hay stats, usar
  `min/max` de los epochs reales de `impl->giroReplayCache` (fuente
  mergeada). Nombre de fuente = `kc2g`.
- **`kc2gMode == false`**: rango activo = `[now - histHours*3600, now]`
  de GIRO, como en v2. Nombre de fuente = `GIRO`.

### 2.2 Funcion pura (ampliacion, sin romper la v2)

Anadir en `src/Data/IrtamState.*`:

```cpp
// Rango explicito de la fuente activa; NO asume GIRO.
bool circuitHasFreshDataInRange(double epoch, double oldest, double newest,
                                bool samplerBusy);
```

- `if (samplerBusy) return false;` primero (condicion 1 del veredicto v2).
- `false` si `oldest <= 0 || newest <= oldest` (rango invalido).
- `false` si `epoch < oldest || epoch > newest`; `true` en el interior,
  bordes inclusive.
- `circuitHasFreshData(...)` de la v2 queda como wrapper de GIRO sobre
  esta funcion (no se rompen los 10 tests existentes).

### 2.3 Textos y gates

- Texto con nombre de fuente:
  - con gap > 0: `HF circuit: sin datos <FUENTE> en este epoch (hist X.X h; +Y)`,
    `+Y` = `staleGapText(gapSec)` verbatim (`+N min` / `+A.B h`);
  - con `gapSec <= 0` (epoch anterior al inicio):
    `HF circuit: sin datos <FUENTE> en este epoch (antes del historial; hist X.X h)`;
  - sin rango: `HF circuit: sin datos <FUENTE> (historial vacio)`.
- Mismos gates de la v2: panel, matriz, ribbon, marcadores y etiquetas;
  `hfTraceVisible` e `hfc::` intactos.

## 3. Criterio binario (falsable, con runtime reproducible)

1. **Unitarios**: checks nuevos para `circuitHasFreshDataInRange` con
   rango kc2g sintetico (`t0=1000, t1=96400`), interior/bordes/fuera,
   `samplerBusy`, rango invalido; los 10 checks v2 siguen pasando.
2. **Build/ctest**: `cmake --build` rc=0; `ctest` rc=0 con el total de
   la base + tests nuevos.
3. **Runtime DENTRO reproducible en failover**: forzar (o usar el
   failover natural) con `historyStats` real; colocar el cursor dentro
   de `[t0,t1]` (p. ej. 2 h antes de `t1`); evidencia: log con el rango
   activo de kc2g y captura del panel con MUF/FOT/h' **frescos** y
   ribbon/marcadores presentes. Fuente declarada `kc2g`.
4. **Runtime FUERA**: cursor anterior a `t0` (o posterior a `t1`);
   texto `sin datos kc2g ...` y ribbon/marcadores ausentes.
5. **Alcance**: diff no toca `hfc::`, TEC, hallazgos DW.13/DW.14 ni
   `hfTraceVisible`.

## 4. No-goals

- No aceptar unitarios como sustituto del criterio runtime (correccion
  explicita del veredicto).
- No derivar MUF de TEC ni alimentar desde IRTAM.
- No cambiar el clamp de `sampleHistoryAt` ni el polling GIRO/kc2g.
- No merge sin re-veredicto + GO.

## 5. Gate

PRE-REGISTRO v3 a **re-veredicto del VERIFICADOR**. Solo despues de
CONFORME se pide GO para modificar codigo. Sin amend del v1/v2; esta
version es un commit adicional.

## Nota de formato del re-veredicto (commit posterior, sin amend)

`VEREDICTO_PRERREGISTRO_V3.md`: **CONFORME CON UNA NOTA DE FORMATO**.
En modo **kc2g**, el `hist X.X h` de los textos **no** debe tomarse de
`giro.getNetStatus().histHours` (puede ser 0 aunque la fuente activa
tenga datos): se calcula como **amplitud del rango activo**
`(t1 - t0) / 3600`, donde `t0`/`t1` vienen de `kc2g.historyStats` (o
del min/max de `giroReplayCache`). Esta misma amplitud se usa en la
rama "antes del historial". En modo **GIRO** se mantiene
`histHours` como hasta ahora. El verificador confirmo que
`Kc2gAdapter.cpp:354-367` devuelve `t0=min`, `t1=max` (el log
`22:30..20:45` es min->max formateado, no inversion), y que
`circuitHasFreshData` v2 se conserva como wrapper con sus 10 tests.
