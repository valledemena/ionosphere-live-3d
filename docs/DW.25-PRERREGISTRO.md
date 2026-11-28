# DW.25 — V5a: grado cinematográfico barato (grano, viñeta, CA)

Estado: PRE-REGISTRO (sin código de la feature). Base `origin/master` =
`b273f0e627e3dd2f5fafd3645dfdc9a6e7e710ba`. Worktree `ionosphere-dw25-cpp`,
rama `dw.25-cinematic-grade`.

Decisión de `@ARQUITECTO`: V5 se divide. **V5a** = grano + viñeta +
chromatic aberration en `composite.frag`, sin FBO nuevos. V5b (bloom mip
+ LUT 3D) y V7 (aurora) van después. V5a fija la etapa final de grade
antes de introducir emisión de aurora.

## 1. Coste MEDIDO (baseline)

Sonda temporal `PROBE-D25`, vista por defecto (sin forzar cámara),
`dt` de frame completo:
```
27,478 (arranque), 16,089, 15,785, 15,330, 15,784, 16,268,
16,289, 15,847, 16,642 ms
```
Baseline steady: **15,3–16,6 ms/frame**. Sonda restaurada (grep 0) y
rebuild `--clean-first` rc=0.

## 2. Estado del código (anclas verificadas)

- `shaders/composite.frag`: `u_scene` + `u_bloom`, `u_bloomEnabled`,
  `u_bloomStrength`, `u_toneMap`, `u_exposure`; `acesFilm` (Narkowicz);
  suma aditiva de bloom **antes** de ACES; `pow(clamp(col,0,1), 1/2.2)`
  con comentario `pow LOCAL (NO GL_FRAMEBUFFER_SRGB)`.
- No hay grano, viñeta, CA ni LUT 3D.
- Fallback inline en `App.cpp:1067-1091` sincronizado con el fichero
  (lección DW.16).
- `compositeShader.use()` en `App.cpp:4447` con uniforms en `:4452-4462`.

## 3. Cambio propuesto (V5a)

- `bool cinematicGrade = false` persistido y con UI, **default OFF**.
  Parámetros `gradeGrain` (0,03), `gradeVignette` (0,35) y `gradeCA`
  (0,003) con sliders.
- **Orden obligatorio**: los tres efectos se aplican en **LDR, después**
  del `pow` sRGB local de `composite.frag`, nunca antes. No se activa
  `GL_FRAMEBUFFER_SRGB`.
- CA: offset radial por canal sobre `u_scene` **ya resuelto** (sin FBO
  nuevo); se reutiliza la misma cadena scene+bloom+ACES+sRGB para las
  muestras desplazadas y se toma R de una y B de la otra.
- Viñeta: atenuación radial suave en LDR.
- Grano: hash determinista de `vUV * u_resolution + u_time`, amplitud
  acotada; `u_time` llega desde `glfwGetTime()`.
- **Fallback inline sincronizado** con `shaders/composite.frag` (mismo
  cuerpo, mismos uniforms).
- OFF byte-idéntico: con `u_grade=false` el fragment shader no ejecuta
  ninguna rama nueva y la salida es la misma que en `b273f0e`.

## 4. Criterio binario (falsable)

1. **Build/tests**: `cmake --build` rc=0; `ctest` rc=0 con los 65 de la
   base; sin diff en tests.
2. **OFF byte-idéntico**: mismo binario, flag OFF, salida del composite
   igual a la del head; grep muestra las ramas nuevas tras el `pow`.
3. **Coste** con la misma sonda, mismo binario, flag etiquetado
   `GRADE OFF`/`ON`, cámara/epoch/replay intactos: `ON <= OFF + 2,0 ms`
   y techo absoluto 26 ms. Peor ON menos mejor OFF, reportado tal cual.
4. **Sincronización**: el fallback inline y `composite.frag` tienen los
   mismos uniforms y el mismo cuerpo de los tres efectos (grep).
5. **Visual**: PARCIALMENTE NO VERIFICADO si no hay captura válida; no
   afirmar igualdad visual no medida (lección DW.23).

## 5. No-goals

- No FBOs nuevos, no LUT 3D, no bloom mip (V5b).
- No aurora (V7), no tocar F1/F2/F3 ni V1-V4.
- No `GL_FRAMEBUFFER_SRGB`; no cambios en el pase categórico 3b.
- No merge sin veredicto del VERIFICADOR.

## 6. Evidencia

`git diff --numstat`, grep de uniforms y orden de efectos, `ctest`,
bloques `[BENCH-D25] flag=GRADE OFF/ON`, capturas (o salvedad),
`handoff.py check`.
