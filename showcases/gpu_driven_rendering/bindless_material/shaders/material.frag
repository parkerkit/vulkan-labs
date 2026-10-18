#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec3 normal;
layout(location = 1) in vec2 texcoord;
layout(location = 2) flat in uint material_id;
layout(location = 0) out vec4 out_color;

// 数组下标是材质 ID，数组元素是纹理描述符索引。
layout(set = 0, binding = 1, std430) readonly buffer MaterialTable {
    uint texture_indices[];
};

layout(set = 0, binding = 2) uniform sampler2D textures[BINDLESS_TEXTURE_COUNT];

void main() {
    uint texture_index = texture_indices[material_id];
    // 同一条 draw 中的多个实例可以采样不同 descriptor。
    vec3 albedo = textureLod(textures[nonuniformEXT(texture_index)], texcoord, 0.0).rgb;
    vec3 light = normalize(vec3(0.45, 1.0, 0.65));
    float diffuse = max(dot(normal, light), 0.0);
    out_color = vec4(albedo * (0.45 + 0.55 * diffuse), 1.0);
}
