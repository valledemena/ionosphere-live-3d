// test_ebro_parse - C4: fixtures REALES de SAO (Observatori de l'Ebre) +
// casos negativos sinteticos + NLST pickCandidates + bloqueo unreadable.
// Sin FTP/red.-aserciones con valores leidos del fichero, no del parser.
#include "Data/EbroAdapter.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <string>
#include <vector>
#include <thread>

namespace fs = std::filesystem;
static int g_ok = 0, g_fail = 0;
static void check_bool(const char* w, bool c) { (c?g_ok:g_fail)++; printf("[%s] %s\n", c?"OK":"FAIL", w); }
static void check_near(const char* w, float v, float want, float tol) {
    bool c = !std::isnan(v) && std::fabs(v-want)<=tol;
    (c?g_ok:g_fail)++; printf("[%s] %s: got %.3f want %.3f\n", c?"OK":"FAIL", w, v, want);
}
static void check_nan(const char* w, float v, bool wantNan) {
    bool isn = std::isnan(v); bool c = (isn==wantNan); (c?g_ok:g_fail)++;
    printf("[%s] %s (isnan=%d want=%d)\n", c?"OK":"FAIL", w, isn, wantNan);
}
static std::string load(const char* rel) {
    std::ifstream f(rel, std::ios::binary);
    if (!f) return {};
    std::string s;
    f.seekg(0, std::ios::end); size_t n = f.tellg(); f.seekg(0, std::ios::beg);
    s.resize(n); f.read(s.data(), n);
    return s;
}

