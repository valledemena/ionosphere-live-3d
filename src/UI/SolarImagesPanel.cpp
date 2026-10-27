// Lote 4 / modulo B: ventana "Solar imagery" (implementacion; ver .h).
//
// Solo dibuja lo que el adaptador tiene: dos imagenes reales (o el aviso de que
// no hay ninguna). Cada seccion declara fuente, hora UTC, edad y "live"/"cached".
#include "UI/SolarImagesPanel.h"

#include <glad/glad.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

namespace {

const SolProduct kOrder[2] = {SolProduct::LascoC2, SolProduct::DrapGlobal};

// Epoch -> texto UTC. "--" si no hay fecha.
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

// Host de una URL https (para el pie de atribucion; "" si no encaja).
const char* hostOf(const char* url) {
    if (!url) return "";
    const char* p = std::strstr(url, "://");
    return p ? p + 3 : url;
}

const char* tooltipFor(SolProduct p) {
    return p == SolProduct::DrapGlobal
               ? "NOAA SWPC D-RAP: predicted D-region absorption (model output, "
                 "not a measurement). Global map, latest publication."
               : "SOHO/LASCO C2 coronagraph: latest white-light image published "
                 "by NASA/ESA. Coronagraph, not a forecast.";
}

}  // namespace

void SolarImagesPanel::syncFromAdapter(const SolImageAdapter& adapter) {
    for (int i = 0; i < 2; ++i) {
        const double stamp = adapter.getLastFetchTime(kOrder[i]);
        if (stamp == stampSeen_[i]) continue;
        stampSeen_[i] = stamp;
        shown_[i] = adapter.getImage(kOrder[i]);
        upload(i, shown_[i]);
    }
}

void SolarImagesPanel::upload(int idx, const SolImage& img) {
    if (!img.valid || img.w <= 0 || img.h <= 0 ||
        img.rgb.size() != (size_t)img.w * (size_t)img.h * 3u) {
        texValid_[idx] = false;  // nada que subir; no se inventa textura
        return;
    }
    if (tex_[idx] == 0) glGenTextures(1, &tex_[idx]);
    if (tex_[idx] == 0) {
        texValid_[idx] = false;
        return;
    }
    GLint prevAlign = 4;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevAlign);
    glBindTexture(GL_TEXTURE_2D, tex_[idx]);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, img.w, img.h, 0, GL_RGB, GL_UNSIGNED_BYTE,
                 img.rgb.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, prevAlign);
    glBindTexture(GL_TEXTURE_2D, 0);  // no se deja la textura ligada al backend
    texW_[idx] = img.w;
    texH_[idx] = img.h;
    texValid_[idx] = true;
    printf("[SolPanel] texture %s %dx%d\n", SolImageAdapter::nameFor(kOrder[idx]),
           img.w, img.h);
}

void SolarImagesPanel::shutdown() {
    for (int i = 0; i < 2; ++i) {
        if (tex_[i] != 0) glDeleteTextures(1, &tex_[i]);
        tex_[i] = 0;
        texW_[i] = texH_[i] = 0;
        texValid_[i] = false;
    }
}

