// Sea surface ("deniz.jpg" draws), shared by the Vulkan (mesh.frag) and OpenGL (gl.cpp) backends.
// Stylised water: four travelling sine waves plus texture ripples for the normal, Schlick Fresnel between a
// tinted body colour and a sky reflection, a tight sun glint and soft crest highlights. The original game
// drew the sea as a static textured plane; this is an addition, not recovered behaviour.

// Texture coordinates of the two ripple layers, scrolling in different directions.
vec2 water_uv(vec2 uv, float t, int layer) {
    return layer == 0 ? uv * 1.0 + vec2(0.021, 0.013) * t : uv * 1.7 + vec2(-0.017, 0.024) * t;
}

// base0/base1: the water texture sampled at water_uv(..., 0/1). P: world position, eye: camera position,
// toLight: unit vector toward the sun. occlusion: 1 = open sky in the reflected direction, 0 = blocked.
vec3 water_shade(vec3 base0, vec3 base1, vec3 P, vec3 eye, vec3 toLight, vec3 amb, vec3 dif, vec3 horizon,
                 float t, float dist, float occlusion) {
    // Travelling waves: deep-water dispersion w = sqrt(g k); slope accumulated for the normal. Seven waves with
    // incommensurate lengths spread around one wind direction, so their sum does not form a visible lattice.
    const vec2 dirs[7] = vec2[7](vec2(0.96, 0.28), vec2(0.71, 0.70), vec2(0.99, -0.14), vec2(0.42, 0.91),
                                 vec2(0.83, -0.56), vec2(0.17, 0.99), vec2(0.93, 0.37));
    const float lens[7] = float[7](9.1, 5.7, 3.9, 2.6, 1.73, 1.17, 0.79);
    float h = 0.0;
    vec2 slope = vec2(0.0);
    for (int i = 0; i < 7; ++i) {
        float k = 6.2831853 / lens[i];
        float a = 0.05 / k;  // equal steepness a*k for every wave
        float ph = k * dot(dirs[i], P.xz) + sqrt(9.81 * k) * t + float(i) * 1.7;
        h += a * sin(ph);
        slope += a * k * cos(ph) * dirs[i];
    }
    // Fine ripples from the difference of the two scrolling texture layers.
    slope += (base0.rg - base1.rg) * 0.12;
    // Fade the perturbation with distance so far water stays calm instead of shimmering.
    slope *= exp(-dist / 160.0);
    vec3 N = normalize(vec3(-slope.x, 1.0, -slope.y));
    vec3 V = normalize(eye - P);
    float nv = max(dot(N, V), 0.0);
    float fresnel = 0.02 + 0.98 * pow(1.0 - nv, 5.0);

    // Reflection: sky gradient from the horizon (fog) colour to a deeper zenith blue, darkened where blocked.
    vec3 R = reflect(-V, N);
    vec3 zenith = vec3(0.22, 0.42, 0.72);
    vec3 sky = mix(horizon, zenith, sqrt(clamp(R.y, 0.0, 1.0)));
    // deniz.jpg has strong light spots; keep only a third of its contrast so they read as ripples, not blobs.
    vec3 tex = mix(vec3(0.40, 0.60, 0.70), 0.5 * (base0 + base1), 0.35);
    vec3 reflection = mix(tex * amb * 0.45, sky, occlusion);

    // Body: the texture pulled toward teal, deeper where the view looks straight down.
    vec3 deep = vec3(0.03, 0.13, 0.18);
    vec3 shallow = tex * vec3(0.55, 0.85, 0.90);
    vec3 body = mix(shallow, deep, nv * 0.6) * (amb + dif * max(dot(N, toLight), 0.0));
    // Light passing through wave crests seen against the sun.
    float crest = clamp(h / 0.15 * 0.5 + 0.5, 0.0, 1.0);
    body += vec3(0.05, 0.22, 0.20) * crest * pow(max(dot(V, -toLight) * 0.5 + 0.5, 0.0), 3.0);

    vec3 col = mix(body, reflection, fresnel);
    // Sun glint (Blinn-Phong) and soft highlights on the highest crests.
    vec3 H = normalize(toLight + V);
    col += dif * (2.2 * pow(max(dot(N, H), 0.0), 220.0) * occlusion);
    col += vec3(0.75, 0.85, 0.85) * smoothstep(0.82, 1.0, crest) * dot(tex, vec3(0.33)) * 0.3;
    return col;
}
