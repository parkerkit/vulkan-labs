#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in uint in_lod;
layout(location = 3) in vec4 in_sphere;

layout(set = 0, binding = 0, std140) uniform CameraUniform {
    mat4 view_projection;
} camera;

layout(location = 0) out vec3 normal;
layout(location = 1) flat out uint lod;

void main() {
    gl_Position = camera.view_projection * vec4(in_position + in_sphere.xyz, 1.0);
    normal = in_normal;
    lod = in_lod;
}
