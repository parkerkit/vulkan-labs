#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_texcoord;

layout(set = 0, binding = 0, std140) uniform CameraUniform {
    mat4 view_projection;
} camera;

layout(location = 0) out vec3 normal;
layout(location = 1) out vec2 texcoord;
layout(location = 2) flat out uint material_id;

void main() {
    // 4 个实例按 2×2 排列，实例编号就是材质 ID。
    material_id = uint(gl_InstanceIndex);
    vec2 cell = vec2(float(material_id % 2u), float(material_id / 2u));
    vec3 offset = vec3(cell * 1.6 - 0.8, 0.0);
    gl_Position = camera.view_projection * vec4(in_position + offset, 1.0);
    normal = in_normal;
    texcoord = in_texcoord;
}
