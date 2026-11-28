# HF Circuit en replay: fuente, estado honesto y color de la ruta

Documento de comportamiento vigente tras DW.15, DW.16, DW.17 y DW.18
(master `b3d264e`). Solo lectura del codigo + evidencia medida; no
describe comportamiento futuro.

## 1. Que alimenta el circuito

El circuito HF **no se alimenta de TEC**. En replay se usa `gsAct`
(`App.cpp:2200` en `6cadad2`): `giroReplayCache` interpolada al epoch
del cursor, o la fusion `mergeKc2g(...)` cuando la fuente activa es
kc2g. Los grids que entran en `HFTraceLayer::Input` son:

- `foF2` y `hmF2` de GIRO (`giroInterpGrid`, vars 0 y 2),
- `LUF` del modelo M7 (`radioInterpGrid`, var 0).

`MUF = min(MUF por salto)`, `FOT = 0.85 * MUF` y `LUF = max sobre
saltos` del grid M7 (`HFTraceLayer.cpp:183-198`).

## 2. Estado fresco de la fuente activa (DW.15)

`circuitHasFreshDataInRange(epoch, oldest, newest, samplerBusy)` decide
si el epoch del cursor tiene dato real:

- en modo **kc2g**, rango = `kc2g.historyStats` (`t0=min`, `t1=max`) o
  min/max de `giroReplayCache`; nombre de fuente `kc2g`;
- en modo **GIRO**, rango = `[now - histHours*3600, now]`; nombre
  `GIRO`.

Si no hay dato fresco, el panel muestra:

```
HF circuit: sin datos <fuente> en este epoch (hist X.X h; +Y)
HF circuit: sin datos <fuente> en este epoch (antes del historial; hist X.X h)
HF circuit: sin datos <fuente> (historial vacio)
```

`X.X` es la **amplitud del rango activo** `(t1-t0)/3600`, no
`histHours` de GIRO; `+Y` es `staleGapText(gapSec)` verbatim (`+N min`
o `+A.B h`). La matriz de bandas muestra `sin datos`.

## 3. La ruta SIEMPRE se dibuja (DW.16)

Con `HF trace layer` activado, el ribbon se renderiza aunque no haya
dato. El estilo lo decide `u_noData` en el shader:

- sin dato: color gris neutro `hfc::traceColorNoData() =
  (0.55, 0.58, 0.62)`, **sin pulso ni neon**, alfa `0.65 * prof`
  (aspecto fantasma, estatico). Marcadores TX/RX tambien grises.
- con dato: verde/ambar/rojo y pulso viajero, sin cambios respecto a
  la version anterior.

El gris significa explicitamente: *trayecto geometrico visible,
atributos no calculados con dato fresco*.

## 4. Color del arco con dato: LUF antes que MUF (DW.18)

`hfc::hopViableWithLuf(f, MUF_salto, LUF_circuito)`:

1. si `LUF > 0` y `f < LUF` -> `0.0` (**rojo/cerrado**);
2. si no, `hopViable(f, MUF_salto)`: verde si `f <= 0.85*MUF`,
   ambar si `f <= MUF`, rojo si `f > MUF`.

`hopViable` queda intacto; el LUF es de circuito (peor salto), por lo
que puede cerrar visualmente un tramo cuyo MUF local lo permitiria. Es
coherente con la matriz de bandas.

La matriz usa `hfc::bandState(f, LUF, FOT, MUF)`: abierta entre `LUF`
y `FOT`, marginal hasta `MUF`, cerrada fuera. Por eso las bandas bajas
por debajo del LUF aparecen `closed` (las bandas se cierran hacia
abajo); el arco hace lo mismo desde DW.18.

## 5. Evidencia

- DW.15: `CROSS verificador_hy4/DW15/VEREDICTO_EJECUCION_V3.md`,
  `dw15v3_kc2g_inside_pw.png`, `dw15v3_kc2g_outside_pw.png`.
- DW.16: `CROSS verificador_hy4/DW16/VEREDICTO_EJECUCION.md`,
  `dw16_outside3_t1.png`, `dw16_outside3_t2.png`, `dw16_inside.png`.
- DW.18: `CROSS verificador_hy4/DW18/VEREDICTO_EJECUCION.md`,
  `dw18_1_0.png` (rojo a 1 MHz), `dw18_10_0.png` (verde a 10 MHz).
- Despliegue final: `CROSS verificador_hy4/FINAL/VEREDICTO_REDEPLOY_FINAL.md`,
  `final_deploy.EVIDENCE.txt`.

## 6. Limites

- No se usa IRTAM como fuente del circuito (queda como opcion futura).
- TEC no da `foF2`/`hmF2` en los puntos de reflexion: no se deriva MUF
  de TEC.
- El `Full 168 h window` cambia el rango del timeline, no la fuente del
  circuito.
