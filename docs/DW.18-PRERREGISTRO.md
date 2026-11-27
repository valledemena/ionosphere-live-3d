# DW.18 - HF: LUF tambien gobierna el color del arco

Estado: PRE-REGISTRO (sin codigo). Base `origin/master` =
`6cadad24f895b159cfc0781266b87e507eb060aa`. Worktree
`ionosphere-dw18-cpp`, rama `dw.18-hf-luf-color`.

Decision de David: opcion **A** — aplicar LUF al color del arco, para
que la ruta roja/verde sea coherente con la matriz de bandas.

## 1. Problema medido (captura del usuario)

`MUF 8,3 / FOT 7,1 / LUF 4,4 MHz`, `Freq 1,0 MHz`:
- el ribbon se ve **verde**;
- la matriz marca 160 m (1,8) y 80 m (3,5) como `closed`, y 60 m (5,3)
  y 40 m (7,0) como `open`.

Causa leida en `6cadad2`:

- `hfc::hopViable(f, MUF_salto)` (`HFTraceLayer.h:171`) devuelve 1.0
  (verde) si `f <= 0,85*MUF_salto`. **No mira LUF**.
- `hfc::bandState(f, LUF, FOT, MUF)` (`:160`) si mira LUF y cierra las
  frecuencias por debajo de el.
- El propio comentario (`:167`) lo declara: "el LUF gatea la matriz,
  no el color del arco".
- `buildPolyline` (`HFTraceLayer.cpp:229`) calcula `via` por salto con
  `hopViable(freq, mufK)`; `r.lufMHz` ya esta calculado en `update`
  (`:183-198`, max sobre saltos del grid LUF M7) y se pasa a
  `buildPolyline(const Input&, const HFCircuitResult& r)`.

Ambas reglas son correctas segun su definicion, pero la UI parece
contradictoria: a 1 MHz el arco dice "abierto" mientras la matriz dice
"cerrado por LUF".

## 2. Cambio propuesto

- Nueva funcion pura en `hfc` (sin tocar `hopViable`, que esta sellado
  por 20+ tests):

```cpp
// Viabilidad VISUAL del tramo: LUF cierra por debajo; MUF por arriba.
inline double hopViableWithLuf(double fMHz, double mufHopMHz,
                               double lufMHz) {
    if (lufMHz > 0.0 && fMHz < lufMHz) return 0.0;   // cerrado (rojo)
    return hopViable(fMHz, mufHopMHz);
}
```

- `buildPolyline` usa `hopViableWithLuf(in.freqMHz, mufK, r.lufMHz)`
  en vez de `hopViable(...)` (`:229`). El LUF es de circuito (max sobre
  saltos), coherente con `HFCircuitResult.lufMHz` y con la matriz.
- Sin cambios en `bandState`, `mufForHopMHz`, `fotOfMHz` ni en la
  fisica de saltos. Sin cambios en App ni en shaders.

## 3. Criterio binario

1. **Unitarios**: nuevos checks en `tests/test_hf_circuit.cpp`:
   - `f=1,0`, `muf=8,3`, `luf=4,4` -> 0,0 (cerrado);
   - `f=5,3` -> 1,0 (abierto);
   - `f=7,5` -> 0,5 (marginal, entre FOT y MUF);
   - `f=9,0` -> 0 (sobre MUF);
   - `luf=0` -> identico a `hopViable` (regresion);
   - los tests existentes de `hopViable` siguen pasando.
2. **Build**: `cmake --build` rc=0; `ctest` rc=0 con el total de la
   base + tests nuevos.
3. **Runtime** (sonda de replay con dato fresco, congelando el cursor):
   - `Freq 1,0 MHz`: arco **rojo/cerrado** en la captura, matriz con
     160/80 m `closed`, MUF/FOT/LUF visibles;
   - `Freq 5,3 MHz`: arco verde y matriz `open` en 60 m;
   - capturas `PrintWindow` con timestamp y epoch del cursor.
4. **Alcance**: diff no toca App, shaders, TEC, GIRO/kc2g, DW.14-17;
   `hopViable` queda byte-identico (solo se anade la nueva funcion).

## 4. No-goals

- No cambiar la matriz ni los numeros MUF/FOT/LUF.
- No tocar la gestion de "sin dato" (gris de DW.16).
- No alterar la geometria de la ruta (saltos, alturas, pulso).
- No merge sin veredicto.

## 5. Evidencia

`git diff --numstat`, grep de `hopViableWithLuf`, salida de `ctest`,
dos capturas con frecuencias 1,0 y 5,3 MHz, `handoff.py check` limpio.

## 6. Riesgos

- El LUF es de circuito (peor salto): puede cerrar visualmente un tramo
  cuyo MUF local permitiria; es coherente con la matriz y se declara.
- `buildPolyline` se ejecuta solo cuando cambia la firma (P7): el color
  sigue al slider de frecuencia porque `in.freqMHz` ya entra en la
  firma (`sigFreq_`).
