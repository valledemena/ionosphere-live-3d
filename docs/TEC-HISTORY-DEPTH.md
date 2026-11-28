# TEC: profundidad del historial (estado final y registro historico)

## Estado final (DW.17, vigente)

**Ya no hay selector de profundidad.** La app pide siempre el maximo
soportado: `glotbf::kDepth72h = 432` frames a 10 min (72 h nominales;
~69,6 h reales en la cache actual por 5 huecos de 10-13 min).
`tecDepthSlots` se inicializa a 432, `saveSettings` escribe 432 y
`loadSettings` ignora cualquier valor antiguo 36/144. El texto del
panel es `GloTEC history: max 72 h (432 frames), always`.

El timeline **no depende de la profundidad**: en modo capa son los
bounds `[tecHist.front(), tecHist.back()]` de los frames en disco; con
`Full 168 h window` es un rango fijo de 168 h.

Lo que sigue es el **registro historico** del comportamiento anterior
(selector 6/24/72 h y persistencia al cerrar). Se conserva como
evidencia; sus instrucciones estan **supersedidas por DW.17**.

---

Este documento registra un comportamiento medido de la app desktop C++
(NOAA GloTEC). Corrige la frase anterior "72 h disponibles", que se
referia al **horizonte/capacidad** del mecanismo, no a frames presentes.

## Resumen para el usuario

El selector `Timeline > History depth` ofrece **6 h / 24 h / 72 h**
(`tecDepthSlots = 36 / 144 / 432` frames a 10 min de cadencia).

**El valor elegido no se escribe en `settings.cfg` hasta que la app se
cierra.** Cambiarlo mientras la app esta abierta actualiza la variable en
memoria y despierta el ciclo de backfill periodico, pero:

- la **precarga inicial** leyo la profundidad al arrancar y no se repite;
- si el hilo de precarga inicial ya se retiro, el cambio no tiene ciclo
  periodico que lo reciba.

**Procedimiento para aplicar 72 h desde el arranque:** seleccionar
**72 h**, **cerrar la app** (se guarda `tecDepthSlots 432` en
`settings.cfg`) y **volver a abrirla**. Dejarla abierta hasta que termine
la precarga; no cerrarla durante esa descarga.

## Conceptos que se confunden

| Nombre | Valor | Significado |
|---|---|---|
| `glotbf::kWindowSec` | 259200 s (72 h) | Horizonte maximo de descarga |
| `teccache::kHistWindowSec` | 259200 s (72 h) | Ventana de historial |
| `kDepth6h/24h/72h` | 36 / 144 / 432 frames | Frames que pide cada opcion |
| `teccache::kCapFrames` | 432 | Capacidad del anillo (72 h lleno) |
| `tecDepthSlots` (settings.cfg) | 36 por defecto | Opcion persistida |

El indice de NOAA
`https://services.swpc.noaa.gov/products/glotec/geojson_2d_urt.json`
tiene ~4465 entradas y una ventana rodante de **744 h (31 dias)** a
144 frames/dia. Los 72 h recientes estan disponibles en la fuente; el
limite esta en la app, no en NOAA.

## Codigo implicado (head `20c8471`)

- `App.cpp:2898`: `saveSettings()` escribe `tecDepthSlots`; se llama desde
  `App::shutdown()` (`App.cpp:6529`), es decir, **al cerrar**.
- `App.cpp:6302-6310`: el `ImGui::Combo` guarda el atomico y hace
  `depthCv.notify_all()`; **no** llama a `saveSettings()`.
- `App.cpp:1176-1180`: el hilo de precarga espera como maximo 180 s a
  `liveTecArrived`; si no llega, hace `return` y el bucle periodico no
  llega a existir.
- `App.cpp:1209`: la precarga inicial lee `tecDepthSlots.load()` una vez.
- `App.cpp:1242` + `src/Data/PeriodicWait.h`: `waitNextCycle(3600, ...)`
  despierta por cambio de profundidad; solo puede hacerlo si el hilo
  llego a esta seccion.

## Evidencia medida (2026-11-27)

### 1) Cambiar la profundidad en caliente no bastó

Con la app en vivo y el selector ya en `72h`, durante ~10 min de muestras:

```
settings.cfg: tecDepthSlots 144      (aun no persistido)
cache: n=337 -> 338, newest 18:45 -> 18:55 UTC
frames en 72 h: 262 -> 263           (solo 1 frame, cadencia del worker)
conexiones TCP: 0 salvo un instante con 2
```

No aparecio el backfill de ~170 frames que implicaria pasar de 24 h a 72 h.

### 2) Cerrar y relanzar con 432 si funcionó

Tras `CloseMainWindow`, `settings.cfg` quedo con `tecDepthSlots 432`.
Al relanzar, stdout de la app:

```
[GloTecAdapter] Index OK: 4464 frames
[GloTecAdapter] Index OK: 4464 frames
[App] History preload: 222 cached + 204 fetched (207 planned, depth 432, 488.1 MB red)
```

Crecimiento de la cache en ~13 min (14 muestras):

```
frames en 72 h: 263 -> 265 -> 285 -> 305 -> 322 -> 334 -> 352 ->
                372 -> 381 -> 395 -> 398 -> 413 -> 429 -> 432
ritmo ~20 frames/min
```

Estado final:

```
n = 432  (tope de kCapFrames)
oldest = 2026-11-24 21:15 UTC
newest = 2026-11-27 18:55 UTC
frames en ultimas 72 h = 432  (rango nominal 72 h; span real 69,67 h)
```

Precision (veredicto `VEREDICTO_PR26_CIERRE.md`): "ventana completa y
continua" era una afirmacion demasiado fuerte. Con `n=432` hay **8 huecos
mayores que 600 s** (618-1200 s, es decir 10-20 min) y 383 intervalos de
600 s; todos los huecos son menores que `kLoopGapJoinSec=3600`, por lo que
DW.14 no tiene nada que saltar en esta sesion. Ademas `planned=207` pero
`stored=204`, sin errores de transporte, y dos lineas de stderr
`[GloTecAdapter] Index empty: HTTP 200, 0 frames` conviven con los
`Index OK`: el log no esta limpio, aunque la precarga principal funciono.

El bloque antiguo del 15 de septiembre desaparecio: al llenarse el anillo
de 432 frames, los frames mas viejos se descartan. Es esperado y sano.

## Consecuencias

- Para ver 72 h de TEC al abrir, hay que **cerrar la app despues de
  seleccionar 72 h**; de lo contrario la sesion en curso no adoptara esa
  profundidad en la precarga inicial.
- Una vez descargados, los frames permanecen en `cache/` aunque se cierre
  la app. El anillo de 432 descarta los mas viejos cuando entran nuevos.
- Con 432 frames no hay hueco mayor que 3600 s (el umbral de DW.14),
  pero si 8 huecos de 10-20 min; la cobertura no es estrictamente
  continua. DW.14 sigue siendo util para suspensiones que dejen huecos
  mayores que ese umbral.
- No se modifico codigo para este hallazgo: es comportamiento documentado.
