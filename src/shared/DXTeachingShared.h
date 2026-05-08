#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <vector>

namespace dxteaching
{

static constexpr uint32_t kTopicCount = 20;
static constexpr uint32_t kSwapChainBufferCount = 2;
static constexpr uint32_t kShadowMapSize = 1024;
static constexpr uint32_t kSceneSampleCount = 4;
static constexpr uint32_t kMaterialTextureSize = 256;
static constexpr uint32_t kInternalRenderScaleNumerator = 1;
static constexpr uint32_t kInternalRenderScaleDenominator = 1;

inline uint32_t ScaledRenderDimension(uint32_t value)
{
    value = std::max<uint32_t>(1u, value);
    return std::max<uint32_t>(
        1u,
        (value * kInternalRenderScaleNumerator + kInternalRenderScaleDenominator - 1u) /
            kInternalRenderScaleDenominator);
}

inline std::vector<uint32_t> BuildMaterialAlbedoTexels()
{
    std::vector<uint32_t> pixels(static_cast<size_t>(kMaterialTextureSize) * kMaterialTextureSize);
    for (uint32_t y = 0; y < kMaterialTextureSize; ++y)
    {
        for (uint32_t x = 0; x < kMaterialTextureSize; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(kMaterialTextureSize);
            const float v = static_cast<float>(y) / static_cast<float>(kMaterialTextureSize);
            const uint32_t cellSize = std::max(1u, kMaterialTextureSize / 8u);
            const bool checker = (((x / cellSize) + (y / cellSize)) & 1u) == 0u;
            const float rLinear = checker ? (0.2f + 0.7f * u) : (0.1f + 0.3f * v);
            const float gLinear = checker ? (0.4f + 0.4f * v) : (0.2f + 0.5f * u);
            const float bLinear = checker ? (0.8f - 0.5f * u) : (0.15f + 0.45f * v);
            const uint8_t r = static_cast<uint8_t>(std::clamp(rLinear * 255.0f, 0.0f, 255.0f));
            const uint8_t g = static_cast<uint8_t>(std::clamp(gLinear * 255.0f, 0.0f, 255.0f));
            const uint8_t b = static_cast<uint8_t>(std::clamp(bLinear * 255.0f, 0.0f, 255.0f));
            const uint8_t a = static_cast<uint8_t>(checker ? 112u : 196u);
            pixels[static_cast<size_t>(y) * kMaterialTextureSize + x] =
                static_cast<uint32_t>(r) |
                (static_cast<uint32_t>(g) << 8u) |
                (static_cast<uint32_t>(b) << 16u) |
                (static_cast<uint32_t>(a) << 24u);
        }
    }
    return pixels;
}

inline std::vector<uint32_t> BuildMaterialNormalTexels()
{
    std::vector<uint32_t> pixels(static_cast<size_t>(kMaterialTextureSize) * kMaterialTextureSize);
    for (uint32_t y = 0; y < kMaterialTextureSize; ++y)
    {
        for (uint32_t x = 0; x < kMaterialTextureSize; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(kMaterialTextureSize - 1u) * 2.0f - 1.0f;
            const float v = static_cast<float>(y) / static_cast<float>(kMaterialTextureSize - 1u) * 2.0f - 1.0f;
            const float nx = std::sin(u * 9.0f) * 0.35f;
            const float ny = std::cos(v * 9.0f) * 0.35f;
            const float nz = std::sqrt(std::max(0.05f, 1.0f - nx * nx - ny * ny));
            const uint8_t r = static_cast<uint8_t>(std::clamp(nx * 127.0f + 128.0f, 0.0f, 255.0f));
            const uint8_t g = static_cast<uint8_t>(std::clamp(ny * 127.0f + 128.0f, 0.0f, 255.0f));
            const uint8_t b = static_cast<uint8_t>(std::clamp(nz * 127.0f + 128.0f, 0.0f, 255.0f));
            pixels[static_cast<size_t>(y) * kMaterialTextureSize + x] =
                static_cast<uint32_t>(r) |
                (static_cast<uint32_t>(g) << 8u) |
                (static_cast<uint32_t>(b) << 16u) |
                (255u << 24u);
        }
    }
    return pixels;
}

struct FrameSettings
{
    float elapsedSeconds = 0.0f;
    int topic = 1;
    bool errorExampleEnabled = false;
    uint32_t width = 1;
    uint32_t height = 1;
};

struct TopicRuntimeProfile
{
    bool useShadow = false;
    bool usePBR = false;
    bool enableBloom = true;
    bool enableParticles = true;
    bool enableTemporal = false;
    uint32_t blurPassCount = 2;
    uint32_t objectCount = 3;
    float exposure = 1.05f;
    float bloomStrength = 0.24f;
    float particleStrength = 0.12f;
    float temporalBlend = 0.08f;
    float shadowStrength = 0.70f;
    float edgeStrength = 0.22f;
    float brightThreshold = 0.85f;
    float rotationSpeed = 1.0f;
};

struct alignas(16) SceneConstantBuffer
{
    float world[16] = {0};
    float viewProj[16] = {0};
    float lightViewProj[16] = {0};
    float lightDirAndStrength[4] = {0.0f, -1.0f, 0.0f, 0.7f};
    float cameraPosAndTime[4] = {0.0f, 1.4f, -4.5f, 0.0f};
    float topicAndFlags[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    float sceneParams[4] = {1.0f, 0.24f, 0.12f, 0.22f};
    float screenInfo[4] = {1.0f, 1.0f, 1.0f, 3.0f};
};

struct alignas(16) PostConstantBuffer
{
    float postParams0[4] = {1.0f, 0.24f, 0.12f, 0.08f};
    float postParams1[4] = {1.0f, 0.0f, 0.70f, 0.85f};
};

struct alignas(16) BlurConstantBuffer
{
    float blurParams[4] = {1.0f, 1.0f, 1.0f, 0.0f};
};

struct alignas(16) ParticleConstantBuffer
{
    float particleParams0[4] = {0.0f, 1.0f, 0.0f, 0.12f};
    float particleParams1[4] = {1.0f, 1.0f, 1.0f, 1.0f};
};

struct VertexPC
{
    float position[3];
    float normal[3];
    float color[3];
};

inline int ClampTopic(int topic)
{
    if (topic < 1)
    {
        return 1;
    }
    if (topic > static_cast<int>(kTopicCount))
    {
        return static_cast<int>(kTopicCount);
    }
    return topic;
}

inline std::string_view TopicTitle(int topic)
{
    static constexpr std::array<std::string_view, kTopicCount> kTitles = {
        "Resource And Memory Modes",
        "Argument Buffer Binding",
        "Function Constants",
        "Indirect Command Buffer",
        "Parallel Render Encoding",
        "Deferred Style Composition",
        "Shadowing Techniques",
        "PBR Shading",
        "HDR Bloom And Temporal",
        "Compute Particles",
        "Advanced Texture Sampling",
        "Synchronization And Scheduling",
        "Ray Tracing Fallback",
        "MetalFX Style Upscaling",
        "Profiling And Debug Markers",
        "Tiled Lighting Visualization",
        "SSAO Approximation",
        "Screen Space Reflections",
        "Variable Rate Shading View",
        "GPU Driven LOD And Culling"};
    return kTitles[static_cast<size_t>(ClampTopic(topic) - 1)];
}

inline TopicRuntimeProfile BuildTopicRuntimeProfile(int topic, bool errorExampleEnabled)
{
    TopicRuntimeProfile profile;
    const int t = ClampTopic(topic);

    switch (t)
    {
        case 1:
            profile.rotationSpeed = 0.8f;
            profile.objectCount = 1;
            profile.enableBloom = false;
            profile.enableParticles = false;
            break;
        case 2:
            profile.rotationSpeed = 1.0f;
            profile.objectCount = 3;
            break;
        case 3:
            profile.rotationSpeed = 1.1f;
            profile.objectCount = 3;
            break;
        case 4:
            profile.rotationSpeed = 1.15f;
            profile.objectCount = 4;
            profile.useShadow = true;
            profile.shadowStrength = 0.45f;
            break;
        case 5:
            profile.rotationSpeed = 1.3f;
            profile.objectCount = 5;
            profile.enableBloom = true;
            profile.blurPassCount = 3;
            break;
        case 6:
            profile.rotationSpeed = 1.0f;
            profile.objectCount = 3;
            profile.enableBloom = true;
            profile.blurPassCount = 4;
            profile.edgeStrength = 0.35f;
            break;
        case 7:
            profile.useShadow = true;
            profile.shadowStrength = 0.85f;
            profile.enableBloom = false;
            profile.enableParticles = false;
            profile.objectCount = 3;
            break;
        case 8:
            profile.useShadow = true;
            profile.usePBR = true;
            profile.shadowStrength = 0.90f;
            profile.exposure = 1.10f;
            profile.enableBloom = true;
            profile.blurPassCount = 4;
            break;
        case 9:
            profile.enableBloom = true;
            profile.blurPassCount = 6;
            profile.enableTemporal = true;
            profile.temporalBlend = 0.35f;
            profile.bloomStrength = 0.85f;
            profile.exposure = 1.25f;
            profile.enableParticles = false;
            profile.brightThreshold = 0.62f;
            break;
        case 10:
            profile.enableBloom = true;
            profile.enableParticles = true;
            profile.particleStrength = 1.0f;
            profile.bloomStrength = 0.45f;
            profile.objectCount = 2;
            break;
        case 11:
            profile.enableBloom = true;
            profile.blurPassCount = 3;
            profile.objectCount = 3;
            break;
        case 12:
            profile.enableTemporal = true;
            profile.temporalBlend = 0.6f;
            profile.rotationSpeed = 0.75f;
            profile.enableParticles = false;
            break;
        case 13:
            profile.useShadow = true;
            profile.shadowStrength = 0.65f;
            profile.enableBloom = true;
            profile.blurPassCount = 3;
            break;
        case 14:
            profile.enableBloom = true;
            profile.enableTemporal = true;
            profile.temporalBlend = 0.4f;
            profile.bloomStrength = 0.55f;
            profile.enableParticles = false;
            break;
        case 15:
            profile.useShadow = true;
            profile.usePBR = true;
            profile.enableBloom = true;
            profile.enableParticles = true;
            profile.enableTemporal = true;
            profile.shadowStrength = 0.8f;
            profile.blurPassCount = 5;
            profile.objectCount = 5;
            profile.particleStrength = 0.4f;
            profile.temporalBlend = 0.25f;
            break;
        case 16:
            profile.useShadow = true;
            profile.usePBR = true;
            profile.enableBloom = true;
            profile.enableParticles = false;
            profile.objectCount = 6;
            profile.blurPassCount = 4;
            profile.bloomStrength = 0.52f;
            profile.edgeStrength = 0.30f;
            profile.rotationSpeed = 1.05f;
            break;
        case 17:
            profile.useShadow = true;
            profile.usePBR = false;
            profile.enableBloom = false;
            profile.enableParticles = false;
            profile.objectCount = 4;
            profile.shadowStrength = 0.82f;
            profile.edgeStrength = 0.42f;
            profile.rotationSpeed = 0.70f;
            break;
        case 18:
            profile.useShadow = true;
            profile.usePBR = true;
            profile.enableBloom = true;
            profile.enableTemporal = true;
            profile.enableParticles = false;
            profile.objectCount = 3;
            profile.exposure = 1.18f;
            profile.bloomStrength = 0.70f;
            profile.temporalBlend = 0.18f;
            profile.blurPassCount = 5;
            profile.rotationSpeed = 0.82f;
            break;
        case 19:
            profile.useShadow = false;
            profile.usePBR = false;
            profile.enableBloom = false;
            profile.enableParticles = false;
            profile.objectCount = 5;
            profile.edgeStrength = 0.55f;
            profile.rotationSpeed = 0.55f;
            break;
        case 20:
            profile.useShadow = true;
            profile.usePBR = true;
            profile.enableBloom = true;
            profile.enableParticles = true;
            profile.enableTemporal = true;
            profile.objectCount = 9;
            profile.bloomStrength = 0.38f;
            profile.particleStrength = 0.30f;
            profile.temporalBlend = 0.20f;
            profile.edgeStrength = 0.34f;
            profile.rotationSpeed = 0.95f;
            break;
        default:
            break;
    }

    if (errorExampleEnabled)
    {
        profile.exposure *= 1.25f;
        profile.bloomStrength *= 1.35f;
        profile.particleStrength *= 1.25f;
        profile.temporalBlend = profile.enableTemporal ? std::fmin(0.88f, profile.temporalBlend + 0.25f) : profile.temporalBlend;
        profile.shadowStrength = std::fmax(0.45f, profile.shadowStrength - 0.2f);
        profile.rotationSpeed *= 1.25f;
    }

    return profile;
}

inline void ComputeClearColor(const FrameSettings &settings, float outColor[4])
{
    const int topic = ClampTopic(settings.topic);

    if (topic == 1)
    {
        outColor[0] = 0.03f;
        outColor[1] = 0.06f;
        outColor[2] = 0.10f;
        outColor[3] = 1.0f;
        return;
    }

    const float timeFactor = settings.elapsedSeconds * (0.22f + 0.02f * static_cast<float>(topic));

    outColor[0] = 0.07f + 0.05f * std::sin(timeFactor * 0.90f + 0.30f * static_cast<float>(topic));
    outColor[1] = 0.09f + 0.05f * std::cos(timeFactor * 0.70f + 0.18f * static_cast<float>(topic));
    outColor[2] = 0.12f + 0.04f * std::sin(timeFactor * 0.50f + 0.11f * static_cast<float>(topic));
    outColor[3] = 1.0f;

    if (settings.errorExampleEnabled)
    {
        outColor[0] += 0.02f;
        outColor[1] -= 0.01f;
    }

    for (int i = 0; i < 3; ++i)
    {
        outColor[i] = std::min(1.0f, std::max(0.0f, outColor[i]));
    }
}

inline const char *TeachingShaderSource()
{
    return R"(
cbuffer SceneCB : register(b0)
{
    float4x4 gWorld;
    float4x4 gViewProj;
    float4x4 gLightViewProj;
    float4 gLightDirAndStrength;
    float4 gCameraPosAndTime;
    float4 gTopicAndFlags;
    float4 gSceneParams;
    float4 gScreenInfo;
};

cbuffer PostCB : register(b1)
{
    float4 gPostParams0;
    float4 gPostParams1;
};

cbuffer BlurCB : register(b2)
{
    float4 gBlurParams;
};

cbuffer ParticleCB : register(b3)
{
    float4 gParticleParams0;
    float4 gParticleParams1;
};

Texture2D gShadowTex : register(t0);
Texture2D gSceneTex : register(t1);
Texture2D gBloomATex : register(t2);
Texture2D gParticleTex : register(t3);
Texture2D gHistoryTex : register(t4);
Texture2D gBloomBTex : register(t5);
Texture2D gPostTex : register(t6);
Texture2D gEdgeTex : register(t7);
Texture2D gAlbedoTex : register(t8);
Texture2D gNormalTex : register(t9);
RWTexture2D<float4> gParticleOut : register(u0);
RWTexture2D<float4> gEdgeOut : register(u1);

SamplerState gLinearClamp : register(s0);
SamplerState gPointClamp : register(s1);
SamplerComparisonState gShadowSampler : register(s2);

struct SceneVSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float3 color : COLOR;
};

struct SceneVSOutput
{
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float3 color : COLOR;
    float4 lightPos : TEXCOORD2;
    float3 localPos : TEXCOORD3;
    float3 localNormal : TEXCOORD4;
};

struct FullscreenVSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float Hash12(float2 p)
{
    return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453123);
}

float2 MaterialUV(float3 localPos, float3 localNormal)
{
    float3 an = abs(localNormal);
    float2 uv = (an.z > an.x && an.z > an.y) ? localPos.xy : ((an.x > an.y) ? localPos.zy : localPos.xz);
    return frac(uv * 0.78 + 0.5);
}

float3 SampleMaterialAlbedo(float3 localPos, float3 localNormal, float3 vertexColor)
{
    float2 uv = MaterialUV(localPos, localNormal);
    float3 tex = gAlbedoTex.Sample(gLinearClamp, uv).rgb;
    float3 tint = lerp(float3(1.0, 1.0, 1.0), saturate(vertexColor * 1.05), 0.35);
    return max(tex * tint * 1.05, 0.0);
}

float3 ApplyMaterialNormal(float3 localPos, float3 localNormal, float3 worldNormal)
{
    float2 uv = MaterialUV(localPos, localNormal);
    float3 map = gNormalTex.Sample(gLinearClamp, uv).xyz * 2.0 - 1.0;
    float3 N = normalize(worldNormal);
    float3 helper = abs(N.y) < 0.9 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    float3 T = normalize(cross(helper, N));
    float3 B = cross(N, T);
    return normalize(T * map.x * 0.26 + B * map.y * 0.26 + N * max(0.45, map.z));
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = max(0.04, roughness * roughness);
    float a2 = a * a;
    float ndh = saturate(dot(N, H));
    float denom = ndh * ndh * (a2 - 1.0) + 1.0;
    return a2 / max(0.0001, 3.14159265 * denom * denom);
}

float GeometrySchlickGGX(float ndv, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return ndv / max(0.0001, ndv * (1.0 - k) + k);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    return GeometrySchlickGGX(saturate(dot(N, V)), roughness) *
           GeometrySchlickGGX(saturate(dot(N, L)), roughness);
}

float3 TopicColorAdjust(int topic, float3 color, float3 normal, float3 worldPos)
{
    const float t = gCameraPosAndTime.w;
    const float radius = length(worldPos.xz) * 0.2;

    if (topic == 1)
    {
        color *= float3(1.05, 1.00, 0.95);
    }
    else if (topic == 2)
    {
        float3 swizzled = float3(color.b, color.r, color.g);
        color = lerp(color, swizzled, 0.35 + 0.20 * sin(t * 1.3));
    }
    else if (topic == 3)
    {
        color = pow(max(color, 0.0), float3(0.90, 1.10, 1.20));
    }
    else if (topic == 4)
    {
        color += 0.12 * abs(sin(t * 1.5 + worldPos.x * 2.0));
    }
    else if (topic == 5)
    {
        color += float3(0.08 * sin(t + worldPos.x * 2.0),
                        0.05 * cos(t * 1.4 + worldPos.y * 2.0),
                        0.06 * sin(t * 1.8));
    }
    else if (topic == 6)
    {
        float3 N = normalize(normal);
        float3 V = normalize(gCameraPosAndTime.xyz - worldPos);
        float rim = pow(saturate(1.0 - abs(dot(N, V))), 3.0);
        float faceTint = 0.08 * saturate(N.y * 0.5 + 0.5);
        color = lerp(color, color * float3(0.82, 0.90, 1.0), 0.18);
        color += (rim * gSceneParams.w + faceTint) * float3(0.65, 0.82, 1.0);
    }
    else if (topic == 7)
    {
        float shadow = saturate(0.72 - (worldPos.y + 0.5) * 0.55);
        color *= max(shadow, 0.25);
    }
    else if (topic == 8)
    {
        float3 N = normalize(normal);
        float3 L = normalize(float3(-0.35, 0.6, 0.7));
        float3 V = float3(0.0, 0.0, 1.0);
        float3 H = normalize(L + V);
        float ndl = saturate(dot(N, L));
        float ndh = saturate(dot(N, H));
        float rough = 0.35;
        float spec = pow(ndh, lerp(16.0, 96.0, 1.0 - rough));
        color = color * (0.25 + 0.75 * ndl) + spec * 0.80;
    }
    else if (topic == 9)
    {
        float bloom = exp(-9.0 * radius);
        color += bloom * float3(0.90, 0.75, 0.45);
        color = 1.0 - exp(-color * 1.5);
    }
    else if (topic == 10)
    {
        float2 p = worldPos.xz * 4.0 + float2(t * 3.0, -t * 1.7);
        float spark = step(0.965, Hash12(floor(p)));
        color += float3(1.0, 0.8, 0.35) * spark * 0.9;
    }
    else if (topic == 11)
    {
        float2 uv = worldPos.xz * 0.25 + 0.5;
        float checker = step(0.5, frac(uv.x * 10.0)) * step(0.5, frac(uv.y * 10.0));
        color = lerp(color, color * float3(0.75, 1.1, 0.9), checker * 0.45);
    }
    else if (topic == 12)
    {
        float stepped = floor(t * 8.0) / 8.0;
        color *= 0.88 + 0.12 * sin(stepped * 6.0);
    }
    else if (topic == 13)
    {
        float refl = smoothstep(1.0, 0.0, radius);
        color = lerp(color, float3(0.35, 0.55, 0.85) * refl + color * 0.72, 0.4);
    }
    else if (topic == 14)
    {
        float2 uv = worldPos.xz * 0.20 + 0.5;
        float2 pix = floor(uv * float2(24.0, 24.0)) / float2(24.0, 24.0);
        color = lerp(color, float3(pix.x, pix.y, 1.0 - pix.x) * 0.85, 0.38);
    }
    else if (topic == 15)
    {
        float heat = smoothstep(0.2, 1.0, abs(sin(t * 2.0 + worldPos.x * 3.0 + worldPos.z * 2.0)));
        color = lerp(color, float3(heat, 0.18, 1.0 - heat), 0.42);
    }
    else if (topic == 16)
    {
        float2 tile = floor((worldPos.xz + 4.0) * 1.25);
        float tileId = Hash12(tile);
        float3 tileColor = 0.45 + 0.55 * float3(
            Hash12(tile + 1.7),
            Hash12(tile + 8.3),
            Hash12(tile + 15.1));
        float movingLight = 0.0;
        [unroll]
        for (int i = 0; i < 4; ++i)
        {
            float fi = (float)i;
            float3 lp = float3(sin(t * (0.55 + fi * 0.18) + fi * 1.9) * 3.2,
                               1.15 + 0.35 * sin(t * 0.7 + fi),
                               cos(t * (0.42 + fi * 0.13) + fi * 2.4) * 2.7);
            float d = length(worldPos - lp);
            movingLight += saturate(1.0 - d / (2.4 + fi * 0.2));
        }
        float grid = max(step(0.94, frac((worldPos.x + 4.0) * 1.25)),
                         step(0.94, frac((worldPos.z + 4.0) * 1.25)));
        color = color * (0.55 + movingLight * 0.55) + tileColor * (0.12 + grid * 0.28 + tileId * 0.05);
    }
    else if (topic == 17)
    {
        float3 N = normalize(normal);
        float horizon = saturate(N.y * 0.5 + 0.5);
        float cavity = 1.0 - smoothstep(0.18, 0.85, abs(N.x) + abs(N.y) + abs(N.z) - 1.0);
        float contact = smoothstep(-0.95, 0.20, -worldPos.y);
        float ao = saturate(0.52 + horizon * 0.34 - cavity * 0.18 - contact * 0.20);
        color *= ao;
        color = lerp(color, color * float3(0.70, 0.82, 1.0), 0.18);
    }
    else if (topic == 18)
    {
        float3 N = normalize(normal);
        float3 V = normalize(gCameraPosAndTime.xyz - worldPos);
        float fresnel = pow(saturate(1.0 - dot(N, V)), 4.0);
        float2 reflectedUv = reflect(-V, N).xz * 0.22 + worldPos.xz * 0.04 + t * float2(0.03, -0.02);
        float bands = 0.5 + 0.5 * sin((reflectedUv.x + reflectedUv.y) * 38.0);
        float3 reflection = lerp(float3(0.08, 0.18, 0.28), float3(0.55, 0.80, 1.0), bands);
        color = lerp(color, reflection + color * 0.55, fresnel * 0.72);
        color += fresnel * float3(0.35, 0.55, 0.85);
    }
    else if (topic == 19)
    {
        float2 screenUv = worldPos.xy * 0.18 + 0.5;
        float2 coarse = floor(screenUv * 12.0) / 12.0;
        float lane = Hash12(coarse);
        float shadeRate = lerp(0.55, 1.15, step(0.36, lane));
        float debugGrid = max(step(0.92, frac(screenUv.x * 12.0)), step(0.92, frac(screenUv.y * 12.0)));
        float3 rateColor = lane < 0.36 ? float3(0.25, 0.55, 1.0) : float3(1.0, 0.62, 0.25);
        color = color * shadeRate;
        color = lerp(color, rateColor, 0.22 + debugGrid * 0.30);
    }
    else if (topic == 20)
    {
        float distanceToCamera = length(gCameraPosAndTime.xyz - worldPos);
        float lod = saturate((distanceToCamera - 3.0) / 5.0);
        float quant = floor(lod * 4.0) / 4.0;
        float stripes = step(0.82, frac((worldPos.x + worldPos.y + worldPos.z) * lerp(8.0, 2.5, quant)));
        float3 lodColor = lerp(float3(0.30, 1.00, 0.55), float3(1.00, 0.45, 0.20), quant);
        color = lerp(color, color * lodColor, 0.22);
        color += stripes * lodColor * (0.10 + quant * 0.18);
    }

    return max(color, 0.0);
}

)" R"(
float ComputeShadowFactor(float4 lightPos, float shadowStrength)
{
    float3 ndc = lightPos.xyz / max(lightPos.w, 1e-5);
    float2 uv = ndc.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || ndc.z < 0.0 || ndc.z > 1.0)
    {
        return 1.0;
    }
    float sampled = gShadowTex.SampleCmpLevelZero(gShadowSampler, uv, ndc.z - 0.0015);
    return lerp(1.0, sampled, saturate(shadowStrength));
}

