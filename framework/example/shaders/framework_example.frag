#version 450

layout(location = 0) in vec3 world_normal;
layout(location = 1) in vec3 color;
layout(location = 0) out vec4 out_color;

void main() {
    vec3 light_direction = normalize(vec3(0.5, 0.8, 0.6));
    float diffuse = max(dot(normalize(world_normal), light_direction), 0.0);
    out_color = vec4(color * (0.22 + 0.78 * diffuse), 1.0);
}
