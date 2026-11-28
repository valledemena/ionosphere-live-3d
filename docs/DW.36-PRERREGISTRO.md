# DW.36 — F5: Topside NeQuick-2 (g=0,125, r=100) + mapa bias/RMSE

Estado: PRE-REGISTRO (sin código de la feature). Base `origin/master` =
`3d053c000dd28cda11ba678cdd6ba6febe006a7c`. Worktree `ionosphere-dw36-cpp`,
rama `dw.36-topside-nequick`.

## 1. Estado verificado

- `DensityVolume.h:127`: `TEC[TECU] = (1/1e16) · ∫Ne[m^-3] dh[m]` con
  integración trapezoidal; es la convención que debe respetar el modelo.
- No hay perfil topside NeQuick ni mapa bias/RMSE modelo vs GloTEC.

## 2. Declaración del modelo (parámetros pineados)

Perfil **paramétrico inspirado en NeQuick-2**, NO el código Fortran:
```
z = h - hmF2  (km),  h en [hmF2, 1000 km]
s = g*z / (1 + g*z/r),  g = 0,125 km^-1,  r = 100 km
Ne(h) = NmF2 * (1+s)^-2 * exp(-s)
NmF2 = 1,24e10 * foF2^2   [m^-3], foF2 en MHz
```
`g` y `r` son los valores del plan. En `z=0`, `Ne=NmF2`. Es una
aproximación analítica declarada, no una validación del modelo oficial.

## 3. Cambio propuesto (núcleo, sin render)

`src/Utils/TopsideNeQuick.h` (header-only, puro):
1. `topsideNe(hKm, foF2, hmF2Km)` → Ne en m^-3.
2. `topsideTec(foF2, hmF2Km, hTopKm=1000, stepKm=20)` → TECU por
   integración trapezoidal con la convención de `DensityVolume.h`.
3. `biasRmseMap(model, observation)` → para cada celda con ambos grids
   válidos: `bias = modelo - observación`, `SE = (modelo-obs)^2`;
   celdas sin ambos datos = **NO MEDIBLE (NaN)**. Devuelve resumen con
   n, bias medio, RMSE global y RMSE por banda de latitud (|lat|<30,
   30–60, >60) con mínimo 2 celdas por banda.

## 4. Criterio binario

1. `cmake --build` rc=0; `ctest` rc=0 con **67 tests** (uno nuevo);
   tests existentes sin diff.
2. Test determinista: en `z=0`, `Ne == NmF2`; perfil monótonamente
   decreciente; TEC finito y positivo; mapa con celdas inválidas da
   NaN y con diferencia conocida recupera bias/RMSE.
3. Coste CPU (`--bench`, 1000 repeticiones) reportado como p50/p95,
   **no sustituye** al gate de frame (NO MEDIBLE por DW.30, bug
   90,8 MB abierto). 0 draws, veredicto PARCIAL, visual NO APLICABLE.

## 5. No-goals

- No tocar `DensityVolume.h`, `App.cpp` ni F4.
- No render, no red nueva, no merge sin veredicto.

@IMPLEMENTADOR
