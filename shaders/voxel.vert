#version 450
// Vértice compacto de 8 bytes: (x, y, z, face|ao) + (camada, u, v, -)
layout(location = 0) in uvec4 inA;
layout(location = 1) in uvec4 inB;

layout(push_constant) uniform PC {
    mat4 viewProj;     // já inclui a pré-rotação do swapchain
    vec4 chunkOrigin;  // xyz = origem do chunk no mundo
    vec4 camPos;       // xyz = câmera, w = distância final da névoa
    vec4 fogColor;
} pc;

layout(location = 0) out vec3 vUV;
layout(location = 1) out float vShade;
layout(location = 2) out float vFog;

const float kFaceShade[6] = float[](0.80, 0.80, 1.00, 0.50, 0.65, 0.65);
const float kAo[4] = float[](0.42, 0.62, 0.82, 1.00);

void main() {
    vec3 world = vec3(inA.xyz) + pc.chunkOrigin.xyz;
    gl_Position = pc.viewProj * vec4(world, 1.0);
    uint face = inA.w & 7u;
    uint ao = (inA.w >> 3u) & 3u;
    vShade = kFaceShade[face] * kAo[ao];
    vUV = vec3(float(inB.y), float(inB.z), float(inB.x));
    float fogEnd = pc.camPos.w;
    float d = length(world - pc.camPos.xyz);
    vFog = clamp((d - fogEnd * 0.55) / (fogEnd * 0.45), 0.0, 1.0);
}
