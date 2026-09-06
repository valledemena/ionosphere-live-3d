# Plan: cáscaras derivadas del volumen (coherencia 2D ↔ 3D)

Objetivo: generar capas 2D baratas **extraídas del `DensityVolume`** en vez
de interpolar las estaciones por segunda vez, de modo que cáscara y volumen
nunca se contradigan (misma fuente). No se elimina nada existente; se añaden
dos toggles. Alcance deliberado: reutilizar, no reinventar.

## 1. Por qué extraer del volumen y no re-interpolar

`buildDensityVolume` ya combina perfiles + día/noche + flares en un campo
coherente. Re-interpolar estaciones para las nuevas cáscaras duplicaría
lógica y podría discrepar del raymarch. Extrayendo del volumen, la cáscara
**es** una vista del volumen por construcción.

## 2. Mecanismo: `GridLayer::updateFromGrids(valueGrid, heightGrid)`

Hoy `GridLayer` desplaza vértices por valor normalizado (`setDisplace`) o
centrado (`setDisplaceCentered`). Añadir un tercer modo: geometría desde una
rejilla de **alturas reales** (km) y color desde una de **valores**:

```cpp
void GridLayer::updateFromGrids(const GridData& values, const GridData& heights);
// posiciones: r = 1 + clamp(heights bilinear, 60..600)/6371 * heightExaggeration(1.0)
// colores:    maquinaria existente (colormap/range) sobre values
```

Reutiliza `sampleRaw` (bilineal con wrap, ya existe) para ambas rejillas.
Sin cambios de shaders ni de VAO. Coste: un draw de esfera más.

## 3. Extracción pico/altura desde `lastVolume`

`App` ya guarda `lastVolume` (72×72×48, log10 normalizado). Nuevo helper
puro en `DensityVolume.h`, testeable sin GL:

```cpp
struct PeakGrids { GridData logNe; GridData hmKm; };  // layout GridData 72x72
PeakGrids extractPeakGrids(const DensityVolume& v);
// por columna: argmax sobre alturas -> logNe = 8+4.5*val, hmKm = alt del maximo
// 250k lecturas por rebuild (~ms). Ojo: con volumen vacio/nocturno el "pico"
// puede ser ruido bajo -> umbral: si peak < 8.5 se marca hmKm = 0 (hueco).
```

Ventaja clave: funciona en live Y en replay (lee `lastVolume`, que ya sigue
a ambos) sin tocar el pipeline de refresco.

## 4. Las dos cáscaras nuevas (solo UI + 2 GridLayers)

- **F2 peak shell** (checkbox en Layers, junto a Outer shell): geometría =
  `hmKm` real por columna, color = `logNe` (foF2 equivalente). En tormenta se
  ve SUBIR y enrojecer literalmente. Colormap auto plasma, opacidad 0.9.
- **TEC-column** (checkbox, mismo sitio): integral vertical del volumen →
  mapa TECU *modelado*, colormap viridis. Sirve para comparar contra GloTEC
  medido (el sesgo en océanos delata el fondo mediano: honestidad visible).

MUF/FOT/LUF/Spread-F se quedan como están (MUF necesita M(D); Spread-F es
estadística, no sale de un perfil medio). Nada se elimina.

## 5. Cableado en `App.cpp`

- Miembros: `GridLayer peakShell, tecColShell;` + init (colormaps fijos,
  opacidades, `setVisible(false)`), destroy, persistencia (`peakShell`,
  `tecColShell` en `settings.cfg`).
- Rebuild (en el bloque de volumen cada 5 s, tras `lastVolume = vol`):
  `auto pk = extractPeakGrids(vol); peakShell.updateFromGrids(picoFoF2(pk),
  pk.hmKm); tecColShell.updateFromGrids(integrateTEC(vol), dummyFlat);`
  (`integrateTEC`: suma Ne·dh por columna → TECU; `dummyFlat` = grid constante
  a 300 km para geometría plana.)
- Render: en el bloque aditivo tras `shellLayer`, con sus `isVisible()`.

## 5bis. Fórmulas exactas cerradas (review: no reinventarlas al codificar)

El volumen guarda `log10(Ne)` normalizado a `[0,1]` sobre rango fijo
`[8, 12.5]`, y las alturas NO son uniformes (`^1.5`). La integral debe
des-normalizar y usar el `Δh` real por tramo (trapecio):

