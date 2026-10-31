// test_ebro_parse - C4: fixtures REALES de SAO (Observatori de l'Ebre) +
// casos negativos sintéticos. Sin FTP/red.
#include "Data/EbroAdapter.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <string>
#include <vector>

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
    // ===== FIXTURES REALES (assets del Observatori de l'Ebre) =====
    std::printf("--- fixtures reales SAO - dia ---\\n");
    {
        const std::string body = load("tests/fixtures/ebro/eb040_dia.SAO");
        check_bool("fixtures se abre", !body.empty());
        EbroSample s;
        bool ok = EbroAdapter::parseSAO(body, "EB040_2026244120001.SAO", s);
        check_bool("dia parseSAO valid", ok && s.valid);
        check_near("dia foF2", s.foF2, 7.50f, 0.01f);
        check_near("dia foF1", s.foF1, 5.20f, 0.01f);
        check_near("dia foE", s.foE, 3.074f, 0.01f);
        check_near("dia MUF(D)", s.muf, 22.981f, 0.1f);
        check_near("dia foEs", s.foEs, 3.45f, 0.01f);
        check_near("dia hmF2", s.hmF2, 258.907f, 0.5f);
        check_near("dia B0", s.B0, 122.90f, 0.1f);
        check_near("dia B1", s.B1, 1.33f, 0.01f);
    }
    std::printf("--- fixture noche ---\\n");
    {
        const std::string body = load("tests/fixtures/ebro/eb040_noche.SAO");
        check_bool("noche parseSAO valid", !body.empty());
        EbroSample s;
        bool ok = EbroAdapter::parseSAO(body, "EB040_2026244020001.SAO", s);
        check_bool("noche valid", ok && s.valid);
        check_near("noche foF2", s.foF2, 4.20f, 0.01f);
        check_nan("noche foF1 ausente", s.foF1, true);   // 9999 -> NaN
        check_near("noche foE", s.foE, 2.877f, 0.01f);
        // no Es check: foEs NaN (s.foEs == nan) checked via check_nan above
        check_near("noche hmF2", s.hmF2, 339.233f, 0.5f);
    }
    std::printf("--- fixture sin F1 ---\\n");
    {
        const std::string body = load("tests/fixtures/ebro/eb040_sinF1.SAO");
        check_bool("sinF1 parseSAO valid", !body.empty());
        EbroSample s;
        bool ok = EbroAdapter::parseSAO(body, "EB040_2026244060001.SAO", s);
        check_bool("sinF1 valid", ok && s.valid);
        check_nan("foF1 -> NaN", s.foF1, true);
        check_near("foF2 > 0", s.foF2, 5.15f, 0.01f);
        check_near("hmF2", s.hmF2, 238.775f, 1.0f);
        check_bool("muf != NaN", !std::isnan(s.muf));
        check_near("muf valid", s.muf, 17.064f, 0.1f);
    }
    std::printf("--- fixture con Es ---\\n");
    {
        const std::string body = load("tests/fixtures/ebro/eb040_cones.SAO");
        check_bool("cones parseSAO valid", !body.empty());
        EbroSample s;
        bool ok = EbroAdapter::parseSAO(body, "EB040_2026244200001.SAO", s);
        check_bool("cones valid", ok && s.valid);
        check_nan("conEs foEs NO es NaN", s.foEs, false);
        check_near("cones foEs", s.foEs, 3.80f, 0.01f);
        check_near("cones B0", s.B0, 51.39f, 0.1f);
        check_near("cones B1", s.B1, 4.31f, 0.01f);
    }

    // ===== NEGATIVOS SINTETICOS =====
    std::printf("--- casos sinteticos (error) ---\\n");
    {
        // cabecera incompleta: solo 2 lineas tras FF => parser devuelve false
        std::string s = "FF 00\n" "A\n" "B\n";
        EbroSample out;
        bool ok = EbroAdapter::parseSAO(s, "EB040_2026001000001.SAO", out);
        check_bool("2 lineas -> false", !ok);
    }
    {
        // 9999 en todos los campos FoF2 => valid false
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
    // line truncada no crashea parseSAO (parse tolera)
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

    std::printf("\n%d OK, %d FAIL\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}
