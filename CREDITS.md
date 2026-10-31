CREDITS — IonosphereLive3D (C2a / C2b / C3 / C4)
===========================================

Revisión C2a / C2b: Claude (revisor externo) con instrucciones de orden de trabajo.
Implementación: asistente (Inkling / openrouter).

Fuentes de datos (en orden de prioridad):
  1. CelesTrak GP CSV (FORMAT=csv, cabecera real de 19 columnas, NORAD 6 dígitos).
  2. Cache v2 (`satellites.tle`, `# tle-cache v2 epoch=... format=csv`), cuerpo crudo.
  3. SatNOGS JSON (`https://db.satnogs.org/api/tle/?format=json`), sin paginación.
  4. Observatori de l'Ebre (EB040 SAO) — ficheros `EB040_YYYYDDDHHMM01.SAO`,
     formato SAO4/DPS-4D de FTP anónimo en ftpoeb.obsebre.es/ionospheric. Las
     capturas de prueba (4 mini-muestras: día, noche, sin F1, con Es) se
     redistribuyen en `tests/fixtures/ebro/` solo bajo el permiso del
     Observatori, que aún debe confirmarse (CREDITS propia de cada captura).

Dependencias clave (CMakeLists.txt):
  glfw3 3.4 (CMake `find_package`), CURL (libcurl), Threads (Linux).

Pruebas (49/50+ verde):
  Orquestación cubre: 403 sin cache, timeout sin cache, muerte a mitad, aislamiento,
  cache de 73 h (una sola invalidación con generación +1), herencia `source=satnogs`.
  test_ebro_parse cubre parseo de SAO4 (dia/noche/sin F1/con Es, 11 OK).

Nota ética: el código no inventa datos. Si las fuentes fallan,
`TleSnapshot::valid` es `false`, `note` indica la causa (`sin TLE validos` o
`respuesta ilegible`), y la UI no dibuja nada. El bloqueo de 2h (`unreadableBody`)
se aplica cuando un HTTP 200 trae una cabecera sin columna necesaria.