// =============================================================================
// test_tle_merge - C2b paso 2: fusion pura de snapshots de varios grupos.
//
// Con varios grupos de CelesTrak (active, stations, weather...) cada uno trae
// su propio snapshot. El catalogo que se dibuja es la union, sin repetir
// objetos: el mismo NORAD puede aparecer en dos grupos con EPOCH distinto.
//
// Reglas que se fijan aqui (y que usa el paso 3 al instalar el resultado):
//   - Dedup por NORAD; gana el EPOCH mas nuevo, sea cual sea el orden.
//   - timeUtc es el del grupo MAS VIEJO: la edad declarada no puede ser mejor
//     que la de la peor parte.
//   - Origenes distintos -> source = Cache (no se declara vivo lo que no lo
//     es) y la nota dice "mixto".
//   - Grupos invalidos o vacios no aportan; todos vacios -> snapshot invalido.
//
// Puro: no toca red ni disco.
// =============================================================================
#include "Data/TleAdapter.h"

#include <cstdio>
#include <string>
#include <vector>

static int g_ok = 0, g_fail = 0;

static void checkBool(const char* what, bool cond) {
    (cond ? g_ok : g_fail) += 1;
    std::printf("[%s] %s\n", cond ? "OK" : "FAIL", what);
}

static void checkInt(const char* what, int got, int want) {
    const bool ok = (got == want);
    (ok ? g_ok : g_fail) += 1;
    std::printf("[%s] %s: got %d want %d\n", ok ? "OK" : "FAIL", what, got, want);
}

static void checkNear(const char* what, double got, double want, double tol = 1e-6) {
    const bool ok = (got - want <= tol) && (want - got <= tol);
    (ok ? g_ok : g_fail) += 1;
    std::printf("[%s] %s: got %.3f want %.3f\n", ok ? "OK" : "FAIL", what, got, want);
}

// Un satelite sintetico: solo importan NORAD, nombre y EPOCH de elementos.
static TleSat mkSat(int norad, const char* name, double epochUtc, double mm = 15.0) {
    TleSat s;
    s.noradId = norad;
    s.name = name;
    s.el.epochUtc = epochUtc;
    s.el.meanMotionRevDay = mm;
    s.el.valid = true;
    return s;
}

static TleSnapshot mkSnap(TleSource src, double timeUtc, std::vector<TleSat> sats) {
    TleSnapshot s;
    s.source = src;
    s.timeUtc = timeUtc;
    s.sats = std::move(sats);
    s.valid = !s.sats.empty();
    if (!s.valid) s.note = "sin datos";
    return s;
}

static int findNorad(const TleSnapshot& s, int norad) {
    for (std::size_t i = 0; i < s.sats.size(); ++i) {
        if (s.sats[i].noradId == norad) return (int)i;
    }
    return -1;
}

