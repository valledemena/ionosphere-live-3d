#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

// AeAdapter: indice AE REAL (auroral electrojet, nT) del World Data Center for
// Geomagnetism (Kyoto WDC), producto "Real-time (Quicklook)". Sustituye el
// proxy por Kp que coloreaba los electrojets (el AE es la medida directa: no
// hay que deducirlo de Kp).
//
// Fuente verificada EN VIVO el 2026-10-27 con curl:
//   https://wdc.kugi.kyoto-u.ac.jp/ae_realtime/data_dir/YYYY/MM/DD/aeYYMMDD
//   OJO: el nombre de fichero lleva el ano en DOS digitos (ae261011). Con
//   cuatro digitos el servidor responde 404.
//
// Respuesta real observada (2026/10/11/ae261011, 200 OK):
//   - 9624 bytes, 24 lineas (una por hora UTC, E00..E23), 400 caracteres por
//     linea, sin BOM ni cabecera de texto.
//   - Ultimo valor AE = 180 nT, instante 2026-10-11 23:59:00 UTC
//     (epoch 1791763140): el ultimo minuto del ultimo dia publicado. OJO: el
//     ultimo CAMPO de esa linea es 98, que es la media horaria, no un minuto.
//   - Dias 2026/10/12..2026/10/26 -> 404 (16 dias de retraso ese dia). El
//     repositorio publica con lag de hasta ~3 semanas POR DISENO: ayer y hoy
//     dan 404 siempre, por eso el fetch hace walk-back.
//
// Formato de linea (real, comprobado caracter a caracter):
//   "AEALAOAU    261011E00AE QUICKLK   448   463 ..." (400 chars exactos)
//   prefijo de texto de 31 chars ("AEALAOAU" + fecha yyMMdd + "EHH" + "AE" +
//   calidad) y despues una rejilla de 61 campos de 6 chars alineados a la
//   derecha que arranca en la columna 34 (34 + 61*6 = 400 exactos):
//     * 60 valores AE de 1 minuto (minutos 00..59 de esa hora), enteros con
//       signo, en nT;
//     * el campo 61 es la MEDIA HORARIA redondeada, NO un minuto 60.
//   Verificado en 144/144 lineas de seis dias (ae261011, ae261001, ae250510,
//   ae250115, ae260320, ae241105): redondear la media de los 60 valores da
//   exactamente el campo 61 (p.ej. 342.32 -> 342). El parser acepta 60 campos
//   (sin media) o 61 (con media) y NUNCA ingiere la media como muestra: un dia
//   completo da 1440 muestras, no 1464.
//
// Este producto solo lleva AE. AL/AO/AU se publican en ficheros aparte
// (alYYMMDD / aoYYMMDD / auYYMMDD, tambien 200 y con el mismo layout, con las
// etiquetas L00AL / O00AO / U00AU). No se piden: al/ao/au quedan a 0.0f y
// AeSample::valid indica que el minuto trae AE real.
// Los valores se copian TAL CUAL (sin enmascarar centinelas): no se inventa.
// En los seis dias comprobados no aparecio ningun centinela (min 19 nT, max
// 1470 nT).
//
// Limites declarados:
//   - Ventana: hasta 1440 muestras (24 h, un unico fichero diario). Se
//     conservan las ULTIMAS 1440 si el fichero trae mas.
//   - Walk-back: tope 35 dias hacia atras desde el dia UTC de `utc`.
//   - Edad: snapshot.timeUtc es el instante UTC del ULTIMO MINUTO del fichero,
//     nunca el del fetch (el AE publicado tiene dias de retraso).
//   - Red: User-Agent "IonosphereLive3D/1.0", timeout 20 s, maximo 1 MB.
//   - Cache de disco opcional `cache/ae.txt`:
//         "AE1 <epoch> <YYYY-MM-DD>\n" + cuerpo crudo del fichero aeYYMMDD
//     <epoch> = instante del fetch (segundos, precision de milisegundos) y
//     <YYYY-MM-DD> = dia del fichero (sourceDay). Se escribe solo tras un fetch
//     bueno y se sirve SOLO como sustituto si la red falla y no hay datos
//     buenos previos (stale-while-revalidate). Al servirla: fromCache = true y
//     sourceDay sale del header.
//   - Sin datos sinteticos: si los 35 dias fallan (y no hay cache) el snapshot
//     queda invalido con el ultimo codigo HTTP y el dia probado.

