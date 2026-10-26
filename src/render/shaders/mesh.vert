#version 460
#include "common.glsl"
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;
layout(location = 2) in vec2 aUv;
layout(location = 0) out vec3 vNrm;
layout(location = 1) out vec2 vUv;
layout(location = 2) out float vDist;
layout(location = 3) out vec3 vWorld;
layout(location = 4) out vec4 vCur;
layout(location = 5) out vec4 vPrev;
void main() {
    Cam c = cams[pc.params.z];
    vec4 w = pc.model * vec4(aPos, 1.0);
    vec4 e = c.view * w;
    vDist = length(e.xyz);
    vNrm = normalize(transpose(inverse(mat3(pc.model))) * aNrm);
    vUv = pc.uvx.xy + aUv * pc.uvx.zw;
    vWorld = w.xyz;
    vCur = c.vp * w;
    vPrev = c.prevVp * (pc.prevModel * vec4(aPos, 1.0));
    gl_Position = c.proj * e;
    gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;  // GL depth range -> Vulkan 0..1
}
