# Plan de difusión — Ionosphere Live 3D

Identificador público único: el repositorio de GitHub
`https://github.com/valledemena/ionosphere-live-3d` (y su companion web).
No se usa nombre real: toda la comunicación va firmada por el proyecto/repositorio,
con una cuenta genérica del proyecto (ej. `ionosphere-live-3d` o handle neutral)
si se crea perfil nuevo.

## Objetivo
Dar a conocer la app como herramienta educativa y científica de código abierto,
basada en datos públicos, con estados honestos (sin sintéticos).

## Mensaje central (usar en todos los canales)
"Visor 3D en tiempo real de la ionosfera y el clima espacial, solo con datos
públicos. Cada capa publica su estado (fresco/stale/degradado/fallido) y se
vacía si no hay dato — nunca inventa. Código abierto, GPL-3."

## Identidad y privacidad
- Usar solo el repo (`valledemena/ionosphere-live-3d`) como firma.
- Crear perfiles nuevos con handle del proyecto, no nombre personal.
- No incluir correos personales; usar GitHub Issues/Discussions como buzón.
- Fotos/videos: solo capturas de la app, sin pantallas con datos personales.

## Fase 0 — Preparación (1-2 días)
1. Verificar repositorio público y README con capturas.
2. Añadir Topics en GitHub: `ionosphere`, `space-weather`, `opengl`, `cpp`,
   `scientific-visualization`, `education`, `ham-radio`.
3. Crear Discussion "Show & Tell / Educational use cases" pinchando casos de uso.
4. Preparar 3 activos:
   - Vídeo corto (30-60s): globo en cinema mode + capa de volumen Ne + IRTAM.
   - Captura estática de alta resolución (screenshot-globe.png).
   - Guía docente de 1 página (ver Propuesta A).
5. Forzar consistencia de links: README → ANNOUNCEMENT.md → release notes.

## Fase 1 — Técnico (semana 1)
| Día | Canal | Acción |
|-----|-------|--------|
| 1 | Hacker News | "Show HN: Ionosphere Live 3D — real ionospheric & space-weather visualiser (C++/OpenGL, public data only)" |
| 1 | r/cpp, r/opengl | Post técnico con build steps y captura |
| 2 | GitHub Discussions | "Showcase" del propio repo; pedir feedback de instalación |
| 3 | r/programming o r/opensource | Enfoque en honest state + no synthetic fallbacks |
| 4 | Dev.to / Medium | Artículo "Building an honest ionosphere visualiser" |
| 5 | Product Hunt | Listado como Open Source / Education tool (opcional) |

## Fase 2 — Radioaficionados / HF (semana 2)
| Canal | Acción |
|-------|--------|
| r/amateurradio | Post práctico: "LF/MF/HF propagation layer + foF2/MUF + Ebre SAO" |
| r/shortwave | Radiosescucha + grey line + terminator |
| QRZ.com / eHam.net | Hilo en Software / Propagation con link al repo |
| Facebook groups | "HF Propagation", "Radio Astronomy" con captura de aurora |
| URE / RSGB / ARRL communities | Enviar como recurso didáctico (sin contacto personal) |

## Fase 3 — Ciencia y educación (semana 3)
| Canal | Acción |
|-------|--------|
| r/space, r/Physics | Post científico con extras (magnetopausa Shue, Van Allen, aurora) |
| r/science o r/AskScience (comentario) | Responder hilos de space weather con link |
| r/dataisbeautiful | Visualización estática con colormap + legend |
| Ciencia ciudadana (Zooniverse/ESA) | Proponer como recurso didáctico |
| Portales educativos (español) | Compartir guía docente (Propuesta A) |

## Fase 4 — Español / comunidad local (continuo)
- Foros de radioaficionados españoles (foroderadio, URE).
- Grupos de Facebook de análisis de espacio.
- Twitter/X con hashtags: #ionosfera #climaespacial #spaceweather #radioaficionado
- Mastodon (servidores de ciencia) — handles neutral.

## Propuesta A — Guía docente (1 página)
Contenido sugerido:
- Objetivo de aprendizaje por capa:
  - TEC → variabilidad diurna/estacional, efecto solar.
  - foF2/MUF/hmF2 → propagación HF, saltos.
  - IRTAM → asimilación de datos y retardo declarado (~3 d).
  - Aurora + viento solar → interacción sol-Tierra.
  - Magnetopausa Van Allen → geometría, escala real.
- Actividad: comparar mediodía vs medianoche; tormenta simulada por Kp alto.
- Preguntas de reflexión sobre "dato faltante" (capa vacía) y honestidad.
- Citar fuentes y licencias.

### Grupos de Telegram / Facebook (añadido)
**Facebook**
- Buscar grupos: "Space Weather", "HF Propagation", "Amateur Radio Satellite", "Ionosphere", "STEM Education", grupos URE/RSGB/ARRL.
- Pedir permiso al admin antes de publicar; compartir el vídeo + link; no bombardear.
- Responder dudas por Issues/Discussions.

**Telegram**
- Canales/grupos: space weather, radioaficionados (HF), física del espacio, programación C++ (si tech).
- Enviar mensaje con captura + link; seguir normas del grupo (algunos solo permiten links con texto).
- Crear propio canal del proyecto (handle neutral) para updates de releases.

**Consejos anti-spam**
- Una publicación por grupo, cadencia semanal.
- Adaptar texto al foco del grupo.
- Evitar enlaces acortados; usar GitHub repo directo.
- `ANNOUNCEMENT.md` ya creado en el repo (corto / educativo / técnico).
- Vídeo + 3 capturas en `docs/media/` (opcional, no versionar binarios grandes).
- Un "Release" de GitHub con changelog y binario precompilado (MSYS2).

## Métricas de éxito (sin PII)
- Stars, forks, Issues/PRs externos en GitHub.
- Descargas/clones (GitHub traffic).
- Discusiones educativas citando el repo.
- (Opcional) Un formulario anónimo de "usé la app en clase" vía GitHub Issues template.

## Riesgos y mitigaciones
- Moderación por spam: publicar primero valor (código, demo), no solo link.
- Atribución de datos: siempre citar fuentes (CREDITS.md) en posts.
- Expectativas: dejar claro "visual-grade" (TLE sin SGP4, extras declarados).
- Plan B en HN/Reddit: responder rápido a Issues, preparar FAQ.

## Calendario sugerido (4 semanas)
- S1: HN + r/cpp + r/opengl + Dev.to
- S2: r/amateurradio + foros + Product Hunt
- S3: r/space + r/dataisbeautiful + ciencia ciudadana
- S4: español/local + recopilación de casos de uso + ajuste de README

## Próximos pasos inmediatos
1. (Tú) confirmas cuenta/handle neutral a usar.
2. Genero plantillas de post adaptadas a cada canal (en los MD ya hay base).
3. (Opcional) Creo guía docente (`docs/educational-guide.md`) y template de Issue.