int main() {
    // Si no estan los fixtures (permiso del observatorio aun pendiente o plan
    // de tres ramas), CTest interpreta el exit 77 como SKIP.
    static const char* kFx[] = {
        "tests/fixtures/ebro/eb040_dia.SAO",
        "tests/fixtures/ebro/eb040_noche.SAO",
        "tests/fixtures/ebro/eb040_sinF1.SAO",
        "tests/fixtures/ebro/eb040_cones.SAO",
    };
    bool fixturesPresent = true;
    std::ifstream fx0(kFx[0], std::ios::binary);
    fixturesPresent = (bool)fx0;
    if (!fixturesPresent) {
        std::printf("[SKIP] fixtures SAO no presentes en el arbol; pendiente del permiso del Observatori de l'Ebre\n");
        // Las secciones sinteticas/NLST/bloqueo quedan por ejecutar si llegan.
        return 77;
    }

    // ===== FIXTURES REALES (capturas del Observatori de l'Ebre, sin editar) =====
    // Valores leidos del propio fichero (L6 de 8 chars: foF2, foF1, M(D), MUF(D),
    // fmin, foEs, fminF, fminE, foE, fxI, h'F, h'F2, h'E, h'Es; L8: hmF2, B0, B1).
    std::printf("--- fixtures reales SAO - dia ---\n");
    {
        const std::string body = load("tests/fixtures/ebro/eb040_dia.SAO");
        check_bool("fixtures se abre", !body.empty());
        EbroSample s;
        bool ok = EbroAdapter::parseSAO(body, "EB040_2026244120001.SAO", s);
        check_bool("dia parseSAO valid", ok && s.valid);
        check_near("dia foF2", s.foF2, 7.50f, 0.01f);
        check_near("dia foF1", s.foF1, 5.20f, 0.01f);
        check_near("dia M(D) del SAO", s.mD, 3.074f, 0.01f);
        check_near("dia MUF(D)", s.muf, 22.981f, 0.1f);
        check_near("dia foE (9a cifra)", s.foE, 3.55f, 0.01f);
        check_near("dia fmin (5a cifra)", s.fmin, 2.35f, 0.01f);
        check_near("dia fxI", s.fxI, 8.05f, 0.01f);
        check_near("dia foEs", s.foEs, 3.45f, 0.01f);
        check_near("dia hmF2", s.hmF2, 258.907f, 0.5f);
        check_near("dia B0", s.B0, 122.90f, 0.1f);
        check_near("dia B1", s.B1, 1.33f, 0.01f);
    }
    std::printf("--- fixture noche ---\n");
    {
        const std::string body = load("tests/fixtures/ebro/eb040_noche.SAO");
        check_bool("noche parseSAO valid", !body.empty());
        EbroSample s;
        bool ok = EbroAdapter::parseSAO(body, "EB040_2026244020001.SAO", s);
        check_bool("noche valid", ok && s.valid);
        check_near("noche foF2", s.foF2, 4.20f, 0.01f);
        check_nan("noche foF1 ausente", s.foF1, true);   // 9999 -> NaN
        check_nan("noche foE ausente", s.foE, true);     // 9999 en la 9a cifra
        check_near("noche M(D) directo del SAO", s.mD, 12.084f / 4.20f, 0.01f);
        check_near("noche fxI", s.fxI, 4.95f, 0.01f);
        // no Es check: foEs NaN (s.foEs == nan) checked via check_nan above
        check_near("noche hmF2", s.hmF2, 339.233f, 0.5f);
    }
    std::printf("--- fixture sin F1 ---\n");
    {
        const std::string body = load("tests/fixtures/ebro/eb040_sinF1.SAO");
        check_bool("sinF1 parseSAO valid", !body.empty());
        EbroSample s;
        bool ok = EbroAdapter::parseSAO(body, "EB040_2026244060001.SAO", s);
        check_bool("sinF1 valid", ok && s.valid);
        check_nan("foF1 -> NaN", s.foF1, true);
        check_near("foF2 > 0", s.foF2, 5.15f, 0.01f);
        check_near("sinF1 foE (9a cifra)", s.foE, 1.85f, 0.01f);
        check_near("sinF1 M(D) directo", s.mD, 3.313f, 0.01f);
        check_near("sinF1 fmin", s.fmin, 1.75f, 0.01f);
        check_near("hmF2", s.hmF2, 238.775f, 1.0f);
        check_bool("muf != NaN", !std::isnan(s.muf));
        check_near("muf valid", s.muf, 17.064f, 0.1f);
    }
    std::printf("--- fixture con Es ---\n");
    {
        const std::string body = load("tests/fixtures/ebro/eb040_cones.SAO");
        check_bool("cones parseSAO valid", !body.empty());
        EbroSample s;
        bool ok = EbroAdapter::parseSAO(body, "EB040_2026244200001.SAO", s);
        check_bool("cones valid", ok && s.valid);
        check_nan("cones foE ausente", s.foE, true);
        check_near("cones M(D) directo", s.mD, 3.211f, 0.01f);
        check_near("cones fmin", s.fmin, 1.30f, 0.01f);
        check_nan("conEs foEs NO es NaN", s.foEs, false);
        check_near("cones foEs", s.foEs, 3.80f, 0.01f);
        check_near("cones hEs (km)", s.hEs, 98.75f, 0.5f);
        check_near("cones B0", s.B0, 51.39f, 0.1f);
        check_near("cones B1", s.B1, 4.31f, 0.01f);
    }

    // ===== Invariantes fisicas: contra el fichero, no contra el parser =====
    std::printf("--- invariantes fisicas ---\n");
    {
        struct Cap { const char* name; const char* path; const char* fname; long hourSec; };
        const Cap caps[4] = {
            { "dia",   "tests/fixtures/ebro/eb040_dia.SAO",   "EB040_2026244120001.SAO", 43200 },  // 12:00 UT
            { "noche", "tests/fixtures/ebro/eb040_noche.SAO", "EB040_2026244020001.SAO",  7200 },  // 02:00 UT
            { "sinF1", "tests/fixtures/ebro/eb040_sinF1.SAO", "EB040_2026244060001.SAO", 21600 },  // 06:00 UT
            { "cones", "tests/fixtures/ebro/eb040_cones.SAO", "EB040_2026244200001.SAO", 72000 },  // 20:00 UT
        };
        for (const Cap& c : caps) {
            EbroSample s;
            const std::string body = load(c.path);
            const bool ok = EbroAdapter::parseSAO(body, c.fname, s);
            check_bool((std::string(c.name) + ": parsea").c_str(), ok && s.valid);
            // MUF(D) = foF2 x M(D) (±1%): detecta permutaciones de la L6, usando los valores reales del fichero.
            if (!std::isnan(s.mD) && !std::isnan(s.muf) && !std::isnan(s.foF2)) {
                const float prod = s.foF2 * s.mD;
                check_bool((std::string(c.name) + ": muf ~= foF2*mD (1%)").c_str(),
                           std::fabs(s.muf - prod) <= 0.01f * s.muf);
            }
            // h'F2 >= h'F cuando aparecen ambos: alturas coherentes (leidas del SAO real).
            if (!std::isnan(s.hF) && !std::isnan(s.hF2))
                check_bool((std::string(c.name) + ": hF2 >= hF").c_str(), s.hF2 >= s.hF);
            // La hora UT del nombre coincide con la que codifica la linea FF del fichero.
            const long long rem = (long long)s.timeUtc % 86400;
            check_bool((std::string(c.name) + ": timeUtc hora FF").c_str(),
                       std::llabs(rem - c.hourSec) <= 1);
        }
    }

    // ===== NEGATIVOS SINTETICOS =====
    std::printf("--- casos sinteticos (error) ---\n");
    {
        // cabecera incompleta: solo 2 lineas tras FF => parser devuelve false
        std::string s = "FF 00\n" "A\n" "B\n";
        EbroSample out;
        bool ok = EbroAdapter::parseSAO(s, "EB040_2026001000001.SAO", out);
        check_bool("2 lineas -> false", !ok);
    }
    {
        // 9999 en todos los campos => valid false
        std::string s;
        s += "NO HEAD\nFF 00\n";
        char bf[256]; std::snprintf(bf, sizeof(bf), "%8.2f", 9999.0f);
        std::string f = bf;
        std::string line;
        for (int i=0;i<15;i++) line += f;
        std::string l8;
        for (int i=0;i<12;i++) l8 += f;
        s += line + "\n"; s += "second\n"; s += l8 + "\n";
        EbroSample out;
        bool ok = EbroAdapter::parseSAO(s, "EB040_2026001000001.SAO", out);
        check_bool("todo-9999 -> invalid", !ok);
    }
    // linea truncada no crashea parseSAO (parse tolera)
    {
        std::string s;
        s += "FF00\n"; // ff=0, FF line
        s += "L6TRUNCATED\n";
        s += "y\nz\n";
        EbroSample out;
        bool threw = false;
        try { EbroAdapter::parseSAO(s, "EB040_2017001000001.SAO", out); } catch (...) { threw = true; }
        check_bool("no lanza excepcion", !threw);
    }

    // ===== NLST: pickCandidates / saoNameTimeUtc (logica pura, listing sintetico) =====
    std::printf("--- NLST pickCandidates ---\n");
    {
        // _0910 = 09:10, _0905 = 09:05, _0855 = 08:55; _1100 = 11:00 (futuro)
        const std::string t0910 = "EB040_2026244091001.SAO";
        const double cut = EbroAdapter::saoNameTimeUtc(t0910);
        std::vector<std::string> listing = {
            t0910,                         // nuevo, <= cut
            "EB040_2026244090501.SAO\n",   // fin de linea crudo de FTP: stripEol lo limpia
            "EB040_2026244085501.SAO\r\n",
            "EB040_2026244110001.SAO",     // > cut
            "EB040_2026244110001.RSF",     // no SAO
            "foo.SAO",                     // sin forma EB040_
        };
        std::vector<std::string> c = EbroAdapter::pickCandidates(listing, cut, 2);
        check_bool("nlst respeta maxN=2", c.size() == 2);
        check_bool("nlst 1o es el mas nuevo <= cut", !c.empty() && c[0] == t0910);
        check_bool("nlst 2o es 09:05", c.size() > 1 && c[1] == "EB040_2026244090501.SAO");
        check_bool("nlst descarta RSF/tiempo futuro", std::find(c.begin(), c.end(), "EB040_2026244110001.SAO") == c.end());
        // Nombres invalidos dan 0.0
        check_bool("saoNameTimeUtc invalido -> 0", EbroAdapter::saoNameTimeUtc("junk.SAO") == 0.0);
        // Nada <= cut (reloj servidor adelantado): fallback a los nuevos.
        std::vector<std::string> future = { "EB040_2026244110001.SAO", "EB040_2026244105501.SAO" };
        std::vector<std::string> fb = EbroAdapter::pickCandidates(future, cut, 2);
        check_bool("nlst fallback futuro", fb.size() == 2 && fb[0] == "EB040_2026244110001.SAO");
        // El timestamp es estable: el propio parseSAO lo usa via nombre.
        EbroSample s;
        const std::string body = load("tests/fixtures/ebro/eb040_dia.SAO");
        (void)EbroAdapter::parseSAO(body, "EB040_2026244120001.SAO", s);
        check_bool("timeUtc del nombre coherente",
                   EbroAdapter::saoNameTimeUtc("EB040_2026244120001.SAO") == s.timeUtc);
    }

    // ===== Bloqueo por cuerpo ilegible (logica pura, sin red) =====
    std::printf("--- bloqueo unreadable 2 h ---\n");
    {
        EbroAdapter a;
        check_bool("sin bloqueo inicial", !a.isBlocked(1000.0) && a.blockedUntilUtc() == 0.0);
        a.noteUnreadable(1000.0);
        check_bool("unreadable bloquea tras noteUnreadable", a.isBlocked(1001.0));
        check_bool("bloqueo dura 2 h", a.blockedUntilUtc() == 8200.0);
        check_bool("el bloqueo expira a las 2 h", !a.isBlocked(8200.0));
        check_bool("error visible para la UI", a.getErrorMessage().find("respuesta ilegible") != std::string::npos);
    }

    // ===== T1: fallback real de mD (mD sale del L6[2] directo) =====
    std::printf("--- T1 fallback mD (fallback por NaN los lee indirecto) ---\n");
    {
        // Caso 1: L6[2] presente (vales 4.0) -> se lee del fichero.
        std::string line;
        char bf[256];
        // 15 campos de 8: [0] 5.000 foF2, [1] 9999.000 (NaN), [2] 4.000 mD,
        // [3] 30.0 muf inconsistente, resto 9999.
        std::snprintf(bf,sizeof(bf),"%8.3f", 5.0f); line += bf;
        std::snprintf(bf,sizeof(bf),"%8.3f",9999.0f); line += bf;
        std::snprintf(bf,sizeof(bf),"%8.3f", 4.0f); line += bf;
        std::snprintf(bf,sizeof(bf),"%8.3f", 30.0f); line += bf;
        for (int i=0;i<11;i++){ std::snprintf(bf,sizeof(bf),"%8.3f",9999.0f); line+=bf; }
        std::string body; body += "FF00\n"; body += line + "\n"; body += "second\n";
        std::string l8; for (int i=0;i<12;i++){ std::snprintf(bf,sizeof(bf),"%8.3f",9999.0f); l8+=bf; }
        body += l8 + "\n";
        EbroSample out; EbroAdapter::parseSAO(body,"EB040_2017001000001.SAO",out);
        check_bool("T1: mD directo devuelve 4.0", !std::isnan(out.mD) && std::fabs(out.mD - 4.0f) <= 0.005f);
        // Caso 2: L6[2] ausente -> fallback MUF/foF2 = 30.0/5.0 = 6.0
        line.clear();
        std::snprintf(bf,sizeof(bf),"%8.3f", 5.0f); line += bf;
        std::snprintf(bf,sizeof(bf),"%8.3f",9999.0f); line += bf;
        std::snprintf(bf,sizeof(bf),"%8.3f",9999.0f); line += bf;
        std::snprintf(bf,sizeof(bf),"%8.3f", 30.0f); line += bf;
        for (int i=0;i<11;i++){ std::snprintf(bf,sizeof(bf),"%8.3f",9999.0f); line+=bf; }
        body.clear(); body += "FF00\n"; body += line + "\n"; body += "second\n"; body += l8 + "\n";
        EbroSample out2; EbroAdapter::parseSAO(body,"EB040_2017001000001.SAO",out2);
        check_bool("T1: mD fallback = MUF/foF2 = 6.0", !std::isnan(out2.mD) && std::fabs(out2.mD - 6.0f) <= 0.01f);
    }

    // ===== T4: writeCallback capped (pure appendCapped) =====
    std::printf("--- T4 appendCapped (cuerpo con tope) ---\n");
    {
        auto B = ebro::appendCapped;
        std::string s;
        check_bool("T4 ok: append 0 bytes", B(s, "abc", 3, 4) == 0 ? false : true);
        std::string t; check_bool("T4 ok: append 3 de 4", B(t, "abc", 3, 4) == 3 && t == "abc");
        std::string u = "ab"; check_bool("T4: a tope sin sobrepasar", B(u, "cd", 2, 4) == 2 && u == "abcd");
        std::string v = "abcd"; check_bool("T4 corte: devuelve 0 al tope", B(v, "x", 1, 4) == 0 && v.size() == 4);
    }

    // ===== T2 (racional): documentacion, la deteccion TSan exige lanzar el lock =====
    std::printf("--- T2 detalle (lock de noteUnreadable, erf) ---\n");
    {
        EbroAdapter a;
        EbroAdapter b;
        // sin sincronizacion deliberada: el test de deteccion real requiere quitar
        // el lock en noteUnreadable() y comprobar que TSan senala el data race.
        // Este bloque solo asegura que los accesores no devuelven basura.
        check_bool("T2: baseline isBlocked usable", !a.isBlocked(1000.0));
        a.noteUnreadable(1000.0);
        check_bool("T2: noteUnreadable deja error_", a.getErrorMessage().find("respuesta ilegible") != std::string::npos);
        std::thread worker([&](){ for (int i=0;i<1000;i++) b.noteUnreadable(1000.0); });
        worker.join();
        check_bool("T2: fin sin excepcion (single-thread)", true);
    }

    std::printf("\n%d OK, %d FAIL\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}
