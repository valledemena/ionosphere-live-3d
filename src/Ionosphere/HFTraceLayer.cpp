#include "HFTraceLayer.h"
#include "Ionosphere/DensityVolume.h"  // densityDir: marco del motor (bug 180 del bucket)
#include <glm/gtc/type_ptr.hpp>
#include <cstdio>
#include <cstring>

// =============================================================================
// M8 (plan 11): implementacion GL de la capa de trazado HF.
// Fisica pura: namespace hfc en HFTraceLayer.h (testeo sin GL). Aqui solo
// geometria CPU de la polilinea + ribbon + uniforms.
// =============================================================================

// Fallbacks inline (patron M6): EXACTAMENTE el mismo GLSL que
// shaders/hftrace.{vert,frag} — si el archivo falta, compila lo mismo.
static const char* kTraceVertSrc = R"(#version 400 core
// M8 (plan 11.3): ribbon camera-facing — la geometria CPU es una polilinea
// (aPos/aT/aViable/aTangent/aSide); el shader la engorda.
// M9c (P1): el uniform entra como semiancho GEOmetrico = halfWidth demo
// (hfc::ribbonHalfWidth) x span (hfc::ribbonSpan) desde render() — el
// ribbon cubre nucleo+aura con el MISMO strip y vertex count; aSide pasa
// como vSide (fraccion del borde geometrico) y el FRAG recorta el perfil.
layout(location=0) in vec3 aPos;
layout(location=1) in float aT;
layout(location=2) in float aViable;
layout(location=3) in vec3 aTangent;
layout(location=4) in float aSide;
uniform mat4 u_view, u_projection;
uniform vec3 u_camPos;
uniform float u_halfWidth;   // M9c: semiancho GEOmetrico = demo 0.0075 (hfc::ribbonHalfWidth) x span (hfc::ribbonSpan)
out float vT;
out float vViable;
out float vSide;             // M9c: -1..+1 respecto al borde geometrico (u_halfWidth)
void main() {
    vT = aT;
    vViable = aViable;
    vSide = aSide;
    vec3 toCam = normalize(u_camPos - aPos);
    vec3 n = normalize(cross(aTangent, toCam));
    vec3 p = aPos + n * (u_halfWidth * aSide);
    gl_Position = u_projection * u_view * vec4(p, 1.0);
}
)";

