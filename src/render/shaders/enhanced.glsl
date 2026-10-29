// Enhanced graphics, shared by the Vulkan (mesh.frag) and OpenGL (gl.cpp) backends. Native extra, not in the
// original game: a raymarched volumetric cloud layer composited over the skybox, and parallax occlusion mapping
// with height from texture luminance on selected surfaces (GpuMesh::Draw::relief).

// ---- volumetric clouds
const float kCloudLo = 350.0, kCloudHi = 950.0;  // slab above the map (metres)

float eg_hash(vec3 p) {
    p = fract(p * 0.3183099 + 0.1) * 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float eg_noise(vec3 x) {  // value noise, 0..1
    vec3 i = floor(x), f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(eg_hash(i), eg_hash(i + vec3(1, 0, 0)), f.x), mix(eg_hash(i + vec3(0, 1, 0)), eg_hash(i + vec3(1, 1, 0)), f.x), f.y),
               mix(mix(eg_hash(i + vec3(0, 0, 1)), eg_hash(i + vec3(1, 0, 1)), f.x), mix(eg_hash(i + vec3(0, 1, 1)), eg_hash(i + vec3(1, 1, 1)), f.x), f.y),
               f.z);
}

// Cloud density at world position p; fewer octaves for the cheaper light samples.
float eg_cloud_density(vec3 p, float t, int octaves) {
    vec3 q = p * vec3(0.0015, 0.0022, 0.0015) + vec3(0.015, 0.0, 0.006) * t;  // wind ~10 m/s
    float n = 0.0, a = 0.5, sum = 0.0;
    for (int i = 0; i < octaves; ++i) {
        n += a * eg_noise(q);
        sum += a;
        q = q * 2.03 + vec3(1.7, 3.1, 0.9);
        a *= 0.5;
    }
    n /= sum;
    float h = (p.y - kCloudLo) / (kCloudHi - kCloudLo);
    float shape = smoothstep(0.0, 0.2, h) * smoothstep(1.0, 0.5, h);  // flat-ish bases, rounded tops
    return clamp((n - mix(1.0, 0.55, shape)) * 6.0, 0.0, 1.0);
}

// Premultiplied cloud colour (rgb) and opacity (a) along the view ray eye + s * dir. frag: gl_FragCoord.xy
// (step jitter against banding).
vec4 eg_clouds(vec3 eye, vec3 dir, vec3 toLight, vec3 amb, vec3 dif, vec3 horizon, float t, vec2 frag) {
    if (dir.y < 0.01) return vec4(0.0);
    float s0 = max((kCloudLo - eye.y) / dir.y, 0.0), s1 = (kCloudHi - eye.y) / dir.y;
    if (s1 <= s0) return vec4(0.0);
    s1 = min(s1, s0 + 4000.0);
    const int kSteps = 28;
    const float kSigma = 0.012;  // extinction per metre at density 1
    float ds = (s1 - s0) / float(kSteps);
    float jitter = fract(52.9829189 * fract(dot(frag, vec2(0.06711056, 0.00583715))));
    float mu = dot(dir, toLight);
    float phase = 0.75 + 1.2 * pow(max(mu, 0.0), 8.0);  // isotropic + forward silver lining
    vec3 sun = vec3(1.0, 0.96, 0.9) * dif * 1.6;
    float T = 1.0;
    vec3 col = vec3(0.0);
    for (int i = 0; i < kSteps; ++i) {
        vec3 p = eye + dir * (s0 + (float(i) + jitter) * ds);
        float d = eg_cloud_density(p, t, 4);
        if (d <= 0.001) continue;
        float od = 0.0;  // optical depth toward the sun, three samples
        for (int j = 0; j < 3; ++j) od += eg_cloud_density(p + toLight * (35.0 + 70.0 * float(j)), t, 2) * 70.0;
        float h = clamp((p.y - kCloudLo) / (kCloudHi - kCloudLo), 0.0, 1.0);
        vec3 ambient = mix(horizon * 0.6, vec3(0.9, 0.92, 0.96), 0.3 + 0.6 * h) * amb * 1.3;
        // Two-lobe transmittance toward the sun approximates multiple scattering (clouds stay bright inside).
        vec3 S = sun * (exp(-kSigma * od) * 0.7 + exp(-kSigma * 0.25 * od) * 0.3) * phase + ambient;
        float a = exp(-kSigma * d * ds);
        col += T * (1.0 - a) * S;  // Beer-Lambert
        T *= a;
        if (T < 0.02) break;
    }
    // Melt into the skybox toward the horizon and with distance.
    float fade = smoothstep(0.01, 0.15, dir.y) * exp(-s0 / 9000.0);
    return vec4(col, 1.0 - T) * fade;
}

