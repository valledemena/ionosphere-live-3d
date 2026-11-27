# TEC: la profundidad del historial solo se persiste al cerrar la app

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
frames en ultimas 72 h = 432  -> ventana completa y continua
```

El bloque antiguo del 15 de septiembre desaparecio: al llenarse el anillo
de 432 frames, los frames mas viejos se descartan. Es esperado y sano.

## Consecuencias

- Para ver 72 h de TEC al abrir, hay que **cerrar la app despues de
  seleccionar 72 h**; de lo contrario la sesion en curso no adoptara esa
  profundidad en la precarga inicial.
- Una vez descargados, los frames permanecen en `cache/` aunque se cierre
  la app. El anillo de 432 descarta los mas viejos cuando entran nuevos.
- Con la ventana de 72 h llena no hay hueco interno; DW.14 (salto de
  huecos en modo capa) sigue siendo util para sesiones anteriores o
  suspensiones que dejen huecos.
- No se modifico codigo para este hallazgo: es comportamiento documentado.
