#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_texcoord;

layout(set = 0, binding = 0, std140) uniform CameraUniform {
    mat4 mvp;
} camera;

struct ObjectState {
    vec4 offset_and_visible;
    vec4 color;
};

layout(set = 1, binding = 2, std430) readonly buffer ObjectStates {
    ObjectState states[];
};

layout(push_constant) uniform DrawParameters {
    uint object_index;
} draw;

layout(location = 0) out vec2 texcoord;
layout(location = 1) flat out uint object_index;

void main() {
    // SSBO 由 Compute 写入，此处在 VERTEX_SHADER 阶段读取，并非顶点属性输入。
    vec3 offset = states[draw.object_index].offset_and_visible.xyz;
    gl_Position = camera.mvp * vec4(in_position + offset, 1.0);
    texcoord = in_texcoord;
    object_index = draw.object_index;
}