static const char* kTraceFragSrc = R"(#version 400 core
// M8 (plan 11.3): color por viabilidad del salto + pulso viajero.
in float vT;
in float vViable;
in float vSide;              // M9c: -1..+1 del borde geometrico (perfil del tubo)
uniform float u_time;
out vec4 fragColor;
void main() {
    // M9e (veredicto DAVID de M9d: "el cursor es blanco y en la demo es
    // verde; el carril siempre verde — nada blanco en ningun punto"):
    // colores DEMO-VERBATIM (shaders.ts:460-462). El R del verde baja de
    // 0.30 a 0.10: con R diminuto el pico no puede lavarse a blanco a
    // NINGUNA ganancia (solo G/B saturan = verde intenso como la demo).
    vec3 col = vec3(0.95, 0.16, 0.16);                   // cerrado (demo)
    if (vViable > 0.75)      col = vec3(0.10, 0.95, 0.55);  // abierto (demo)
    else if (vViable > 0.25) col = vec3(0.98, 0.72, 0.10);  // marginal (demo)
    // M9 (paridad demo, literales shaders.ts:450/465-467): cursor 0.22
    // ciclos/s (recorrido 4.5 s), 2.5 pulsos en arco, envolvente
    // 0.9*exp(-36*(p-0.5)^2), ganancia 0.9, alpha 0.9. Direccion
    // TX->RX intacta: la fase sigue siendo (arco*k - t*v).
    // M9fix: mezcla MULTIPLICATIVA col*(0.55+pulse), verbatim demo
    // (shaders.ts:465-467): el cursor hereda el color del tramo; la
    // aditiva col+pulse clipeaba a blanco (1.0+0.9) siempre.
    float p = fract(u_time * 0.22 - vT * 2.5);
    // M9d: envolvente unitaria del cursor (pulse = 0.9*env — la MISMA
    // exp, cero uniforms y cero exp nuevas; twins hfc::haloPulse*).
    float env = exp(-36.0 * (p - 0.5) * (p - 0.5));
    float pulse = 0.9 * env;
    // M9c (P1, paridad demo): perfil transversal tubo+halo — s = |vSide|
    // (fraccion del borde geometrico). Nucleo pleno con pedestal 1.35
    // hasta s=0.45 (±0.0068), hombro AA 0.45->0.55 que reemplaza el corte
    // duro M9b (el borde ±0.0075 cae en s=0.5 con 0.85), pedestal 0.35 y
    // aura hasta s=1 (0 = borde geometrico ±0.015 invisible). Twin C++:
    // hfc::traceProfile (tests seccion O). Anclas 0.85/0.26/0 del sello
    // exactas; f(0)=1.35 (correccion aritmetica: el pedestal del halo
    // vive DENTRO del nucleo — sin el, la aura saltaria 0->0.35 en 0.55).
    float s = abs(vSide);
    float core = 1.0 - smoothstep(0.45, 0.55, s);
    float halo = 0.35 * (1.0 - smoothstep(0.55, 1.0, s));
    // M9d (neon ambiental, peticion de DAVID tras la fair shot M9c): el
    // halo RESPIRA con la envolvente del cursor — haloEff =
    // halo*(0.6+0.8*env) (twins hfc::haloPulseFloor/haloPulseGain):
    // x0.6 en reposo (aura tenue), x1.4 al paso de la cabeza (aura
    // encendida que la envuelve). El nucleo NO respira: el cuerpo del
    // tubo queda estable. Twin: hfc::traceProfileBreath(s, env); el
    // perfil estatico M9c (traceProfile) = breath con env = 0.5 exacto.
    float haloEff = halo * (0.6 + 0.8 * env);
    float prof = core + haloEff;
    // M9c (P2): cabeza del pulso — aditivo ACOTADO a la cima (|p-0.5| <
    // 0.12, hfc::pulseHeadBoost) x nucleo transversal (s < 0.45,
    // hfc::pulseHeadGate). M9e (matizacion de DAVID): la cabeza va TENIDA
    // por col — el verde del cursor sale del propio color del tramo como
    // en la demo, no de una suma blanquecina (el vec3(0.85,0.92,1.0) M9c
    // eliminado: con R=0.10 el pico satura G/B = verde intenso, nunca
    // blanco). Fuera de la cabeza add = 0 (M9fix intacto).
    float boost = 1.0 - smoothstep(0.0, 0.12, abs(p - 0.5));
    float gate  = 1.0 - smoothstep(0.15, 0.45, s);
    vec3 add = col * (0.55 * boost * gate);
    // M9d: derrame neon del cursor — luz TENIDA por el color del tramo
    // (M9fix: nunca blanca) que se sale del tubo por todo el span con
    // caida lineal (twin hfc::glowSpill = 0.45*env*(1-s)). En rgb Y en
    // alpha: el ribbon mezcla (SRC_ALPHA, ONE) — dst recibe rgb*alpha, y
    // un spill solo-rgb quedaria atenuado a ~0.03 en la falda (alpha
    // ~0.33 ahi); con alpha propio la falda al paso de la cabeza llega a
    // ~3-4x M9c. M9e: gain 0.30 -> 0.45 (plan-B sellado: "derrame exterior
    // flojo") y CABALGA LA ENVOLVENTE (env, ancha: +-0.25 de fase ~ 20%
    // del circuito — medido contra la demo en vivo; halo 2-3x el carril)
    // en vez del collar estrecho (boost, +-0.12): el tramo que avanza se
    // lee LARGO y con fuerza como el gloom de la demo ("manda lo que se
    // ve"). En reposo env=0 => spill=0 (arco puro multiplicativo M9d).
    float spill = 0.45 * env * (1.0 - s);
    fragColor = vec4(col * ((0.55 + pulse) * prof + spill) + add,
                     0.9 * prof + spill);
}
)";

