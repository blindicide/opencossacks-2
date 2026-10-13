#version 450
// Рельеф карты: вершины в мировых координатах, проекция как в оригинале —
// экранные X = x, Y = y / 2 − z (FUN_015590b0), затем сдвиг камеры и масштаб.

layout(push_constant) uniform Push {
    vec2 camera; // левый верхний угол экрана в пикселях проекции
    vec2 scale;  // 2 · масштаб / размер экрана
    float time;  // секунды — для воды
    float waterLayer; // слой массива с текстурой отражения воды (< 0 — воды нет)
} pc;

layout(location = 0) in vec3 inPos;     // мировые x, y, высота
layout(location = 1) in vec2 inUv;      // координаты в фактуре (1 = 64 мировые единицы)
layout(location = 2) in uvec4 inLayer1; // фактуры слоя 1 трёх вершин, номер этой вершины
layout(location = 3) in uvec4 inLayer2; // фактуры слоя 2 трёх вершин
layout(location = 4) in vec4 inShade;   // освещение, вес слоя 2

layout(location = 0) out vec2 uv;
layout(location = 1) flat out uvec3 layer1;
layout(location = 2) flat out uvec3 layer2;
layout(location = 3) out vec3 bary;
layout(location = 4) out float light;
layout(location = 5) out float blend2;
layout(location = 6) out float height;

void main() {
    vec2 screen = vec2(inPos.x, inPos.y * 0.5 - inPos.z) - pc.camera;
    gl_Position = vec4(screen * pc.scale - 1.0, 0.0, 1.0);
    uv = inUv;
    layer1 = inLayer1.xyz;
    layer2 = inLayer2.xyz;
    bary = vec3(inLayer1.w == 0u, inLayer1.w == 1u, inLayer1.w == 2u);
    light = inShade.x;
    blend2 = inShade.y;
    height = inPos.z;
}
