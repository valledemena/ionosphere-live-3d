# DW.29 — V10a: D-layer y Es 3D (capas bajas analíticas)

Estado: PRE-REGISTRO (sin código de la feature). Base `origin/master` =
`8859186b0cccc32aec089da7aebdaf2a15dd19bf`. Worktree `ionosphere-dw29-cpp`,
rama `dw.29-new-layers`.

V10 tiene cinco capas (Es 3D, D-layer, plasmasfera, anomalía ±15°, TOI).
Se divide como V3/V5: **V10a = D-layer + Es 3D**, ambas analíticas y con
datos ya presentes. V10b (plasmasfera + anomalía + TOI) va después.

## 1. Coste MEDIDO (baseline)

Sonda temporal `PROBE-D29`, vista por defecto, `dt` de frame completo:
```
60,954 (arranque con stall de red de 7,5 s), estacionario:
19,471, 20,159, 19,381, 20,595, 19,317, 20,151, 19,629, 19,949,
19,417, 20,754, 19,125, 19,550, 20,009, 19,886, 19,256 ms
```
Baseline steady: **19,1–20,8 ms/frame**. Sonda restaurada (grep 0) y
rebuild `--clean-first` rc=0.

## 2. Estado del código (anclas verificadas)

- No existen capas Es 3D, D-layer, plasmasfera, anomalía ±15° ni TOI.
- `IonosondeLayer.h`: `StationPoint` trae `foEs`, `hEs` y `valid` de
  GIRO/kc2g. `App.cpp` ya aplica muestras con `giroPointsFrom(...)`.
- `XrayAdapter`: `latest.flux` y `flareFactor(flux)`; `App.cpp` ya calcula
  `flare` para el panel HF.
- `RadioPropagationAdapter`: LUF con absorción D simplificada (Sauer‑
  Wilkinson) ya verificada; **no se toca**.

## 3. Cambio propuesto (V10a)

Clase nueva `LowerIonoLayers` (`src/Ionosphere/LowerIonoLayers.h/.cpp`)
con dos flags **independientes**, ambos **default OFF** y persistidos:

### 3.1 `dLayerEnabled`
- Carcasa esférica a **80 km** (región D, 60–90 km), color ámbar.
- Opacidad por iluminación solar `max(dot(r, sunDir), 0)` multiplicada
  por `(1 + flare)` con `flare` del flujo GOES real. Un draw aditivo.
- Declarado: absorción **proxy visual**, no el cálculo Sauer‑Wilkinson
  del LUF (que sigue intacto).

### 3.2 `es3dEnabled`
- Un parche por estación GIRO con `foEs > 3 MHz`, a la altura `hEs` o
  110 km por defecto, con color según `foEs`. Un draw aditivo.
- Se registra el número de parches y el `foEs` máximo real del epoch.
- Declarado: visualización de la medición, no una predicción de Es.

Ambas con depth test, depth mask off, sin FBOs nuevos; las capas
existentes quedan intactas.

## 4. Criterio binario (falsable)

1. **Build/tests**: `cmake --build` rc=0; `ctest` rc=0 (65 tests); sin
   diff en tests.
2. **OFF byte-idéntico**: las dos capas nuevas sólo corren con flag ON;
   el path de `RadioPropagationAdapter` no se toca.
3. **Coste** con la misma sonda, mismo binario, flags etiquetados
   (D OFF/ES OFF, D ON/ES OFF, D OFF/ES ON, ambos ON), cámara/epoch/
   replay intactos: cada `ON <= OFF + 2,0 ms`, techo 26 ms.
4. **Falsabilidad**: grep de 80 km, 110 km, `foEs > 3`, `flareFactor`,
   conteo de parches y `foEs` máximo real del epoch.
5. **Visual**: PARCIALMENTE NO VERIFICADO si falla la captura; declarar
   los cuatro flags de post (grade/LUT/MIP) además de los dos nuevos.

## 5. No-goals

- No tocar LUF/SID, `IonosondeLayer`, ni las capas V1–V7.
- No plasmasfera, anomalía ni TOI (V10b).
- No merge sin veredicto del VERIFICADOR.

## 6. Evidencia

`git diff --numstat`, grep de constantes y conteo, `ctest`, bloques
`[BENCH-D29] flag=D/ES`, capturas (o salvedad), `handoff.py check`.
