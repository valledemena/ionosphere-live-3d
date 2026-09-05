#version 400 core
out vec4 fragColor;

void main() {
    // Terminator line: faint deep-orange ring on the day/night boundary.
    // Geometry is rebuilt in the sun-perpendicular plane (Terminator::update),
    // so the color is constant here. Kept dim on purpose: additive blending
    // over bright day side would turn a brighter line white.
    fragColor = vec4(0.85, 0.32, 0.08, 0.22);
}
