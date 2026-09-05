#pragma once
#include <cstddef>
#include <string>

// Paletas compartidas (viridis/inferno/plasma/turbo/aurora) como gradientes
// lineales entre stops RGB. Extraido de GridLayer.cpp para reutilizar en
// VolumeRenderer (sampler1D) sin duplicar tablas.
namespace colormap {
inline void buildPalette(const std::string& name, size_t n, float* out) {
    struct Stop { float pos, r, g, b; };
    const Stop* stops = nullptr;
    int nStops = 0;

    // Viridis
    static const Stop vir[] = {
        {0.00f, 0.267f, 0.004f, 0.329f}, {0.13f, 0.282f, 0.140f, 0.458f},
        {0.25f, 0.253f, 0.265f, 0.530f}, {0.38f, 0.191f, 0.407f, 0.557f},
        {0.50f, 0.127f, 0.566f, 0.551f}, {0.63f, 0.206f, 0.718f, 0.473f},
        {0.75f, 0.478f, 0.824f, 0.318f}, {0.88f, 0.741f, 0.873f, 0.150f},
        {1.00f, 0.993f, 0.906f, 0.144f},
    };
    // Inferno (aproximado)
    static const Stop inf[] = {
        {0.00f, 0.000f, 0.000f, 0.040f}, {0.13f, 0.108f, 0.031f, 0.310f},
        {0.25f, 0.358f, 0.045f, 0.451f}, {0.38f, 0.595f, 0.083f, 0.405f},
        {0.50f, 0.786f, 0.153f, 0.203f}, {0.63f, 0.932f, 0.318f, 0.131f},
        {0.75f, 0.984f, 0.500f, 0.147f}, {0.88f, 0.988f, 0.711f, 0.300f},
        {1.00f, 0.988f, 0.998f, 0.645f},
    };
    // Plasma (aproximado)
    static const Stop plm[] = {
        {0.00f, 0.050f, 0.030f, 0.527f}, {0.13f, 0.240f, 0.017f, 0.678f},
        {0.25f, 0.471f, 0.028f, 0.696f}, {0.38f, 0.681f, 0.107f, 0.594f},
        {0.50f, 0.838f, 0.264f, 0.443f}, {0.63f, 0.944f, 0.446f, 0.320f},
        {0.75f, 0.986f, 0.654f, 0.258f}, {0.88f, 0.989f, 0.852f, 0.354f},
        {1.00f, 0.940f, 0.975f, 0.131f},
    };
    // Turbo (approximation multi-hue)
    static const Stop trb[] = {
        {0.00f, 0.190f, 0.071f, 0.232f}, {0.13f, 0.214f, 0.357f, 0.770f},
        {0.25f, 0.165f, 0.604f, 0.843f}, {0.38f, 0.196f, 0.784f, 0.582f},
        {0.50f, 0.450f, 0.866f, 0.314f}, {0.63f, 0.762f, 0.784f, 0.042f},
        {0.75f, 0.954f, 0.555f, 0.036f}, {0.88f, 0.966f, 0.256f, 0.052f},
        {1.00f, 0.648f, 0.033f, 0.112f},
    };
    // Aurora (NOAA Ovation overlay): transparent black -> deep green -> neon.
    static const Stop aur[] = {
        {0.00f, 0.000f, 0.010f, 0.000f}, {0.25f, 0.000f, 0.250f, 0.050f},
        {0.50f, 0.050f, 0.650f, 0.150f}, {0.75f, 0.300f, 1.000f, 0.350f},
        {1.00f, 0.850f, 1.000f, 0.550f},
    };

    const std::string& nm = name;
    if (nm == "inferno") { stops = inf; nStops = sizeof(inf)/sizeof(inf[0]); }
    else if (nm == "plasma") { stops = plm; nStops = sizeof(plm)/sizeof(plm[0]); }
    else if (nm == "turbo") { stops = trb; nStops = sizeof(trb)/sizeof(trb[0]); }
    else if (nm == "aurora") { stops = aur; nStops = sizeof(aur)/sizeof(aur[0]); }
    else { stops = vir; nStops = sizeof(vir)/sizeof(vir[0]); }

    for (size_t i = 0; i < n; i++) {
        float t = (float)i / (float)(n - 1);
        int s = 0;
        for (int j = 0; j < nStops - 1; j++) {
            if (t >= stops[j].pos && t <= stops[j + 1].pos) { s = j; break; }
        }
        float f = (t - stops[s].pos) / (stops[s + 1].pos - stops[s].pos + 1e-6f);
        out[i * 3 + 0] = stops[s].r + f * (stops[s + 1].r - stops[s].r);
        out[i * 3 + 1] = stops[s].g + f * (stops[s + 1].g - stops[s].g);
        out[i * 3 + 2] = stops[s].b + f * (stops[s + 1].b - stops[s].b);
    }
}
}  // namespace colormap
