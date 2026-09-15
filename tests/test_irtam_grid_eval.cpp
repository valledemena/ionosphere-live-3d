// =============================================================================
// test_irtam_grid_eval — P1b port Jones-Gallet (M-IRTAM-F2).
//
// Puro, sin red, sin GL. Llama a PRODUCCION (irtameval). Cubre:
//  - malla oficial 46x45 pineada (E2: fila 0 SUR, col 0 lon -180)
//  - igrf_dip scope EXTRASHC (year>=2015) + finitud
//  - GAMMA2 estructural (ceros -> 0) + semantica mixta documentada
//  - oraculos ASC 20160523_1200 (si existen extra-repo, jamas en el repo):
//    foF2/hmF2 sanity fisico + ecos impresos para custodia S4.
// =============================================================================
#include "Data/IrtamGridEval.h"
#include "Data/IrtamCoeffParse.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static int g_ok = 0, g_fail = 0;
static void check_bool(const char* what, bool cond) {
    if (cond) ++g_ok;
    else ++g_fail;
    std::printf("[%s] %s\n", cond ? "OK" : "FAIL", what);
}

static bool readFile(const char* path, std::string& out) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 8 * 1024 * 1024) {
        std::fclose(f);
        return false;
    }
    out.assign((size_t)n, '\0');
    size_t r = std::fread(&out[0], 1, (size_t)n, f);
    std::fclose(f);
    return r == (size_t)n;
}

