#version 450
// Фактуры вершин треугольника смешиваются по барицентрическим весам; второй слой
// накладывается с весом из TIL2; освещение — множитель с удвоением (0x80 — без изменений).

layout(set = 0, binding = 0) uniform sampler2DArray factures;

layout(location = 0) in vec2 uv;
layout(location = 1) flat in uvec3 layer1;
layout(location = 2) flat in uvec3 layer2;
layout(location = 3) in vec3 bary;
layout(location = 4) in float light;
layout(location = 5) in float blend2;

layout(location = 0) out vec4 outColor;

vec3 sampleLayer(uvec3 f) {
    vec3 c = texture(factures, vec3(uv, float(f.x))).rgb * bary.x;
    c += texture(factures, vec3(uv, float(f.y))).rgb * bary.y;
    c += texture(factures, vec3(uv, float(f.z))).rgb * bary.z;
    return c;
}

void main() {
    vec3 c = sampleLayer(layer1);
    if (blend2 > 0.0) c = mix(c, sampleLayer(layer2), blend2);
    outColor = vec4(c * light * (255.0 / 128.0), 1.0);
}
