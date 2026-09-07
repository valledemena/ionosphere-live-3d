# Plan: identificar capas + corte de limbo + vida + cine

Combina ambas listas con criterio propio donde difieren (anotado como
DECISIÓN). Reglas globales: ningún cambio toca `evalNeTotal`/`DensityVolume`
salvo añadir un campo; todo lo nuevo trae checkbox + tooltip EN +
persistencia y arranca apagado; nada altera datos, solo representación.

## Decisiones donde me aparto de Claude (documentadas)

- **Slice A=B**: tal cual degenera en línea de ancho cero (slerp idéntico).
  En vez de "reutilizarlo", modo cortina de ancho fijo: offset perpendicular
  explícito ±2° alrededor del punto. (~15 líneas en `rebuildSlice`.)
- **Cortinas de aurora verticales**: nuestra aurora es una cáscara plana sin
  dimensión vertical → solo parpadeo de brillo (modulada por Kp), no scroll
  vertical. Sin cortinas falsas.
- **Sin auto-dimming `1/sqrt(n)`** (decidido antes): las capas son
  independientes; el presupuesto visual lo pone el usuario con opacidades.
- **Isosuperficies sí**, pero como bandas de logNe fijo (10.5/11/11.5) por
  uniform, no umbrales de Nm por capa (varían por posición y confundirían).

## Fase 1 — Identidad de capas (ver para creer)

1. **Canal ganador en el volumen**: `buildDensityVolume` escribe además por
   muestra la rama ganadora del `max()` (0=D,1=E,2=F1,3=F2). Textura 3D
   `R8` → `RG8` (R=densidad, G=ganador/4; ~500 KB, trivial). `updateVolume`
   sube ambos; el shader añade `uniform int u_colorMode` (0=densidad viridis,
   1=categórico 4 colores fijos).
   OJO FILTRADO (review): `GL_LINEAR` corrompería los IDs en fronteras
   (1.7 entre E y F1 no es ninguna capa). En GLSL, canal G por `texelFetch`
   (coordenadas enteras, sin filtrar) y canal R por `texture()` en el mismo
   shader: `ivec3(uv.x*lonRes, uv.y*latRes, altNorm*altRes)` redondeado y
   clampedo (pasar dims como `uniform ivec3 u_volRes`). Cero cambios en C++.
2. **Combo Densidad/Capas** en Layers + leyenda que cambia con el modo
   (escala continua vs 4 etiquetas). Tooltip EN.
   BYPASS TONEMAP + BLOOM (review): la capa categórica se renderiza DESPUÉS
   del composite (directo a pantalla, como ImGui) y SIN bloom. Motivo
   decisivo (no solo precedente): el `texelFetch` fija fronteras exactas y
   el blur las volvería a difuminar en pantalla, deshaciendo lo arreglado
   en el sampler. Si el plano resulta "pegote", alternativa aprobada: rim
   fino por gradiente de normal en el mismo pase (nunca blur posterior).
3. **Regla de altitud HUD**: barra vertical 60–700 km fija en lateral, marcas
   D/E/F1/F2 nominales + indicador móvil del pico real bajo cursor/pineada
   (máximo de columna vía `sampleVolume`, bajo demanda al cambiar hover —
   48 lecturas, despreciable).
4. **Etiquetas 3D**: 3 anclas proyectadas (pico F2 sobre pineada, óvalo,
   shell TEC) con texto ImGui + líder. Toggle. Se apagan con su capa.
5. **Vista explotada**: slider `Separación ×1–×5` que multiplica offsets de
   altitud (TEC/outer/volumen/anillos). Tooltip.
6. **Solo-al-hover en leyenda**: hover sobre una entrada aísla esa capa.
- Tests: canal ganador con perfiles sintéticos puros (F2-only → 3 en todo
  lo alto); categórico determinista; `extractPeakGrids` intacto.