// ---- parallax occlusion mapping
// Height 0..1 from luminance, contrast-stretched around the texture's mean (its smallest mip) so that low-contrast
// textures such as asphalt grain still read as relief.
float eg_height(sampler2D s, vec2 uv, vec2 gx, vec2 gy, float sgn, float mean) {
    float l = clamp(0.5 + (dot(textureGrad(s, uv, gx, gy).rgb, vec3(0.299, 0.587, 0.114)) - mean) * 3.0, 0.0, 1.0);
    return sgn > 0.0 ? l : 1.0 - l;
}

// uv: texture coordinate, P: world position, eye: camera, depth: relief in texels (< 0: dark is high), dist: eye
// distance (fades out 25..40 m), toLight: unit vector toward the sun. Returns the displaced uv; N (world) becomes
// the relief normal facing the viewer and shadow the relief's self-shadowing (1 = lit). The tangent frame comes
// from screen derivatives, so the vertex format needs no tangents.
vec2 eg_parallax(sampler2D s, vec2 uv, vec3 P, vec3 eye, float depth, float dist, vec3 toLight, inout vec3 N,
                 out float shadow) {
    shadow = 1.0;
    vec3 dpx = dFdx(P), dpy = dFdy(P);
    vec2 dux = dFdx(uv), duy = dFdy(uv);
    vec3 V = normalize(eye - P);
    if (dot(N, V) < 0.0) N = -N;
    float fade = 1.0 - smoothstep(25.0, 40.0, dist);
    vec3 Ng = cross(dpx, dpy);
    float area = length(Ng);
    if (fade <= 0.0 || area < 1e-10) return uv;
    Ng /= area;
    if (dot(Ng, V) < 0.0) Ng = -Ng;
    float det = dot(Ng, cross(dpx, dpy));
    vec3 r1 = cross(dpy, Ng) / det, r2 = cross(Ng, dpx) / det;
    vec3 Gu = r1 * dux.x + r2 * duy.x, Gv = r1 * dux.y + r2 * duy.y;  // world gradients of u and v (1/m)
    float nv = max(dot(Ng, V), 0.2);
    // Depth in metres: `depth` texels at this surface's texel size, capped at 15 cm.
    vec2 tsz = vec2(textureSize(s, 0));
    float texel = 0.5 / max(length(Gu) * tsz.x, 1e-4) + 0.5 / max(length(Gv) * tsz.y, 1e-4);
    float sgn = sign(depth), h = min(abs(depth) * texel, 0.15) * fade;
    float mean = dot(textureLod(s, uv, 16.0).rgb, vec3(0.299, 0.587, 0.114));
    vec2 dUv = -vec2(dot(Gu, V), dot(Gv, V)) / nv * h;  // uv shift at full depth
    // March depth layers (0 = surface top) until the ray goes below the height field, then interpolate.
    int n = int(mix(24.0, 8.0, nv));
    float stepL = 1.0 / float(n), layer = 0.0;
    vec2 cur = uv, prevUv = uv;
    float dep = 1.0 - eg_height(s, uv, dux, duy, sgn, mean), prevDep = dep, prevL = 0.0;
    for (int i = 0; i < 24; ++i) {
        if (i >= n || layer >= dep) break;
        prevUv = cur;
        prevDep = dep;
        prevL = layer;
        layer += stepL;
        cur = uv + dUv * layer;
        dep = 1.0 - eg_height(s, cur, dux, duy, sgn, mean);
    }
    float after = dep - layer, before = prevDep - prevL;
    if (after < 0.0 && before > 0.0) cur = mix(cur, prevUv, after / (after - before));
    // Relief normal from the height gradient (metres per metre).
    vec2 ts = 1.0 / vec2(textureSize(s, 0));
    float h0 = eg_height(s, cur, dux, duy, sgn, mean);
    float hu = eg_height(s, cur + vec2(ts.x, 0.0), dux, duy, sgn, mean), hv = eg_height(s, cur + vec2(0.0, ts.y), dux, duy, sgn, mean);
    N = normalize(N - h * ((hu - h0) / ts.x * Gu + (hv - h0) / ts.y * Gv));
    // Self-shadowing: walk up toward the sun from the hit point; relief above the ray darkens it.
    float nl = dot(Ng, toLight);
    if (nl > 0.05) {
        vec2 lUv = vec2(dot(Gu, toLight), dot(Gv, toLight)) / nl * h;  // uv shift per unit of rise
        float d0 = 1.0 - h0;
        for (int i = 1; i <= 6; ++i) {
            float f = float(i) / 6.0 * d0;
            float above = eg_height(s, cur + lUv * f, dux, duy, sgn, mean) - (1.0 - d0 + f);
            shadow = min(shadow, 1.0 - 0.7 * clamp(above * 5.0, 0.0, 1.0));
        }
    }
    return cur;
}
