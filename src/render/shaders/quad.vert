#version 460
#include "common.glsl"
layout(location = 0) out vec2 vUv;
void main() {
    vec2 a = vec2(float(gl_VertexIndex & 1), float(gl_VertexIndex >> 1));  // strip (0,0) (1,0) (0,1) (1,1)
    vUv = pc.uvx.xy + a * (pc.uvx.zw - pc.uvx.xy);
    gl_Position = pc.model * vec4(pc.rect.xy + a * pc.rect.zw, 0.0, 1.0);
    gl_Position.z = 0.5;
}
