#version 450
// Вершины спрайтов задаются в пикселях экрана; перевод в NDC — через push-константы.

layout(push_constant) uniform Push {
    vec2 scale;  // 2 / размер экрана
    vec2 offset; // −1, −1
} pc;

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec2 uv;
layout(location = 1) out vec4 color;

void main() {
    gl_Position = vec4(inPos * pc.scale + pc.offset, 0.0, 1.0);
    uv = inUv;
    color = inColor;
}