void HFTraceLayer::init() {
    if (!shader_.load("shaders/hftrace.vert", "shaders/hftrace.frag")) {
        fprintf(stderr, "[HFTraceLayer] shaders/hftrace.{vert,frag} no encontrado, usando fallback inline\n");
        shader_.loadFromSource(kTraceVertSrc, kTraceFragSrc);
    }
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    printf("[OK] HFTraceLayer initialized\n");
}

void HFTraceLayer::destroy() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    vao_ = vbo_ = 0;
    vertCount_ = 0;
}

void HFTraceLayer::update(const Input& in) {
    // P7 (plan 11.5): firma — si (tx/rx | freq | stamp | exag M9 | offset M9fix) no cambia,
    // no se recomputa nada (el pulso viaja por uniform u_time).
    if (sigValid_ || sigStamp_ >= 0) {
        const bool sameTx = (in.txLatDeg == sigTxLat_) && (in.txLonDeg == sigTxLon_);
        const bool sameRx = (in.rxLatDeg == sigRxLat_) && (in.rxLonDeg == sigRxLon_);
        if (sameTx && sameRx && in.freqMHz == sigFreq_ && in.gridStamp == sigStamp_ &&
            in.txValid == sigValid_ && in.heightExag == sigExag_ &&
            in.apexOffsetKm == sigOffset_) {   // M9: exag | M9fix: offset de capa -> rebuild
            return;
        }
    }
    sigTxLat_ = in.txLatDeg; sigTxLon_ = in.txLonDeg;
    sigRxLat_ = in.rxLatDeg; sigRxLon_ = in.rxLonDeg;
    sigFreq_ = in.freqMHz;
    sigExag_ = in.heightExag;             // M9: entra a la firma
    sigOffset_ = in.apexOffsetKm;         // M9fix: entra a la firma
    sigStamp_ = in.gridStamp;
    sigValid_ = in.txValid;
    heightExag_ = in.heightExag;          // M9: para el ancho del render
    apexOffset_ = in.apexOffsetKm;        // M9fix: para buildPolyline

    HFCircuitResult r;
    r.txLatDeg = in.txLatDeg; r.txLonDeg = in.txLonDeg;
    r.rxLatDeg = in.rxLatDeg; r.rxLonDeg = in.rxLonDeg;
    if (in.txValid && in.rxValid && in.foF2Grid && in.hmF2Grid && in.lufGrid) {
        const double D = hfc::greatCircleKm(in.txLatDeg, in.txLonDeg, in.rxLatDeg, in.rxLonDeg);
        if (D >= 1.0) {
            // h' del circuito: hmF2 del punto medio geodesico (P2).
            double midLat = 0.0, midLon = 0.0;
            hfc::arcPoint(in.txLatDeg, in.txLonDeg, in.rxLatDeg, in.rxLonDeg, 0.5, midLat, midLon);
            const double hV = hfc::virtualHeightKm(
                hfc::sampleGrid(in.hmF2Grid, in.gridW, in.gridH, midLat, midLon));
            const hfc::HopLayout lay = hfc::hopLayout(D, hV);
            r.valid = true;
            r.distKm = D;
            r.nHops = lay.nHops;
            r.hVirtualKm = hV;
            r.elevDeg = lay.elevDeg;
            double worstMuf = 1.0e30;   // min sobre saltos = MUF del circuito
            double worstLuf = -1.0e30;  // max sobre saltos = peor absorcion
            for (int k = 0; k < lay.nHops; k++) {
                double rLat = 0.0, rLon = 0.0;
                hfc::arcPoint(in.txLatDeg, in.txLonDeg, in.rxLatDeg, in.rxLonDeg,
                              hfc::reflectionFraction(k, lay.nHops), rLat, rLon);
                const double foF2R = hfc::sampleGrid(in.foF2Grid, in.gridW, in.gridH, rLat, rLon);
                const double hmF2R = hfc::sampleGrid(in.hmF2Grid, in.gridW, in.gridH, rLat, rLon);
                const double lufR  = hfc::sampleGrid(in.lufGrid, in.gridW, in.gridH, rLat, rLon);
                const double mufK = hfc::mufForHopMHz(foF2R, hmF2R, lay.hopKm);
                r.hopMufMHz.push_back(mufK);
                if (mufK < worstMuf) worstMuf = mufK;
                if (lufR > worstLuf) worstLuf = lufR;
            }
            r.mufMHz = worstMuf;
            r.fotMHz = hfc::fotOfMHz(worstMuf);
            r.lufMHz = (worstLuf > 0.0) ? worstLuf : 0.0;
        }
    }
    result_ = r;
    if (result_.valid) buildPolyline(in, result_);
    else vertCount_ = 0;
}