- Validación visual §9: con modo Capas, norte diurno debe leerse "zona F2"
  donde el TEC es alto, y el slice debe cruzar los colores coherentemente.
- Iso-bandas y PiP: movidos a Fase 2 (ver abajo) para mantener este hito
  compacto; la paleta categórica ya da el grueso del valor.

## Fase 2 — Cortes (limbo + meridiano) + iso-bandas

0. **Iso-bandas**: uniforms `u_isoOn/u_isoLevels[3]/u_isoWidth` en el frag
   del volumen (realzan logNe 10.5/11/11.5 sobre cualquier modo). Toggle.
   (Movido aquí desde Fase 1: es el ítem con más superficie de validación.)
1. **Slice generalizado**: endpoints lat/lon libres (no solo estaciones) +
   modo punto-fijo (cortina de ±2° con offset perpendicular explícito —
   A=B degenera, no reutilizar).
2. **PiP de limbo**: lupa x3 del limbo este con el propio framebuffer
   (IMPLEMENTADO ASI en vez de segundo render: cero coste de render, misma
   imagen, sigue al limbo automaticamente; ver `Limb x3` en App.cpp).
   Alternativa anotada y descartada: segundo render a 1/4 (doble coste +
   refactor de `render()`; reconsiderar solo si la lupa no basta).
   Alternativa anotada (no elegida): corte ortográfico muestreado con la
   función de transferencia del volumen horneada (como `SliceLayer`) —
   más barato pero un escalón de acabado por debajo; reconsiderar si el
   doble render pesa en iGPU.
3. Validación: el corte PiP y el slice sobre el mismo punto muestran las
   mismas bandas.

## Fase 3 — Vivo sin tocar datos (solo shaders/UI)

1. **Domain warp**: 1 octava de baja frecuencia animando la posición de
   muestreo en el raymarch (uniform on/off + cantidad). Coste iGPU: detrás
   de toggle de calidad + lookup a textura de ruido 3D precalculada
   (32³ R8, 32 KB, generada una vez en `init`) en vez de ruido algebraico
   (10-20 instrucciones/muestra), + bajar pasos 64→48 mientras el warp esté
   activo como colchón.
2. **Shimmer aurora**: flicker de brillo modulado por Kp (honesto: sin scroll
   vertical falso sobre cáscara plana).
3. **Partículas Faraday**: Points procedurales derivando por los vectores
   (semilla fija, wrap). Parallax gratis para el volumen.
4. **Pulso global**: ±5% en opacidad del volumen/bloom atado a velocidad de
   viento o Kp normalizados (movimiento con significado).
5. **Flow en cáscaras 2D**: máscara alpha procedural con scroll lento en el
   frag de `GridLayer` (sin tocar su colormap), sutil.
6. Tests: ninguno nuevo exigible salvo smoke (todo es visual); el dato sigue
   cubierto por los tests existentes.

## Fase 4 — Cine

1. **Letterbox + grano + viñeta** (+ CA sutil) en `composite.frag`, toggles
   separados. 20 líneas, 80% del look.
2. **Lens flare** en el sol proyectado (billboard + streaks; el bloom ya
   existe y ayuda).
3. **Cámara narrativa**: en flare M/X o tormenta (Kp≥7), slerp lento de
   `rotateX/Y`+distancia hacia el lado diurno / aurora. Toggle "Director".
4. **Highlight-reel**: acelera el tiempo solo y frena ante eventos
   (extiende `replaySpeed` + `cineSpeed` existentes, no piezas nuevas).

## Fuera de alcance (a propósito)

- Nada de `evalNeTotal`/física: el volumen de datos no se toca.
- Nada siempre-activo sin interruptor.
- Sin texturas nuevas ni endpoints nuevos.
- Futuro (no este plan): contribución auroral real dentro de `evalNeTotal`
  (producción E-region a alta latitud magnética modulada por Kp/AE) — daría
  cortinas con dimensión vertical de verdad, con motivo físico y no cosmético.
