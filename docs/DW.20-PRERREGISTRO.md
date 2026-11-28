# DW.20 - Retardo L1 → Tierra en el viento solar (OMNI x/v)

Estado: PRE-REGISTRO (sin codigo). Base `origin/master` =
`010382616bb6476c644201573bcb8b387df8b706`. Worktree
`ionosphere-dw20-cpp`, rama `dw.20-l1-delay`.

Origen: plan conjunto F3, veredicto CONFORME CON NOTAS; autonomia total
de David para los tres agentes.

## 1. Problema medido

- `SolarWindAdapter` entrega `latest_` con `speed`, `density`, `bz`,
  `bx`, `timeUtc`, `valid` (`src/Data/SolarWindAdapter.h:6-19`). En
  `fetch()` (`SolarWindAdapter.cpp:168-275`) se construyen historiales
  de Bz y Bx con `time_tag` ISO, pero **solo se conserva la ultima
  velocidad/densidad**.
- `App.cpp` consume `lastWind->latest` sin desplazar:
  - flechas `solarWindLayer.updateArrowData(sw.speed, sw.bz, sw.density, ...)`
    (`:2505`);
  - magnetopausa `magnetosphere.setSolarWind(swMag.speed, ...)` (`:2550`);
  - rio `swRiver.setSolarWind(swRiv.speed, ...)` (`:2565`).
- El punto L1 esta a ~1,5·10^6 km: 3750 s (62,5 min) a 400 km/s y
  1500 s (25 min) a 1000 km/s. Es el estandar OMNI; hoy se usa el dato
  "de ahora" como si ya hubiera llegado a la Tierra.

## 2. Cambio propuesto

### 2.1 Historial completo en el adaptador

- Anadir `std::vector<SolarWindSample> fullHistory_` a
  `SolarWindAdapter`, con getter `getFullHistory()`.
- En `fetch()`, tras ordenar `windRows` y `magHist`, alinear cada fila
  de plasma con la muestra magnetica mas cercana en <= 120 s y guardar
  una `SolarWindSample` completa (velocidad, densidad, Bz, Bx,
  `hasBx`, `timeUtc`, `valid=true`). Recortar a las ultimas ~24 h.
- Si no hay alineacion, guardar la fila de plasma con `bz=0` y
  `valid=false` (no inventar campo). El `latest_` actual no cambia.

### 2.2 Helper puro y testeable

Nuevo `src/Utils/SolarWindDelay.h` (header solo std):

```cpp
namespace swdelay {
inline constexpr double kL1Km = 1.5e6;
struct DelayedWind { SolarWindSample sample; bool available=false; double travelSec=0.0; };
DelayedWind sampleAt(const std::vector<SolarWindSample>& hist,
                     double nowUtc, double distanceKm = kL1Km);
}
```

- Para cada muestra (de mas reciente a mas vieja), `travel = distanceKm /
  max(speed, 1)`. Se devuelve la primera con
  `timeUtc + travel <= nowUtc` (retardo con la velocidad **de esa
  muestra**, no una constante).
- Si ninguna muestra ha tenido tiempo de llegar: `available=false`,
  `sample={}` y `travelSec` de la mas reciente. Se usa el default
  declarado del modulo, **sin inventar dato**.
- Bordes: historial vacio, velocidad 0/negativa/NaN, tiempos futuros.

### 2.3 Consumidores en App.cpp

- Calcular `const swdelay::DelayedWind dw = swdelay::sampleAt(
  impl->lastWind->getFullHistory(), now);` una vez por frame.
- Si `dw.available`: usar `dw.sample` para flechas, magnetopausa y rio;
  si no, conservar los defaults actuales y mostrar en el HUD
  `wind: propagacion L1 pendiente (x/v)` con el `travelSec`.
- Tooltip/etiqueta: declarar `L1 x/v` y el retardo calculado en minutos.

## 3. Criterio binario

1. **Unitarios** (`tests/test_solar_wind_delay.cpp`, nuevo target):
   - 1,5e6 km a 400 km/s → `travelSec=3750`; a 1000 → `1500`;
   - historial con muestra en t=0, `now=4000`, v=400 → disponible y
     devuelta esa muestra; con `now=3000` → no disponible;
   - se elige la muestra mas reciente que ya llego, no la mas vieja;
   - velocidad 0/NaN/inf y array vacio → no disponible, sin NaN.
   - `ctest` rc=0 con el total base + 1.
2. **Build**: `cmake --build` rc=0.
3. **Runtime**: con viento real, log/HUD con `travelSec` entre
   1500 y 3750 s segun velocidad, y captura del HUD mostrando el
   estado propagado; sin historia suficiente, texto "pendiente" y
   defaults (no dato inventado).
4. **Alcance**: no tocar Shue, cinturones (DW.19), TEC, HF, ni
   Web/Android en este slice; `latest_` sigue intacto para el HUD.

## 4. No-goals

- No usar un retardo constante: se calcula con v de cada muestra.
- no mezclar marcos GSM/GSE (Bz sigue GSM, Bx GSE se conserva tal cual).
- no borrar ni reescribir el historial de Bz/Bx de la mini-grafica.
- no merge sin veredicto.

## 5. Evidencia

`git diff --numstat`, grep de `sampleAt`/`fullHistory_`, salida de
`ctest`, captura del HUD con retardo, `handoff.py check` limpio.

## 6. Riesgos

- El feed RTSW publica ~1 muestra/min; 24 h bastan para 62,5 min.
- Si el reloj de replay se usa en ahora, el retardo debe aplicarse
  sobre tiempo de pared del dato, no sobre el epoch del cursor; se
  declara.
