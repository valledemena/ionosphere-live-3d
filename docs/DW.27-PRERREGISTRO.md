# DW.27 — V5b: LUT 3D + bloom mip (cadena de post cerrada antes de V7)

Estado: PRE-REGISTRO (sin código de la feature). Base `origin/master` =
`dab37ecea4bf4fde5396f1606d9fb92567eaa3cf`. Worktree `ionosphere-dw27-cpp`,
rama `dw.27-bloom-mip-lut`.

Decisión de `@ARQUITECTO`: V5b **antes** que V7, porque el ancho del glow
y la grade global afectan directamente a los colores emisivos de V7
(557,7 / 630 / N₂⁺). V7 se re-ejecutará visualmente tras V5b con la
misma cámara/epoch/replay.

## 1. Coste MEDIDO (baseline)

Sonda temporal `PROBE-D27`, vista por defecto, `bloom=1`, `dt` de frame
completo:
```
32,700 / 30,508 (picos de red), estacionario:
19,995, 19,219, 19,612, 18,948, 19,549 ms
```
Baseline steady: **18,9–20,0 ms/frame**. Sonda restaurada (grep 0) y
rebuild `--clean-first` rc=0.

## 2. Estado del código (anclas verificadas)

- `shaders/bloom.frag`: bright-pass soft-knee, peine denso **k=1..24**
  con 24 pesos literales pinados (suma 0,386486194), núcleo 0,227027,
  near-taps desacoplados a 2,1/4,2 texels (M9g), `u_intensity` solo en
  la pasada vertical (M1), pasada V con
  `hfc::bloomBypassThreshold()` (M9f). **Gold pinnado: NO se toca.**
- `App.cpp`: `bloomFBO[0/1]` a media resolución, dos pasadas H→V,
  `bloomShader` con fallback inline sincronizado.
- `shaders/composite.frag`: V5a ya aplica grano/viñeta/CA en LDR tras
  el `pow` sRGB local; fallback inline generado desde el fichero.

## 3. Cambio propuesto (V5b)

Dos flags **independientes**, ambos **default OFF**:

### 3.1 `lut3d` — LUT 3D procedural
- Textura `GL_TEXTURE_3D` 32³ construida en código (identidad por
  defecto); parámetros `contrast`, `saturation`, `lift` con sliders.
- **Sin fichero externo**: lección DW.16 (redeploy sin copiar assets).
- `composite.frag`: un `sampler3D u_lut3d` muestreado **al final**,
  después de V5a, en LDR; con `u_lutEnabled=false` no se ejecuta.
- Fallback inline sincronizado desde el fichero (misma técnica que V5a).

### 3.2 `bloomMip` — cascada mip
- Tres FBOs adicionales a 1/4, 1/8 y 1/16 de la escena; downsample y
  upsample aditivo sobre el resultado V ya calculado.
- Las dos pasadas gold (H/V con los 24 pesos y M9f) quedan **intactas**;
  la cascada es puramente aditiva y sólo corre con el flag ON.
- OFF byte-idéntico: sin FBOs nuevos activos ni samplers nuevos en
  ejecución; la salida es la del head `dab37ec`.

## 4. Criterio binario (falsable)

1. **Build/tests**: `cmake --build` rc=0; `ctest` rc=0 (65 tests); sin
   diff en tests; `shaders/bloom.frag` sin diff (gold pinnado).
2. **OFF funcional**: mismo binario, ambos flags OFF, salida igual a
   `dab37ec` por construcción (ramas nuevas detrás de los uniforms).
3. **Coste** con la misma sonda, flag etiquetado, cámara/epoch/replay
   intactos: cada flag `ON <= OFF + 2,0 ms`, techo absoluto 26 ms.
   Peor ON menos mejor OFF, reportado tal cual.
4. **Mecánica**: grep de `glTexImage3D`, `sampler3D`, los tres FBOs
   mip y de que `bloom.frag` no cambió.
5. **Visual**: PARCIALMENTE NO VERIFICADO si no hay captura válida;
   V7 se re-ejecuta visualmente tras este slice (condición de
   `@ARQUITECTO`).

## 5. No-goals

- No tocar `shaders/bloom.frag` ni los pesos/pases M1/M9f/M9g.
- No assets externos, no LUT desde fichero.
- No aurora (V7), no F1/F2/F3 ni V1–V5a.
- No merge sin veredicto del VERIFICADOR.

## 6. Evidencia

`git diff --numstat`, grep de samplers/FBOs y bloom.frag sin diff,
`ctest`, bloques `[BENCH-D27] flag=LUT/MIP`, capturas (o salvedad),
`handoff.py check`.
