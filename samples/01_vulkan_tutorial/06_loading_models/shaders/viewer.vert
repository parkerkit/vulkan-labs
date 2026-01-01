#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv0;
layout(location = 3) in vec2 inUv1;
layout(location = 4) in vec4 inColor;
layout(location = 5) in vec4 inWeights;
layout(location = 6) in vec4 inJoints;
layout(location = 7) in vec3 inMorphPosition0;
layout(location = 8) in vec3 inMorphPosition1;
layout(location = 9) in vec3 inMorphNormal0;
layout(location = 10) in vec3 inMorphNormal1;
layout(location = 11) in uint inFlags;

layout(location = 0) out vec3 worldPosition;
layout(location = 1) out vec3 worldNormal;
layout(location = 2) out vec2 uv0;
layout(location = 3) out vec2 uv1;
layout(location = 4) out vec4 vertexColor;
layout(location = 5) flat out uint flags;

layout(push_constant) uniform ViewProjection {
    mat4 view;
    mat4 projection;
} camera;

layout(set = 0, binding = 0, std140) uniform NodeData {
    mat4 model;
    vec4 morphWeights;
    ivec4 animation;
} node;

layout(set = 2, binding = 0, std430) readonly buffer SkinData {
    mat4 joints[];
} skin;

void main() {
    vec3 position = inPosition;
    vec3 normal = inNormal;
    if (node.animation.y != 0 && (inFlags & 8u) != 0u) {
        position += node.morphWeights.x * inMorphPosition0 + node.morphWeights.y * inMorphPosition1;
        normal += node.morphWeights.x * inMorphNormal0 + node.morphWeights.y * inMorphNormal1;
    }
    mat4 skinMatrix = mat4(1.0);
    if (node.animation.x != 0 && (inFlags & 4u) != 0u) {
        ivec4 joint = clamp(ivec4(inJoints), ivec4(0), ivec4(skin.joints.length() - 1));
        skinMatrix = inWeights.x * skin.joints[joint.x] + inWeights.y * skin.joints[joint.y] +
                     inWeights.z * skin.joints[joint.z] + inWeights.w * skin.joints[joint.w];
    }
    mat4 transform = node.model * skinMatrix;
    vec4 world = transform * vec4(position, 1.0);
    mat3 linearTransform = mat3(transform);
    vec3 transformedNormal = abs(determinant(linearTransform)) > 1e-10
        ? transpose(inverse(linearTransform)) * normal : mat3(node.model) * normal;
    worldPosition = world.xyz;
    worldNormal = dot(transformedNormal, transformedNormal) > 1e-12
        ? normalize(transformedNormal) : vec3(0.0, 1.0, 0.0);
    uv0 = inUv0;
    uv1 = inUv1;
    vertexColor = inColor;
    flags = inFlags | (determinant(linearTransform) < 0.0 ? 32u : 0u);
    gl_Position = camera.projection * camera.view * world;
}
