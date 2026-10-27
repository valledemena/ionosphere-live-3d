// Panel "Geospace" (implementacion; ver GeospacePanel.h).
//
// Solo dibuja lo que los adaptadores han traido: WSPR (tabla por banda +
// sparkline de sesion), CME ENLIL (evento vigente + historico del boletin) y
// celdas NWS. Ningun valor se rellena a mano: si el snapshot esta invalido se
// muestra su nota y la seccion se queda sin filas.
#include "UI/GeospacePanel.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#include "Ionosphere/WsprArcs.h"

namespace {

// Orden canonico de bandas (el mismo de WsprArcs::bandColor y de la web).
const char* const kBandOrder[] = {"160m", "80m", "40m", "30m", "20m", "17m",
                                  "15m", "12m", "10m", "6m", "2m"};
const int kBandCount = (int)(sizeof(kBandOrder) / sizeof(kBandOrder[0]));

// Epoch UTC -> texto con strftime. gmtime y no localtime: el epoch ya es UTC y
// la app habla siempre en UTC. Deja "--" y devuelve false si no hay fecha.
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

// Nota del adaptador con texto de reserva: la linea nunca queda vacia.
void sectionNote(const char* note, const char* fallback) {
    const char* txt = (note && note[0]) ? note : fallback;
    ImGui::TextDisabled("%s", txt ? txt : "");
}

// glm::vec3 (0..1) -> color ImGui, con saturacion explicita.
ImU32 rgbToU32(const glm::vec3& c) {
    auto q = [](float v) {
        const float x = (v < 0.0f) ? 0.0f : ((v > 1.0f) ? 1.0f : v);
        return (int)(x * 255.0f + 0.5f);
    };
    return IM_COL32(q(c.x), q(c.y), q(c.z), 255);
}

}  // namespace

