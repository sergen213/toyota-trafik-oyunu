#version 460
#extension GL_EXT_nonuniform_qualifier : require
#ifdef RT
#extension GL_EXT_ray_query : require
layout(set = 0, binding = 2) uniform accelerationStructureEXT tlas;
#endif
#include "common.glsl"
layout(set = 0, binding = 0) uniform sampler2D tex[];
layout(location = 0) in vec3 vNrm;
layout(location = 1) in vec2 vUv;
layout(location = 2) in float vDist;
layout(location = 3) in vec3 vWorld;
layout(location = 4) in vec4 vCur;
layout(location = 5) in vec4 vPrev;
layout(location = 0) out vec4 oColor;
#ifdef MOTION
layout(location = 1) out vec2 oMotion;  // previousUV - UV (FSR 3 low-res, unjittered)
#endif
void main() {
    Cam c = cams[pc.params.z];
    vec4 t = texture(tex[nonuniformEXT(pc.params.x)], vUv);
    if (t.a < 0.5) discard;
    vec3 col = t.rgb * pc.color.rgb;
    uint flags = pc.params.y;
    if ((flags & 1u) == 0u) {
        vec3 n = normalize(vNrm);
        vec3 L = -normalize(c.lightDir.xyz);
        float d = abs(dot(n, L));  // GLScene materials light both faces
        float lit = 1.0;
#ifdef RT
        if ((flags & 4u) != 0u && d > 0.001) {
            // Hard sun shadow: one ray query toward the light against the frame's TLAS.
            vec3 nn = dot(n, L) < 0.0 ? -n : n;
            rayQueryEXT q;
            rayQueryInitializeEXT(q, tlas, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT, 0xFF,
                                  vWorld + nn * 0.03, 0.02, L, 400.0);
            while (rayQueryProceedEXT(q)) {}
            if (rayQueryGetIntersectionTypeEXT(q, true) != gl_RayQueryCommittedIntersectionNoneEXT) lit = 0.0;
        }
#endif
        col *= c.amb.rgb + c.dif.rgb * d * lit;
    }
    if (c.fogColor.w > 0.5 && (flags & 2u) == 0u) {
        float f = clamp((c.fogRange.y - vDist) / (c.fogRange.y - c.fogRange.x), 0.0, 1.0);
        col = mix(c.fogColor.rgb, col, f);
    }
    oColor = vec4(col, pc.color.a);
#ifdef MOTION
    vec2 cur = vCur.xy / vCur.w, prev = vPrev.xy / vPrev.w;
    oMotion = vec2(prev.x - cur.x, cur.y - prev.y) * 0.5;  // NDC y is up, UV v is down
#endif
}
