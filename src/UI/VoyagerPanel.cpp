// Lote 5 / modulo B: ventana "Voyager" (implementacion; ver .h).
//
// Solo texto: distancia, fecha de la fila, tiempo-luz, potencia recibida y
// factor de barrido, con la fuente y la edad. Sin dato no se pinta ninguna
// cifra: "No Voyager range available" + el motivo del adaptador.
#include "UI/VoyagerPanel.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>

namespace {

const char* const kHorizonsHost = "ssd.jpl.nasa.gov";
const char* const kHorizonsUrl = "https://ssd.jpl.nasa.gov/api/horizons.api";

// Epoch -> texto UTC; "--" si no hay fecha.
bool fmtUtc(double epoch, const char* fmt, char* buf, size_t n) {
    if (!buf || n == 0 || !fmt) return false;
    if (!std::isfinite(epoch) || epoch <= 0.0) {
        std::snprintf(buf, n, "--");
        return false;
    }
    const std::time_t tt = (std::time_t)epoch;
    std::tm tm{};
#if defined(_WIN32)
    if (gmtime_s(&tm, &tt) != 0) {
        std::snprintf(buf, n, "--");
        return false;
    }
#else
    if (gmtime_r(&tt, &tm) == nullptr) {
        std::snprintf(buf, n, "--");
        return false;
    }
#endif
    if (std::strftime(buf, n, fmt, &tm) == 0) {
        std::snprintf(buf, n, "--");
        return false;
    }
    return true;
}

// Edad legible y corta: "40 s", "7 min", "2.3 h", "5.1 d".
void fmtAge(double seconds, char* buf, size_t n) {
    const double s = (std::isfinite(seconds) && seconds > 0.0) ? seconds : 0.0;
    if (s < 60.0) std::snprintf(buf, n, "%.0f s", s);
    else if (s < 3600.0) std::snprintf(buf, n, "%.0f min", s / 60.0);
    else if (s < 86400.0) std::snprintf(buf, n, "%.1f h", s / 3600.0);
    else std::snprintf(buf, n, "%.1f d", s / 86400.0);
}

// Una sonda: tres lineas de cifras y una de etiquetas. La copia del fix ya
// viene validada por el adaptador; aqui solo se formatea.
void drawProbe(const char* name, const VoyagerProbe& probe, const std::string& err) {
    ImGui::PushID(name);
    ImGui::TextUnformatted(name);
    if (ImGui::IsItemHovered())
        ImGui::SetItemTooltip("JPL Horizons target %s, geocentric observer (500@399).",
                              name);

    if (!probe.fix.valid) {
        ImGui::SameLine();
        ImGui::TextDisabled("no range");
        if (ImGui::IsItemHovered())
            ImGui::SetItemTooltip("This probe returned no usable row. Nothing is "
                                  "invented to fill the gap.");
        if (!err.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.72f, 0.25f, 1.0f));
            ImGui::TextWrapped("%s", err.c_str());
            ImGui::PopStyleColor();
        } else {
            ImGui::TextDisabled("No Voyager range available");
        }
        ImGui::PopID();
        return;
    }

    ImGui::SameLine();
    ImGui::Text("%.3f AU", probe.fix.au);
    if (ImGui::IsItemHovered())
        ImGui::SetItemTooltip("Geocentric range from JPL Horizons (quantity 20, "
                              "apparent range). Refreshed on every fetch, never "
                              "pinned to a catalogue value.");

    ImGui::TextDisabled("  row %s UTC", probe.fix.date.c_str());
    if (ImGui::IsItemHovered())
        ImGui::SetItemTooltip("Time tag of the ephemeris row used (UTC). The "
                              "range is interpolated by JPL at this epoch, not "
                              "measured now.");

    ImGui::Text("  light time %.2f h one way", probe.lightHours);
    if (ImGui::IsItemHovered())
        ImGui::SetItemTooltip("One-way light time = range / c. This is how old "
                              "the signal is when it arrives.");

    ImGui::Text("  %.2e W received", probe.receivedW);
    if (ImGui::IsItemHovered())
        ImGui::SetItemTooltip("Link budget, not telemetry: 23 W TX, 3.7 m "
                              "spacecraft dish, 8.42 GHz, DSN 70 m, eta 0.55/0.65 "
                              "(declared). Order of magnitude only.");

    ImGui::Text("  sweep x %.0f over 10 s", probe.pulseFactor);
    if (ImGui::IsItemHovered())
        ImGui::SetItemTooltip("Animation factor: the real one-way transit is "
                              "compressed into a 10 s sweep. The compression is "
                              "shown, not hidden.");
    ImGui::PopID();
}

}  // namespace