```cpp
// TEC[TECU] = (1/1e16) * integral trapezoidal de Ne[m^-3] dh[m]
float integrateTEC(const DensityVolume& v, int latIdx, int lonIdx) {
    double tec_m2 = 0.0;
    for (int a = 0; a + 1 < v.altRes; a++) {
        float n0 = v.data[((size_t)a*v.latRes+latIdx)*v.lonRes+lonIdx];
        float n1 = v.data[((size_t)(a+1)*v.latRes+latIdx)*v.lonRes+lonIdx];
        float Ne0 = powf(10.0f, 8.0f + 4.5f*n0);   // des-normalizar
        float Ne1 = powf(10.0f, 8.0f + 4.5f*n1);
        float dh_m = (volumeAltKm(a+1,v.altRes,v.altMinKm,v.altMaxKm)
                    - volumeAltKm(a,  v.altRes,v.altMinKm,v.altMaxKm)) * 1000.0f;
        tec_m2 += 0.5 * (Ne0 + Ne1) * dh_m;
    }
    return (float)(tec_m2 / 1.0e16);
}
```

Refinamiento sub-muestra del pico (el paso cerca de la F2 es ~10-13 km;
sin esto la `hmKm` discreta salta ±5-6 km y la validación §6 lo marcaría
como bug siendo solo resolución): parábola de 3 puntos alrededor del máximo:

```cpp
float y0 = d[i-1], y1 = d[i], y2 = d[i+1];
float denom = (y0 - 2*y1 + y2);
float delta = (fabsf(denom) > 1e-6f) ? 0.5f*(y0-y2)/denom : 0.0f;
delta = clamp(delta, -1.0f, 1.0f);
float hmRefined = h[i] + delta * (h[i+1]-h[i]);  // paso local aprox
```

Rango vertical (decisión explícita): `altMaxKm` 500 → **700**,
`altRes` 48 → **60** (coste +25% en build, ~nulo en raymarch: los pasos son
fijos). Motivo: en tormenta fuerte la hmF2 real supera 500 km y cortarla
ahí amputaría justo el fenómeno que vende la feature. Además, avisar
(`printf`) cuando una hmF2 interpolada supere 650 km (cerca del techo).
`SliceLayer::updateSlice` y el bucle de `rebuildSlice` parametrizan su rango
(hoy 60-500 hardcodeado) desde `lastVolume.altMinKm/altMaxKm` para no
quedarse atrás.
- UI: dos checkboxes + tooltips EN (patrón existente). Legend: al activar
  una, mostrar su unidad (MHz / TECU) — extender `LEG_NAMES/UNITS` o una
  línea propia si conviven con Variable.

## 5ter. Contrato UI: todo conmutable (regla del proyecto)

Nada de lo nuevo se muestra sin interruptor. Patrón obligatorio (el mismo de
todas las capas existentes):
- Cada capa/overlay nuevo = checkbox propio en Layers + tooltip EN.
- Todo lo que tenga intensidad = slider (opacidad).
- Todo persiste en `settings.cfg` (load + save + aplicar en `init`).
- Nada visible por defecto salvo que aporte al primer arranque (las dos
  cáscaras nuevas arrancan **ocultas**; el volumen ya existe y sigue igual).
- El render respeta `isVisible()` dentro del bloque aditivo; con todo
  apagado el frame queda en Tierra+nubes+estrellas.

## 6. Validación

- Unitaria: volumen sintético con pico conocido → `extractPeakGrids` devuelve
  ese pico y esa altura (patrón `test_*.cpp` existente).
- Visual §9: el anillo F2 de `RefShells` (mediana hmF2) debe flotar pegado a
  la altura media de la F2 peak shell; los puntos GIRO NmF2 deben coincidir
  en color con la cáscara bajo ellos.
- TEC-column vs GloTEC: de día y sobre tierra deben parecerse; en océanos de
  noche divergen (esperado y documentado en el tooltip).

## 7. Fases

1. `updateFromGrids` + `extractPeakGrids`/`integrateTEC` + tests (sin UI).
2. F2 peak shell cableada (init/UI/render/persistencia).
3. TEC-column + comparativa.
4. (Opcional, posterior) Deprecar `shellLayer` (Outer shell) si la peak shell
   la vuelve redundante — decidir con captura en mano, no antes.