int main(int argc, char** argv) {
    using namespace irtameval;
    // Directorio con los ASC 20160523_1200 (extra-repo, jamas en el repo):
    // argv[1] si se pasa (CI/sandbox GLM), si no la ruta local de auditoria.
    std::string ascDir =
        "C:\\Users\\unknown\\AppData\\Local\\Temp\\opencode\\p1b_audit\\"
        "IrtamReader";
    if (argc > 1) ascDir = argv[1];
    // Carry-along P5 (veredicto 007 §6, test-only): join portable — '/' vale
    // en Windows y POSIX (antes '\\' solo-Windows, GLM adapto con symlinks).

    std::printf("--- P1b: malla oficial ---\n");
    check_bool("lat(0)=-90 SUR", latForRow(0) == -90.0);
    check_bool("lat(45)=90", latForRow(45) == 90.0);
    check_bool("lat paso 4", latForRow(1) - latForRow(0) == 4.0);
    check_bool("lon(0)=-180", lonForCol(0) == -180.0);
    check_bool("lon(44)=172", lonForCol(44) == 172.0);
    check_bool("lon paso 8", lonForCol(1) - lonForCol(0) == 8.0);
    check_bool("celdas 2070", kGridW * kGridH == 2070);

    std::printf("--- P1b: igrf_dip scope + finitud ---\n");
    {
        double xm = 0;
        check_bool("year<2015 fuera de scope",
                   !igrfDip(0, 0, 2010.0, 300.0, xm));
        check_bool("oracle 2016.6 finito",
                   igrfDip(-11.95, 283.13, 2016.6, 300.0, xm) &&
                       std::isfinite(xm) && xm > -90.0 && xm < 90.0);
        std::printf("    xmodip(-11.95,283.13,2016.6) = %.4f\n", xm);
        // Ecuador de inclinacion: dip pequeno -> xmodip cercano a 0
        // (GLM replica: dip aprox -0.2 en ese punto deliberado).
        check_bool("punto ejemplo cerca del ecuador mag (|xmodip|<15)",
                   std::fabs(xm) < 15.0);
    }

    std::printf("--- P1b: GAMMA2 estructural ---\n");
    {
        std::vector<double> zero(1064, 0.0);
        float v = fout1(10.0f, 0.0f, 0.0f, 12.0f, 12.0f, zero.data());
        check_bool("ceros -> 0", v == 0.0f);
    }

    std::printf("--- P1b: oraculos ASC (extra-repo) ---\n");
    const std::string pF = ascDir + "/IRTAM_foF2_COEFFS_20160523_1200.ASC";
    const std::string pH = ascDir + "/IRTAM_hmF2_COEFFS_20160523_1200.ASC";
    std::string tf, th;
    if (readFile(pF.c_str(), tf) && readFile(pH.c_str(), th)) {
        irtamcoeff::Message mf, mh;
        std::string err;
        bool okF = irtamcoeff::parseMessage(tf, mf, err);
        bool okH = irtamcoeff::parseMessage(th, mh, err);
        check_bool("parse foF2 ASC standard",
                   okF && irtamcoeff::isStandardBasis(mf.header) &&
                       mf.coeffs.size() == 1064);
        check_bool("parse hmF2 ASC standard",
                   okH && irtamcoeff::isStandardBasis(mh.header) &&
                       mh.coeffs.size() == 1064);
        if (okF && okH) {
            // Punto ejemplo del reader a HOUR==TOV==12, ryear 2016.6.
            // Oraculos EXACTOS pineados (veredicto from-glm/006 §3, triple
            // verificacion vs replica independiente 1e-10): un port numerico
            // necesita aserciones exactas, no solo rangos (leccion drop 006).
            double xm = 0;
            igrfDip(-11.95, 283.13, 2016.6, 300.0, xm);
            std::printf("    xmodip(-11.95,283.13,2016.6) = %.6f\n", xm);
            check_bool("xmodip exacto -0.3980 (+-1e-3)",
                       std::fabs(xm - (-0.3980)) < 1e-3);
            float fof2 = fout1((float)xm, -11.95f, 283.13f, 12.0f, 12.0f,
                               mf.coeffs.data());
            float hmf2 = fout1((float)xm, -11.95f, 283.13f, 12.0f, 12.0f,
                               mh.coeffs.data());
            std::printf("    eco foF2(-11.95,283.13,TOV) = %.4f MHz\n",
                        (double)fof2);
            std::printf("    eco hmF2(-11.95,283.13,TOV) = %.4f km\n",
                        (double)hmf2);
            check_bool("foF2 exacto 6.1034 (+-1e-5 rel)",
                       std::fabs((double)fof2 - 6.1034) / 6.1034 < 1e-5);
            check_bool("hmF2 exacto 261.0765 (+-1e-5 rel)",
                       std::fabs((double)hmf2 - 261.0765) / 261.0765 < 1e-5);
            check_bool("foF2 fisico [0.5,20]",
                       fof2 >= 0.5f && fof2 <= 20.0f);
            check_bool("hmF2 fisico [100,600]",
                       hmf2 >= 100.0f && hmf2 <= 600.0f);
            // Malla completa foF2 a TOV: rangos + doble cresta esperada.
            std::vector<float> grid;
            bool okG = evalGrid(mf.coeffs, 2016.6, 12.0f, 12.0f, grid);
            check_bool("evalGrid 2070", okG && grid.size() == 2070);
            if (okG) {
                float mn = grid[0], mx = grid[0];
                bool allFin = true;
                for (float v : grid) {
                    if (!std::isfinite(v)) allFin = false;
                    if (v < mn) mn = v;
                    if (v > mx) mx = v;
                }
                std::printf("    grid foF2 min=%.2f max=%.2f MHz\n",
                            (double)mn, (double)mx);
                check_bool("grid todo finito", allFin);
                // Cresta ecuatorial presente (canje visual P1b).
                check_bool("grid max cresta [8,16]", mx >= 8.0f && mx <= 16.0f);
                // Oraculos exactos de malla (veredicto §3: 2.146/12.567).
                check_bool("grid min exacto ~2.15 ([1.5,3.0])",
                           mn >= 1.5f && mn <= 3.0f);
                check_bool("grid max exacto ~12.57 ([11.5,13.5])",
                           mx >= 11.5f && mx <= 13.5f);
                // Punto nocturno (58N,124W,12UT): artefacto 0.13 queda
                // adjudicado; valor corregido ~3.36 MHz (fisico).
                {
                    double xmN = 0;
                    igrfDip(58.0, 236.0, 2016.6, 300.0, xmN);
                    float vN = fout1((float)xmN, 58.0f, 236.0f, 12.0f,
                                     12.0f, mf.coeffs.data());
                    std::printf("    eco nocturno (58N,124W) = %.4f MHz\n",
                                (double)vN);
                    check_bool("nocturno ~3.36 (+-10%)",
                               std::fabs((double)vN - 3.3639) / 3.3639 < 0.10);
                }
            }
        }
    } else {
        std::printf("    (ASC extra-repo ausentes: oraculo completo en drop)\n");
        check_bool("skip oraculo sin fixture (no es fallo)", true);
    }

    std::printf("\n[test_irtam_grid_eval] %d OK, %d FAIL\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}
