// M-IRTAM-F2 (P1b): port Jones-Gallet + igrf_dip (ver IrtamGridEval.h).
#include "Data/IrtamGridEval.h"

#include <cmath>
#include <cstring>

namespace irtameval {
namespace {

// --- INITIZE (float por COMMON/IGRF1 REAL) ---
inline float umrF() { return std::atan(1.0f) * 4.0f / 180.0f; }
inline constexpr float kEra = 6371.2f;
inline constexpr float kAquad = 6378.16f * 6378.16f;
inline constexpr float kBquad = 6356.775f * 6356.775f;

#include "IrtamIgrfCoeffs.inc"  // kIgrf2015[195], kIgrf2015s[80]

// G normalizado Schmidt (float, COMMON/MODEL REAL) + NMAX. Hoisting: se
// calcula una vez por ano (desviacion aceptada GLM #5: determinista).
static float g_G[196] = {0.0f};
static int g_NMAX = 0;
static double g_yearBuilt = -1e100;
static bool g_built = false;

static float sgnF(float v) { return v >= 0.0f ? 1.0f : -1.0f; }

// FELDCOF rama EXTRASHC year>=2015 (scope pineado). Devuelve false si
// year<2015 (dgrf/interp fuera de scope).
bool buildModel(double year) {
    if (year < 2015.0) return false;
    if (g_built && year == g_yearBuilt) return true;
    // EXTRASHC: base igrf2015 (NMAX1=13) + SV igrf2015s (NMAX2=8).
    const double factor = year - 2015.0;
    double gha[195];
    const int k = 8 * 10;  // 80
    for (int i = 0; i < k; i++)
        gha[i] = kIgrf2015[i] + factor * kIgrf2015s[i];
    for (int i = k; i < 195; i++) gha[i] = kIgrf2015[i];
    const int nmax = 13;
    // Normalizacion Schmidt (REAL*8 X/F0/F; IS=0). GHA 1-based en Fortran.
    double gh1[196] = {0.0};
    gh1[0] = 0.0;  // GH1(1) = 0
    int ii = 1;    // 0-based de GH1(2)
    double f0 = 1.0e-5;
    f0 = -f0;  // IS==0
    const double sqrt2 = std::sqrt(2.0);
    for (int n = 1; n <= nmax; n++) {
        double x = (double)n;
        f0 = f0 * x * x / (4.0 * x - 2.0);
        f0 = f0 * (2.0 * x - 1.0) / x;  // IS==0
        double f = f0 * 0.5;
        f = f * sqrt2;  // IS==0
        gh1[ii] = gha[ii - 1] * f0;
        ii++;
        for (int m = 1; m <= n; m++) {
            f = f * (x + m) / (x - m + 1.0);
            f = f * std::sqrt((x - m + 1.0) / (x + m));  // IS==0
            gh1[ii] = gha[ii - 1] * f;
            gh1[ii + 1] = gha[ii] * f;
            ii += 2;
        }
    }
    for (int i = 0; i < 196; i++) g_G[i] = (float)gh1[i];
    g_NMAX = nmax;
    g_yearBuilt = year;
    g_built = true;
    return true;
}

// FELDG entry geodesico (todo REAL*4). Con fall-through explicito del
// K-loop (IF(I-1) 5,4,2: label-2 CAE a 4 y 5 siempre; K=3 ultimo escritor).
bool feldg(float glat, float glon, float alt, float& bnorth, float& beast,
           float& bdown, float& babs) {
    const float umr = umrF();
    float rlat = glat * umr;
    float ct = std::sin(rlat);  // Fortran: CT=SIN(RLAT), ST=COS(RLAT)
    float st = std::cos(rlat);
    float d = std::sqrt(kAquad - (kAquad - kBquad) * ct * ct);
    float rlon = glon * umr;
    float cp = std::cos(rlon);
    float sp = std::sin(rlon);
    float zzz = (alt + kBquad / d) * ct / kEra;
    float rho = (alt + kAquad / d) * st / kEra;
    float xxx = rho * cp;
    float yyy = rho * sp;
    float rq = 1.0f / (xxx * xxx + yyy * yyy + zzz * zzz);
    float xi1 = xxx * rq, xi2 = yyy * rq, xi3 = zzz * rq;

    float h[196] = {0.0f};
    const int nmax = g_NMAX;
    const int ihmax = nmax * nmax + 1;      // 1-based IHMAX
    const int last = ihmax + nmax + nmax;   // 1-based LAST
    const int imax = nmax + nmax - 1;
    // DO 8 I=IHMAX,LAST (1-based): H(I)=G(I) -> 0-based h[i-1]=g_G[i-1]
    for (int i = ihmax; i <= last; i++) h[i - 1] = g_G[i - 1];
    // DO 6 K=1,3,2
    for (int kpass = 1; kpass <= 3; kpass += 2) {
        int i = imax;   // 1-based
        int ih = ihmax;  // 1-based
        // label 1:
        while (true) {
            int il = ih - i;  // 1-based
            float f = 2.0f / (float)(i - kpass + 2);
            float x = xi1 * f;
            float y = xi2 * f;
            float z = xi3 * (f + f);
            i = i - 2;
            int branch = (i - 1);  // <0 -> 5, ==0 -> 4, >0 -> 2
            if (branch > 0) {
                // label 2: DO 3 M=3,I,2 (I ya decrementado)
                for (int m = 3; m <= i; m += 2) {
                    // 1-based -> 0-based (-1)
                    h[il + m] = g_G[il + m] + z * h[ih + m] +
                                x * (h[ih + m + 2] - h[ih + m - 2]) -
                                y * (h[ih + m + 1] + h[ih + m - 3]);
                    h[il + m - 1] =
                        g_G[il + m - 1] + z * h[ih + m - 1] +
                        x * (h[ih + m + 1] - h[ih + m - 3]) +
                        y * (h[ih + m + 2] + h[ih + m - 2]);
                }
                // FALL-THROUGH a label 4 y 5 (explicito, critico GLM #3)
            }
            if (branch >= 0) {
                // label 4
                h[il + 1] = g_G[il + 1] + z * h[ih + 1] + x * h[ih + 3] -
                            y * (h[ih + 2] + h[ih - 1]);
                h[il] = g_G[il] + z * h[ih] + y * h[ih + 3] +
                        x * (h[ih + 2] - h[ih - 1]);
            }
            // label 5 (siempre)
            h[il - 1] = g_G[il - 1] + z * h[ih - 1] +
                        2.0f * (x * h[ih] + y * h[ih + 1]);
            ih = il;
            if (i < kpass) break;  // IF(I.GE.K) GOTO 1
        }
    }
    float s = 0.5f * h[0] + 2.0f * (h[1] * xi3 + h[2] * xi1 + h[3] * xi2);
    float t = (rq + rq) * std::sqrt(rq);
    float bxxx = t * (h[2] - s * xxx);
    float byyy = t * (h[3] - s * yyy);
    float bzzz = t * (h[1] - s * zzz);
    babs = std::sqrt(bxxx * bxxx + byyy * byyy + bzzz * bzzz);
    beast = byyy * cp - bxxx * sp;
    float brho = byyy * sp + bxxx * cp;
    bnorth = bzzz * st - brho * ct;
    bdown = -bzzz * ct - brho * st;
    return std::isfinite(babs) && babs > 0.0f;
}

}  // namespace

double latForRow(int i) { return -90.0 + 4.0 * (double)i; }
double lonForCol(int j) { return -180.0 + 8.0 * (double)j; }

double decimalYearForTov(double tovEpoch) {
    // Epoch -> civil (Howard Hinnant days_from_civil inverso simple).
    long long days = (long long)std::floor(tovEpoch / 86400.0);
    // days -> y/m/d
    long long z = days + 719468;
    long long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int y = (int)yoe + (int)(era * 400);
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned d = doy - (153 * mp + 2) / 5 + 1;
    unsigned m = mp + (mp < 10 ? 3 : -9);
    y += (m <= 2 ? 1 : 0);
    bool leap = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
    int doy1 = 0;
    static const int kMlen[12] = {31, 28, 31, 30, 31, 30,
                                  31, 31, 30, 31, 30, 31};
    for (unsigned k = 1; k < m; k++)
        doy1 += kMlen[k - 1] + (k == 2 && leap ? 1 : 0);
    doy1 += (int)d;
    return (double)y + ((double)doy1 - 0.5) / (leap ? 366.0 : 365.0);
}

bool igrfDip(double latDeg, double lonDeg, double decimalYear,
             double heightKm, double& xmodipOut) {
    if (!std::isfinite(latDeg) || !std::isfinite(lonDeg) ||
        !std::isfinite(decimalYear) || !std::isfinite(heightKm))
        return false;
    if (!buildModel(decimalYear)) return false;
    const float umr = umrF();
    float bn = 0, be = 0, bd = 0, ba = 0;
    if (!feldg((float)latDeg, (float)lonDeg, (float)heightKm, bn, be, bd, ba))
        return false;
    float horiz2 = be * be + bn * bn;
    if (!(horiz2 > 0.0f) || !std::isfinite(horiz2)) return false;
    float dip = std::asin(bd / ba);  // radianes, float
    float xlati = (float)latDeg;
    float dipdiv = dip / std::sqrt(dip * dip + std::cos(xlati * umr));
    if (!std::isfinite(dipdiv)) return false;
    if (std::fabs(dipdiv) > 1.0f) dipdiv = sgnF(dipdiv);
    float smodip = std::asin(dipdiv);
    xmodipOut = (double)(smodip / umr);
    return std::isfinite(xmodipOut);
}

float fout1(float xmodip, float lat, float lon, float hourUt, float tov,
            const double* coeffs1064) {
    // Trunca SFE a float (replica READIRTAMCOF 4E16.8 -> REAL).
    float sfe[1064];
    for (int i = 0; i < 1064; i++) sfe[i] = (float)coeffs1064[i];
    static const int QF[9] = {11, 11, 8, 4, 1, 0, 0, 0, 0};
    const int iharm = 6, k1 = 9, m = 76, mm = 13;
    const float umr = umrF();
    const float xmin = (hourUt - tov) * 60.0f + 720.0f;  // :151 exacto
    const float hou = (15.0f * hourUt - 180.0f) * umr;
    // C/S REAL*8 con semilla float (SIN/COS single del original).
    double c[12] = {0}, s[12] = {0};
    {
        float s1 = std::sin(hou);
        float c1 = std::cos(hou);
        s[0] = (double)s1;
        c[0] = (double)c1;
        for (int k = 2; k <= iharm; k++) {
            c[k - 1] = c[0] * c[k - 2] - s[0] * s[k - 2];
            s[k - 1] = c[0] * s[k - 2] + s[0] * c[k - 2];
        }
    }
    double coef[100] = {0};
    const int mmm = m * mm;  // 988
    for (int k = 1; k <= m; k++) {
        int mi = (k - 1) * mm;  // 0-based de SFE(MI+1)
        double acc = (double)sfe[mi];
        for (int j = 1; j <= iharm; j++)
            acc += (double)sfe[mi + 2 * j - 1] * s[j - 1] +
                   (double)sfe[mi + 2 * j] * c[j - 1];
        float slopeProd = sfe[mmm + k - 1] * xmin;  // SINGLE (critico)
        acc += (double)slopeProd;
        coef[k - 1] = acc;
    }
    // Espacial en float salvo SUM (double).
    double sum = coef[0];
    float ss = std::sin(xmodip * umr);
    float s3 = ss;
    float xsinx[13] = {0};
    xsinx[0] = 1.0f;
    int index = QF[0];
    for (int j = 1; j <= index; j++) {
        sum += coef[j] * (double)ss;
        xsinx[j] = ss;
        ss = ss * s3;
    }
    xsinx[QF[0] + 1] = ss;
    int np = QF[0] + 1;  // 1-based NP
    ss = std::cos(lat * umr);
    s3 = ss;
    for (int j = 2; j <= k1; j++) {
        float s0 = lon * (float)(j - 1) * umr;
        float s1 = std::cos(s0);
        float s2 = std::sin(s0);
        index = QF[j - 1] + 1;
        for (int l = 1; l <= index; l++) {
            np++;
            sum += coef[np - 1] * (double)xsinx[l - 1] * (double)ss *
                   (double)s1;
            np++;
            sum += coef[np - 1] * (double)xsinx[l - 1] * (double)ss *
                   (double)s2;
        }
        ss = ss * s3;
    }
    return (float)sum;  // GAMMA2=SUM trunca a REAL*4
}

bool evalGrid(const std::vector<double>& coeffs1064, double decimalYear,
              float hourUt, float tov, std::vector<float>& out2070) {
    if (coeffs1064.size() != (size_t)kCoeffN) return false;
    if (!buildModel(decimalYear)) return false;
    out2070.assign((size_t)kGridW * (size_t)kGridH, 0.0f);
    for (int i = 0; i < kGridH; i++) {
        double lat = latForRow(i);
        for (int j = 0; j < kGridW; j++) {
            double lon = lonForCol(j);
            double xmodip = 0;
            // lon E -> 0..360 para igrf (trig-equivale, pero normalizamos
            // como el ejemplo: along=283.13 E).
            double lonE = lon < 0 ? lon + 360.0 : lon;
            if (!igrfDip(lat, lonE, decimalYear, (double)kModipHeightKm,
                         xmodip))
                return false;
            float v = fout1((float)xmodip, (float)lat, (float)lonE, hourUt,
                            tov, coeffs1064.data());
            if (!std::isfinite(v)) return false;
            out2070[(size_t)i * (size_t)kGridW + (size_t)j] = v;
        }
    }
    return true;
}

}  // namespace irtameval
