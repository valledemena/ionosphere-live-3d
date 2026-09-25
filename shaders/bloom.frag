#version 400 core
in vec2 vUV;
uniform sampler2D u_texture;
uniform vec2 u_direction;
uniform vec2 u_resolution;
uniform float u_threshold;
uniform float u_intensity;
uniform int u_pass;   // M1: 0 = pasada H, 1 = pasada V (la ultima)
out vec4 fragColor;

// M1: bright-pass con soft-knee (smoothstep sobre la LUMINANCIA) en vez de
// resta dura por canal: sigue extrayendo SOLO brillos (sin el halo uniforme
// que "agrandaba" el globo al difuminar toda la escena iluminada), pero el
// peso entra suave alrededor del umbral: sin bordes duros ni popping al
// mover el threshold.
vec3 brightPass(vec3 c) {
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    return c * smoothstep(u_threshold, u_threshold + 0.25, l);
}

void main() {
    // 045 (a1): peine denso k=1..24 (1 texel half-res = 2 px) — sustituye el
    // peine disperso i=1..4 (stride 6 = 12 px) que sellaba cada fuente puntual
    // sobre umbral en la retícula de 12 px (causa 043, decisión 045). Pesos:
    // la curva del kernel anterior muestreada cada 2 px y renormalizada a la
    // MISMA masa discreta por dirección (suma 24 literales = 0.386486194 vs
    // 0.3864862 base); núcleo 0.227027 y near-taps M9g intactos.
    float wfar[24] = float[](
        0.005501586, 0.01100317, 0.01650476, 0.02200634, 0.02750793, 0.03300952,
        0.03094642, 0.02888332, 0.02682023, 0.02475713, 0.02269404, 0.02063094,
        0.01872067, 0.0168104, 0.01490012, 0.01298985, 0.01107957, 0.0091693,
        0.008099543, 0.007029786, 0.005960028, 0.004890271, 0.003820514, 0.002750756);
    vec3 result = brightPass(texture(u_texture, vUV).rgb) * 0.227027;
    vec2 texel = u_direction / u_resolution;
    for (int k = 1; k <= 24; k++) {
        result += brightPass(texture(u_texture, vUV + texel * float(k)).rgb) * wfar[k - 1];
        result += brightPass(texture(u_texture, vUV - texel * float(k)).rgb) * wfar[k - 1];
    }
    // M9g FIX C (paridad mip0 UnrealBloom, prescrito GLM): taps cercanos a
    // 2.1/4.2 texels del RT half-res (= 4.2/8.4 px pantalla) — DESACOPLADOS
    // de u_direction desde 045 (PIN: eran 0.35/0.70×texel relativos al
    // stride 6; con stride 1 colapsarían a 0.7/1.4 px y matarían el núcleo).
    // Sin renormalizar: fuera de la cabeza aportan ~0 -> falda lejana y
    // reposo intactos (L 0.587 < 0.72 -> brightPass~=0).
    vec2 nt1 = texel * 2.1;
    vec2 nt2 = texel * 4.2;
    result += brightPass(texture(u_texture, vUV + nt1).rgb) * 0.10;
    result += brightPass(texture(u_texture, vUV - nt1).rgb) * 0.10;
    result += brightPass(texture(u_texture, vUV + nt2).rgb) * 0.06;
    result += brightPass(texture(u_texture, vUV - nt2).rgb) * 0.06;
    // M1: u_intensity SOLO en la pasada vertical (la final). Antes se
    // aplicaba en H y en V -> intensidad efectiva = u_intensity^2.
    if (u_pass == 1) result *= u_intensity;
    fragColor = vec4(result, 1.0);
}
