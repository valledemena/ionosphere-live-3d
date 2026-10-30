// test_ebro_parse - C4: parseo de perfiles SAO4 para EB040. Sin red:
// le pasamos strings sinteticos con formato EXACTO que exige parseSAO
// (dia/noche/sin F1/con Es). Piloto del backlog C4.
#include "Data/EbroAdapter.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_ok = 0, g_fail = 0;
static void check_bool(const char* w, bool c) { (c ? g_ok : g_fail)++; printf("[%s] %s\n", c ? "OK":"FAIL", w); }
static void check_nan(const char* w, float v, bool wantNan) {
    bool isn = std::isnan(v); bool c = (isn == wantNan); (c?g_ok:g_fail)++; printf("[%s] %s (isnan=%d want=%d)\n", c?"OK":"FAIL", w, isn, wantNan);
}

// Monta un SAO4 minimo con campos de 8 chars cada uno.
static std::string makeSao(const char* fnameSuffix, float foF2, float foF1, float foE, float muf, float foEs, float hmF2, float B0, float B1) {
    std::string L6;
    char buf[32];
    auto field = [&](float v) {
        std::snprintf(buf, sizeof(buf), "%8.2f", std::isnan(v) ? 9999.0f : v);
        return std::string(buf);
    };
    L6 += field(foF2);
    L6 += field(foF1);
    L6 += field(foE);
    L6 += field(muf);
    L6 += field(ebro::nan());  // foEsL
    L6 += field(foEs);
    L6 += field(ebro::nan());  // foEsH
    L6 += field(9999.0f);      // fmin
    L6 += field(9999.0f);      // (idx 8)
    L6 += field(9999.0f);      // fxI
    L6 += field(9999.0f);      // h'F
    L6 += field(9999.0f);      // h'F2
    L6 += field(9999.0f);      // h'E
    L6 += field(9999.0f);      // h'Es
    std::string L8;
    L8 += field(ebro::nan());  // idx 0
    L8 += field(hmF2);         // hmF2
    for (int i=2;i<=11;i++) L8 += field(ebro::nan());
    L8 += field(B0);
    L8 += field(B1);

    char head[64];
    std::snprintf(head, sizeof(head), "EB040_%s", fnameSuffix);
    std::string s;
    s += "NO HEADER\n";
    s += "FF 00 00 00 00 00 00 00\n";   // marcador ff = 1
    s += L6 + "\n";
    s += "SECOND LINE (skip - idx ff+2)\n";
    s += L8 + "\n";
    s += "TAIL\n";
    return s;
}

int main() {
    std::printf("--- casos deparseo SAO4 ---\\n");
    const double epoch = 1793041500.0; // dentro del ano/noche? solo para fname
    char fname[64];
    snprintf(fname, sizeof(fname), "EB040_%04d%03d%02d%02d01.SAO", 2026, 1, 18, 15);

    // 4 fixtures: dia, noche, sin F1, con Es.
    const std::vector<std::pair<const char*, std::string>> cases = {
        // caso dia: F1 y MUF presentes
        {"dia", makeSao("2026001181501.SAO", 8.5f, 4.3f, 2.8f, 35.2f, ebro::nan(), 280.0f, 90.0f, 3.0f)},
        // caso noche: sin Es, MUF 0/X?, foF1 ausente tratado como 9999
        {"noche", makeSao("2026001181501.SAO", 6.2f, ebro::nan(), 2.2f, 24.5f, ebro::nan(), 260.0f, 85.0f, 2.5f)},
        // caso sin F1 explicito: foF1=9999 mais fuerte
        {"sinF1", makeSao("2026001181501.SAO", 7.0f, 9999.0f, 2.5f, 28.0f, ebro::nan(), 270.0f, 88.0f, 2.7f)},
        // caso con Es: foEs activa
        {"conEs", makeSao("2026001181501.SAO", 9.1f, 4.5f, 3.0f, 37.0f, 8.7f, 300.0f, 95.0f, 3.2f)},
    };

    for (auto& [name, body] : cases) {
        EbroSample s;
        bool parsed = EbroAdapter::parseSAO(body, "EB040_2026001181501.SAO", s);
        check_bool(name, parsed && s.valid);
        check_bool((std::string(name) + ": foF2 > 0").c_str(), !std::isnan(s.foF2) && s.foF2 > 0.0f);
        std::printf("   [%s] foF2=%.2f foF1=%.2f foE=%.2f MUF=%.1f foEs=%.2f B0=%.1f\\n",
                    name, s.foF2,s.foF1,s.foE,s.muf,s.foEs,s.B0);
    }
    // caso sin F1: foF1 NaN y foEs = NaN
    {
        EbroSample s;
        EbroAdapter::parseSAO(cases[2].second, "EB040_2026001181501.SAO", s);
        check_nan("sin-F1 foF1 es NaN", s.foF1, true);
    }
    // caso Es: foEs != NaN y hmF2 coincide
    {
        EbroSample s;
        EbroAdapter::parseSAO(cases[3].second, "EB040_2026001181501.SAO", s);
        check_nan("con-Es foEs NO es NaN", s.foEs, false);
        check_bool("hmF2 viene L8[1]", std::fabs(s.hmF2 - 300.0f) < 0.01f);
    }
    std::printf("\n%d OK, %d FAIL\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}
