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

## Post-M1 (mismo HW, 1360x728, 2026-09-08)

- **MSAA en este driver: NO SOPORTADO (ni 4x ni 2x).** Intel 20.19.15.x:
  `Framebuffer (MSAA 4x/2x) incomplete` → fallback a 0x con aviso en stderr,
  app 100% funcional. El coste MSAA aquí es 0 por construcción (resolve no-op).
  La validación visual del AA queda para una GPU capaz.
- Vol scene + tone + bloom (MSAA pedido 4x → fallback 0x): **16.1 FPS**
  (lado noche; baseline 33.1 era lado día — la varianza la domina el contenido,
  no M1: con fallback el path es idéntico al pre-M1).
- Migración v1→v2 verificada: `bloomIntensity 0.69 → 0.4761` (= I², D1).
- Determinismo: mismo binario a 12 s = 80.6% píxeles exactos (resto: twinkle
  de estrellas); pre-patch = 91.5% / mismo perfil. V1-cero literal imposible
  en LIVE (sol + nubes con u_time + refresco de datos); igualdad del path
  MSAA-0 probada por auditoría + perfil idéntico.
