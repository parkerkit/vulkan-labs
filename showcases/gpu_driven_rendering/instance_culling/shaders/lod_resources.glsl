struct DrawIndexedIndirectCommand {
    uint index_count;
    uint instance_count;
    uint first_index;
    int vertex_offset;
    uint first_instance;
};

layout(set = 0, binding = 0, std430) readonly buffer Objects {
    vec4 spheres[];
};

layout(set = 0, binding = 1, std430) writeonly buffer IndirectCommands {
    DrawIndexedIndirectCommand commands[];
};

layout(set = 0, binding = 2, std140) uniform CullingUniform {
    vec4 frustum_planes[6];
    vec4 view_depth;
    vec4 lod_parameters;
    uvec4 options;
    uvec4 meshes[3];
} culling;

layout(set = 0, binding = 3, std430) buffer LodStats {
    uint draw_count;
    uint lod_counts[3];
} stats;

layout(set = 0, binding = 4, std430) writeonly buffer VisibleInstances {
    uint object_indices[];
};
