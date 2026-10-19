#version 450
// Vértice compacto de 8 bytes: (x, y, z, face|ao) + (camada, u, v, flags)
layout(location = 0) in uvec4 inA;
layout(location = 1) in uvec4 inB;

layout(push_constant) uniform PC {
    mat4 viewProj;     // já inclui a pré-rotação do swapchain
    vec4 chunkOrigin;  // xyz = origem do chunk no mundo, w = altura da superfície da água
    vec4 camPos;       // xyz = câmera, w = distância final da névoa
    vec4 fogColor;     // rgb = cor, a = fração de camPos.w onde a névoa começa
} pc;

layout(location = 0) out vec3 vUV;
layout(location = 1) out float vShade;
layout(location = 2) out float vFog;

const float kFaceShade[6] = float[](0.80, 0.80, 1.00, 0.50, 0.65, 0.65);
const float kAo[4] = float[](0.42, 0.62, 0.82, 1.00);

void main() {
    vec3 world = vec3(inA.xyz) + pc.chunkOrigin.xyz;
    if ((inB.w & 1u) != 0u) world.y -= 0.125;  // superfície da água um pouco abaixo do bloco
    gl_Position = pc.viewProj * vec4(world, 1.0);
    uint face = inA.w & 7u;
    uint ao = (inA.w >> 3u) & 3u;
    vShade = kFaceShade[face] * kAo[ao];
    if ((inB.w & 2u) != 0u) {
        // Fundo submerso: quanto mais fundo, mais escuro (a água por cima dá o tom azul).
        float depth = max(pc.chunkOrigin.w - world.y, 0.0);
        vShade *= mix(0.3, 1.0, exp(-depth * 0.15));
    }
    vUV = vec3(float(inB.y), float(inB.z), float(inB.x));
    float fogEnd = pc.camPos.w;
    float d = length(world - pc.camPos.xyz);
    float fogStart = fogEnd * pc.fogColor.a;
    vFog = clamp((d - fogStart) / max(fogEnd - fogStart, 1e-3), 0.0, 1.0);
}