// Polilinea continua del circuito (plan 11.3): por salto, 2 patas x 14
// muestras compartiendo extremos (14 + 13 = 27 unicos por salto); pata =
// slerp del punto de suelo al punto de reflexion con altitud h'·t (subida)
// y h'·(1-t) (bajada) — todo · explodeFactor y +(altKm-300) en M9/M9fix
// (hfc::legAltitudeKm: el arco acompana la altura visual de las capas y
// al slider «Altitude (km)»; la rampa es lineal en t, LEGS=14 sigue
// sub-pixel a cualquier exageracion); posicion mundo
// via densityDir (marco del motor). aT = longitud de
// arco acumulada (0..1); tangentes por diferencias centrales; cada muestra
// se emite dos veces (side -1/+1) para el TRIANGLE_STRIP del ribbon.
void HFTraceLayer::buildPolyline(const Input& in, const HFCircuitResult& r) {
    struct Sample { float x, y, z; float viable; };
    std::vector<Sample> s;
    const int LEGS = 14;
    for (int k = 0; k < r.nHops; k++) {
        double gLat0 = 0.0, gLon0 = 0.0, rLat = 0.0, rLon = 0.0, gLat1 = 0.0, gLon1 = 0.0;
        const double f0 = (double)k / (double)r.nHops;
        const double fr = hfc::reflectionFraction(k, r.nHops);
        const double f1 = (double)(k + 1) / (double)r.nHops;
        hfc::arcPoint(in.txLatDeg, in.txLonDeg, in.rxLatDeg, in.rxLonDeg, f0, gLat0, gLon0);
        hfc::arcPoint(in.txLatDeg, in.txLonDeg, in.rxLatDeg, in.rxLonDeg, fr, rLat, rLon);
        hfc::arcPoint(in.txLatDeg, in.txLonDeg, in.rxLatDeg, in.rxLonDeg, f1, gLat1, gLon1);
        const double mufK = r.hopMufMHz.empty() ? 0.0 : r.hopMufMHz[(size_t)k];
        const float via = (float)hfc::hopViable((double)in.freqMHz, mufK);
        // Pata 1: suelo_k -> reflexion (t: 0..1, alt = (h'+off)·t·exag).
        for (int j = 0; j < LEGS; j++) {
            const double t = (double)j / (double)(LEGS - 1);
            double la = 0.0, lo = 0.0;
            hfc::arcPoint(gLat0, gLon0, rLat, rLon, t, la, lo);
            glm::vec3 p = densityDir((float)la, (float)lo) *
                (1.0f + (float)hfc::legAltitudeKm(r.hVirtualKm, t, (double)heightExag_, (double)apexOffset_) / 6371.0f);
            s.push_back({ p.x, p.y, p.z, via });
        }
        // Pata 2: reflexion -> suelo_{k+1} (t: 0..1, alt = (h'+off)·(1-t)·exag); la
        // muestra t=0 es la reflexion (ultima de la pata 1): se comparte.
        for (int j = 1; j < LEGS; j++) {
            const double t = (double)j / (double)(LEGS - 1);
            double la = 0.0, lo = 0.0;
            hfc::arcPoint(rLat, rLon, gLat1, gLon1, t, la, lo);
            glm::vec3 p = densityDir((float)la, (float)lo) *
                (1.0f + (float)hfc::legAltitudeKm(r.hVirtualKm, 1.0 - t, (double)heightExag_, (double)apexOffset_) / 6371.0f);
            s.push_back({ p.x, p.y, p.z, via });
        }
    }
    const int n = (int)s.size();
    if (n < 2) { vertCount_ = 0; return; }

    // aT: longitud de arco acumulada (cuerdas como proxy — 27 muestras por
    // salto la hacen practicamente exacta para el pulso).
    std::vector<float> arc((size_t)n, 0.0f);
    double total = 0.0;
    for (int i = 1; i < n; i++) {
        const Sample& a = s[(size_t)(i - 1)];
        const Sample& b = s[(size_t)i];
        const float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
        total += std::sqrt((double)(dx * dx + dy * dy + dz * dz));
        arc[(size_t)i] = (float)total;
    }
    if (total <= 0.0) { vertCount_ = 0; return; }
    for (int i = 0; i < n; i++) arc[(size_t)i] = (float)((double)arc[(size_t)i] / total);

    // Vertices: 9 floats (pos3, aT, aViable, tangent3, side) x (side -1,+1).
    std::vector<float> flat;
    flat.reserve((size_t)n * 2 * 9);
    float lastTan[3] = { 1.0f, 0.0f, 0.0f };
    for (int i = 0; i < n; i++) {
        const Sample& cur = s[(size_t)i];
        float tx, ty, tz;
        if (i < n - 1 && i > 0) {
            const Sample& a = s[(size_t)(i - 1)];
            const Sample& b = s[(size_t)(i + 1)];
            tx = b.x - a.x; ty = b.y - a.y; tz = b.z - a.z;
        } else if (i < n - 1) {
            const Sample& b = s[(size_t)(i + 1)];
            tx = b.x - cur.x; ty = b.y - cur.y; tz = b.z - cur.z;
        } else {
            const Sample& a = s[(size_t)(i - 1)];
            tx = cur.x - a.x; ty = cur.y - a.y; tz = cur.z - a.z;
        }
        float tl = std::sqrt(tx * tx + ty * ty + tz * tz);
        if (tl < 1.0e-9f) {   // muestra degenerada: tangente anterior
            tx = lastTan[0]; ty = lastTan[1]; tz = lastTan[2];
        } else {
            tx /= tl; ty /= tl; tz /= tl;
            lastTan[0] = tx; lastTan[1] = ty; lastTan[2] = tz;
        }
        for (int side = 0; side < 2; side++) {
            const float sd = (side == 0) ? -1.0f : 1.0f;
            flat.push_back(cur.x); flat.push_back(cur.y); flat.push_back(cur.z);
            flat.push_back(arc[(size_t)i]);
            flat.push_back(cur.viable);
            flat.push_back(tx); flat.push_back(ty); flat.push_back(tz);
            flat.push_back(sd);
        }
    }
    uploadVertices(flat);
}

