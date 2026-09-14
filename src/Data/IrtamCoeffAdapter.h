#pragma once
// =============================================================================
// M-IRTAM-F2 (P4): adaptador gambit-coeffs (via numerica IRTAM) — plumbing.
//
// Worker/backfill-local, twin SdoImageAdapter/GiroAdapter: curl + UA
// IonosphereLive3D/1.0 + timeout 20 s + FOLLOWLOCATION. La URL gambit vive
// AQUI y solo aqui (UN sitio, G6: 3er endpoint lgdc sancionado).
//
// Reglas de trafico (C1 refinada, ruling F2-R2 cond. 3):
//   - CONSULTA el gate antes de lanzar (canLaunchGambit) y REGISTRA al lanzar
//     (recordLaunch). Cero primitivas de pacing nuevas: el choke point unico
//     sigue siendo lgdcpacing.
//   - Backfill incremental: solo faltantes de la ventana (96 slots x 2 params
//     = 192 req >= 15 s UNA vez, resumable via cache P2); steady: el hilo de
//     backfill rechequea cada 300 s (de forma natural <= 2: nace ~1 slot/ciclo).
//   - B0/B1 DIFERIDOS a backlog (scope tight: foF2 + hmF2).
//
// El contenido se valida con irtamcoeff::parseMessage (P1a) y persiste via
// irtamccache (P2, familia irtamc_). La EVALUACION a malla 46x45 es P1b: este
// modulo mueve bytes+buckets, jamas grids.
//
// Bundle honesto (F2-R2 cond. 1): IrtamBundle declara la estructura FINAL
// (GridData foF2/hmF2) + valid explicito. P4 publica valid=false SIEMPRE:
// la capa esta AUSENTE del render (jamas un grid de ceros como dato).
// P1b hara flip aditivo (rellena + valid=true, sin tocar tipos ni wiring).
// =============================================================================
#include "Ionosphere/GridLayer.h"  // GridData (estructura final del bundle)

#include <map>
#include <string>
#include <vector>

namespace irtamfetch {

// Nombres declarados para el fold (grep): irtamfetch::FetchTarget,
// irtamfetch::planMissing, irtamfetch::quantizeSlot, irtamfetch::windowEndFor,
// irtamfetch::formatGambitTime, irtamfetch::dataAgeMin,
// irtamfetch::kGambitLagSec, irtamfetch::IrtamCoeffAdapter::fetchOnce,
// irtamfetch::IrtamBundle.

inline constexpr int kSlots = 96;               // 24 h a cadencia IRTAM 15 min
inline constexpr double kSlotSec = 900.0;       // 15 min
inline constexpr double kGambitLagSec = 259200.0;  // 3 d: ultimos 3 dias NO disponibles (E11)
inline constexpr const char* kParams[2] = {"foF2", "hmF2"};  // B0/B1 diferidos

struct FetchTarget {
    std::string param;  // "foF2" | "hmF2" (forma display; el fichero sanea a minusculas)
    double epoch = 0.0;  // TOV objetivo, cuantizada a 900 s
};

// Piso a multiplo de 900 s (los slots del backfill).
double quantizeSlot(double epoch);

// Fin de ventana objetivo: el slot de 15 min mas reciente con dato
// disponible (lag de diseno 3 d). Toda la ventana = [fin-95*900, fin].
double windowEndFor(double nowUtc);

// "2026.09.10T12:00" UTC (formato time= del endpoint, E11).
std::string formatGambitTime(double epoch);

// Edad del DATO en minutos (nowUtc - dataEpoch)/60; -1 si dataEpoch <= 0
// (sin buckets). Se calcula AL PINTAR con el reloj de lectura (E10).
double dataAgeMin(double dataEpoch, double nowUtc);

struct CacheHave {
    std::map<std::string, std::vector<double>> epochs;  // param display -> epochs presentes
    double freshestTov = 0.0;  // TOV mas fresco visto (0 = cache vacio)
};

// Faltantes de la ventana, oldest-first, param-major (foF2 y luego hmF2).
// Puro (sin I/O): el adapter aporta el scan, el test aporta sinteticos.
std::vector<FetchTarget> planMissing(const CacheHave& have, double windowEnd);

// Bundle publicado al hilo principal (molde TecBundle/SdoBundle).
// P4: grids VACIOS (0x0, jamas 46x45 de ceros) + valid=false SIEMPRE.
struct IrtamBundle {
    GridData foF2;  // P1b: malla 46x45 real + valid=true (flip aditivo)
    GridData hmF2;
    double dataEpoch = 0.0;  // TOV fresca del cache P2 (fuente del badge DATA-age)
    bool valid = false;
    std::string error;
};

class IrtamCoeffAdapter {
public:
    void init();  // solo curl_global_init + printf (sin red)
    void setCacheDir(const std::string& d) { dir_ = d; }

    // Escaneo del cache P2 (have-sets + TOV fresca). Nunca lanza.
    CacheHave scan();

    // scan() + planMissing(): lo que falta fetchgear ahora.
    std::vector<FetchTarget> planBackfill(double nowUtc);

    // UN intento: gate-consulta -> curl -> gate-registro -> parse -> save.
    // false si el gate no permite, fallo HTTP/curl, o bucket no-estructural
    // (fail-soft: el error queda en getErrorMessage, el cache se conserva).
    bool fetchOnce(const FetchTarget& t);

    double freshestTov() const { return freshestTov_; }
    const std::string& getErrorMessage() const { return error_; }

    // La URL gambit vive AQUI una vez (G6). E11: time=yyyy.mm.ddThh:mm.
    static std::string urlFor(const std::string& param, double epoch);

private:
    static size_t writeCallback(void* contents, size_t size, size_t nmemb,
                                std::string* userp);
    bool fetchURL(const std::string& url, std::string& response);

    std::string dir_ = "cache";
    double freshestTov_ = 0.0;
    std::string error_;
};

}  // namespace irtamfetch