int main() {
    const double tOld = 1792000000.0;
    const double tNew = tOld + 3600.0;

    std::printf("--- C2b-MRG-1: dos grupos disjuntos se suman; timeUtc = el mas viejo ---\n");
    {
        const std::vector<TleSnapshot> parts = {
            mkSnap(TleSource::Celestrak, tNew,
                   {mkSat(25544, "ISS", 1000.0), mkSat(90001, "SAT-90001", 1000.0)}),
            mkSnap(TleSource::Celestrak, tOld, {mkSat(90002, "SAT-90002", 900.0)}),
        };
        const TleSnapshot m = TleAdapter::mergeSnapshots(parts);
        checkBool("valido", m.valid);
        checkInt("3 satelites", (int)m.sats.size(), 3);
        checkNear("timeUtc del grupo mas viejo", m.timeUtc, tOld);
        checkBool("origen comun: Celestrak", m.source == TleSource::Celestrak);
        checkBool("orden por primera aparicion",
                  m.sats.size() == 3 && m.sats[0].noradId == 25544 &&
                      m.sats[1].noradId == 90001 && m.sats[2].noradId == 90002);
        checkBool("nota sin duplicados", m.note.find("duplicados") == std::string::npos);
        checkBool("nota menciona los grupos", m.note.find("2 grupos") != std::string::npos);
    }

    std::printf("--- C2b-MRG-2: NORAD repetido, gana el EPOCH mas nuevo ---\n");
    {
        const std::vector<TleSnapshot> parts = {
            mkSnap(TleSource::Celestrak, tOld, {mkSat(25544, "VIEJO", 1000.0, 15.0)}),
            mkSnap(TleSource::Celestrak, tNew, {mkSat(25544, "NUEVO", 2000.0, 15.5)}),
        };
        const TleSnapshot m = TleAdapter::mergeSnapshots(parts);
        checkInt("un solo objeto", (int)m.sats.size(), 1);
        checkBool("gana el nombre del mas nuevo",
                  m.sats.size() == 1 && m.sats[0].name == "NUEVO");
        checkNear("gana el movimiento medio del mas nuevo",
                  m.sats.size() == 1 ? m.sats[0].el.meanMotionRevDay : 0.0, 15.5);
        checkBool("la nota cuenta 1 duplicado", m.note.find("1 duplicados") != std::string::npos);
    }

    std::printf("--- C2b-MRG-3: si el mas nuevo llega primero, el viejo no lo pisa ---\n");
    {
        const std::vector<TleSnapshot> parts = {
            mkSnap(TleSource::Celestrak, tNew, {mkSat(25544, "NUEVO", 2000.0, 15.5)}),
            mkSnap(TleSource::Celestrak, tOld, {mkSat(25544, "VIEJO", 1000.0, 15.0)}),
        };
        const TleSnapshot m = TleAdapter::mergeSnapshots(parts);
        checkInt("un solo objeto", (int)m.sats.size(), 1);
        checkBool("sigue el mas nuevo", m.sats.size() == 1 && m.sats[0].name == "NUEVO");
        checkNear("movimiento medio intacto",
                  m.sats.size() == 1 ? m.sats[0].el.meanMotionRevDay : 0.0, 15.5);
    }

    std::printf("--- C2b-MRG-4: empate de EPOCH, se queda el primero ---\n");
    {
        const std::vector<TleSnapshot> parts = {
            mkSnap(TleSource::Celestrak, tOld, {mkSat(25544, "PRIMERO", 1000.0)}),
            mkSnap(TleSource::Celestrak, tOld, {mkSat(25544, "SEGUNDO", 1000.0)}),
        };
        const TleSnapshot m = TleAdapter::mergeSnapshots(parts);
        checkBool("se queda el primero", m.sats.size() == 1 && m.sats[0].name == "PRIMERO");
    }

    std::printf("--- C2b-MRG-5: grupos caidos o vacios no aportan ---\n");
    {
        std::vector<TleSnapshot> parts(3);
        parts[0] = mkSnap(TleSource::None, 0.0, {});
        parts[1] = mkSnap(TleSource::Celestrak, tOld,
                          {mkSat(25544, "ISS", 1000.0), mkSat(90001, "SAT-90001", 1000.0)});
        parts[2] = mkSnap(TleSource::Celestrak, 0.0, {});
        parts[2].valid = true;   // valid sin filas no debe contar como grupo
        const TleSnapshot m = TleAdapter::mergeSnapshots(parts);
        checkBool("valido con el grupo bueno", m.valid);
        checkInt("solo las 2 filas del grupo bueno", (int)m.sats.size(), 2);
        checkNear("timeUtc del unico grupo util", m.timeUtc, tOld);
        checkBool("nota: 1 grupo", m.note.find("1 grupos") != std::string::npos);
    }

    std::printf("--- C2b-MRG-6: todos caidos -> snapshot invalido ---\n");
    {
        const std::vector<TleSnapshot> parts = {TleSnapshot{}, TleSnapshot{}};
        const TleSnapshot m = TleAdapter::mergeSnapshots(parts);
        checkBool("invalido", !m.valid);
        checkBool("sin filas", m.sats.empty());
        checkNear("timeUtc 0", m.timeUtc, 0.0);
        checkBool("origen None", m.source == TleSource::None);
        checkBool("nota 'sin datos'", m.note == "sin datos");
    }

    std::printf("--- C2b-MRG-7: origenes distintos se declaran como cache ---\n");
    {
        const std::vector<TleSnapshot> parts = {
            mkSnap(TleSource::Celestrak, tNew, {mkSat(25544, "ISS", 1000.0)}),
            mkSnap(TleSource::Cache, tOld, {mkSat(90001, "SAT-90001", 1000.0)}),
        };
        const TleSnapshot m = TleAdapter::mergeSnapshots(parts);
        checkInt("2 satelites", (int)m.sats.size(), 2);
        checkBool("no se declara vivo", m.source == TleSource::Cache);
        checkBool("la nota avisa de la mezcla", m.note.find("mixto") != std::string::npos);
        checkNear("timeUtc sigue siendo el mas viejo", m.timeUtc, tOld);
    }

    std::printf("--- C2b-MRG-8: todo cache sigue siendo cache ---\n");
    {
        const std::vector<TleSnapshot> parts = {
            mkSnap(TleSource::Cache, tOld, {mkSat(25544, "ISS", 1000.0)}),
            mkSnap(TleSource::Cache, tNew, {mkSat(90001, "SAT-90001", 1000.0)}),
        };
        const TleSnapshot m = TleAdapter::mergeSnapshots(parts);
        checkBool("origen Cache", m.source == TleSource::Cache);
        checkBool("sin marca de mixto", m.note.find("mixto") == std::string::npos);
    }

    std::printf("--- C2b-MRG-9: sin NORAD no hay dedup (se conservan ambos) ---\n");
    {
        const std::vector<TleSnapshot> parts = {
            mkSnap(TleSource::SatNogs, tOld,
                   {mkSat(0, "SIN-NORAD-A", 1000.0), mkSat(0, "SIN-NORAD-B", 1000.0)}),
        };
        const TleSnapshot m = TleAdapter::mergeSnapshots(parts);
        checkInt("se conservan las 2", (int)m.sats.size(), 2);
        checkInt("la primera es A", findNorad(m, 0), 0);
        checkBool("la segunda sigue siendo B", m.sats.size() == 2 && m.sats[1].name == "SIN-NORAD-B");
    }

    std::printf("\n[test_tle_merge] %d OK, %d FAIL\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}
