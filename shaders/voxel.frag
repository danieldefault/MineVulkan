#version 450
layout(set = 0, binding = 0) uniform sampler2DArray uTex;

layout(push_constant) uniform PC {
    mat4 viewProj;
    vec4 chunkOrigin;
    vec4 camPos;
    vec4 fogColor;
} pc;

layout(location = 0) in vec3 vUV;
layout(location = 1) in float vShade;
layout(location = 2) in float vFog;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 c = texture(uTex, vUV).rgb * vShade;
    outColor = vec4(mix(c, pc.fogColor.rgb, vFog), 1.0);
}
