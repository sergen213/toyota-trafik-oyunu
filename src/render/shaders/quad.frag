#version 460
#extension GL_EXT_nonuniform_qualifier : require
#include "common.glsl"
layout(set = 0, binding = 0) uniform sampler2D tex[];
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 oColor;
void main() { oColor = texture(tex[nonuniformEXT(pc.params.x)], vUv) * pc.color; }
