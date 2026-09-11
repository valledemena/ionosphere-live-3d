#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <cmath>
#include <string>
#include <vector>
#include "Render/Shader.h"
#include "Utils/HfMath.h"

// =============================================================================
// HFTraceLayer — M8 (plan 11): ray-tracing HF multi-salto.
//
// Dos zonas con frontera dura (P1 del sello):
//   1. namespace hfc — fisica del circuito PURA (double, sin GL, sin red):
//      geodesica, arco de circulo maximo, h', MUF/FOT por salto, band-state,
//      muestreo bilineal de los grids 72x72 de giroInterpGrid/radioInterpGrid.
//      Todo inline en este header: tests/test_hf_circuit.cpp llama a
//      PRODUCCION sin enlazar el .cpp (patron test_model_foF2).
//   2. class HFTraceLayer — la capa GL: ribbon camera-facing (la geometria
//      CPU es una polilinea; el vertex shader la engorda), pulso viajero,
//      blending aditivo con depth write OFF (lo fija el bloque de overlays
//      de App.cpp), shader shaders/hftrace.{vert,frag} + fallback inline
//      (patron M6).
//
// P0 del sello (FE DE ERRATAS M9b — retract de MUSE, verificado contra el
// catalogo kc2g): D del circuito = geodesica REAL de las coordenadas del
// catalogo. EG931 = Eglin AFB, FL (30.5N, 273.5E) — FF051 es Fairford: la
// "correccion factual" de la era M4R (481 km / 1 salto / ~50.4 grados)
// usaba las coords de Fairford y era falsa. DB049->EG931 = ~7533 km =>
// 3 saltos de ~2511 km a ~7.9 grados: el plan 11.2 (7533/3/7.9) se
// acepta TAL CUAL. Los anclajes 2511/7.9/M=2.7143 siguen sinteticos en
// tests/test_hop_geometry.cpp (la formula, intacta).
//
// Convencion esferica: hfc:: opera en lat/lon puros (el slerp de arcPoint
// usa internamente la convencion de CoordinateSystem, transparente al
// round-trip); las posiciones MUNDO del ribbon y de los marcadores TX/RX
// (App.cpp) salen de densityDir (marco del motor, theta = pi - lon) — NO
// de latLonAltToPos, que esta 180 grados rotado sobre Y (bug del bucket en
// vivo, corregido). La esfera es de radio 1 normalizado: el ancho del
// ribbon 0.0075 (M9b: semiancho demo-verbatim, constante) es directo en unidades de mundo.
// M9 (port demo->C++, peticion de DAVID): el arco acompana la altura
// visual de las capas — la altitud de las patas se multiplica por
// explodeFactor (slider «Explode» 1..5 del panel Layers, default 1 =
// escala fisica; h' SIGUE vivo del grid GIRO). El ancho es el de la demo:
// 0.0075 CONSTANTE — re-sellado M9b con la comparativa lado a lado (la
// demo no escala el tubo con la exageracion; hfc::ribbonHalfWidth).
// Pulso a paridad de la demo (0.22/2.5/0.9/0.9).
// Fisica del circuito, grids y matriz de bandas intactas.
// M9fix (re-check de DAVID con la demo al lado): (1) el apice ADEMAS
// sigue al slider «Altitude (km)» de Layers — apice = (h' + altKm -
// 300)·exag, offset en la firma P7 (la shell de color ya vive a
// altKm·explode: el arco y la shell se mueven juntos, hueco (300-h')·
// exag constante); (2) cursor del pulso MULTIPLICATIVO col*(0.55+
// pulse) verbatim demo: el aditivo clipeaba a blanco. Fisica intacta.
// M9c (comparativa lado a lado con la demo, peticion de DAVID): perfil
// transversal del trazo = tubo+halo — render() pasa el semiancho
// GEOmetrico (hfc::ribbonHalfWidth x hfc::ribbonSpan: MISMO vertex
// count y 1 draw) y el frag recorta nucleo/hombro/aura
// (hfc::traceProfile) + cabeza del pulso acotada TENIDA por col desde
// M9e (hfc::pulseHeadBoost/Gate/Amplitude). Ancho nucleo M9b 0.0075 y
// mezcla multiplicativa M9fix intactos; muestras y colores por tramo
// intactos (P3/P4 sin cambio).
// M9d (neon ambiental del cursor — fair shot M9c de DAVID: "en la demo
// el cursor lleva un glow ambiental muy marcado; aqui solo brilla el
// nucleo"; diseno MUSE sellado por GLM): el halo RESPIRA con la
// envolvente del pulso (halo*(0.6+0.8*env) — hfc::haloPulseFloor/
// haloPulseGain) y la cabeza DERRAMA luz tenida por todo el span
// (hfc::glowSpill = 0.45*env*(1-s) desde M9e). ADAPTACION obligatoria al
// sello: el ribbon mezcla (SRC_ALPHA, ONE) — dst recibe rgb*alpha, asi
// que el derrame vive en rgb Y en alpha (un spill solo-rgb quedaria
// atenuado a ~0.03 en la falda). Cero uniforms/vertex/draw nuevos; la
// exp del cursor se reestructura como env unitaria (pulse = 0.9*env,
// misma cuenta). El nucleo NO respira y la mezcla M9fix queda intacta:
// solo el aura respira, solo la cabeza derrama (contraste dst del aura
// cabeza/reposo ~21x en s=0.7, 2.6x en M9c). Twins: traceProfileBreath/
// glowSpill (tests seccion P); el perfil M9c = breath(s, 0.5) exacto.
// M9e (veredicto DAVID de la re-captura M9d: nucleo bien, "derrame
// exterior flojo... el cursor es blanco y en la demo es verde; el carril
// siempre verde, el gloom avanza y SORESALE CON FUERZA del carril — nada
// blanco"; + matizacion MUSE: cabeza tenida por col capada, spill 0.45,
// velocidad/longitud como la demo): (1) colores de banda DEMO-VERBATIM
// shaders.ts:460-462 (hfc::traceColor*): el R del verde 0.30->0.10 mata
// el lavado a blanco a cualquier ganancia; (2) cabeza TENIDA (add =
// col*amp: el verde sale del propio col, suma blanquecina eliminada);
// (3) derrame 0.30->0.45 (plan-B sellado) y cabalgando la ENVOLVENTE
// env (medida en vivo: el glow de la demo avanza ancho +-0.25 de fase,
// halo 2-3x el carril — no el collar +-0.12). VELOCIDAD/LONGITUD: los
// literales 0.22/2.5 son demo-verbatim DESDE M9 (shaders.ts:450, u_time
// en segundos reales en ambos, aT = longitud de arco): verificado en
// vivo TX->RX ~13 s de captura = 11.36 s teoricos — la percepcion de
// "tramo corto/dardo" era el collar estrecho + cabeza blanca, no la
// velocidad. Twins: traceColor*/pulseCycleHz/pulseRepeatsPerArc
// (tests seccion Q).
// =============================================================================

