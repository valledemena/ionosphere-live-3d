# Baseline FPS pre-M1 (16b6fa5 + Fase 0)

**Fecha:** 2026-09-07. **Máquina:** la de David (Windows, pantalla 1360x768).
**Build:** Release MSYS2 UCRT64 + patch Fase 0. **Settings base:** toneMap 0,
bloom ON (0.201/0.69/0.37), exposure 0.524, resto defaults del usuario.

> Desviación del protocolo §16.0: no hay 1080p en esta máquina (pantalla
> 1360x768). Se mide a pantalla completa 1360x728 siempre. Cine-mode ON
> (default), lecturas HUD instantáneas (varían con el contenido ±30%).

| Config | Ventana | FPS HUD | Captura |
|---|---|---|---|
| Volumen OFF (hidden) | ~1340x720 | 77.8 | `%TEMP%\base_off.png` |
| Volumen OFF (hidden) | 800x600 (resize M0.1) | 46.2 / 38.4 | `base_resize800.png`, `base_off2.png` |
| Volumen ON modo scene (`[logNe]`) | 1360x728 | 33.1 | `base_volscene.png` |
| Volumen ON modo categórico (`[layer]`, Pass 3b) | 1360x728 | 27.3 | `base_volcat.png` |

**Lectura:** el volumen cuesta ~2-3x vs OFF a esta resolución (33/27 frente a
~40-78). El modo categórico post-composite rinde parecido al de escena
(27 vs 33, dentro del ruido de contenido). Todo con datos vivos
(GIRO 27/129, historia 10-18/18 en curso durante las medidas).

**Criterio para M1 y siguientes:** ninguna fase degrada estos números >10%
con toggles OFF; los nuevos defaults (MSAA, atm High) deben validarse contra
la fila que les corresponda. Si MSAA 4x cuesta >15%, default a 2x.
