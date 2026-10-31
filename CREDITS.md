# CREDITS — IonosphereLive3D

Fuentes de datos de las que la aplicacion lee durante su uso (todas se
acceden en vivo; el repositorio no redistribuye sus salidas salvo la
excepcion de las capturas del ultimo punto):

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
- **KC2G** (prop.kc2g.com): modelo MUF(3000)F2 por estacion
  (`api/stations.json`), datos comunitarios.
- **DIAS/NOA** (electron.space.noa.gr): pager SAO de Ionoso Portugal
  (ionostream, `idb/sao/pager`).
- **Observatori de l'Ebre** (Roquetes, EB040): SAO4/DPS-4D via FTP
  anonimo (ftpoeb.obsebre.es/ionospheric). Las 4 mini-capturas de
  `tests/fixtures/ebro/` son ficheros reales sin editar; EL PERMISO
  PARA SU REDISTRIBUCION AUN NO ESTA CONFIRMADO con el observatorio
  (decision pendiente de David: pedirlo formalmente o sustituir las
  capturas por un script de descarga). Cualquier otra salida del
  adaptador va solo a cache local.

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
- **SDO** (sdo.gsfc.nasa.gov) e **SOHO/LASCO** (soho.nascom.nasa.gov):
  imagenes solares en tiempo real.
- **Copernicus Space Weather** (weather.spacedata.copernicus.eu):
  indices ESA SSA (respaldo).

## Modelos y codigo incluidos (vendored)
- **IGRF-14**: coeficientes oficiales de NOAA NGDC
  (ngdc.noaa.gov/IAGA/vmod/coeffs/igrf14coeffs.txt), fuente citada en
  `src/Utils/Igrf.cpp`.
- **NRLMSIS 2.1 port**: port propio con licencia NRL-SOF "Open Source
  Academic Research" (uso academico/no lucrativo); detalles y avisos,
  en `src/lib/iono/msis/README.md`.
- **GLFW 3.4**, **Dear ImGui**, **GLAD**, **libcurl** y la biblioteca
  estandar de C++: dependencias vendidas/externas con sus licencias
  habituales (MIT/permisivas; ver cada directorio vendored).

## Procedencia de las decisiones de revision
- Suite de revision externa por Claude; la implementacion es del
  asistente de OpenCode bajo la direccion de David (valledemena).
