#version 450
// Фактуры вершин треугольника смешиваются по барицентрическим весам; второй слой
// накладывается с весом из TIL2; освещение — множитель с удвоением (0x80 — без изменений).
// Вода (FUN_0168e650, состояния BumpWater): поверх земли ниже нуля — отражение облаков
// с непрозрачностью (64 − 4·высота) / 255 (FUN_0168e5f0); текстура привязана к экрану:
// координаты = пиксель / (256, 400) + время / 9 с · скорость (FUN_0168df10).
// Волны оригинала — смещения от решётки 256×256 с имитацией (BUMPENVMAP ×10); здесь
// их заменяет синусная рябь.

layout(push_constant) uniform Push {
    vec2 camera;
    vec2 scale;
    float time;
    float waterLayer;
} pc;

layout(set = 0, binding = 0) uniform sampler2DArray factures;

layout(location = 0) in vec2 uv;
layout(location = 1) flat in uvec3 layer1;
layout(location = 2) flat in uvec3 layer2;
layout(location = 3) in vec3 bary;
layout(location = 4) in float light;
layout(location = 5) in float blend2;
layout(location = 6) in float height;

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
    c *= light * (255.0 / 128.0);
    const float alpha = clamp((64.0 - 4.0 * height) / 255.0, 0.0, 1.0);
    if (pc.waterLayer >= 0.0 && alpha > 0.0) {
        const vec2 p = gl_FragCoord.xy;
        const float t = pc.time / 9.0;
        vec2 w = p / vec2(256.0, 400.0) + t * vec2(0.6, 0.25);
        w += 0.012 * vec2(sin(p.y * 0.11 + pc.time * 1.7), sin(p.x * 0.07 + pc.time * 1.3));
        c = mix(c, texture(factures, vec3(w, pc.waterLayer)).rgb, alpha);
    }
    outColor = vec4(c, 1.0);
}
