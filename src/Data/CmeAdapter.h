#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <atomic>

// CME con ETA del modelo ENLIL (port de ionosphere-web/src/lib/iono/cme.ts,
// regla 252). Puro calculo de datos: sin DOM ni OpenGL.
//
// FUENTE PINADA (verificada viva el 2026-10-26):
//   GET https://ccmc.gsfc.nasa.gov/DONKI-API/get/WSAEnlilSimulations
//   (la forma sin "/get/" da 404). Responde un ARRAY de simulaciones:
//     [{ simulationID, modelCompletionTime, au, cmeInputs: [{ cmeStartTime,
//        latitude, longitude, speed, halfAngle, time21_5, featureCode,
//        isMostAccurate, CMEID, ... }], estimatedShockArrivalTime|null,
//        estimatedDuration|null, isEarthGB, kp_90/kp_135/kp_180|null,
//        impactList|null, link }]
//   Las horas son cadenas ISO "2026-10-14T11:30Z" (a menudo sin segundos).
//
// Sin parser JSON general (Utils/Json.h solo saca el primer numero de una
// clave en todo el documento: inservible aqui), se usa un escaner dirigido:
// se parte el array en objetos de llaves balanceadas (respetando cadenas y
// escapes) y dentro de cada objeto se busca la PRIMERA aparicion de cada
// clave. Nunca lanza: JSON malformado -> snapshot invalido.
//
// Honestidad (O1.1): cero constantes de evento. El reposo es la ultima
// simulacion CON ETA publicada; el pronostico ENLIL publica un ETA, NO un
// Dp ni un indice de actividad, asi que aqui no se inventa acoplamiento.
// Sin red el snapshot se queda invalido con el texto de error.

struct CmeEvent {
    std::string simId, cmeId, link;
    double modelTimeUtc = 0.0, startUtc = 0.0, arrivalUtc = 0.0;
    double durationH = 0.0;
    float speedKms = 0.0f, halfAngleDeg = 45.0f, srcLat = 0.0f, srcLon = 0.0f, kpMax = 0.0f;
    bool earthGB = false;
    bool hasStart = false, hasEta = false, hasHalfAngle = false;
    int simCount = 0;
};

// Resumen compacto de cada simulacion del boletin, para el historico del
// panel Geospace. Se guarda UNA entrada por simulacion (la de su cmeInputs
// mas exacta) y no duplica el array completo de entradas: el panel solo
// necesita modelCompletionTime, ETA, velocidad y geometria.
struct CmeSimBrief {
    std::string simId, cmeId;
    double modelTimeUtc = 0.0, startUtc = 0.0, arrivalUtc = 0.0;
    float speedKms = 0.0f, halfAngleDeg = 0.0f, kpMax = 0.0f;
    bool earthGB = false;
    bool hasEta = false, hasStart = false;
};

// Orden de sims[]: modelCompletionTime descendente (la mas reciente primero),
// el mismo orden con el que se elige el evento. No es el orden del JSON.
struct CmeSnapshot {
    CmeEvent event;
    std::vector<CmeSimBrief> sims;   // historico del boletin (max 32)
    double timeUtc = 0.0;
    bool valid = false;        // true si el JSON se parseo (aunque este "quiet")
    bool hasEvent = false;     // true si hay ETA publicada
    std::string note;          // "sin simulaciones" / "sin ETA" / "ENLIL <simId>"
};

class CmeAdapter {
public:
    void init();
    // Descarga + parsea en hilo background (no bloquea el render).
    void fetchAsync(double utc);
    bool isFetching() const { return fetching_; }
    CmeSnapshot getSnapshot() const;
    double getLastFetchTime() const { return lastFetchUtc_; }
    // Copia bajo mutex: el texto lo escribe el hilo de fetch, asi que devolver
    // una referencia seria una carrera al leerla desde el hilo principal.
    std::string getErrorMessage() const;

    // Expuesto para pruebas: no toca red ni mutex.
    static bool parseEnlil(const std::string& json, double nowUtc, CmeSnapshot& out);

    // Progreso 0..1 del transito Sol->Tierra (0 = inicio, 1 = llegada).
    // startUtc <= 0 (inicio desconocido) -> binario 0/1: declarado, no
    // inventado. Port literal de cmeProgress() de la demo web.
    static float cmeProgress(double startUtc, double arrivalUtc, double nowUtc);
    // Cuenta atras ASCII en ingles: "now", "in 2 d 3 h", "arrived 5 h ago".
    static std::string cmeCountdown(double arrivalUtc, double nowUtc);

private:
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp);
    bool fetch(double utc);

    mutable std::mutex mutex_;
    CmeSnapshot latest_;
    std::atomic<bool> fetching_{false};
    double lastFetchUtc_ = 0.0;
    std::string error_;
};
