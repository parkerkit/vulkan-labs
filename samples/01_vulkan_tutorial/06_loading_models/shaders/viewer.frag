#version 450

layout(location = 0) in vec3 worldPosition;
layout(location = 1) in vec3 worldNormal;
layout(location = 2) in vec2 uv0;
layout(location = 3) in vec2 uv1;
layout(location = 4) in vec4 vertexColor;
layout(location = 5) flat in uint flags;
layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0, std140) uniform MaterialData {
    vec4 baseColor;
    vec4 emissive;
    vec4 surface;
    vec4 tint;
    vec4 cameraPosition;
    vec4 lightSettings;
    ivec4 options;
    ivec4 uvSets;
    ivec4 materialOptions;
    vec4 alphaSettings;
} material;
layout(set = 1, binding = 1) uniform sampler2D baseColorTexture;
layout(set = 1, binding = 2) uniform sampler2D metallicRoughnessTexture;
layout(set = 1, binding = 3) uniform sampler2D normalTexture;
layout(set = 1, binding = 4) uniform sampler2D occlusionTexture;
layout(set = 1, binding = 5) uniform sampler2D emissiveTexture;

const float PI = 3.14159265359;

vec3 safeNormalize(vec3 value, vec3 fallback) {
    float lengthSquared = dot(value, value);
    return lengthSquared > 1e-12 ? value * inversesqrt(lengthSquared) : fallback;
}

bool hasTexture(int bit, int uvSet) {
    return (material.options.z & bit) != 0 && (flags & (1u << uint(uvSet))) != 0u;
}

vec2 textureUv(int set) {
    return set == 0 ? uv0 : uv1;
}

bool isFrontFace() {
    return (flags & 32u) != 0u ? !gl_FrontFacing : gl_FrontFacing;
}

vec3 surfaceNormal() {
    vec3 normal = safeNormalize(worldNormal, vec3(0.0, 1.0, 0.0));
    if (!isFrontFace()) {
        normal = -normal;
    }
    // Cotangent frame also works on meshes that omit TANGENT and on deformed geometry.
    if (hasTexture(4, material.uvSets.z)) {
        vec2 uv = textureUv(material.uvSets.z);
        vec3 dp1 = dFdx(worldPosition);
        vec3 dp2 = dFdy(worldPosition);
        vec2 duv1 = dFdx(uv);
        vec2 duv2 = dFdy(uv);
        vec3 dp2perp = cross(dp2, normal);
        vec3 dp1perp = cross(normal, dp1);
        vec3 tangent = dp2perp * duv1.x + dp1perp * duv2.x;
        vec3 bitangent = dp2perp * duv1.y + dp1perp * duv2.y;
        float frameLength = max(dot(tangent, tangent), dot(bitangent, bitangent));
        if (frameLength > 1e-12) {
            vec3 mapped = texture(normalTexture, uv).xyz * 2.0 - 1.0;
            mapped.xy *= material.surface.z;
            float scale = inversesqrt(frameLength);
            normal = safeNormalize(mat3(tangent * scale, bitangent * scale, normal) * mapped, normal);
        }
    }
    return normal;
}

vec3 fresnelSchlick(float cosine, vec3 f0) {
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cosine, 0.0, 1.0), 5.0);
}

float distributionGgx(float nDotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float denominator = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * denominator * denominator, 1e-12);
}

float geometrySchlick(float nDotDirection, float roughness) {
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    return nDotDirection / max(nDotDirection * (1.0 - k) + k, 1e-6);
}

vec3 encodeSrgb(vec3 linear) {
    return mix(12.92 * linear, 1.055 * pow(linear, vec3(1.0 / 2.4)) - 0.055,
               greaterThan(linear, vec3(0.0031308)));
}