void HFTraceLayer::uploadVertices(const std::vector<float>& flat) {
    vertCount_ = (int)(flat.size() / 9);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, flat.size() * sizeof(float), flat.data(), GL_DYNAMIC_DRAW);
    const GLsizei stride = 9 * sizeof(float);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(0);                       // aPos
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);                       // aT
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, (void*)(4 * sizeof(float)));
    glEnableVertexAttribArray(2);                       // aViable
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, stride, (void*)(5 * sizeof(float)));
    glEnableVertexAttribArray(3);                       // aTangent
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, stride, (void*)(8 * sizeof(float)));
    glEnableVertexAttribArray(4);                       // aSide
    glBindVertexArray(0);
}

void HFTraceLayer::render(const glm::mat4& view, const glm::mat4& projection,
                          const glm::vec3& camPos, float timeSec) {
    if (!visible_ || vertCount_ < 4) return;
    shader_.use();
    shader_.setMat4("u_view", view);
    shader_.setMat4("u_projection", projection);
    shader_.setVec3("u_camPos", camPos);
    shader_.setFloat("u_time", timeSec);
    // M9c: semiancho GEOmetrico — nucleo demo 0.0075 (M9b intacto) x span
    // (halo incluido, ±0.015). Plan-B de ancho = hfc::ribbonSpan + anclas.
    shader_.setFloat("u_halfWidth", hfc::ribbonHalfWidth(heightExag_) * hfc::ribbonSpan());
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, vertCount_);
    glBindVertexArray(0);
}
