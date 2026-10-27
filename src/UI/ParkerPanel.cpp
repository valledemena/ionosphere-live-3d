// Lote 5 / modulo D: ventana "Parker spiral" (implementacion; ver .h).
//
// Todo el dibujo va por ImGui::GetWindowDrawList() sobre la region del panel:
// sin texturas, sin GL directo y sin llamar a la red. Si no hay viento no hay
// espiral (solo el aviso); si no hay Bx el brazo es gris y la polaridad no se
// presenta como dato.
#include "UI/ParkerPanel.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace {

constexpr double kCrossingWindowSec = 6.0 * 3600.0;

const ImU32 kColBg = IM_COL32(10, 14, 22, 235);
const ImU32 kColGrid = IM_COL32(96, 118, 148, 70);
const ImU32 kColGridText = IM_COL32(130, 150, 180, 150);
const ImU32 kColSun = IM_COL32(251, 191, 36, 255);
const ImU32 kColSunRing = IM_COL32(251, 191, 36, 90);
const ImU32 kColEarth = IM_COL32(56, 189, 248, 255);
const ImU32 kColLabel = IM_COL32(150, 170, 200, 190);
const ImU32 kColToward = IM_COL32(248, 113, 113, 255);
const ImU32 kColAway = IM_COL32(96, 165, 250, 255);
const ImU32 kColNoBx = IM_COL32(100, 116, 139, 255);

// Epoch -> "HH:MM". false si no hay fecha utilizable.
bool fmtUtcHm(double epoch, char* buf, size_t n) {
    if (!buf || n == 0) return false;
    if (!std::isfinite(epoch) || epoch <= 0.0) {
        std::snprintf(buf, n, "--:--");
        return false;
    }
    const std::time_t tt = (std::time_t)epoch;
    std::tm tm{};
#if defined(_WIN32)
    if (gmtime_s(&tm, &tt) != 0) {
        std::snprintf(buf, n, "--:--");
        return false;
    }
#else
    if (gmtime_r(&tt, &tm) == nullptr) {
        std::snprintf(buf, n, "--:--");
        return false;
    }
#endif
    if (std::strftime(buf, n, "%H:%M", &tm) == 0) {
        std::snprintf(buf, n, "--:--");
        return false;
    }
    return true;
}

// Color del brazo segun polaridad declarada (gris si no hay Bx).
ImU32 armColor(bool hasBx, const char* pol) {
    if (!hasBx || !pol) return kColNoBx;
    return (pol[0] == 't') ? kColToward : kColAway;
}

// Pie comun: esquema declarado + limites (se dibuja en los dos estados).
void footerText() {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped(
        "2D schematic, Earth fixed at (1,0). v_sw live; Bx polarity declared "
        "(+X GSE = toward). No wind, no spiral.");
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered())
        ImGui::SetItemTooltip(
            "Schematic only: 1 AU does not fit the 3D view in Earth radii.");
}

}  // namespace

