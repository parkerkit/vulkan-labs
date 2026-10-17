#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_color;

layout(set = 0, binding = 0, std140) uniform CameraUniform {
    mat4 model_view_projection;
    mat4 model;
} camera;

layout(location = 0) out vec3 world_normal;
layout(location = 1) out vec3 color;

void main() {
    gl_Position = camera.model_view_projection * vec4(in_position, 1.0);
    world_normal = mat3(camera.model) * in_normal;
    color = in_color;
}