SceneVSOutput SceneVS(SceneVSInput input)
{
    SceneVSOutput output;
    float4 worldPos = mul(float4(input.position, 1.0), gWorld);
    output.position = mul(worldPos, gViewProj);
    output.worldPos = worldPos.xyz;
    output.normal = normalize(mul(float4(input.normal, 0.0), gWorld).xyz);
    output.color = input.color;
    output.lightPos = mul(worldPos, gLightViewProj);
    output.localPos = input.position;
    output.localNormal = input.normal;
    return output;
}

float4 ShadowVS(SceneVSInput input) : SV_POSITION
{
    float4 worldPos = mul(float4(input.position, 1.0), gWorld);
    return mul(worldPos, gLightViewProj);
}

float4 ScenePS(SceneVSOutput input) : SV_TARGET
{
    int topic = (int)round(gTopicAndFlags.x);
    bool errorEnabled = gTopicAndFlags.y > 0.5;
    bool usePBR = gTopicAndFlags.z > 0.5;
    bool useShadow = gTopicAndFlags.w > 0.5;

    float3 normal = ApplyMaterialNormal(input.localPos, normalize(input.localNormal), normalize(input.normal));
    float3 lightDir = normalize(-gLightDirAndStrength.xyz);
    float3 viewDir = normalize(gCameraPosAndTime.xyz - input.worldPos);
    float ndl = saturate(dot(normal, lightDir));

    float2 materialUv = MaterialUV(input.localPos, input.localNormal);
    float materialPattern = gAlbedoTex.Sample(gLinearClamp, materialUv).a;
    float3 baseColor = SampleMaterialAlbedo(input.localPos, input.localNormal, input.color);
    float3 litColor = baseColor * (0.18 + 0.82 * ndl);

    if (usePBR)
    {
        float3 L = lightDir;
        float3 V = normalize(viewDir);
        float3 H = normalize(L + V);
        float roughness = lerp(0.26, 0.72, materialPattern);
        float metallic = lerp(0.04, 0.42, saturate(materialPattern * 1.2 - 0.2));
        float3 F0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);
        float D = DistributionGGX(normal, H, roughness);
        float G = GeometrySmith(normal, V, L, roughness);
        float3 F = FresnelSchlick(saturate(dot(H, V)), F0);
        float3 specular = (D * G * F) / max(0.0001, 4.0 * saturate(dot(normal, V)) * ndl);
        float3 diffuse = (1.0 - F) * (1.0 - metallic) * baseColor / 3.14159265;
        float3 ambient = baseColor * 0.08;
        litColor = ambient + (diffuse + specular) * ndl * 2.7;
    }

    if (useShadow)
    {
        float shadowFactor = ComputeShadowFactor(input.lightPos, gLightDirAndStrength.w);
        litColor *= (0.25 + 0.75 * shadowFactor);
    }

    float3 color = TopicColorAdjust(topic, litColor, normal, input.worldPos);

    if (errorEnabled)
    {
        float radius = length(input.worldPos.xz) * 0.25;
        float jitter = 0.92 + 0.35 * abs(sin(gCameraPosAndTime.w * 2.7 + radius * 9.0));
        color = pow(max(color * jitter, 0.0), 0.78);
        if (topic == 9 || topic == 14)
        {
            color += float3(0.34, 0.16, 0.05);
        }
    }

    return float4(max(color, 0.0), 1.0);
}

)" R"(
FullscreenVSOutput FullscreenVS(uint vertexId : SV_VertexID)
{
    FullscreenVSOutput output;
    float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    output.uv = uv;
    output.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

float4 BrightExtractPS(FullscreenVSOutput input) : SV_TARGET
{
    float3 scene = gSceneTex.Sample(gLinearClamp, input.uv).rgb;
    int topic = (int)round(gPostParams1.x);
    float threshold = gPostParams1.w;
    if (topic == 9)
    {
        threshold *= 0.85;
    }
    else if (topic == 18)
    {
        threshold *= 0.78;
    }
    else if (topic == 20)
    {
        threshold *= 0.90;
    }
    float luminance = dot(scene, float3(0.2126, 0.7152, 0.0722));
    float knee = saturate((luminance - threshold) / max(0.0001, (1.0 - threshold)));
    return float4(scene * knee, 1.0);
}

float3 BlurKernel(Texture2D tex, float2 uv, float2 texel, float2 dir)
{
    float3 c = tex.Sample(gLinearClamp, uv).rgb * 0.227027;
    c += tex.Sample(gLinearClamp, uv + dir * texel * 1.384615).rgb * 0.316216;
    c += tex.Sample(gLinearClamp, uv - dir * texel * 1.384615).rgb * 0.316216;
    c += tex.Sample(gLinearClamp, uv + dir * texel * 3.230769).rgb * 0.070270;
    c += tex.Sample(gLinearClamp, uv - dir * texel * 3.230769).rgb * 0.070270;
    return c;
}

float4 BlurHPS(FullscreenVSOutput input) : SV_TARGET
{
    float2 texel = gBlurParams.xy;
    return float4(BlurKernel(gBloomATex, input.uv, texel, float2(1.0, 0.0)), 1.0);
}

float4 BlurVPS(FullscreenVSOutput input) : SV_TARGET
{
    float2 texel = gBlurParams.xy;
    return float4(BlurKernel(gBloomBTex, input.uv, texel, float2(0.0, 1.0)), 1.0);
}

float Luminance(float3 color)
{
    return dot(color, float3(0.299, 0.587, 0.114));
}

float4 CompositePS(FullscreenVSOutput input) : SV_TARGET
{
    float3 scene = gSceneTex.Sample(gLinearClamp, input.uv).rgb;
    float3 bloom = gBloomATex.Sample(gLinearClamp, input.uv).rgb;
    float3 particles = gParticleTex.Sample(gLinearClamp, input.uv).rgb;
    float3 history = gHistoryTex.Sample(gLinearClamp, input.uv).rgb;
    float edgeFromTexture = gEdgeTex.Sample(gLinearClamp, input.uv).r;

    float exposure = gPostParams0.x;
    float bloomStrength = gPostParams0.y;
    float particleStrength = gPostParams0.z;
    float temporalBlend = gPostParams0.w;
    float edgeStrength = gPostParams1.z;

    float lumaC = Luminance(scene);
    float edgeMask = edgeFromTexture;
    float3 edgeTint = lerp(float3(0.02, 0.035, 0.06), float3(0.75, 0.88, 1.0), saturate(lumaC));
    scene = scene + edgeTint * edgeMask * edgeStrength * 0.28;

    float2 centered = input.uv * 2.0 - 1.0;
    float vignette = 1.0 - smoothstep(0.35, 1.2, dot(centered, centered));
    scene *= 0.85 + 0.15 * vignette;

    float3 hdr = scene + bloom * bloomStrength + particles * particleStrength;
    float3 mapped = 1.0 - exp(-hdr * max(0.1, exposure));
    mapped = lerp(mapped, history, saturate(temporalBlend));

    int topic = (int)round(gPostParams1.x);
    if (topic == 15)
    {
        float heat = saturate(dot(hdr, float3(0.2, 0.7, 0.1)));
        float3 heatColor = lerp(float3(0.2, 0.8, 1.0), float3(1.0, 0.2, 0.1), heat);
        mapped = lerp(mapped, heatColor, 0.20);
    }
    else if (topic == 16)
    {
        float2 tile = floor(input.uv * float2(18.0, 10.0));
        float grid = max(step(0.965, frac(input.uv.x * 18.0)), step(0.945, frac(input.uv.y * 10.0)));
        float3 tileTint = 0.18 * float3(Hash12(tile + 2.0), Hash12(tile + 7.0), Hash12(tile + 13.0));
        mapped = saturate(mapped + tileTint + grid * float3(0.15, 0.28, 0.42));
    }
    else if (topic == 17)
    {
        float2 centered = input.uv * 2.0 - 1.0;
        float ao = 1.0 - smoothstep(0.20, 1.05, dot(centered, centered)) * 0.25;
        mapped *= ao;
    }
    else if (topic == 18)
    {
        float sweep = 0.5 + 0.5 * sin((input.uv.x + input.uv.y) * 28.0 + gPostParams1.y * 2.0);
        mapped += sweep * 0.05 * float3(0.35, 0.65, 1.0);
    }
    else if (topic == 19)
    {
        float2 coarseUv = floor(input.uv * float2(20.0, 12.0)) / float2(20.0, 12.0);
        float rate = Hash12(coarseUv * 31.0);
        float3 rateTint = rate < 0.42 ? float3(0.15, 0.35, 0.95) : float3(0.95, 0.45, 0.10);
        float grid = max(step(0.96, frac(input.uv.x * 20.0)), step(0.94, frac(input.uv.y * 12.0)));
        mapped = lerp(mapped, rateTint, 0.16 + grid * 0.35);
    }
    else if (topic == 20)
    {
        float scan = step(0.92, frac(input.uv.y * 24.0 + gPostParams1.y * 0.7));
        mapped = saturate(mapped + scan * float3(0.08, 0.20, 0.12));
    }

    return float4(saturate(mapped), 1.0);
}

)" R"(
float4 ResolvePostAA(float2 uv)
{
    uint width = 1;
    uint height = 1;
    gPostTex.GetDimensions(width, height);
    float2 texel = 1.0 / max(float2((float)width, (float)height), float2(1.0, 1.0));
    float2 outputTexel = max(abs(float2(ddx(uv.x), ddy(uv.y))), texel);

    float3 rgbM = gPostTex.Sample(gLinearClamp, uv).rgb;
    float3 rgbNW = gPostTex.Sample(gLinearClamp, uv + texel * float2(-1.0, -1.0)).rgb;
    float3 rgbNE = gPostTex.Sample(gLinearClamp, uv + texel * float2(1.0, -1.0)).rgb;
    float3 rgbSW = gPostTex.Sample(gLinearClamp, uv + texel * float2(-1.0, 1.0)).rgb;
    float3 rgbSE = gPostTex.Sample(gLinearClamp, uv + texel * float2(1.0, 1.0)).rgb;

    float lumaM = Luminance(rgbM);
    float lumaNW = Luminance(rgbNW);
    float lumaNE = Luminance(rgbNE);
    float lumaSW = Luminance(rgbSW);
    float lumaSE = Luminance(rgbSE);

    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));
    float contrast = lumaMax - lumaMin;
    float edgeThreshold = max(0.0312, lumaMax * 0.125);
    if (contrast < edgeThreshold)
    {
        return float4(rgbM, 1.0);
    }

    float2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y = ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.0078125, 0.0009765625);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin, float2(-8.0, -8.0), float2(8.0, 8.0)) * texel;

    float3 rgbA = 0.5 * (
        gPostTex.Sample(gLinearClamp, uv + dir * (1.0 / 3.0 - 0.5)).rgb +
        gPostTex.Sample(gLinearClamp, uv + dir * (2.0 / 3.0 - 0.5)).rgb);
    float3 rgbB = rgbA * 0.5 + 0.25 * (
        gPostTex.Sample(gLinearClamp, uv + dir * -0.5).rgb +
        gPostTex.Sample(gLinearClamp, uv + dir * 0.5).rgb);

    float lumaB = Luminance(rgbB);
    float3 resolved = (lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB;
    float blend = saturate((contrast - edgeThreshold) * 6.0);

    float3 downsampled = (
        gPostTex.Sample(gLinearClamp, uv + outputTexel * float2(-0.25, -0.25)).rgb +
        gPostTex.Sample(gLinearClamp, uv + outputTexel * float2(0.25, -0.25)).rgb +
        gPostTex.Sample(gLinearClamp, uv + outputTexel * float2(-0.25, 0.25)).rgb +
        gPostTex.Sample(gLinearClamp, uv + outputTexel * float2(0.25, 0.25)).rgb) * 0.25;

    float3 antialiased = lerp(downsampled, resolved, blend);
    return float4(saturate(antialiased), 1.0);
}