void VoyagerPanel::draw(const VoyagerPanelInput& in, VoyagerGeom& geom) {
    const ImVec2 dsp = ImGui::GetIO().DisplaySize;
    if (!geomInit_) {
        geomInit_ = true;
        if (geom.x < 0.0f) {
            geom.x = std::max(8.0f, (dsp.x - geom.w) * 0.5f);
            geom.y = std::max(8.0f, std::min(140.0f, dsp.y - geom.h - 8.0f));
        }
        ImGui::SetNextWindowPos(ImVec2(geom.x, geom.y));
        ImGui::SetNextWindowSize(ImVec2(geom.w, geom.h));
    }
    if (!ImGui::Begin("Voyager", in.pVisible)) {
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

    if (!in.adapter) {
        ImGui::TextDisabled("No Voyager range available");
        ImGui::TextWrapped("No Voyager adapter attached.");
        ImGui::End();
        return;
    }

    const VoyagerSnapshot snap = in.adapter->getSnapshot();
    const std::string err = in.adapter->getErrorMessage();
    const bool busy = in.adapter->isFetching();

    if (ImGui::Button("Refresh")) {
        if (in.pKick) in.pKick->store(true);  // despierta al worker ya
    }
    if (ImGui::IsItemHovered())
        ImGui::SetItemTooltip("Ask JPL Horizons for both probes now instead of "
                              "waiting for the next cycle (two small POSTs).");
    ImGui::SameLine();
    ImGui::TextDisabled(busy ? "fetching..." : "JPL Horizons");
    if (ImGui::IsItemHovered())
        ImGui::SetItemTooltip("Observer ephemeris, geocentric, range in AU.");

    ImGui::Separator();

    if (!snap.valid) {
        ImGui::TextDisabled("No Voyager range available");
        if (!err.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.72f, 0.25f, 1.0f));
            ImGui::TextWrapped("%s", err.c_str());
            ImGui::PopStyleColor();
        } else {
            ImGui::TextWrapped("No fetch completed yet.");
        }
        ImGui::Separator();
        ImGui::TextDisabled("Source: JPL Horizons (%s)", kHorizonsHost);
        ImGui::TextDisabled("No range, no claim - no synthetic distance.");
        ImGui::End();
        return;
    }

    // Si el adaptador trae una sola sonda, el motivo del hueco se ensena en
    // esa sonda: la nota del snapshot dice cual falta.
    const std::string errForV1 = snap.v1.fix.valid ? std::string() : err;
    const std::string errForV2 = snap.v2.fix.valid ? std::string() : err;
    drawProbe("Voyager 1", snap.v1, errForV1);
    ImGui::Spacing();
    drawProbe("Voyager 2", snap.v2, errForV2);

    ImGui::Separator();

    char checked[32] = "--";
    fmtUtc(snap.timeUtc, "%Y-%m-%d %H:%M", checked, sizeof(checked));
    char age[24] = "0 s";
    const double sec = (in.utc > snap.timeUtc) ? (in.utc - snap.timeUtc) : 0.0;
    fmtAge(sec, age, sizeof(age));

    ImGui::TextDisabled("Source: JPL Horizons (%s)", kHorizonsHost);
    if (ImGui::IsItemHovered())
        ImGui::SetItemTooltip("%s\nTargets -31 (Voyager 1) and -32 (Voyager 2).",
                              kHorizonsUrl);

    ImGui::TextDisabled("Checked %s UTC, %s old", checked, age);
    if (ImGui::IsItemHovered())
        ImGui::SetItemTooltip("Time of the last successful download, not the "
                              "ephemeris row epoch.");

    ImGui::SameLine();
    if (snap.fromCache) {
        ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f), "cached");
        if (ImGui::IsItemHovered())
            ImGui::SetItemTooltip("Served from cache/voyager.txt (max 6 h). "
                                  "Not a live fetch.");
    } else {
        ImGui::TextDisabled("live");
        if (ImGui::IsItemHovered())
            ImGui::SetItemTooltip("Downloaded from JPL Horizons in this run.");
    }

    ImGui::TextDisabled("No range, no claim - no synthetic distance.");
    if (ImGui::IsItemHovered())
        ImGui::SetItemTooltip("If the range is missing, nothing is drawn or "
                              "scaled: no invented distance.");

    // Nota del adaptador (que sonda falto, o que se sirvio de cache). Solo si
    // anade algo a las lineas de arriba.
    if (!snap.note.empty() && snap.note.compare(0, 5, "live:") != 0) {
        ImGui::TextWrapped("%s", snap.note.c_str());
    }

    ImGui::End();
}
