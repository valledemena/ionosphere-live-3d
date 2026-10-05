#pragma once
// S-4 tour didactico (ruling 130 sec.6.4): cola de keyframes pura.
// Sin GL, sin red, sin estado global: el reloj lo lleva App (tourT avanza con
// dt SOLO si hay dato listo; pausado si no). Curva determinista de t.
// Secuencia base del catalogo: terminador -> anomalia ecuatorial ->
// aurora -> tormenta. Waypoints continuos (sin saltos entre tramos).
namespace tour {

struct Key {
    float ry;      // rotacion Y de camara al final del tramo (rad)
    float dist;    // distancia de camara al final del tramo
    float dur;     // duracion del tramo (s)
    const char* es;
    const char* en;
};

// Cola: 4 tramos (W0 inicial implicito). Total 44 s.
inline int count() { return 4; }
inline const Key& key(int i) {
    static const Key k[4] = {
        // 1. Terminador (W0 0.0/3.2 -> 0.6/3.2, 12 s).
        {0.6f, 3.2f, 12.0f,
         "Terminador: la linea dia-noche. La ionosfera despierta con el Sol: la densidad crece en el lado diurno y decae de noche.",
         "Terminator: the day-night line. The ionosphere wakes with the Sun: density grows on the dayside and decays at night."},
        // 2. Anomalia ecuatorial (0.6/3.2 -> 1.4/2.6, 12 s).
        {1.4f, 2.6f, 12.0f,
         "Anomalia ecuatorial: dos crestas de densidad a +-15 grados del ecuador magnetico, con el valle encima del ecuador.",
         "Equatorial anomaly: two density crests at +-15 degrees of the magnetic equator, with the trough above the equator."},
        // 3. Aurora (1.4/2.6 -> 2.4/3.0, 10 s).
        {2.4f, 3.0f, 10.0f,
         "Aurora: el ovalo auroral se aviva con la actividad geomagnetica (Kp alto, Bz sur).",
         "Aurora: the auroral oval brightens with geomagnetic activity (high Kp, southward Bz)."},
        // 4. Tormenta (2.4/3.0 -> 3.4/2.8, 10 s).
        {3.4f, 2.8f, 10.0f,
         "Tormenta: viento solar rapido y Bz sur perturban la propagacion HF (MUF inestable, bandas cerradas).",
         "Storm: fast solar wind and southward Bz disturb HF propagation (unstable MUF, closed bands)."},
    };
    return k[i < 0 ? 0 : (i > 3 ? 3 : i)];
}
inline float total() { return 44.0f; }  // 12+12+10+10

struct State {
    float ry;
    float dist;
    int index;  // tramo activo 0..3
    bool done;
};

// Curva t -> estado (suavizado smoothstep dentro del tramo).
inline State eval(float t) {
    if (t <= 0.0f) return {0.0f, 3.2f, 0, false};
    float acc = 0.0f;
    float prevRy = 0.0f, prevDist = 3.2f;
    for (int i = 0; i < 4; ++i) {
        const Key& k = key(i);
        if (t < acc + k.dur) {
            float u = (t - acc) / k.dur;
            u = u * u * (3.0f - 2.0f * u);
            return {prevRy + (k.ry - prevRy) * u, prevDist + (k.dist - prevDist) * u, i, false};
        }
        acc += k.dur;
        prevRy = k.ry;
        prevDist = k.dist;
    }
    return {3.4f, 2.8f, 3, true};
}

// Avance del reloj: solo suma dt si hay dato listo (condicion verificable).
inline float advance(float clock, float dt, bool ready) {
    return ready ? clock + dt : clock;
}

}  // namespace tour
