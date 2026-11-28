# DW.34 — V11b: marcadores de sondas y Luna en magnetosfera

Estado: PRE-REGISTRO (sin código de la feature). Base `origin/master` =
`ac156becfb51a848c215c8bbb82e7b36d28c6bd7`. Worktree `ionosphere-dw34-cpp`,
rama `dw.34-heliosphere-probes`.

Cierra V11 (V11a ya mergeada: cámara 200 R_E + espirales Parker).

## 1. Coste

Dos draws nuevos (billboards + esfera lunar) → `DRAW_GATE`
(`glFinish` + reloj, 300 muestras, p50/p95/min/max, **ambos brazos**
llaman a `glFinish`, emparejados por índice), gate
`ON_draw_p50 <= OFF_draw_p50 + 2,0 ms`. OFF ejecuta 0 draws.
**Frame completo NO MEDIBLE** por DW.30 (bug 90,8 MB abierto); coste
real NO VERIFICADO; veredicto PARCIAL.

## 2. Honestidad (restricción dura)

- Las sondas Voyager están a ~170 UA: **no se dibujan a escala real**
  (la Tierra sería <1 px). Los marcadores son **ILUSTRATIVOS**, a una
  escala comprimida declarada, y sólo aparecen si el adaptador tiene
  `fix.valid`; la distancia real sigue en el panel Voyager.
- La Luna **no tiene efemérides en vivo** en este proyecto: su fase
  será un **modelo sintético declarado** (`angulo = t * omega`,
  radio 60 R_E), NO un dato JPL. El tooltip lo dirá explícitamente.

## 3. Cambio propuesto

Clase `HeliosphereProbes` con dos flags **OFF** y persistidos:

- `probeMarkersEnabled`: dos billboards (GL_POINTS) para V1/V2 en
  posiciones comprimidas declaradas (60 y 75 R_E) con colores fijos;
  aparecen sólo si `voyagerSnap.v1/v2.fix.valid`. Un draw.
- `moonCrossingEnabled`: esfera low-poly (UV 12x8) a 60 R_E con fase
  sintética en el tiempo; un draw. Declara "model-only, no ephemeris".

Sin FBOs nuevos; no tocar V11a ni el panel Voyager.

## 4. Criterio

1. build/ctest rc=0 (65), sin diff tests; OFF byte-idéntico.
2. DRAW_GATE con 2 draws (OFF 0), p50/p95/min/max.
3. Grep de escala comprimida, `fix.valid`, `model-only` y 2 draws.
4. Visual PARCIALMENTE NO VERIFICADO si falla la captura.

@IMPLEMENTADOR
