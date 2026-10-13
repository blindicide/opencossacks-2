#version 450
// Текстура, умноженная на цвет вершины с удвоением: цвет 0x80 соответствует ровно 1.

layout(set = 0, binding = 0) uniform sampler2D tex;

layout(location = 0) in vec2 uv;
layout(location = 1) in vec4 color;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 t = texture(tex, uv);
    outColor = vec4(t.rgb * color.rgb * (255.0 / 128.0), t.a * color.a);
}
