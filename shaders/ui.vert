#version 450
layout(location = 0) in vec2 inPos;    // pixels lógicos
layout(location = 1) in vec4 inColor;

layout(push_constant) uniform PC {
    vec4 scale;  // xy = 2/largura, 2/altura (lógicas)
    vec4 rot;    // pré-rotação: x' = a*x + b*y, y' = c*x + d*y
} pc;

layout(location = 0) out vec4 vColor;

void main() {
    vec2 ndc = inPos * pc.scale.xy - 1.0;
    gl_Position = vec4(pc.rot.x * ndc.x + pc.rot.y * ndc.y, pc.rot.z * ndc.x + pc.rot.w * ndc.y, 0.0, 1.0);
    vColor = inColor;
}