void GeospacePanel::draw(const GeospacePanelInput& in, GeospaceGeom& geom) {
    const ImVec2 dsp = ImGui::GetIO().DisplaySize;
    if (!geomInit_) {
        geomInit_ = true;
        if (geom.x < 0.0f) {
            // Default: misma columna que Sun, justo debajo si cabe; si la
            // pantalla no da para los 470 px, se sube lo justo para que la
            // ventana quepa entera (nunca a costa de salirse por abajo).
            geom.x = std::max(8.0f, dsp.x - 438.0f);
            const float belowSun = 40.0f + 470.0f + 8.0f;
            geom.y = std::max(8.0f, std::min(belowSun, dsp.y - geom.h - 8.0f));
        }
        ImGui::SetNextWindowPos(ImVec2(geom.x, geom.y));
        ImGui::SetNextWindowSize(ImVec2(geom.w, geom.h));
    }
    if (!ImGui::Begin("Geospace", in.pVisible)) {
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

    // Muestreo declarado de la sparkline: un punto cada 60 s como maximo (el
    // panel solo acumula lo observado en esta sesion, tope 120 puntos ~2 h).
    const int spots = (in.wspr && in.wspr->valid) ? (int)in.wspr->spots.size() : 0;
    if (hist_.empty() || (in.utc > histLast_ && in.utc - histLast_ >= 60.0)) {
        hist_.push_back({in.utc, spots});
        histLast_ = in.utc;
        while (hist_.size() > 120) hist_.pop_front();
    }

    const std::vector<WsprSpot>* wsprSpots =
        (in.wspr && in.wspr->valid) ? &in.wspr->spots : nullptr;

    // --- WSPR ---------------------------------------------------------------
    if (in.pWsprVisible) {
        ImGui::Checkbox("WSPR arcs", in.pWsprVisible);
        if (ImGui::IsItemHovered())
            ImGui::SetItemTooltip(
                "Live WSPRnet spots. Bands are the same colours as the 3D arcs, "
                "and the sparkline samples the spot count every 60 s while this "
                "panel is open.");
    }
    if (in.wsprFetching) {
        ImGui::TextUnformatted("fetching...");
    } else if (!in.wspr || !in.wspr->valid) {
        sectionNote(in.wspr ? in.wspr->note.c_str() : nullptr, "no spots");
    } else {
        const double ageMin =
            (in.utc > in.wspr->timeUtc) ? (in.utc - in.wspr->timeUtc) / 60.0 : 0.0;
        ImGui::Text("%d spots, newest %.0f min old", (int)in.wspr->spots.size(), ageMin);
    }

    // Agregado por banda (solo las bandas canonicas; una banda desconocida no
    // inventa fila).
    struct BandAgg {
        int count = 0;
        double snrSum = 0.0;
    };
    BandAgg agg[kBandCount];
    if (wsprSpots) {
        for (const WsprSpot& s : *wsprSpots) {
            for (int i = 0; i < kBandCount; ++i) {
                if (s.band == kBandOrder[i]) {
                    agg[i].count += 1;
                    agg[i].snrSum += (double)s.snrDb;
                    break;
                }
            }
        }
    }
    int bandsUsed = 0;
    for (int i = 0; i < kBandCount; ++i)
        if (agg[i].count > 0) ++bandsUsed;

    if (bandsUsed > 0) {
        if (ImGui::BeginTable("geowspr", 3, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Band");
            ImGui::TableSetupColumn("Spots");
            ImGui::TableSetupColumn("Mean SNR");
            ImGui::TableHeadersRow();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            for (int i = 0; i < kBandCount; ++i) {
                if (agg[i].count <= 0) continue;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                // Mismo color que los arcos 3D (tabla unica en WsprArcs).
                glm::vec3 rgb(0.6f, 0.6f, 0.6f);
                WsprArcs::bandColor(std::string(kBandOrder[i]), rgb);
                const ImVec2 cp = ImGui::GetCursorScreenPos();
                const float lh = ImGui::GetTextLineHeight();
                const float side = 9.0f;
                const float top = cp.y + (lh - side) * 0.5f;
                dl->AddRectFilled(ImVec2(cp.x, top), ImVec2(cp.x + side, top + side),
                                  rgbToU32(rgb));
                ImGui::Dummy(ImVec2(side + 5.0f, lh));
                ImGui::SameLine(0.0f, 0.0f);
                ImGui::TextUnformatted(kBandOrder[i]);
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%d", agg[i].count);
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%+.1f dB", agg[i].snrSum / (double)agg[i].count);
            }
            ImGui::EndTable();
        }
    } else {
        ImGui::TextDisabled("no spots in the last 10 min");
    }

    // Sparkline del total de spots (historia de esta sesion, no de la fuente).
    {
        const float availW = ImGui::GetContentRegionAvail().x;
        ImGui::InvisibleButton("geowsprspark", ImVec2(availW, 46.0f));
        const bool sparkHovered = ImGui::IsItemHovered();
        const ImVec2 p = ImGui::GetItemRectMin();
        const ImVec2 sz = ImGui::GetItemRectSize();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), IM_COL32(0, 0, 0, 70));
        dl->AddRect(p, ImVec2(p.x + sz.x, p.y + sz.y), IM_COL32(255, 255, 255, 28));

        int maxCount = 1;
        for (const HistPoint& hp : hist_) maxCount = std::max(maxCount, hp.spots);
        const float scale = (float)std::max(1, maxCount);
        const float innerX = p.x + 2.0f;
        const float innerY = p.y + 2.0f;
        const float innerW = std::max(1.0f, sz.x - 4.0f);
        const float innerH = std::max(1.0f, sz.y - 4.0f);
        auto yOf = [&](int v) {
            return innerY + innerH - innerH * ((float)v / scale);
        };
        if (hist_.size() == 1) {
            // Un solo punto: segmento horizontal corto (no hay tendencia aun).
            const float y = yOf(hist_.front().spots);
            dl->AddLine(ImVec2(innerX, y), ImVec2(innerX + innerW, y),
                        IM_COL32(255, 217, 51, 220), 1.5f);
        } else if (hist_.size() > 1) {
            std::vector<ImVec2> pts;
            pts.reserve(hist_.size());
            const float step = innerW / (float)(hist_.size() - 1);
            for (size_t i = 0; i < hist_.size(); ++i)
                pts.push_back(ImVec2(innerX + step * (float)i, yOf(hist_[i].spots)));
            dl->AddPolyline(pts.data(), (int)pts.size(), IM_COL32(255, 217, 51, 220),
                            ImDrawFlags_None, 1.5f);
        }
        if (sparkHovered)
            ImGui::SetItemTooltip(
                "Total spots sampled every 60 s (this session only, not a source "
                "history).");
    }

    // --- CME (WSA-ENLIL) ----------------------------------------------------
    ImGui::Separator();
    ImGui::TextUnformatted("CME (WSA-ENLIL)");
    if (in.pCmeVisible) {
        ImGui::Checkbox("CME front (ENLIL)", in.pCmeVisible);
        if (ImGui::IsItemHovered())
            ImGui::SetItemTooltip(
                "Shock front of the newest WSA-ENLIL simulation with a published "
                "Earth arrival time. The radius is a declared linear compression "
                "of the model progress (1.8 to 19.5 R_E) because 1 AU is about "
                "215 R_E: the position in that range is not physical, the ETA "
                "and speed are. ENLIL forecasts an arrival, not a Dp.");
    }
    if (in.cmeFetching) {
        ImGui::TextUnformatted("fetching...");
    } else if (!in.cme || !in.cme->valid) {
        sectionNote(in.cme ? in.cme->note.c_str() : nullptr, "no ENLIL data");
    } else {
        const CmeSnapshot& cs = *in.cme;
        if (cs.hasEvent) {
            const CmeEvent& ev = cs.event;
            const std::string countdown = CmeAdapter::cmeCountdown(ev.arrivalUtc, in.utc);
            ImGui::TextUnformatted(countdown.c_str());

            char overlay[48];
            std::snprintf(overlay, sizeof(overlay), "transit %.0f%%",
                          (double)in.cmeProgress * 100.0);
            ImGui::ProgressBar(in.cmeProgress, ImVec2(-1.0f, 0.0f), overlay);

            char line[128];
            std::snprintf(line, sizeof(line), "%.0f km/s   +/-%.0f deg   Kp %.1f",
                          (double)ev.speedKms, (double)ev.halfAngleDeg, (double)ev.kpMax);
            ImGui::TextUnformatted(line);

            if (in.cmeFrontR > 0.0f) {
                // Dos lineas: la ventana mide 430 px y la frase entera se
                // cortaba por la derecha (ImGui no parte TextDisabled).
                ImGui::TextDisabled("front at %.1f R_E (declared linear compression)",
                                    (double)in.cmeFrontR);
                if (in.cmeProgress >= 1.0f)
                    ImGui::TextDisabled("arrived - front held at max compression");
            }

            char id[192];
            std::snprintf(id, sizeof(id), "%s%s", ev.simId.c_str(),
                          ev.earthGB ? " - Earth-directed" : "");
            ImGui::TextDisabled("%s", id);
        } else {
            ImGui::TextDisabled("%s", cs.note.c_str());
        }

        // Historico del boletin (sims ya viene ordenado: mas reciente primero).
        if (!cs.sims.empty()) {
            ImGui::TextDisabled("Bulletin history (%d sims)", (int)cs.sims.size());
            ImGui::BeginChild("geocmehist", ImVec2(0.0f, 92.0f),
                              ImGuiChildFlags_Borders);
            const int nSims = (int)cs.sims.size();
            for (int i = 0; i < nSims && i < 10; ++i) {
                const CmeSimBrief& s = cs.sims[i];
                char model[24] = "--";
                char eta[24] = "--";
                char kp[32] = "";
                char row[160];
                fmtUtc(s.modelTimeUtc, "%m-%d %H:%M", model, sizeof(model));
                if (s.kpMax > 0.0f)
                    std::snprintf(kp, sizeof(kp), "  Kp %.1f", (double)s.kpMax);
                if (s.hasEta) {
                    fmtUtc(s.arrivalUtc, "%m-%d %H:%M", eta, sizeof(eta));
                    std::snprintf(row, sizeof(row), "%s  ETA %s  %.0f km/s%s", model, eta,
                                  (double)s.speedKms, kp);
                    ImGui::TextUnformatted(row);
                } else {
                    std::snprintf(row, sizeof(row), "%s  no ETA  %.0f km/s%s", model,
                                  (double)s.speedKms, kp);
                    ImGui::TextDisabled("%s", row);
                }
            }
            ImGui::EndChild();
            if (ImGui::IsItemHovered())
                ImGui::SetItemTooltip(
                    "Every simulation in the current ENLIL bulletin, newest first. "
                    "Model completion time, published arrival, speed.");
        }
    }

    // --- Tormentas (NWS) ----------------------------------------------------
    ImGui::Separator();
    ImGui::TextUnformatted("Storm cells (NWS)");
    if (in.pStormVisible) {
        ImGui::Checkbox("Storm sprites + whistlers", in.pStormVisible);
        if (ImGui::IsItemHovered())
            ImGui::SetItemTooltip(
                "Active NWS Severe Thunderstorm Warning polygons over CONUS: a "
                "sprite marker on each centroid plus the real IGRF dipole duct "
                "through it. No flash counts are claimed (the GLM granules are "
                "NetCDF and out of scope). CONUS only. No warnings, nothing "
                "drawn.");
    }
    if (in.stormFetching) {
        ImGui::TextUnformatted("fetching...");
    } else if (!in.storm || !in.storm->valid) {
        sectionNote(in.storm ? in.storm->note.c_str() : nullptr, "no NWS data");
    } else {
        ImGui::Text("%d active warnings (CONUS)", (int)in.storm->cells.size());
        const int nCells = std::min((int)in.storm->cells.size(), 8);
        for (int i = 0; i < nCells; ++i) {
            const StormCell& cell = in.storm->cells[i];
            ImGui::BulletText("%s", cell.area.c_str());
            char detail[128];
            if (cell.hasEnds) {
                char ends[24] = "--";
                fmtUtc(cell.endsUtc, "%H:%M", ends, sizeof(ends));
                std::snprintf(detail, sizeof(detail), "%.2f, %.2f  - ends %s",
                              (double)cell.lat, (double)cell.lon, ends);
            } else {
                std::snprintf(detail, sizeof(detail), "%.2f, %.2f  - no end time",
                              (double)cell.lat, (double)cell.lon);
            }
            ImGui::Indent(16.0f);
            ImGui::TextDisabled("%s", detail);
            ImGui::Unindent(16.0f);
        }
    }

    // --- Fulguraciones (DONKI) ----------------------------------------------
    ImGui::Separator();
    ImGui::TextUnformatted("Solar flares (DONKI, 72 h)");
    if (in.flaresFetching) {
        ImGui::TextUnformatted("fetching...");
    } else if (!in.flares || !in.flares->valid) {
        sectionNote(in.flares ? in.flares->note.c_str() : nullptr,
                    "no flare data");
    } else if (in.flares->events.empty()) {
        ImGui::TextDisabled("no flares in the last 72 h");
    } else {
        ImGui::Text("%d flares (newest first)", (int)in.flares->events.size());
        ImGui::TextDisabled("%s", in.flares->fromCache ? "served from cache"
                                                       : "live");
        const int nF = std::min((int)in.flares->events.size(), 6);
        for (int i = 0; i < nF; ++i) {
            const FlareEvent& fe = in.flares->events[i];
            // Color de clase como en los badges de rayos X: C ambar, M naranja,
            // X rojo. Es codigo de clase, no flujo.
            ImVec4 col(0.75f, 0.75f, 0.75f, 1.0f);
            if (!fe.classType.empty()) {
                if (fe.classType[0] == 'X') col = ImVec4(1.0f, 0.45f, 0.45f, 1.0f);
                else if (fe.classType[0] == 'M') col = ImVec4(1.0f, 0.65f, 0.30f, 1.0f);
                else if (fe.classType[0] == 'C') col = ImVec4(0.95f, 0.85f, 0.40f, 1.0f);
            }
            char peak[24] = "--:--";
            const double t = fe.hasPeak ? fe.peakUtc : fe.beginUtc;
            fmtUtc(t, "%m-%d %H:%M", peak, sizeof(peak));
            ImGui::BulletText("%s  %s", fe.classType.c_str(), peak);
            if (ImGui::IsItemHovered())
                ImGui::SetItemTooltip(
                    "GOES class code from the DONKI FLR catalog (C/M/X + peak "
                    "flux). Not a measured flux value here.");
            char detail[96];
            if (fe.activeRegion > 0)
                std::snprintf(detail, sizeof(detail), "AR %d  %s",
                              fe.activeRegion, fe.sourceLocation.c_str());
            else
                std::snprintf(detail, sizeof(detail), "%s",
                              fe.sourceLocation.empty() ? "region unknown"
                                                        : fe.sourceLocation.c_str());
            ImGui::Indent(16.0f);
            ImGui::TextColored(col, "%s", detail);
            ImGui::Unindent(16.0f);
        }
    }

    // --- Radiation belt enhancements (DONKI RBE) ---------------------------
    // Lote 5: eventos del catalogo RBE (incrementos del cinturon externo de
    // electrones). Es un catalogo de eventos, no un flujo: no se dibuja nada
    // en el globo ni se colorea el toro con esto.
    ImGui::Separator();
    ImGui::TextUnformatted("Radiation belt enhancements (DONKI, 58 d)");
    if (in.rbeFetching) {
        ImGui::TextUnformatted("fetching...");
    } else if (!in.rbe || !in.rbe->valid) {
        sectionNote(in.rbe ? in.rbe->note.c_str() : nullptr,
                    "no RBE data");
    } else if (in.rbe->events.empty()) {
        ImGui::TextDisabled("no RBE events in the last 58 days");
    } else {
        ImGui::Text("%d RBE events (newest first)", (int)in.rbe->events.size());
        ImGui::TextDisabled("%s", in.rbe->fromCache ? "served from cache"
                                                    : "live");
        const int nR = std::min((int)in.rbe->events.size(), 5);
        for (int i = 0; i < nR; ++i) {
            const RbeEvent& re = in.rbe->events[i];
            char when[24] = "--:--";
            fmtUtc(re.timeUtc, "%m-%d %H:%M", when, sizeof(when));
            ImGui::BulletText("%s  %s", when,
                              re.instrument.empty() ? "instrument unknown"
                                                    : re.instrument.c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetItemTooltip(
                    "DONKI RBE event: the catalog flags an enhancement of the "
                    "outer electron belt. Catalog entry, not a measured flux.");
        }
    }

    // --- Pie ----------------------------------------------------------------
    ImGui::Separator();
    ImGui::TextDisabled(
        "Sources: wsprnet.org volunteers | CCMC WSA-ENLIL | NWS alerts | CCMC DONKI");
    ImGui::TextDisabled("No data means nothing is drawn: no synthetic fallback.");

    ImGui::End();
}
