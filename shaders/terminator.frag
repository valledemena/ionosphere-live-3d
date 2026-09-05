#version 400 core
out vec4 fragColor;

void main() {
    // Terminator line: faint orange ring on the day/night boundary.
    // Geometry is rebuilt in the sun-perpendicular plane (Terminator::update),
    // so the color is constant here.
    fragColor = vec4(1.0, 0.5, 0.2, 0.4);
}
