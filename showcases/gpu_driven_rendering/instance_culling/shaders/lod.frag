#version 450

layout(location = 0) in vec3 normal;
layout(location = 1) flat in uint lod;
layout(location = 0) out vec4 out_color;

const vec3 k_lod_colors[3] = vec3[3](
    vec3(0.24, 0.78, 1.0),
    vec3(0.33, 0.95, 0.52),
    vec3(1.0, 0.62, 0.22)
);

void main() {
    vec3 light = normalize(vec3(0.45, 1.0, 0.65));
    float diffuse = max(dot(normalize(normal), light), 0.0);
    out_color = vec4(k_lod_colors[lod] * (0.28 + 0.72 * diffuse), 1.0);
}
