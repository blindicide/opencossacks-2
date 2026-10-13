#version 450
// Рельеф карты: вершины в мировых координатах, проекция как в оригинале —
// экранные X = x, Y = y / 2 − z (FUN_015590b0), затем сдвиг камеры и масштаб.

layout(push_constant) uniform Push {
    vec2 camera; // левый верхний угол экрана в пикселях проекции
    vec2 scale;  // 2 · масштаб / размер экрана
} pc;

layout(location = 0) in vec3 inPos;      // мировые x, y, высота
layout(location = 1) in vec2 inUv;       // координаты в фактуре (1 = 64 мировые единицы)
layout(location = 2) in uvec4 inFacture; // фактура слоя 1, слоя 2, —, —
layout(location = 3) in vec4 inShade;    // освещение, вес слоя 2, —, —

layout(location = 0) out vec2 uv;
layout(location = 1) flat out uvec2 facture;
layout(location = 2) out float light;
layout(location = 3) out float blend2;

void main() {
    vec2 screen = vec2(inPos.x, inPos.y * 0.5 - inPos.z) - pc.camera;
    gl_Position = vec4(screen * pc.scale - 1.0, 0.0, 1.0);
    uv = inUv;
    facture = inFacture.xy;
    light = inShade.x;
    blend2 = inShade.y;
}