void SolarImagesPanel::draw(const SolarImagesInput& in, SolImagesGeom& geom) {
    const ImVec2 dsp = ImGui::GetIO().DisplaySize;
    if (!geomInit_) {
        geomInit_ = true;
        if (geom.x < 0.0f) {
            geom.x = std::max(8.0f, (dsp.x - geom.w) * 0.5f);
            geom.y = std::max(8.0f, std::min(120.0f, dsp.y - geom.h - 8.0f));
        }
        ImGui::SetNextWindowPos(ImVec2(geom.x, geom.y));
        ImGui::SetNextWindowSize(ImVec2(geom.w, geom.h));
    }
    if (!ImGui::Begin("Solar imagery", in.pVisible)) {
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
        ImGui::TextDisabled("No image available yet");
        ImGui::TextWrapped("No image adapter attached.");
        ImGui::End();
        return;
    }
    syncFromAdapter(*in.adapter);

    const bool busy = in.adapter->isFetching();
    if (ImGui::Button("Refresh")) {
        if (in.pKick) in.pKick->store(true);  // despierta al worker ya
    }
    if (ImGui::IsItemHovered())
        ImGui::SetItemTooltip("Download both images now instead of waiting for the "
                              "next cycle.");
    ImGui::SameLine();
    ImGui::TextDisabled(busy ? "fetching..." : "LASCO C2 + D-RAP");

    const ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchSame |
                                  ImGuiTableFlags_BordersInnerV;
    if (ImGui::BeginTable("solimgcols", 2, flags)) {
        ImGui::TableSetupColumn("LASCO C2");
        ImGui::TableSetupColumn("D-RAP");
        ImGui::TableNextRow();
        for (int i = 0; i < 2; ++i) {
            const SolProduct prod = kOrder[i];
            const SolImage& img = shown_[i];
            const std::string prodErr = in.adapter->getErrorMessage(prod);

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(SolImageAdapter::sourceNameFor(prod));

            const bool drawable = img.valid && texValid_[i] && tex_[i] != 0;
            if (drawable) {
                const float availW = std::max(64.0f, ImGui::GetContentRegionAvail().x);
                const float aspect = (float)img.w / (float)img.h;
                const float maxH = 190.0f;
                float dw = availW;
                float dh = dw / aspect;
                if (dh > maxH) {
                    dh = maxH;
                    dw = dh * aspect;
                }
                ImGui::Image((ImTextureID)(intptr_t)tex_[i], ImVec2(dw, dh));
                if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("%s", tooltipFor(prod));

                char fetched[32] = "--";
                fmtUtc(img.timeUtc, "%m-%d %H:%M", fetched, sizeof(fetched));
                char age[24] = "0 s";
                const double sec = (in.utc > img.timeUtc) ? (in.utc - img.timeUtc) : 0.0;
                fmtAge(sec, age, sizeof(age));
                ImGui::Text("%s UTC", fetched);
                if (ImGui::IsItemHovered())
                    ImGui::SetItemTooltip("Time of the download, not the time the "
                                          "instrument exposed the frame.");
                ImGui::TextDisabled("%s, %s old", SolImageAdapter::sourceTag(img), age);
                ImGui::TextDisabled("%dx%d px", img.w, img.h);
                if (!prodErr.empty()) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.72f, 0.25f, 1.0f));
                    ImGui::TextWrapped("refresh failed, showing previous image: %s",
                                       prodErr.c_str());
                    ImGui::PopStyleColor();
                }
            } else {
                ImGui::Dummy(ImVec2(1.0f, 6.0f));
                ImGui::TextDisabled("No image available yet");
                if (!prodErr.empty()) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.72f, 0.25f, 1.0f));
                    ImGui::TextWrapped("%s", prodErr.c_str());
                    ImGui::PopStyleColor();
                }
            }
        }
        ImGui::EndTable();
    }

    // Pie: atribucion + honestidad. El error combinado solo se repite aqui si
    // las dos celdas tienen imagen (si falta alguna, ya se ve arriba).
    ImGui::Separator();
    ImGui::TextDisabled("Sources: %s | %s", hostOf(SolImageAdapter::urlFor(kOrder[0])),
                        hostOf(SolImageAdapter::urlFor(kOrder[1])));
    if (ImGui::IsItemHovered()) {
        ImGui::SetItemTooltip("%s\n%s", SolImageAdapter::urlFor(kOrder[0]),
                              SolImageAdapter::urlFor(kOrder[1]));
    }
    ImGui::TextDisabled("D-RAP is model output, not a measurement. LASCO C2 is a "
                        "coronagraph image.");
    ImGui::TextDisabled("No data means nothing is drawn: no synthetic image.");
    const bool bothShown = shown_[0].valid && shown_[1].valid;
    if (bothShown) {
        const std::string all = in.adapter->getErrorMessage();
        if (!all.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.72f, 0.25f, 1.0f));
            ImGui::TextWrapped("Last error: %s", all.c_str());
            ImGui::PopStyleColor();
        }
    }

    ImGui::End();
}
