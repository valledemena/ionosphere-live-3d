#pragma once
// =============================================================================
// C2a: puerta de red para fuentes con cuota (hoy: CelesTrak).
//
// Modulo PURO: solo std. Sin curl, sin GL, sin relojes implicitos (todo entra
// por parametro), asi que se testea con una tabla de casos.
//
// POR QUE EXISTE
// La politica de CelesTrak (2026) pide una descarga por actualizacion (2 h) en
// el grupo Active, y bloquea la IP tras 50 respuestas 301/403/404 en 2 h. El
// adaptador anterior tenia tres agujeros:
//   - el TTL de 6 h vivia solo en memoria: cada arranque pedia a red aunque
//     la cache en disco tuviera 20 minutos;
//   - la cache solo se consultaba despues de fallar CelesTrak y SatNOGS;
//   - un fallo no dejaba rastro, asi que se reintentaba en el siguiente pase.
//
// REGLA (acordada con la revision externa)
//   - cache < 6 h               -> se sirve etiquetada, CERO red.
//   - cache 6..72 h             -> se sirve etiquetada y se pide con jitter.
//   - sin cache o > 72 h        -> se pide ya.
//   - 301/403/404/429/5xx       -> bloqueo de 2 h (persistido, sobrevive al
//                                  reinicio); 429 respeta Retry-After.
//   - error de transporte o de parseo -> backoff 5 -> 60 min, sin bloqueo.
// =============================================================================

namespace fetchgate {

// TTL del snapshot vivo: durante este plazo no se toca la red. Coincide con
// la cadencia de la demo web y esta por encima del minimo de 2 h de la
// politica de CelesTrak (4 peticiones/dia como maximo teorico).
constexpr double kLiveTtlSec = 6.0 * 3600.0;

// Edad maxima de la cache servida como sustituto cuando la fuente falla.
// Por encima de esto no se dibuja nada (estado honesto, sin datos viejos).
constexpr double kMaxFallbackAgeSec = 72.0 * 3600.0;

// Bloqueo tras una respuesta HTTP distinta de 200.
constexpr double kBlockSec = 2.0 * 3600.0;

// Recorte de cordura para un `until` absurdo (reloj del sistema adelantado o
// sidecar manipulado): nunca bloquear mas de 2 h desde "ahora".
constexpr double kBlockSkewSec = 2.0 * 3600.0;

// Jitter maximo del primer fetch cuando hay cache vieja que mostrar.
constexpr double kJitterMaxSec = 20.0 * 60.0;

enum class Action {
    CacheOnly,     // servir cache y no tocar la red
    FetchNow,      // pedir ya
    FetchJittered, // pedir, pero no antes de fetchAtUtc
    Blocked        // no pedir hasta blockedUntilUtc (la cache sigue valiendo)
};

struct State {
    double lastAttemptUtc = 0.0;    // ultima peticion lanzada (exito o fallo)
    double lastSuccessUtc = 0.0;    // ultimo HTTP 200 con cuerpo valido
    double blockedUntilUtc = 0.0;   // vigente si > now; nace de un no-200
    int lastHttp = 0;               // ultimo codigo HTTP visto (0 = ninguno)
    int lastCurl = 0;               // ultimo CURLcode (0 = OK)
    int retryFails = 0;             // fallos "blandos" seguidos (transporte/parseo)
};

struct Decision {
    Action action = Action::FetchNow;
    double fetchAtUtc = 0.0;   // instante a partir del cual se puede pedir
    const char* reason = "";   // texto corto para la nota del panel
};

// `cacheAgeSec` < 0 significa "no hay cache en disco".
// `jitterSec` lo muestrea el llamante (0..kJitterMaxSec) para que esta funcion
// siga siendo determinista y testeable.
//
// ANCLAJE DEL JITTER (revision C2a): el instante de reintento se calcula como
//   eligibleAt = (now - cacheAge) + kLiveTtlSec + jitter
// es decir, anclado a cuando la cache cumplio el TTL, NO a "ahora + jitter".
// Con "ahora + jitter" cada reevaluacion volvia a posponer y el TTL efectivo
// pasaba a ser el de 72 h. Invariante: reevaluar en t = fetchAtUtc devuelve
// una accion que permite red (FetchNow).
Decision decide(double nowUtc, const State& st, double cacheAgeSec, double jitterSec);

// Recorta un bloqueo irracional (reloj adelantado) y normaliza negativos.
double sanitizeBlockedUntil(double blockedUntilUtc, double nowUtc);

// 5, 10, 20, 40, 60 min (tope) segun fallos blandos consecutivos.
double transportBackoffSec(int consecutiveFails);

// Clasificacion de una respuesta. `httpCode` 0 + `curlCode` != 0 = transporte.
struct Outcome {
    bool block = false;          // true -> escribir blockedUntilUtc
    bool transport = false;      // true -> backoff blando (DNS, timeout, TLS)
    double retryAfterSec = 0.0;  // valor de Retry-After si vino (429)
    const char* label = "";      // "URL obsoleta (301)", "403", "429", ...
};

Outcome classify(long httpCode, int curlCode, double retryAfterSec);

// HTTP 200 pero el cuerpo no se pudo interpretar. La descarga YA consumio la
// ventana de actualizacion, asi que reintentar en 5 min solo puede dar 403:
// se trata como bloqueo, no como fallo blando.
Outcome unreadableBody();

// Duración del bloqueo a aplicar tras `classify`: Retry-After si es mayor,
// acotado a 24 h por cordura; si no, kBlockSec.
double blockDurationSec(const Outcome& out);

}  // namespace fetchgate