void ParkerPanel::draw(const ParkerPanelInput& in, ParkerGeom& geom) {
    const ImVec2 dsp = ImGui::GetIO().DisplaySize;
    if (!geomInit_) {
        geomInit_ = true;
        if (geom.x < 0.0f) {
            geom.x = std::max(8.0f, dsp.x - geom.w - 24.0f);
            geom.y = std::max(8.0f, (dsp.y - geom.h) * 0.5f);
        }
        ImGui::SetNextWindowPos(ImVec2(geom.x, geom.y));
        ImGui::SetNextWindowSize(ImVec2(geom.w, geom.h));
    }
    if (!ImGui::Begin("Parker spiral", in.pVisible)) {
        ImGui::End();
        return;
    }
    // Geometria persistente (App la guarda en settings.cfg al salir).
    {
        const ImVec2 p = ImGui::GetWindowPos();
        const ImVec2 s = ImGui::GetWindowSize();
        geom.x = p.x;
        geom.y = p.y;
        geom.w = s.x;
        geom.h = s.y;
    }

    const bool hasWind =
        in.hasWind && std::isfinite(in.speedKms) && in.speedKms > 0.0f;
    const char* pol = in.hasBx ? parker::sectorPolarity(in.bxNt) : nullptr;
    const double v = hasWind ? (double)in.speedKms : 0.0;
    const double lag = parker::spiralLagDeg(v, 1.0);

    if (!hasWind) {
        ImGui::Dummy(ImVec2(1.0f, 6.0f));
        ImGui::TextDisabled("Waiting for live solar wind...");
        if (ImGui::IsItemHovered())
            ImGui::SetItemTooltip("No spiral without a live v_sw. Nothing is synthesised.");
        ImGui::Separator();
        footerText();
        ImGui::End();
        return;
    }

    // ---- Region de dibujo --------------------------------------------------
    const float availW = std::max(120.0f, ImGui::GetContentRegionAvail().x);
    const float availH = ImGui::GetContentRegionAvail().y;
    float plotH = std::max(110.0f, std::min(availH - 92.0f, 260.0f));
    if (!(plotH < availH)) plotH = std::max(90.0f, availH - 8.0f);

    const ImVec2 plotMin = ImGui::GetCursorScreenPos();
    const ImVec2 plotSize(availW, plotH);
    ImGui::InvisibleButton("##parker_plot", plotSize);
    if (ImGui::IsItemHovered())
        ImGui::SetItemTooltip(
            "Parker 1958 spiral for the live v_sw. Snapshot, not a forecast.");

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 plotMax(plotMin.x + plotSize.x, plotMin.y + plotSize.y);
    dl->PushClipRect(plotMin, plotMax, true);
    dl->AddRectFilled(plotMin, plotMax, kColBg, 3.0f);
    dl->AddRect(plotMin, plotMax, IM_COL32(255, 255, 255, 18), 3.0f);

    // Brazo espiral (foto instantanea con el v vivo).
    const std::vector<parker::Pt> pts = parker::spiralPoints(v, 64);

    // Encuadre: la ventana AU se ajusta al contenido real (Sol, Tierra y el
    // brazo completo) con un margen, manteniendo escala 1:1 en x/y para no
    // deformar la espiral. Asi el esquema llena el panel en vez de dejar dos
    // tercios vacios con un recorte fijo.
    double bbMinX = 0.0, bbMaxX = 1.0, bbMinY = 0.0, bbMaxY = 0.0;
    for (const parker::Pt& p : pts) {
        bbMinX = std::min(bbMinX, p.x);
        bbMaxX = std::max(bbMaxX, p.x);
        bbMinY = std::min(bbMinY, p.y);
        bbMaxY = std::max(bbMaxY, p.y);
    }
    const double padAu = 0.14;
    const double spanX = std::max(1.4, (bbMaxX - bbMinX) + 2.0 * padAu);
    const double spanY = std::max(0.9, (bbMaxY - bbMinY) + 2.0 * padAu);
    const double bbCx = 0.5 * (bbMinX + bbMaxX);
    const double bbCy = 0.5 * (bbMinY + bbMaxY);

    float pxAu = (float)std::min(140.0,
                                 std::min((double)(plotSize.x - 8.0f) / spanX,
                                          (double)(plotSize.y - 8.0f) / spanY));
    pxAu = std::max(6.0f, pxAu);
    const float sunX = plotMin.x + plotSize.x * 0.5f - (float)bbCx * pxAu;
    const float sunY = plotMin.y + plotSize.y * 0.5f + (float)bbCy * pxAu;

    // Rejilla discreta: 0.5 / 1.0 / 1.5 AU alrededor del Sol. Un anillo mayor
    // que el encuadre se recorta, y su etiqueta solo se pinta si cae dentro.
    const float kRings[3] = {0.5f, 1.0f, 1.5f};
    const char* kRingText[3] = {"0.5 AU", "1.0 AU", "1.5 AU"};
    for (int i = 0; i < 3; ++i) {
        const float rPx = kRings[i] * pxAu;
        dl->AddCircle(ImVec2(sunX, sunY), rPx, kColGrid, 64, 1.0f);
        const ImVec2 lp(sunX - rPx + 3.0f, sunY + rPx - 14.0f);
        if (lp.x >= plotMin.x && lp.x + 44.0f <= plotMax.x && lp.y >= plotMin.y &&
            lp.y + 14.0f <= plotMax.y)
            dl->AddText(lp, kColGridText, kRingText[i]);
    }

    // Brazo espiral (ya calculado arriba para el encuadre).
    std::vector<ImVec2> line;
    line.reserve(pts.size());
    for (const parker::Pt& p : pts)
        line.emplace_back(sunX + (float)p.x * pxAu, sunY - (float)p.y * pxAu);
    const ImU32 col = armColor(in.hasBx, pol);
    if (line.size() >= 2)
        dl->AddPolyline(line.data(), (int)line.size(), col, ImDrawFlags_None, 1.8f);

    // Sol y Tierra (la Tierra SIEMPRE en (1, 0) AU).
    dl->AddCircleFilled(ImVec2(sunX, sunY), 4.5f, kColSun);
    dl->AddCircle(ImVec2(sunX, sunY), 8.0f, kColSunRing, 24, 1.0f);
    dl->AddText(ImVec2(sunX - 9.0f, sunY + 9.0f), kColLabel, "Sun");

    const float earthX = sunX + pxAu;  // x = 1 AU
    const float earthY = sunY;         // y = 0
    dl->AddCircleFilled(ImVec2(earthX, earthY), 3.0f, kColEarth);
    dl->AddCircle(ImVec2(earthX, earthY), 6.0f, IM_COL32(56, 189, 248, 80), 16, 1.0f);
    dl->AddText(ImVec2(earthX + 6.0f, earthY - 14.0f), IM_COL32(125, 211, 252, 220),
                "Earth");

    dl->PopClipRect();

    // ---- Texto -------------------------------------------------------------
    ImGui::Text("v_sw %.0f km/s  |  lag at 1 AU %.1f deg", v, lag);
    if (ImGui::IsItemHovered())
        ImGui::SetItemTooltip(
            "Live solar wind speed and the Parker lag angle it produces at 1 AU.");

    if (in.hasBx && pol) {
        const ImVec4 c = (pol[0] == 't') ? ImVec4(0.97f, 0.44f, 0.44f, 1.0f)
                                         : ImVec4(0.38f, 0.65f, 0.98f, 1.0f);
        ImGui::TextColored(c, "Polarity: %s", pol);
        if (ImGui::IsItemHovered())
            ImGui::SetItemTooltip(
                "Declared convention, not a sector measurement: +X GSE = toward.");
    } else {
        ImGui::TextDisabled("Polarity: no Bx data");
        if (ImGui::IsItemHovered())
            ImGui::SetItemTooltip("No magnetic field feed: polarity is not a datum here.");
    }

    // Ultimo cruce REAL dentro de 6 h (sin cruce no hay aviso).
    bool hasRecent = false;
    double recentT = 0.0;
    if (in.bxHistory && std::isfinite(in.utc) && in.utc > 0.0) {
        const std::vector<double> crosses = parker::findCrossings(*in.bxHistory);
        for (size_t i = crosses.size(); i-- > 0;) {
            const double age = in.utc - crosses[i];
            if (age >= 0.0 && age <= kCrossingWindowSec) {
                hasRecent = true;
                recentT = crosses[i];
                break;
            }
        }
    }
    if (hasRecent) {
        char hm[16] = "--:--";
        fmtUtcHm(recentT, hm, sizeof(hm));
        ImGui::TextColored(ImVec4(0.99f, 0.90f, 0.68f, 1.0f),
                           "Sector crossing %s UTC (last 6 h)", hm);
        if (ImGui::IsItemHovered())
            ImGui::SetItemTooltip("Sign change of the measured Bx. Real sample times.");
    } else {
        ImGui::TextDisabled("no recent sector crossing");
        if (ImGui::IsItemHovered())
            ImGui::SetItemTooltip("No sign change in the measured Bx within 6 h.");
    }

    ImGui::Separator();
    footerText();

    ImGui::End();
}
