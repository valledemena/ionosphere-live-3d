// test_layer_profile - NaN/inf de las fo criticas NO cruzan al perfil:
// el SAO trae 9999 = "sin dato" -> NaN (de noche foE/foF1 ausentes) y un
// NmE = NaN envenenaria el max() de evalNeTotal y el shader del volumen.
// Valores de las capturas reales EB040 (dia 12:00 UT / noche 02:00 UT).
#include "Ionosphere/LayerProfile.h"

#include <cmath>
#include <cstdio>

static int g_ok = 0, g_fail = 0;
static void check_bool(const char* w, bool c) { (c?g_ok:g_fail)++; printf("[%s] %s\n", c?"OK":"FAIL", w); }
static void check_near(const char* w, double v, double want, double rel) {
    bool c = std::isfinite(v) && std::fabs(v - want) <= rel * std::fabs(want);
    (c?g_ok:g_fail)++; printf("[%s] %s: got %.4e want %.4e\n", c?"OK":"FAIL", w, v, want);
}

// Columna de densidad finita en [h0, h1]; devuelve false si hay NaN/inf o
// si aparece un winner de capa marcada como ausente.
static bool columnFinite(const LayerProfile& p, float h0, float h1, bool allowE, bool allowF1) {
    for (float h = h0; h <= h1; h += 5.0f) {
        for (float cosChi : { -0.5f, 0.0f, 0.8f }) {
            int w = -2;
            const float v = evalNeTotal(p, h, cosChi, 1.0f, &w);
            if (!std::isfinite(v)) { printf("  no finito en h=%.0f cosChi=%.2f: %e\n", h, cosChi, v); return false; }
            if (w < -1) { printf("  winner sin fijar en h=%.0f\n", h); return false; }
            if (w == 1 && !allowE) { printf("  winner=E sin capa E en h=%.0f\n", h); return false; }
            if (w == 2 && !allowF1) { printf("  winner=F1 sin capa F1 en h=%.0f\n", h); return false; }
        }
    }
    return true;
}

int main() {
    const float nanf = std::nanf("");

    std::printf("--- usableFo ---\n");
    {
        check_bool("usableFo(NaN) = false", !usableFo(nanf));
        check_bool("usableFo(+inf) = false", !usableFo(HUGE_VALF));
        check_bool("usableFo(0) = false", !usableFo(0.0f));
        check_bool("usableFo(-1) = false", !usableFo(-1.0f));
        check_bool("usableFo(3.55) = true", usableFo(3.55f));
    }

    std::printf("--- noche EB040 (foE/foF1 = NaN, sin capa E ni F1) ---\n");
    {
        StationPoint s;   // valores del fixture eb040_noche.SAO (02:00 UT)
        s.foF2 = 4.20f; s.foF1 = nanf; s.foE = nanf; s.foEs = nanf;
        s.hmF2 = 339.233f; s.B0 = 68.56f; s.B1 = 2.34f;
        s.valid = true; s.code = "EB040"; s.name = "Roquetes";
        const LayerProfile p = profileFromStation(s);
        check_bool("foE NaN -> sin capa E (!hasE, NmE == 0)", !p.hasE && p.NmE == 0.0f);
        check_bool("foF1 NaN -> sin capa F1", !p.hasF1 && p.NmF1 == 0.0f);
        check_bool("foF2 presente -> F2 si", p.hasF2);
        check_near("NmF2 = 1.24e10 * foF2^2", p.NmF2, 1.24e10 * 4.20 * 4.20, 1e-3);
        check_bool("columna 50-700 km finita (noche, sin E/F1)",
                   columnFinite(p, 50.0f, 700.0f, false, false));
        check_bool("suelo F2 = -1 sin capa E", f2FloorKm(p) < 0.0f);
    }

    std::printf("--- dia EB040 (foE 3.55) ---\n");
    {
        StationPoint s;   // valores del fixture eb040_dia.SAO (12:00 UT)
        s.foF2 = 7.50f; s.foF1 = 5.20f; s.foE = 3.55f; s.foEs = 3.45f;
        s.hmF2 = 258.907f; s.B0 = 122.90f; s.B1 = 1.33f;
        s.valid = true; s.code = "EB040"; s.name = "Roquetes";
        const LayerProfile p = profileFromStation(s);
        check_bool("foE 3.55 -> capa E", p.hasE);
        check_near("NmE = 1.24e10 * foE^2", p.NmE, 1.24e10 * 3.55 * 3.55, 1e-3);
        check_bool("columna 50-700 km finita (dia, con E/F1/F2)",
                   columnFinite(p, 50.0f, 700.0f, true, true));
        // Pico Chapman de E exacto en hmE (sin F2 para aislarlo).
        StationPoint soloE; soloE.foE = 3.55f; soloE.valid = true;
        const LayerProfile pe = profileFromStation(soloE);
        check_bool("perfil solo-E tiene E y nada mas", pe.hasE && !pe.hasF1 && !pe.hasF2);
        int w = -2;
        const float v = evalNeTotal(pe, pe.hmE, 0.0f, 1.0f, &w);
        check_bool("pico de E en hmE == NmE", w == 1 && std::fabs(v - pe.NmE) <= 0.5f);
    }

    std::printf("--- inf y saneadores ---\n");
    {
        StationPoint s;
        s.foE = HUGE_VALF; s.foF2 = 7.5f; s.hmF2 = 300.0f;
        s.valid = true;
        const LayerProfile p = profileFromStation(s);
        check_bool("foE = +inf -> sin capa E (isfinite)", !p.hasE && p.NmE == 0.0f);
        check_bool("columna finita con foE = +inf", columnFinite(p, 50.0f, 700.0f, false, false));
        check_bool("sanitizeB0(NaN) = 100", sanitizeB0(nanf) == 100.0f);
        check_bool("sanitizeB1(NaN) = 3", sanitizeB1(nanf) == 3.0f);
        check_bool("sanitizeB1(1.0) sube a 1.7", sanitizeB1(1.0f) == 1.7f);
        check_bool("sanitizeB0(400) baja a 300", sanitizeB0(400.0f) == 300.0f);
    }

    std::printf("\n%d OK, %d FAIL\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}
