#version 450
// Два слоя фактур: второй накладывается с весом из TIL2; освещение вершин —
// множитель с удвоением, как цвет вершин у D3D (0x80 — без изменений).

layout(set = 0, binding = 0) uniform sampler2DArray factures;

layout(location = 0) in vec2 uv;
layout(location = 1) flat in uvec2 facture;
layout(location = 2) in float light;
layout(location = 3) in float blend2;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 a = texture(factures, vec3(uv, float(facture.x))).rgb;
    vec3 b = texture(factures, vec3(uv, float(facture.y))).rgb;
    vec3 c = mix(a, b, blend2);
    outColor = vec4(c * light * (255.0 / 128.0), 1.0);
}
