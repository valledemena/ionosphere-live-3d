// =============================================================================
// C2a-test: EPOCH en UTC, independiente del entorno TZ (punto 1 de Claude).
//
// El parser usa days_from_civil (Hinnant) sin mktime, por lo que cambiar TZ
// NO debe alterar el epoch. Si en el futuro se reemplaza por mktime, este
// test fallara inmediatamente porque con TZ no UTC el epoch se desplazaria.
// =============================================================================
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include "Data/TleAdapter.h"
#include "tle_csv_fixture.h"

int main() {
    int ok = 0, fail = 0;

    // Fijar TZ a zona fija no UTC. En POSIX: setenv. En Windows/MinGW:
    // _putenv. Intentamos ambas sin lanzar excepciones.
    std::printf("Fijando TZ=CET-1CEST...\n");
    putenv("TZ=CET-1CEST,M3.2.0/2:00,M10.5.0/3:00");
    // No llamamos a tzset/_tzset de forma explicita; el parser no las usa.

    const std::string csv = tlefixture::catalogCsv();
    TleSnapshot snap;
    const bool parsed = TleAdapter::parseCelesTrakCsv(csv, tlefixture::kCatalogEpochUtc, snap);
    const bool epoch_ok = parsed && snap.valid
        && std::fabs(snap.sats[0].el.epochUtc - tlefixture::kCatalogEpochUtc) < 1e-6;

    (epoch_ok ? ok : fail) += 1;
    std::printf("[%c] EPOCH con TZ no UTC: got %.6f want %.6f (parser: %s)\n",
                epoch_ok ? 'O' : 'X', snap.sats[0].el.epochUtc,
                tlefixture::kCatalogEpochUtc, parsed ? "OK" : "FAIL");

    const char* tz_env = getenv("TZ");
    std::printf("TZ activo al final: %s\n", tz_env ? tz_env : "(vacío)");
    (tz_env && std::string(tz_env).find("CET") != std::string::npos) ? ok++ : fail++;

    std::printf("%d OK, %d FAIL (TZ-UTC)\n", ok, fail);
    return fail == 0 ? 0 : 1;
}
