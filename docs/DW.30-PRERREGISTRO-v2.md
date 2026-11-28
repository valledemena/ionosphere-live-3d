# DW.30 — V10b v2: metodología de coste bajo carga de red

Estado: PRE-REGISTRO v2 (sin código de la feature). Base `origin/master` =
`354149bcc51c445ba148ca7026649be0fb4fb131`. Worktree `ionosphere-dw30-cpp`,
rama `dw.30-mid-layers`.

## 1. Problema medido con el entorno

Dos corridas de baseline con sonda de frame completo dieron:
```
corrida 1: 147,712 / 34,708 / 26,234 / 29,288 / 27,930 / 41,232 ...
corrida 2: 263,205 / 44,599 / 16,603 / 17,838 / 34,728 / 98,869 /
           35,142 / 35,365 ms
```
Causa observada en los logs: `History preload: 391 cached + 38 fetched
(90,8 MB red)`, fetch SDO A304/HMIIF y bucket IRTAM simultáneos. El
propio brazo **OFF** supera el techo de 26 ms en varios bloques, por lo
que el criterio original del pre-registro v1 (frame completo + techo
absoluto) **no es medible de forma fiable ahora**.

## 2. Enmienda metodológica propuesta (v2)

1. **Coste relativo**: mismos bloques de 300 frames, flag etiquetado,
   mismos brazos que se puedan medir, comparando el **mínimo steady**
   de cada brazo en la misma corrida (el mínimo es el bloque menos
   contaminado por red). Criterio:
   `min(ON) <= min(OFF) + 2,0 ms`.
2. **Techo absoluto 26 ms: NO VERIFICADO** en este entorno; se declara
   explícitamente y no se afirma que se cumple.
3. Se reportan **todos** los bloques, incluidos los contaminados, sin
   cherry-picking. Si el verificador prefiere otra metodología, se
   re-ejecuta antes de ejecutar la feature.

## 3. Alcance de la feature (sin cambios respecto a v1)

Tres capas nuevas en `src/Ionosphere/MidIonoLayers.*`, flags
**independientes OFF** y persistidos:
- **Plasmasfera**: carcasa paramétrica con escala L, opacidad por Kp
  vivo (`currentKp()`). Proxy visual, no densidad física.
- **Anomalía ±15°**: dos crestas a ±15° de latitud **magnética** (eje
  IGRF‑14) a 300 km, moduladas por el TEC vivo.
- **TOI**: onda viajera analítica `sin(m·lon − ω·t)` a 300 km con
  amplitud por Kp/AE; animación declarada, no dato medido.

Un draw aditivo por capa, sin FBOs nuevos, `RadioPropagationAdapter`
y las capas V1–V10a intactas. Criterio visual con los seis flags de
post/capas declarados; NO VERIFICADA si falla la captura.

## 4. Falsabilidad

- grep de `L_pp`, `±15`, `sin(`, `currentKp()`, eje IGRF.
- `ctest` rc=0 (65), sin diff en tests, OFF byte-idéntico por ramas.
- Bloques `[BENCH-D30] arm=N ...` con todos los valores.

@IMPLEMENTADOR