namespace hfc {

// --- geodesia y arco (circulo maximo, slerp exacto) ---

// Distancia geodesica (haversine, R = 6371 — misma constante que HfMath).
inline double greatCircleKm(double lat1, double lon1, double lat2, double lon2) {
    const double p1 = lat1 * (M_PI / 180.0);
    const double p2 = lat2 * (M_PI / 180.0);
    const double dp = p2 - p1;
    const double dl = (lon2 - lon1) * (M_PI / 180.0);
    const double a = std::sin(dp / 2.0) * std::sin(dp / 2.0) +
                     std::cos(p1) * std::cos(p2) * std::sin(dl / 2.0) * std::sin(dl / 2.0);
    return 2.0 * hfmath::kEarthRadiusKm * std::asin(std::min(1.0, std::sqrt(a)));
}

// Punto a fraccion f del arco TX->RX (f=0 -> TX, f=1 -> RX). Slerp exacto
// sobre la esfera unitaria con la convencion de CoordinateSystem.
inline void arcPoint(double lat1, double lon1, double lat2, double lon2, double f,
                     double& outLat, double& outLon) {
    const double p1 = lat1 * (M_PI / 180.0), l1 = lon1 * (M_PI / 180.0);
    const double p2 = lat2 * (M_PI / 180.0), l2 = lon2 * (M_PI / 180.0);
    const double ax = std::cos(p1) * std::cos(l1), ay = std::sin(p1), az = -std::cos(p1) * std::sin(l1);
    const double bx = std::cos(p2) * std::cos(l2), by = std::sin(p2), bz = -std::cos(p2) * std::sin(l2);
    double dot = ax * bx + ay * by + az * bz;
    if (dot > 1.0) dot = 1.0;
    if (dot < -1.0) dot = -1.0;
    const double omega = std::acos(dot);
    if (omega < 1.0e-12) { outLat = lat1; outLon = lon1; return; }
    const double so = std::sin(omega);
    const double s1 = std::sin((1.0 - f) * omega) / so;
    const double s2 = std::sin(f * omega) / so;
    const double x = s1 * ax + s2 * bx;
    double y = s1 * ay + s2 * by;
    const double z = s1 * az + s2 * bz;
    if (y > 1.0) y = 1.0;
    if (y < -1.0) y = -1.0;
    outLat = std::asin(y) * (180.0 / M_PI);
    outLon = std::atan2(-z, x) * (180.0 / M_PI);
}

// --- fisica del circuito (P2/P3/P4 del sello, literales del plan 11.1) ---

// h' del circuito: clamp(hmF2 del punto medio, 140, 420) — plan 11.1.
inline double virtualHeightKm(double hmF2MidKm) {
    if (hmF2MidKm < 140.0) return 140.0;
    if (hmF2MidKm > 420.0) return 420.0;
    return hmF2MidKm;
}

// MUF(D) del salto: foF2(reflexion) · M(D) (Dudeney + ITU-R P.1240, HfMath).
inline double mufForHopMHz(double foF2ReflMHz, double hmF2ReflKm, double hopKm) {
    return foF2ReflMHz * hfmath::mFactor(hmF2ReflKm, hopKm);
}

// FOT = 0.85 · MUF — plan 11.1 (a nivel de circuito: MUF = min de los saltos).
inline double fotOfMHz(double mufMHz) { return 0.85 * mufMHz; }

// Estado de banda (plan 11.1, literales):
//   open     si LUF <= f <= FOT
//   marginal si 0.85·LUF <= f <= MUF
//   closed   en el resto
// open y marginal se solapan ([LUF,FOT] cae dentro de [0.85·LUF, MUF]):
// open gana — orden de evaluacion fijado por los bordes del test.
enum BandState { BAND_CLOSED = 0, BAND_MARGINAL = 1, BAND_OPEN = 2 };
inline BandState bandState(double fMHz, double lufMHz, double fotMHz, double mufMHz) {
    if (fMHz >= lufMHz && fMHz <= fotMHz) return BAND_OPEN;
    if (fMHz >= 0.85 * lufMHz && fMHz <= mufMHz) return BAND_MARGINAL;
    return BAND_CLOSED;
}

// aViable por salto (plan 11.3 literal: "0/0.5/1 segun MUF del salto vs
// frecuencia del panel" — el LUF gatea la matriz, no el color del arco):
//   1   si f <= 0.85·MUF_hop (tipo-FOT)
//   0.5 si f <= MUF_hop
//   0   si f > MUF_hop
inline double hopViable(double fMHz, double mufHopMHz) {
    if (fMHz <= 0.85 * mufHopMHz) return 1.0;
    if (fMHz <= mufHopMHz) return 0.5;
    return 0.0;
}

// Fraccion del arco del punto de reflexion del salto k (k = 0..n-1).
inline double reflectionFraction(int hopIdx, int nHops) {
    return ((double)hopIdx + 0.5) / (double)nHops;
}

// Layout de saltos: n = hopCountForDistance(D) (plan 11.1), salto = D/n,
// elevacion por biseccion (HfMath, [1,88] grados).
struct HopLayout { int nHops; double hopKm; double elevDeg; };
inline HopLayout hopLayout(double totalKm, double hVirtualKm) {
    HopLayout h;
    h.nHops = hfmath::hopCountForDistance(totalKm);
    if (h.nHops < 1) h.nHops = 1;
    h.hopKm = totalKm / (double)h.nHops;
    h.elevDeg = hfmath::elevationForHop(h.hopKm, hVirtualKm);
    return h;
}

// Muestreo bilineal de los grids 72x72 (layout giroInterpGrid: fila 0 =
// lat -90, col 0 = lon -180; nodos en las esquinas de celda — el relleno
// muestrea exactamente en el nodo (-90 + 180/H·r, -180 + 360/W·c); clamp
// a bordes fuera de rango).
inline double sampleGrid(const float* values, int w, int h, double lat, double lon) {
    if (!values || w < 2 || h < 2) return 0.0;
    double fy = (lat + 90.0) * (double)h / 180.0;
    double fx = (lon + 180.0) * (double)w / 360.0;
    if (fy < 0.0) fy = 0.0;
    if (fx < 0.0) fx = 0.0;
    if (fy > (double)(h - 1)) fy = (double)(h - 1);
    if (fx > (double)(w - 1)) fx = (double)(w - 1);
    const int y0 = (int)fy, x0 = (int)fx;
    const int y1 = (y0 + 1 < h) ? y0 + 1 : y0;
    const int x1 = (x0 + 1 < w) ? x0 + 1 : x0;
    const double ty = fy - (double)y0, tx = fx - (double)x0;
    const double v00 = (double)values[(size_t)y0 * w + x0];
    const double v10 = (double)values[(size_t)y0 * w + x1];
    const double v01 = (double)values[(size_t)y1 * w + x0];
    const double v11 = (double)values[(size_t)y1 * w + x1];
    return (1.0 - ty) * ((1.0 - tx) * v00 + tx * v10) + ty * ((1.0 - tx) * v01 + tx * v11);
}


// --- M9: exageracion visual del arco, acoplada a explodeFactor ---
// (App.cpp: volumeLayer/refShells/shell ya lo siguen; el arco era la
// unica capa que no. La fisica NO cambia: solo como se DIBUJA.) ---

// Altitud de una muestra de pata (subida t, bajada 1-t): (h'+off)·t·exag.
// M9fix: off = apexOffsetKm (App: altKm - 300, slider «Altitude (km)» de
// Layers) — el apice sigue a la shell de color (applyAltitudes mueve
// TEC/outer al mismo altKm·explode; default 300 => off = 0 = M9 exacto).
// Lineal en t => exageracion y offset escalan una recta, sin curvatura:
// LEGS=14 (27/salto) sigue sub-pixel a cualquier exag. Floor: la base
// h'+off no baja de 0 (altKm=100 con h' bajo => sin arco subterraneo).
inline double legAltitudeKm(double hVirtualKm, double legT, double heightExag,
                            double apexOffsetKm = 0.0) {
    double base = hVirtualKm + apexOffsetKm;   // M9fix: h' + (altKm - 300)
    if (base < 0.0) base = 0.0;                // M9fix: floor anti-subterraneo
    return base * legT * heightExag;
}

// Semiancho del ribbon: 0.0075 CONSTANTE a cualquier Explode (re-sellado
// M9b con la comparativa lado a lado demo/C++: la demo usa tubo de radio
// 0.0075 fijo — el ancho NO escala con la exageracion). El escalado M9
// (0.004@1 -> 0.012@5) se retira: preservaba un "look M8 @1.0" que
// David nunca vio (el ribbon fue invisible hasta M8fix-2).
inline float ribbonHalfWidth(float /*heightExag*/) {
    return 0.0075f;   // M9b: demo verbatim (tubo de radio 0.0075)
}

// --- M9c: perfil transversal del trazo (tubo+halo, paridad demo) ---
// Comparativa lado a lado (DAVID via MUSE + VLM GLM): la demo es un
// tubo grueso de nucleo con caida suave y halo envolvente; el M9b era
// cinta plana de corte duro lavada contra la textura diurna. M9c NO
// anade geometria: render() pasa el semiancho geometrico (halfWidth x
// span; mismo strip, mismo vertex count 54-324, mismo 1 draw) y el FRAG
// recorta el perfil compuesto. s = |vSide| = fraccion del borde
// geometrico.

// smoothstep con semantica GLSL EXACTA (clamp 0..1, t*t*(3-2t)),
// exige e0 < e1. Twin C++ de los literales del shader M9c — NO reusa
// ::smoothstep de LayerProfile.h (aquel soporta bordes invertidos:
// contrato distinto, plan 8.3).
inline float smoothstepf(float e0, float e1, float x) {
    float t = (x - e0) / (e1 - e0);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

// Ensanche geometrico: borde del ribbon = halfWidth*SPAN = ±0.015. El
// nucleo perceptual M9b (±halfWidth = ±0.0075) vive en s < 0.5. Un
// plan-B de ancho re-sella SPAN y las anclas JUNTAS (el nucleo queda
// anclado a halfWidth en s = 1/SPAN) — no es una constante suelta.
inline float ribbonSpan() { return 2.0f; }

// Perfil f(s) = nucleo + hombro + aura (continuo, monotono
// no-creciente): nucleo pleno con pedestal 1.35 hasta s=0.45 (±0.0068),
// hombro AA 0.45->0.55 que reemplaza el corte duro M9b (el borde M9b
// ±0.0075 cae en s=0.5 con f=0.85), pedestal 0.35 en s=0.55 y aura
// 0.35->0 hasta el borde geometrico. Anclas: f(0)=1.35, f(0.45)=1.35,
// f(0.5)=0.85, f(0.55)=0.35, f(0.7)~0.26, f(1)=0. (El sello M9c decia
// f(0)=1.0 — correccion aritmetica al implementar: el pedestal del
// halo vive tambien DENTRO del nucleo; sin el, la aura saltaria 0->0.35
// en s=0.55. Las tres anclas 0.85/0.26/0 del sello cuadran exacto.)
inline float traceProfile(float s) {
    const float core = 1.0f - smoothstepf(0.45f, 0.55f, s);
    const float halo = 0.35f * (1.0f - smoothstepf(0.55f, 1.0f, s));
    return core + halo;
}

// Cabeza del pulso (P2): boost = envolvente longitudinal en la cima
// (|p-0.5| < 0.12 de fase); gate = acota al nucleo transversal (s <
// 0.45 — hombro y aura no llevan cabeza); amplitud 0.55*boost*gate.
// M9e: el shader aplica la amplitud TENIDA por col (add = col*amp) —
// la cabeza hereda el color del tramo como la demo. Con el verde demo
// (R 0.10) el pico satura G/B (verde intenso) y R queda diminuto
// (~0.32 incluso a la cima x3.16) — nunca blanco (veredicto DAVID).
inline float pulseHeadBoost(float absPhaseFromPeak) {
    return 1.0f - smoothstepf(0.0f, 0.12f, absPhaseFromPeak);
}
inline float pulseHeadGate(float s) {
    return 1.0f - smoothstepf(0.15f, 0.45f, s);
}
inline float pulseHeadAmplitude(float boost, float gate) {
    return 0.55f * boost * gate;
}

// --- M9d: neon ambiental del cursor (respiracion del halo + derrame) ---
// Diseno MUSE (fair shot M9c: "en la demo el cursor lleva un halo neon
// marcado; aqui solo brilla el nucleo"), sellado con una adaptacion
// obligatoria: el ribbon mezcla (SRC_ALPHA, ONE) — dst recibe rgb*alpha,
// asi que el derrame vive en rgb Y en alpha (solo-rgb se atenuaria a
// ~0.03 en la falda). El halo respira en la escala ANCHA (env, la misma
// envolvente del cursor) y desde M9e el derrame TAMBIEN cabalga env
// (medicion en vivo: el glow de la demo avanza ancho, no en collar) —
// una sola escala de neon, ancha y con fuerza. El nucleo y la mezcla
// M9fix quedan intactos: en reposo el cuerpo del tubo apenas cambia
// (pedestal de aura al 60%) y en reposo spill = 0 (arco puro
// multiplicativo).
inline float haloPulseFloor() { return 0.6f; }   // reposo: aura al 60%
inline float haloPulseGain()  { return 0.8f; }   // cima: aura x1.4
inline float glowSpillGain()  { return 0.35f; }  // paridad bloom-demo: aura via post-proceso

// Perfil con respiracion: core(s) + halo(s)*(floor + gain*env), env en
// [0,1]. INVARIANTE: breath(s, 0.5) == traceProfile(s) exacto (halo x1.0
// en f32: 0.6f + 0.8f*0.5f redondea a 1.0f) — el perfil M9c sellado es
// el breath a media envolvente. Anclas: breath(0, 1) = 1.49,
// breath(0, 0) = 1.21, breath(0.55, 1) = 0.49, breath(0.7, 1) =
// 0.362963, breath(0.7, 0) = 0.155556.
inline float traceProfileBreath(float s, float env) {
    const float core = 1.0f - smoothstepf(0.45f, 0.55f, s);
    const float halo = 0.35f * (1.0f - smoothstepf(0.55f, 1.0f, s));
    return core + halo * (haloPulseFloor() + haloPulseGain() * env);
}

// Derrame transversal del cursor: gain*env*(1-s) — luz tenida (col)
// con caida lineal hasta el borde geometrico, en rgb Y en alpha
// (shader). M9e: cabalga la ENVOLVENTE (arg = env, la misma del pulso;
// antes boost): el glow avanza ANCHO (+-0.25 de fase ~ 20% del
// circuito) como el de la demo — en reposo env=0 => spill=0.
inline float glowSpill(float s, float env) {
    return glowSpillGain() * env * (1.0f - s);
}

// --- M9e: paleta demo-verbatim + literales del pulso (shaders.ts) ---
// Colores de banda EXACTOS de la demo (shaders.ts:460-462). El punto
// critico es el R del verde = 0.10: a cualquier ganancia multiplicativa
// el canal R queda diminuto => el cursor lee VERDE saturado, nunca
// lavado a blanco (veredicto DAVID de M9d; los (0.30,1.00,0.40) M8
// dejaban R=0.30 y, con el aditivo blanquecino, el pico se iba a
// blanco-cian). Los marg/closed tambien demo-verbatim.
inline glm::vec3 traceColorOpen()     { return glm::vec3(0.10f, 0.95f, 0.55f); }
inline glm::vec3 traceColorMarginal() { return glm::vec3(0.98f, 0.72f, 0.10f); }
inline glm::vec3 traceColorClosed()   { return glm::vec3(0.95f, 0.16f, 0.16f); }

// Literales del pulso (shaders.ts:450, demo-verbatim desde M9):
// p = fract(u_time*0.22 - aT*2.5) — ciclo de emision 4.545 s, cada
// paquete recorre el circuito en 1/(0.22/2.5) = 11.36 s, 2.5 paquetes
// en vuelo. Verificado en vivo contra la demo (TX->RX ~13 s de
// captura). La percepcion de "tramo corto" era el collar + cabeza
// blanca (M9c), no la velocidad.
inline float pulseCycleHz()       { return 0.22f; }
inline float pulseRepeatsPerArc() { return 2.5f; }
} // namespace hfc

// =============================================================================
// Zona 2: la capa GL.
// =============================================================================
struct HFCircuitResult {
    bool valid = false;
    double distKm = 0.0;      // geodesica real TX->RX (P0)
    int nHops = 0;
    double hVirtualKm = 0.0;  // clamp(hmF2(midpoint), 140, 420)
    double elevDeg = 0.0;     // del salto (D/n por h')
    double mufMHz = 0.0;      // min sobre saltos (MUF del peor salto)
    double fotMHz = 0.0;      // 0.85 · MUF
    double lufMHz = 0.0;      // max sobre saltos (peor absorcion, M7 incluido)
    double txLatDeg = 0.0, txLonDeg = 0.0;   // resueltos (marcadores)
    double rxLatDeg = 0.0, rxLonDeg = 0.0;
    std::vector<double> hopMufMHz;           // por salto (detalle/depuracion)
};

class HFTraceLayer {
public:
    struct Input {
        bool txValid = false, rxValid = false;
        double txLatDeg = 0.0, txLonDeg = 0.0, rxLatDeg = 0.0, rxLonDeg = 0.0;
        float freqMHz = 14.0f;
        float heightExag = 1.0f;             // M9: multiplicador visual de altura (App: explodeFactor)
        float apexOffsetKm = 0.0f;           // M9fix: offset del apice sobre h' (App: altKm - 300, slider de capa)
        long long gridStamp = 0;             // identidad de los grids (P7)
        const float* foF2Grid = nullptr;     // giroInterpGrid var 0 (72x72)
        const float* hmF2Grid = nullptr;     // giroInterpGrid var 2
        const float* lufGrid = nullptr;      // radioInterpGrid var 0 (M7)
        int gridW = 72, gridH = 72;
    };

