// M11: ventana "Sun" (implementacion; ver SunPanel.h).
#include "UI/SunPanel.h"

#include <imgui.h>

#include <cmath>
#include <cstdio>

#include "Utils/SolarPosition.h"

void SunPanel::draw(const SunPanelInput& in, SunGeom& geom) {
    ImVec2 dsp = ImGui::GetIO().DisplaySize;
    if (!geomInit_) {
        geomInit_ = true;
        if (geom.x < 0.0f) {
            geom.x = dsp.x - 352.0f;
            geom.y = 40.0f;
        }
        ImGui::SetNextWindowPos(ImVec2(geom.x, geom.y));
        ImGui::SetNextWindowSize(ImVec2(geom.w, geom.h));
    }
    if (!ImGui::Begin("Sun", in.pVisible)) {
        ImGui::End();
        return;
    }
    // Geometria persistente (App la guarda en settings.cfg al salir).
    {
        ImVec2 p = ImGui::GetWindowPos();
        ImVec2 s = ImGui::GetWindowSize();
        geom.x = p.x;
        geom.y = p.y;
        geom.w = s.x;
        geom.h = s.y;
    }

    ImGui::TextUnformatted(in.prodName);
    if (in.texId != 0) {
        ImGui::Image((ImTextureID)(intptr_t)in.texId, ImVec2(300, 300));
        if (in.pProduct && ImGui::IsItemHovered() && ImGui::IsMouseClicked(0))
            *in.pProduct ^= 1;  // M11: click alterna HMIIF<->A304 sin recomputar
        if (ImGui::IsItemHovered())
            ImGui::SetItemTooltip("Click: alterna SDO/HMI <-> SDO/AIA-304");

        // Markers sobre grupos oscuros (plan §13.4 + paridad demo :128-133).
        if (in.spots && !in.spots->empty()) {
            const double jd = sundisk::unixToJD(in.utc);
            const double l0 = sundisk::carringtonL0(jd);
            const double lam = SolarPosition::getApparentEclipticLonDeg(in.utc);
            const double b0 = sundisk::solarB0(lam);
            ImVec2 r0 = ImGui::GetItemRectMin();
            ImVec2 rsz = ImGui::GetItemRectSize();
            ImVec2 mouse = ImGui::GetMousePos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            std::map<int, sundisk::Sunspot> byReg = sundisk::latestByRegion(*in.spots);
            for (const auto& kv : byReg) {
                const sundisk::Sunspot& s = kv.second;
                double la = s.lat, dlon = 0.0;
                bool okLoc = !s.loc.empty() &&
                             sundisk::parseLocation(s.loc, la, dlon);
                if (!okLoc) {
                    if (s.hasDlon)
                        dlon = s.dlon;
                    else if (s.hasLonCarr)
                        dlon = sundisk::wrap180(s.lonCarr - l0);
                    else
                        continue;  // sin posicion usable
                }
                sundisk::SpotProj p = sundisk::projectSpot(la, dlon, b0);
                if (!p.visible) continue;
                ImVec2 c(r0.x + (float)((p.x * 0.5 + 0.5) * rsz.x),
                         r0.y + (float)((0.5 - p.y * 0.5) * rsz.y));
                dl->AddCircleFilled(c, 5.0f, IM_COL32(0, 230, 255, 230));
                dl->AddCircle(c, 8.0f, IM_COL32(0, 230, 255, 140), 24, 1.5f);
                float dx = mouse.x - c.x, dy = mouse.y - c.y;
                if (dx * dx + dy * dy < 81.0f) {
                    char tip[96];
                    if (!s.loc.empty())
                        std::snprintf(tip, sizeof(tip), "AR%d · %s · %d spots",
                                      s.region, s.loc.c_str(), s.numspot);
                    else
                        std::snprintf(tip, sizeof(tip), "AR%d · Carr %.0f · %d spots",
                                      s.region, s.lonCarr, s.numspot);
                    ImGui::SetItemTooltip("%s", tip);
                }
            }
        }
    } else {
        ImGui::TextDisabled("SDO: waiting for image...");
    }
    char cap[64];
    std::snprintf(cap, sizeof(cap), "SDO/%s · updated %s UTC", in.prodTag, in.imgTime);
    ImGui::TextUnformatted(cap);
    ImGui::Text("X-ray: %s", in.xrayText);
    if (ImGui::IsItemHovered())
        ImGui::SetItemTooltip("GOES X-ray class (dayside D-layer driver).");
    ImGui::End();
}
