#pragma once
// =============================================================================
// Lote 4 / modulo B: imagenes solares reales (corona LASCO C2 + mapa D-RAP).
//
// Que trae: DOS imagenes publicas, descargadas tal cual y decodificadas a RGB8:
//   - LASCO C2 (SOHO, NASA/ESA): coronagrafo, JPEG. Ultima imagen publicada.
//   - D-RAP global (NOAA SWPC): mapa de absorcion de la region D, PNG. Es
//     SALIDA DE MODELO (prediccion), no una medida de absorcion.
//
// FUENTES VERIFICADAS EN VIVO el 2026-10-27 (curl, User-Agent de la app):
//   LASCO C2 200 image/jpeg 456817 B (~446 KB), Last-Modified 03:51 UTC.
//     https://soho.nascom.nasa.gov/data/realtime/c2/1024/latest.jpg
//     Espejo declarado si la de 1024 falla (200 image/jpeg 92695 B):
//     https://soho.nascom.nasa.gov/data/realtime/c2/512/latest.jpg
//   D-RAP global 200 image/png 14839 B, Last-Modified del mismo minuto.
//     https://services.swpc.noaa.gov/images/d-rap/global.png
//     (la ruta `images/animations/d-rap/global/d-rap_global_latest.png` del
//     enunciado responde 404 a dia de hoy: se usa la ruta viva de arriba).
//
// HONESTIDAD:
//   - Nunca se sintetiza una imagen. Sin bytes validos `valid == false` y el
//     motivo queda en el error del producto.
//   - Si un fetch falla se CONSERVA la ultima imagen buena (con su hora real:
//     la edad del caption crece) y el error explica el fallo. Si nunca hubo
//     imagen, la imagen queda invalida.
//   - Si la imagen sale del cache en disco, `fromCache == true` y la UI lo
//     declara ("cached, <edad>"): no se disfraza de dato recien bajado.
//
// CACHE EN DISCO (un fichero por producto, cuerpo binario tal cual):
//   cache/sol_lasco_c2.img   ->  [8 B] marca de tiempo + [N B] cuerpo HTTP
//   cache/sol_drap_global.img->  [8 B] marca de tiempo + [N B] cuerpo HTTP
//   Cabecera propia de 8 bytes: epoch UTC del fetch en uint64 little-endian
//   (segundos desde 1970, sin decimales). Detras va el cuerpo EXACTO que
//   devolvio el servidor (JPEG o PNG), de modo que el cache se puede
//   re-decodificar sin tocar nada mas. Al leer se rechaza un fichero de
//   <= 8 B, un cuerpo de mas de 6 MB o un epoch fuera de [2000, 2100).
//
// Limites de trafico: 6 MB de cuerpo y 15 s de timeout por descarga.
//
// VIDA: igual que WsprAdapter, el hilo de fetch es detached y captura `this`,
// asi que el adaptador debe vivir hasta que la app cierra (fetching_ se apaga
// al terminar, pero no se une el hilo).
// =============================================================================
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

// Producto. El orden fija el indice interno (0..1).
enum class SolProduct { LascoC2 = 0, DrapGlobal = 1 };

struct SolImage {
    int w = 0, h = 0;
    std::vector<unsigned char> rgb;  // RGB8, w*h*3 bytes (JPEG/PNG sin alpha)
    double timeUtc = 0.0;            // epoch UTC del fetch (o del cache)
    int product = 0;                 // indice de SolProduct
    bool valid = false;              // true solo con pixeles reales decodificados
    bool fromCache = false;          // true si salio del cache en disco
};

class SolImageAdapter {
public:
    // Cabecera del cache: 8 bytes (epoch uint64 little-endian).
    static constexpr size_t kCacheHeaderBytes = 8;
    static constexpr size_t kMaxBodyBytes = 6u * 1024u * 1024u;
    static constexpr long kTimeoutSec = 15L;

    void init();
    // Descarga LASCO C2 + D-RAP en un hilo detached. No-op si ya hay una en
    // curso (fetching_). No bloquea al hilo de render.
    void fetchAsync(double utc);
    bool isFetching() const { return fetching_.load(); }

    // Copia bajo mutex: el hilo de fetch reescribe el snapshot.
    SolImage getImage(SolProduct product) const;
    // Epoch del ultimo dato bueno del producto (live o cache). 0 = ninguno.
    double getLastFetchTime(SolProduct product) const;
    // Copia bajo mutex de todos los fallos (uno por linea, con el producto).
    std::string getErrorMessage() const;
    // Motivo del ultimo fallo de ESE producto ("" si el ultimo intento fue
    // bien). La UI lo pone bajo "No image available yet".
    std::string getErrorMessage(SolProduct product) const;

    // Directorio del cache (por defecto "cache"). Debe fijarse antes de init()
    // si se quiere otro sitio.
    void setCacheDir(const std::string& dir) { cacheDir_ = dir; }
    static std::string cacheFileName(SolProduct product);

    // URL y nombre/atribucion de cada producto (puros, sin red).
    static const char* urlFor(SolProduct product);
    static const char* nameFor(SolProduct product);
    static const char* sourceNameFor(SolProduct product);

    // Etiqueta para el caption: "live" (dato bajado en esta sesion), "cached"
    // (viene del disco) o "none" (sin imagen valida). Pura.
    static const char* sourceTag(const SolImage& img);

    // Decodifica JPEG/PNG a RGB8. Pura (sin red, sin GL): deja `out` INVALIDO
    // si el cuerpo no es una imagen o stb no puede decodificarla. Devuelve true
    // solo si hay pixeles.
    static bool decodeImage(const std::vector<unsigned char>& bytes, SolImage& out);

    // Cache en disco (puro, sin red): [8 B epoch LE][cuerpo]. writeCacheFile
    // rechaza epoch <= 0, cuerpo vacio o cuerpo > 6 MB.
    static bool writeCacheFile(const std::string& path,
                               const std::vector<unsigned char>& body, double epoch,
                               std::string* err = nullptr);
    static bool readCacheFile(const std::string& path, std::vector<unsigned char>& body,
                              double& epoch);

private:
    static size_t writeCallback(char* ptr, size_t size, size_t nmemb, void* userdata);
    bool download(const std::string& url, std::vector<unsigned char>& body,
                  std::string& err) const;
    // Lee el cache de disco de ambos productos (init).
    void restoreCache();
    bool saveCache(SolProduct product, const std::vector<unsigned char>& body,
                   double epoch);
    bool fetch(double utc);

    mutable std::mutex mutex_;
    SolImage images_[2];
    double lastFetchUtc_[2] = {0.0, 0.0};
    std::string err_[2];   // motivo por producto ("" = ultimo intento bien)
    std::atomic<bool> fetching_{false};
    std::string cacheDir_ = "cache";
};
