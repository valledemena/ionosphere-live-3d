# Guía docente — Ionosphere Live 3D

Para uso en aulas, clubes de radio y actividades de divulgación científica.
Identificador público: repositorio GitHub `valledemena/ionosphere-live-3d`.

## Objetivos de aprendizaje
- Comprender la ionosfera como medio para la propagación de ondas de radio HF.
- Distinguir medición (TEC, foF2, hmF2) de modelo (IRTAM) y datos derivados.
- Interpretar estados de una fuente de datos: ok / stale / degraded / rate-limited / failed.
- Entender que la ausencia de datos no se rellena con inventos (diseño honesto).

## Mapa capa → concepto
- **TEC (total electron content)**: variabilidad diurna/estacional, efecto
  solar/geomagnético; leer el legend de unidades (TECU).
- **foF2 / MUF / hmF2**: densidad de plasma y altura del pico F2; impacto en la
  frecuencia máxima utilizable.
- **IRTAM (foF2, hmF2, B0, B1)**: asimilación de datos; retardo declarado
  (~3 días) por diseño. Comparar IRTAM vs GIRO.
- **Aurora (Ovation) / viento solar / Kp / Dst**: acoplamiento sol-Tierra,
  magnetosfera y actividad geomagnética.
- **HF trace (Circuit)**: path de rayo por saltos; estados abierto/marginal/cerrado.
- **Extras (magnetopausa Shue, Van Allen, West Ford)**: geometría y escala física.

## Actividades sugeridas (60–90 min)
1. **Mediodía vs medianoche**: observar TEC y foF2; relacionar con terminador.
2. **Tormenta simulada**: subir Kp en settings/visual; ver aurora y belts.
3. **HF**: elegir banda; comparar MUF y hoja de propagación con el trace layer.
4. **Dato faltante**: activar capa con fuente sin conexión; identificar mensaje
   "off/failed" y edad del dato.

## Preguntas de reflexión
- ¿Por qué el IRTAM tiene retardo por diseño y no pretende ser "tiempo real"?
- ¿Qué ventaja pedagógica tiene que una capa se quede vacía en vez de
  sintetizar datos?
- ¿Cómo afecta la geomagnetismo (Bz, Kp) a la ionosfera polar?

## Fuentes y licencias
Consultar `CREDITS.md` en el repositorio. Citar siempre las fuentes (NOAA, GIRO,
Ebre, CCMC, Kyoto, JPL, etc.) en trabajos derivados.

## Instalación mínima (para clase)
```
cmake -B build -G Ninja
cmake --build build -j
./build/ionosphere-live-3d
```
Tests sin GPU: `ctest --test-dir build`.
