#version 450

layout(set = 0, binding = 0, std140) uniform CameraUniform {
    mat4 mvp;
    mat4 view_projection;
    mat4 inverse_culling_view_projection;
} camera;

struct DrawInput {
    vec4 bounding_sphere;
    uint index_count;
    uint first_index;
    int vertex_offset;
};

layout(set = 1, binding = 0, std430) readonly buffer DrawInputs {
    DrawInput inputs[];
};

layout(set = 1, binding = 4, std430) readonly buffer Visibility {
    uint visible_objects[];
};

layout(push_constant) uniform DebugMode {
    uint mode;
} debug_mode;

layout(location = 0) out vec3 color;

const vec3 k_frustum_corners[8] = vec3[8](
    vec3(-1.0, -1.0, 0.0), vec3(1.0, -1.0, 0.0),
    vec3(1.0, 1.0, 0.0), vec3(-1.0, 1.0, 0.0),
    vec3(-1.0, -1.0, 1.0), vec3(1.0, -1.0, 1.0),
    vec3(1.0, 1.0, 1.0), vec3(-1.0, 1.0, 1.0)
);
const uint k_frustum_edges[24] = uint[24](
    0u, 1u, 1u, 2u, 2u, 3u, 3u, 0u,
    4u, 5u, 5u, 6u, 6u, 7u, 7u, 4u,
    0u, 4u, 1u, 5u, 2u, 6u, 3u, 7u
);

void main() {
    uint vertex = uint(gl_VertexIndex);
    if (debug_mode.mode == 0u) {
        // Vulkan 的近、远平面分别为 z=0 和 z=1；冻结视锥始终在世界空间中绘制。
        vec4 world = camera.inverse_culling_view_projection * vec4(k_frustum_corners[k_frustum_edges[vertex]], 1.0);
        world /= world.w;
        gl_Position = camera.view_projection * world;
        color = vec3(1.0, 0.85, 0.15);
    } else {
        // 三个互相垂直的大圆构成包围球线框，所有物体独立于主绘制的剔除结果提交。
        const uint segments = 32u;
        uint circle = vertex / (segments * 2u);
        uint segment = (vertex / 2u) % segments;
        float angle = 6.283185307 * float(segment + (vertex & 1u)) / float(segments);
        vec2 point = vec2(cos(angle), sin(angle));
        vec3 direction = circle == 0u ? vec3(point, 0.0)
                          : circle == 1u ? vec3(point.x, 0.0, point.y) : vec3(0.0, point);
        uint object_index = uint(gl_InstanceIndex);
        vec4 sphere = inputs[object_index].bounding_sphere;
        gl_Position = camera.mvp * vec4(sphere.xyz + sphere.w * direction, 1.0);
        // 压紧后的命令索引与物体索引无关，调试颜色读取按物体索引存储的可见性。
        color = visible_objects[object_index] != 0u ? vec3(0.15, 0.95, 0.35) : vec3(1.0, 0.2, 0.2);
    }
}