    void init();    // shader (archivo + fallback inline) + VAO/VBO
    void destroy();
    // P7: recomputa el circuito SOLO si la firma cambia (tx/rx | freq |
    // stamp de grids | exag | offset M9fix); el pulso viaja por uniform, sin rebuild.
    void update(const Input& in);
    void render(const glm::mat4& view, const glm::mat4& projection,
                const glm::vec3& camPos, float timeSec);

    const HFCircuitResult& result() const { return result_; }
    void setVisible(bool v) { visible_ = v; }
    bool isVisible() const { return visible_; }

private:
    void buildPolyline(const Input& in, const HFCircuitResult& r);
    void uploadVertices(const std::vector<float>& flat);

    Shader shader_;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    int vertCount_ = 0;
    bool visible_ = false;
    float heightExag_ = 1.0f;                // M9: exag del ultimo rebuild (ancho en render)
    float apexOffset_ = 0.0f;                // M9fix: offset del ultimo rebuild (buildPolyline)
    HFCircuitResult result_;
    // firma del ultimo update aplicado (P7) — arranque invalido
    double sigTxLat_ = 0.0, sigTxLon_ = 0.0, sigRxLat_ = 0.0, sigRxLon_ = 0.0;
    float sigFreq_ = -1.0f;
    float sigExag_ = 0.0f;                   // M9 (sentinela: fuera de [1,5])
    float sigOffset_ = -1.0e30f;             // M9fix (sentinela: fuera de [-200,500])
    long long sigStamp_ = -1;
    bool sigValid_ = false;
};
