struct Cam {
    mat4 view;
    mat4 proj;
    vec4 lightDir;  // xyz: direction the light travels
    vec4 amb;
    vec4 dif;
    vec4 fogColor;  // w: enabled
    vec4 fogRange;  // x start, y end
    mat4 vp;        // unjittered view-projection (motion vectors)
    mat4 prevVp;    // previous frame's unjittered view-projection
};
layout(std430, set = 0, binding = 1) readonly buffer Cams { Cam cams[]; };
layout(push_constant) uniform Push {
    mat4 model;    // 2D: ortho projection
    vec4 color;
    vec4 uvx;      // 3D: uv offset/scale; 2D: uv rect
    vec4 rect;     // 2D: pixel rect
    uvec4 params;  // x texture slot, y flags (1 unlit, 2 no fog, 4 ray shadows, 8 water, 16 clouds), z camera index,
                   // w parallax depth (float bits, 0 = off)
    mat4 prevModel;  // last frame's model matrix (FSR motion vectors)
} pc;
