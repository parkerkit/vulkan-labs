#version 450

layout(set = 0, binding = 1) uniform sampler2D tex;

struct ObjectState {
    vec4 offset_and_visible;
    vec4 color;
};

layout(set = 1, binding = 2, std430) readonly buffer ObjectStates {
    ObjectState states[];
};

layout(location = 0) in vec2 texcoord;
layout(location = 1) flat in uint object_index;
layout(location = 0) out vec4 out_color;

void main() {
    // 片元着色器直接读取 Compute 生成的颜色，独立演示 Compute -> Fragment 依赖。
    out_color = texture(tex, texcoord) * states[object_index].color;
}