void main() {
    int shading = material.options.x;
    int source = material.options.y;
    // Material mode preserves glTF's explicit unlit materials (e.g. background cubes).
    if (shading == 2 && source == 0 && material.materialOptions.y != 0) {
        shading = 0;
    }
    if (!isFrontFace() && material.materialOptions.z == 0) {
        discard;
    }
    vec4 texel = hasTexture(1, material.uvSets.x)
        ? texture(baseColorTexture, textureUv(material.uvSets.x)) : vec4(1.0);
    vec4 albedo = material.baseColor * texel * vertexColor;
    if (source == 1) {
        albedo = hasTexture(1, material.uvSets.x) ? texel : material.baseColor;
    } else if (source == 2) {
        albedo = material.tint;
    } else if (source == 3) {
        albedo = (flags & 16u) != 0u ? vertexColor : material.baseColor;
    }
    if (source != 2) {
        albedo.rgb *= material.tint.rgb;
    }
    if (material.options.w == 1 && albedo.a < material.alphaSettings.x) {
        discard;
    }
    float alpha = material.options.w == 2 ? clamp(albedo.a, 0.0, 1.0) : 1.0;
    vec3 color = albedo.rgb;
    if (shading != 0) {
        vec3 normal = surfaceNormal();
        vec3 view = safeNormalize(material.cameraPosition.xyz - worldPosition, vec3(0.0, 0.0, -1.0));
        vec3 right = safeNormalize(cross(view, vec3(0.0, 1.0, 0.0)), vec3(1.0, 0.0, 0.0));
        vec3 up = safeNormalize(cross(right, view), vec3(0.0, 1.0, 0.0));
        vec3 directions[3] = vec3[3](safeNormalize(view + right * 0.6 + up * 0.8, view),
            safeNormalize(view - right * 0.8 + up * 0.3, view), safeNormalize(-view + up * 0.8, view));
        vec3 radiances[3] = vec3[3](vec3(1.0, 0.96, 0.90), vec3(0.35, 0.42, 0.55), vec3(0.5));
        if (shading == 1) {
            color = albedo.rgb * material.lightSettings.y;
            for (int i = 0; i < 3; ++i) {
                color += albedo.rgb * radiances[i] * max(dot(normal, directions[i]), 0.0) * material.lightSettings.x;
            }
        } else {
            vec2 factors = material.surface.xy;
            if (hasTexture(2, material.uvSets.y)) {
                vec4 mrSample = texture(metallicRoughnessTexture, textureUv(material.uvSets.y));
                factors.x *= (material.options.z & 64) == 0 ? mrSample.g : 1.0;
                factors.y *= (material.options.z & 128) == 0 ? mrSample.b : 1.0;
            }
            float roughness = clamp(factors.x, 0.045, 1.0);
            float metallic = clamp(factors.y, 0.0, 1.0);
            vec3 f0 = mix(vec3(0.04), albedo.rgb, metallic);
            float ao = hasTexture(8, material.uvSets.w)
                ? mix(1.0, texture(occlusionTexture, textureUv(material.uvSets.w)).r, material.surface.w) : 1.0;
            // Approximate ambient only; this viewer does not perform HDRI/IBL convolution.
            color = material.lightSettings.y * (albedo.rgb * (1.0 - metallic) + f0) * ao;
            float nDotV = max(dot(normal, view), 0.0);
            for (int i = 0; i < 3; ++i) {
                vec3 light = directions[i];
                vec3 halfDirection = safeNormalize(view + light, normal);
                float nDotL = max(dot(normal, light), 0.0);
                float distribution = distributionGgx(max(dot(normal, halfDirection), 0.0), roughness);
                float geometry = geometrySchlick(nDotV, roughness) * geometrySchlick(nDotL, roughness);
                vec3 fresnel = fresnelSchlick(max(dot(halfDirection, view), 0.0), f0);
                vec3 specular = distribution * geometry * fresnel / max(4.0 * nDotV * nDotL, 1e-6);
                vec3 diffuse = (1.0 - fresnel) * (1.0 - metallic) * albedo.rgb / PI;
                color += (diffuse + specular) * radiances[i] * material.lightSettings.x * nDotL;
            }
            vec3 emission = material.emissive.rgb;
            if (hasTexture(16, material.materialOptions.x)) {
                emission *= texture(emissiveTexture, textureUv(material.materialOptions.x)).rgb;
            }
            color += emission;
        }
        color = max(color * material.lightSettings.z, vec3(0.0));
        color = color / (color + vec3(1.0));
    }
    color = clamp(color, 0.0, 1.0);
    // SRGB attachments encode on write; UNORM fallback requires explicit output encoding.
    if (material.lightSettings.w < 0.5) {
        color = encodeSrgb(color);
    }
    outColor = vec4(color, alpha);
}