struct AeSample {
    double timeUtc = 0.0;   // instante UTC del minuto (epoch, segundos)
    float ae = 0.0f;        // indice AE (nT) de ese minuto
    float al = 0.0f;        // 0.0f: este producto no publica AL (fichero aparte)
    float ao = 0.0f;        // 0.0f: idem AO
    float au = 0.0f;        // 0.0f: idem AU
    bool valid = false;
};

struct AeSnapshot {
    std::vector<AeSample> samples;   // cronologico ascendente, <= 1440
    double timeUtc = 0.0;            // instante de la ultima muestra (no del fetch)
    bool valid = false;
    bool fromCache = false;
    int dayOffset = -1;              // 0 = hoy, 1 = ayer, ... (-1 = desconocido)
    std::string sourceDay;           // "YYYY-MM-DD" del fichero servido
    std::string note;                // resumen legible (muestras / descartes)
};

class AeAdapter {
public:
    void init();
    // Descarga + parsea en hilo detached. No-op si ya hay una en curso.
    void fetchAsync(double utc);
    bool isFetching() const { return fetching_.load(); }
    AeSnapshot getSnapshot() const;   // copia bajo mutex

    double getLastFetchTime() const;
    // Copia bajo mutex: el texto lo escribe el hilo de fetch.
    std::string getErrorMessage() const;

    // Puro (sin red): parsea un fichero aeYYMMDD completo.
    //   - exige "AEALAOAU" al inicio de la primera linea con contenido (si no,
    //     devuelve false);
    //   - hora de "EHH" (0..23; se toleran 24..47 como horas despues de las
    //     00:00 del dia dado, para ficheros que cruzan medianoche);
    //   - exige 60 valores de 1 minuto por linea (61 = 60 + media horaria, que
    //     se descarta); menos de 60 -> la linea se DESCARTA (no se corrige);
    //   - timeUtc = epoch(year-month-day HH:MM:00 UTC) por conversion propia;
    //   - se queda con las ultimas 1440 muestras;
    //   - valid = true si queda al menos 1 muestra.
    static bool parseAeFile(const std::string& text, int year, int month, int day,
                            AeSnapshot& out);

    static std::string buildUrl(int year, int month, int day);
    static std::string fileName(int year, int month, int day);   // "aeYYMMDD"

    // Cache en disco expuesta para tests (sin red). Formato:
    //   "AE1 <epoch> <YYYY-MM-DD>\n" + cuerpo crudo.
    // readCacheFile tolera el header sin dia ("AE1 <epoch>\n"), pero entonces
    // sourceDay queda vacio y no se puede fechar el cuerpo al parsearlo.
    static bool writeCacheFile(const std::string& path, double epoch,
                               const std::string& sourceDay, const std::string& body);
    static bool readCacheFile(const std::string& path, std::string& body,
                              double& epoch, std::string& sourceDay);

private:
    static size_t writeCallback(void* contents, size_t size, size_t nmemb,
                                void* userp);
    // Una descarga con tope de 1 MB. Devuelve true solo con HTTP 200.
    bool fetchUrl(const std::string& url, std::string& body, long& httpCode);
    bool fetch(double utc);

    mutable std::mutex mutex_;
    AeSnapshot latest_;
    std::atomic<bool> fetching_{false};
    double lastFetchUtc_ = 0.0;
    std::string error_;
};