float4 CopyPS(FullscreenVSOutput input) : SV_TARGET
{
    return ResolvePostAA(input.uv);
}

[numthreads(8, 8, 1)]
void ParticleCS(uint3 dtid : SV_DispatchThreadID)
{
    uint width = (uint)gParticleParams1.x;
    uint height = (uint)gParticleParams1.y;
    if (dtid.x >= width || dtid.y >= height)
    {
        return;
    }

    float time = gParticleParams0.x;
    float topic = gParticleParams0.y;
    float errorEnabled = gParticleParams0.z;
    float particleStrength = gParticleParams0.w;

    float2 uv = (float2(dtid.xy) + 0.5) * gParticleParams1.zw;
    float t = time * (0.6 + topic * 0.05);
    float n = frac(sin(dot(uv * float2(width, height) + float2(t * 13.0, t * 7.0), float2(12.9898, 78.233))) * 43758.5453123);
    float n2 = frac(sin(dot(uv * float2(width, height) + float2(-t * 9.0, t * 11.0), float2(93.9898, 67.345))) * 12731.4421);

    float spark = step(0.995 - particleStrength * 0.2, n);
    float trail = smoothstep(0.85, 1.0, n2) * 0.35;
    float pulse = 0.5 + 0.5 * sin(t * 2.0 + uv.x * 18.0 + uv.y * 24.0);

    float3 color = (spark + trail) * pulse * particleStrength * float3(1.0, 0.8, 0.35);
    if (errorEnabled > 0.5)
    {
        color *= float3(1.2, 0.7, 0.5);
    }

    gParticleOut[dtid.xy] = float4(color, 1.0);
}

)" R"(
[numthreads(8, 8, 1)]
void EdgeDetectCS(uint3 dtid : SV_DispatchThreadID)
{
    uint width = 1;
    uint height = 1;
    gEdgeOut.GetDimensions(width, height);
    if (dtid.x >= width || dtid.y >= height)
    {
        return;
    }

    int2 p = int2(dtid.xy);
    int2 maxP = int2(width - 1, height - 1);
    float3 c00 = gSceneTex.Load(int3(clamp(p + int2(-1, -1), int2(0, 0), maxP), 0)).rgb;
    float3 c10 = gSceneTex.Load(int3(clamp(p + int2(0, -1), int2(0, 0), maxP), 0)).rgb;
    float3 c20 = gSceneTex.Load(int3(clamp(p + int2(1, -1), int2(0, 0), maxP), 0)).rgb;
    float3 c01 = gSceneTex.Load(int3(clamp(p + int2(-1, 0), int2(0, 0), maxP), 0)).rgb;
    float3 c21 = gSceneTex.Load(int3(clamp(p + int2(1, 0), int2(0, 0), maxP), 0)).rgb;
    float3 c02 = gSceneTex.Load(int3(clamp(p + int2(-1, 1), int2(0, 0), maxP), 0)).rgb;
    float3 c12 = gSceneTex.Load(int3(clamp(p + int2(0, 1), int2(0, 0), maxP), 0)).rgb;
    float3 c22 = gSceneTex.Load(int3(clamp(p + int2(1, 1), int2(0, 0), maxP), 0)).rgb;

    float gx = -Luminance(c00) - 2.0 * Luminance(c01) - Luminance(c02) +
                Luminance(c20) + 2.0 * Luminance(c21) + Luminance(c22);
    float gy = -Luminance(c00) - 2.0 * Luminance(c10) - Luminance(c20) +
                Luminance(c02) + 2.0 * Luminance(c12) + Luminance(c22);
    float edge = smoothstep(0.05, 0.32, length(float2(gx, gy)));
    gEdgeOut[dtid.xy] = float4(edge, edge, edge, 1.0);
}
)";
}

} // namespace dxteaching
