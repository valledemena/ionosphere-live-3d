# ADR R4.7 — Decisión NDK-MSIS

> Estado: **PROPUESTA — pendiente de decisión explícita de David y dictamen de GLM**.
> Fecha: 2026-11-21. Preparado en el repo raiz; destino final una vez aprobado:
> `docs/r4/ADR-NDK-MSIS.md` en `valledemena/ionosphere-live-3d-android`
> (PR docs-only, sin código). No se ha publicado ni enviado a GLM.

## 1. Contexto

El plan R4 (`docs/r4/PLAN-R4-TIEMPO-PROFUNDIDAD.md`, secciones 1.1 E7, 5
R4.7, 7 E7 y 10 Q1) deja una decisión diferida:

> **Q1**: "¿El volumen y la cortina necesitan NDK-MSIS o basta el perfil de
> capas? Diferir a R4.7; decidir con la paridad medida, no antes."

R4.7 se ejecuta "después de R4.4 y R4.6, cuando ya se sabe si el perfil de
capas cubre la cortina y el volumen con paridad aceptable". Esa condición ya
se cumplió: R4.4 (modelo Ne(h) puro), R4.5 (cortina), R4.6 / R4.6.3
(raymarch `density`/`chapman`) y R4.6.5 (gate visual en dispositivo) están
cerrados. La tabla de riesgos del plan añade: "NDK-MSIS entra sin licencia
→ bloqueo legal/distribución → ADR obligatorio; sin ADR no hay dependencia".

## 2. Decisión propuesta

**NO introducir NDK-MSIS en R4. Documentar el descarte.**

- No se añade dependencia nativa, toolchain NDK, ABI ni artefacto `.so`.
- Licencia declarada: **N/A** — no entra código de terceros; por tanto no
  hay licencia de modelo atmosférico que revisar ni distribuir.
- El perfil de capas Kotlin (`density`/`chapman`) sigue siendo la única
  fuente del fondo del modelo para cortina y volumen.
- Si en el futuro aparece un requisito que exija MSIS nativo, hará falta un
  **ADR nuevo** con ABI, licencia verificada, tamaño, build, fallback e
  impacto en FPS, y su implementación irá en un **PR aparte**, nunca dentro
  de R4.7 (según el propio plan).

## 3. Evidencia que sustenta el descarte

1. **Paridad del modelo, cerrada por GLM.** R4.6.3 con R1 fijada (`innerR =
   PLANET_R + 60/6371 = 1.00942`, espejo C++ `VolumeRenderer.cpp:256/257`)
   fue dictaminada FAVORABLE en el acta `0430`; el oráculo python de GLM
   refrendó el barrido y el packing byte-igual. No quedó desviación de
   modelo entre el perfil Kotlin y el C++.
2. **Paridad visual, cerrada por GLM.** El gate v3 (acta `0436` FAVORABLE,
   acuse de cierre `0437`) midió, con geometría propia de GLM, la franja
   viridis en **[1.010, 1.080]·R**, es decir el cascarón 60..500 km del
   7,85 % del radio pre-registrado desde el C++. Señal/control **46,2x y
   45,2x** en dos ciclos, OFF con **0 píxeles viridis**, VLM conforme en
   ambos limbos, y comportamiento nocturno más débil (+95..+166 frente a
   +226 diurno) idéntico al oráculo. La capa se ve y coincide con el C++
   sin una sola línea nativa.
3. **Paridad de coste, verificada.** `steps_ = 64` por defecto en C++
   (`VolumeRenderer.h:59`) y `VolumeQuality.HIGH = VOL_STEPS_REF = 64` en
   el puerto (`GlobeRenderer.kt:192`). GLM dictaminó que "el coste ON es
   el coste fiel del default del oráculo"; la caída de 18,74 fps frente a
   27,75 fps en el Mi 10 es una diferencia de hierro (Adreno 650), no un
   modelo incompleto. La mitigación existe dentro del puerto
   (`LOW`=16 / `MEDIUM`=32 / `HIGH`=64), es la recomendación (b) del acta
   `0436` y queda como decisión de producto de David, no como motivo para
   NDK.
4. **La condición del plan se cumple.** R4.7 dice literalmente: "Si la
   cobertura es suficiente: **NO** se añade NDK; se documenta el descarte".
   La cobertura medida es suficiente en modelo, geometría, color y coste.
5. **Riesgo evitado.** El único riesgo legal del plan ligado a este ADR
   ("NDK-MSIS entra sin licencia") desaparece al no introducir la
   dependencia. Tampoco se añaden superficie ABI, tamaño de APK, pasos de
   build ni ruta de fallback nuevos.

## 4. Alternativas consideradas

| Opción | Resultado | Motivo |
|---|---|---|
| Introducir NDK-MSIS ahora | **Rechazada** | No hay hueco de paridad que cubra; añade licencia, ABI, build, tamaño y riesgo legal sin beneficio medido. |
| Diferir la decisión a R5 | **Rechazada** | El plan pide decidir en R4.7 con los datos de R4.4/R4.6, que ya existen. Diferir dejaría E7 abierto en el cierre R4.8. |
| No introducir NDK y documentar el descarte | **Recomendada** | Cumple Q1 con evidencia medida y cierra E7 sin dependencias nuevas. |

## 5. Consecuencias

- E7 queda satisfecho con "decisión NO NDK + licencia N/A" once David firme
  y GLM dicte.
- R4.8 puede arrancar sin compilers nativos: solo paridad de replay y
  informe de batería de 30 min.
- Quedan como deudas declaradas, **no como motivos para NDK**:
  (b) valorar `VolumeQuality` / `MEDIUM` por defecto por UX; (d) roll de
  -23,4 grados de la Tierra frente al volumen, divergencia fiel al C++.
  Ambas exigen GO de David y re-gate; el acta `0436` las declaró no
  bloqueantes para R4.6.5.

## 6. DoD — comprobación

| Requisito del plan | Estado |
|---|---|
| Decisión explícita de David | **Pendiente** |
| Dictamen de GLM | **Pendiente** (no enviado; David eligió cerrar la relación en R4.6.5) |
| Licencia declarada | **N/A — sin dependencia nativa** |
| Ninguna dependencia nativa entra sin ADR | **Cumplido** (no entra ninguna) |
| Publicación como PR docs-only con CI 3/3 | **Pendiente de GO para publicar** |
| Sin código, sin `data/**`, sin `.github/**`, sin checker | **Cumplido por construcción** |

## 7. Referencias

- Plan: `docs/r4/PLAN-R4-TIEMPO-PROFUNDIDAD.md` §1.1 E7, §5 R4.7, §7 E7,
  §8 riesgo "NDK-MSIS entra sin licencia", §10 Q1.
- `docs/r4/R4.6.3-VOLUMEN-DENSITY-CHAPMAN.md` (pre-registro del volumen,
  64 pasos, `VolumeQuality` como único mando de coste).
- Actas del relay: `0430` (R4.6.3 FAVORABLE), `0434` (causa raíz del
  binding), `0435` (código CONFORME), `0436` (visual FAVORABLE +
  recomendaciones a-e), `0437` (acuse de cierre de R4.6.5).
- Commits: `1646d56` (PR #57, modelo `density`/`chapman`), `f3079d9` (fix
  del binding), `234eab4` (PR #58, gate visual). CI `main` `44921675310`
  3/3, `RESUMEN-TESTS ficheros=55 tests=356 fallos=0`.
- Evidencia del gate: `work/r465-v3-*` (capturas, UI dumps, FPS y análisis
  de píxeles) y `docs/R4.6.5-POSTGATE.md`.
