CREDITS — IonosphereLive3D (C2a / C2b)
==========================================
Revisión C2a / C2b: Claude (revisor externo) con instrucciones de orden de trabajo.
Implementación: asistente (Inkling / openrouter).

Fuentes de datos (en orden de prioridad):
  1. CelesTrak GP CSV (FORMAT=csv, cabecera real de 19 columnas, NORAD 6 dígitos).
  2. Cache v2 (`satellites.tle`, `# tle-cache v2 epoch=... format=csv`), cuerpo crudo.
  3. SatNOGS JSON (`https://db.satnogs.org/api/tle/?format=json`), sin paginación.

Dependencias clave (CMakeLists.txt):
  glfw3 3.4 (CMake `find_package`), CURL (libcurl), Threads (Linux).

Pruebas (43/43 verde):
  test_fetch_gate, test_orchestration (59 OK), test_tle_csv (42 OK), test_tz_utc (2 OK).
  Orquestación cubre: 403 sin cache, timeout sin cache, muerte a mitad, aislamiento,
  cache de 73 h (una sola invalidación con generación +1), herencia `source=satnogs`.

Estado de C2b pendiente (defecto activo):
  - Grupos `celestrak.amateur` y `celestrak.stations` (puerta + cache por grupo).
  - Dedup por NORAD (conservar EPOCH más reciente).
  - Opción `active` explícita en `settings.cfg` con aviso de tamaño.
  - Límite 1 petición/proveedor/2h (ya existe en `fetchgate::kBlockSec`).

Nota ética: el código no inventa datos. Si las tres fuentes fallan,
`TleSnapshot::valid` es `false`, `note` indica la causa (`sin TLE validos` o
`respuesta ilegible`), y la UI no dibuja nada. El bloqueo de 2h (`unreadableBody`)
se aplica cuando un HTTP 200 trae una cabecera sin columna necesaria (ej. falta
`MEAN_MOTION`) o un CSV que no parsea, protegiendo la cuota de CelesTrak.
