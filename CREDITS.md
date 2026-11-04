# CREDITS — IonosphereLive3D

Datos vivos que la aplicacion lee (nunca redistribuidos: todo va a cache
local del usuario), texturas y codigo de terceros incluidos en el
repositorio, y sus licencias.

## Satelites / TLE
- **CelesTrak** (Dr. T.S. Kelso, celestrak.org): GP en formato CSV
  (`gp.php?GROUP=...&FORMAT=csv`). Condiciones de uso y notas de
  atribucion en celestrak.org. Sin copiar al repositorio salvo cache
  local por usuario.
- **SatNOGS DB** (Libre Space Foundation, db.satnogs.org): TLE en JSON
  (`/api/tle/?format=json`) como fuente de respaldo.

## Ionosfera (perfil, sondas y modelos)
- **GIRO / LGDC** (UML, lgdc.uml.edu): DIDBase (`DIDBFastStationList`,
  `fastchar/getbest`) y coeficientes IRTAM (`rix/gambit-coeffs`).
  Datos CC BY-NC-SA 4.0 (uso no comercial y educativo); citar
  Reinisch & Galkin (2011), *Global ionospheric radio observatory*,
  Earth Planets Space 63, 377-381, doi:10.5047/eps.2011.03.001.
- **KC2G** (prop.kc2g.com): failover de foF2/MUF por estacion
  (`api/stations.json`). Rediseminacion de datos GIRO (con permiso),
  eSWua/INGV y feeds de NOAA, financiada por WWROF; su API no publica
  licencia explicita: uso por API con atribucion a esos origenes.
- **DIAS/NOA** (electron.space.noa.gr): pager SAO de Ionoso Portugal
  (ionostream, `idb/sao/pager`).
- **Observatori de l'Ebre** (Roquetes, EB040): perfiles SAO4/DPS-4D via
  FTP anonimo (ftpoeb.obsebre.es/ionospheric), cada 5 min. El repositorio
  NO redistribuye ficheros .SAO: el permiso para usar 4 capturas como
  fixtures de test se solicito por correo el 2026-10-31 (castellano y
  catala) y esta pendiente de respuesta; hasta concederse,
  `test_ebro_parse` se salta (ctest: skip) si no estan presentes
  localmente. Contacto del proyecto: Issues del repositorio.

## Meteofisica espacial (NOAA SWPC, NASA CCMC y mas)
- **NOAA SWPC** (services.swpc.noaa.gov): viento solar (DSCOVR/rtsw),
  Kp/Ap 1-min, Dst 1h, rayos X GOES, Ovation Aurora, D-RAP, GloTEC,
  sunspot/SSN y pronostico 45 dias. Trabajos del Gobierno de EE. UU.
  (uso publico; detalles en swpc.noaa.gov).
- **NASA CCMC DONKI** (ccmc.gsfc.nasa.gov): fulguraciones FLR,
  WSA-Enlil (CME) y RBE.
- **NASA JPL Horizons** (ssd.jpl.nasa.gov): posiciones de Voyager.
- **WDC Kyoto** (wdc.kugi.kyoto-u.ac.jp): indices AE en tiempo real.
- **NWS api.weather.gov**: alertas de tormenta.
- **WSPRnet** (wsprnet.org): spots de radioaficionados.
- **Copernicus Space Weather** (weather.spacedata.copernicus.eu):
  indices ESA SSA (respaldo).

## Imagenes solares (en vivo, nunca redistribuidas)
- **SDO** (sdo.gsfc.nasa.gov): credito "NASA/SDO y los equipos
  cientificos AIA/HMI".
- **SOHO/LASCO** (soho.nascom.nasa.gov): SOHO es una mision de
  cooperacion internacional entre ESA y NASA; coronografias LASCO
  cortesia del consorcio SOHO/LASCO.

## Texturas incluidas en el repositorio
- **Luces de noche** (`textures/earth-night.jpg`,
  `textures/earth-night-hires.jpg`): NASA Black Marble 2016 (NASA Earth
  Observatory, compuestos de Suomi NPP/VIIRS). Material producido por
  NASA; credito habitual "NASA Earth Observatory / Black Marble".
- **Textura diurna** (`textures/earth-day.jpg`): NASA Earth Observatory
  / Blue Marble Next Generation — compuesto mensual de diciembre de 2004
  (MODIS) con topografia y batimetria, 5400x2700 (~7,4 km/px en el
  ecuador), material de dominio publico de la NASA. Procedencia
  verificada (2026-11-01) contra la fuente canonica: sha256 identico bit
  a bit.

## Modelos y codigo incluidos (vendored)
- **IGRF-14**: coeficientes oficiales de NOAA NGDC
  (ngdc.noaa.gov/IAGA/vmod/coeffs/igrf14coeffs.txt), fuente citada en
  `src/Utils/Igrf.cpp`.
- **NRLMSIS 2.1 port** (`src/lib/iono/msis/`): traduccion propia del
  paquete de referencia NRLMSIS 2.1 de NRL. Licencia del original:
  NRL-SOF-014-1, "Open Source Academic Research License" — uso,
  reproduccion y modificacion SOLO con fines de investigacion,
  academicos y sin animo de lucro; cualquier uso comercial esta
  prohibido (seccion 2 del acuerdo). Esa restriccion NO es una licencia
  libre y NO es compatible con la GPL-3 del resto del repositorio: el
  directorio queda vallado como excepcion no-GPL (las traducciones son
  obra derivada del NRL-SOF y llevan los avisos de cambio de la seccion
  4(b); `msis21.parm` y `nrlmsis2.1_license.txt` son byte-exact del
  original). Texto completo y estado de las obligaciones:
  `src/lib/iono/msis/README.md`. Salida del repositorio: decision
  pendiente (2026-10-31), ligada a la respuesta del Observatori.
- **Dear ImGui** (`libs/imgui`, MIT), **GLAD** generado (`libs/glad*`,
  MIT) y **stb** (`libs/stb`, dominio publico / MIT): vendored, con su
  licencia en cada directorio.
- **GLFW** y **GLM** (externas, no vendidas): la build exige GLFW >= 3.4
  (`find_package(glfw3 3.4)`; la CI compila 3.4 desde fuente porque
  Ubuntu noble empaqueta 3.3) y GLM del sistema (`libglm-dev`). GLFW se
  distribuye bajo zlib/libpng; GLM bajo la MIT ("Happy Bunny").
- **libcurl** (del sistema): licencia curl (estilo MIT, con excepcion de
  marca). Ver curl.se/docs/copyright.html.
