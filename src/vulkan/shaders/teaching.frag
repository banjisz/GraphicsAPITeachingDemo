#version 450

layout(location = 0) in vec3 inColor;
layout(location = 1) in vec2 inUv;
layout(location = 2) in float inTime;
layout(location = 3) in float inTopic;
layout(location = 4) in float inErrorFlag;
layout(location = 5) in vec3 inWorldPos;
layout(location = 6) in vec3 inNormal;
layout(location = 7) in float inMode;

layout(location = 0) out vec4 outColor;

float hash12(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

vec3 applyTopic(int topic, vec3 color, vec2 uv, float t)
{
    float radius = length(uv - vec2(0.5));

    if (topic == 1)
    {
        color *= vec3(1.05, 1.00, 0.95);
    }
    else if (topic == 2)
    {
        vec3 swizzled = vec3(color.b, color.r, color.g);
        color = mix(color, swizzled, 0.35 + 0.20 * sin(t * 1.3));
    }
    else if (topic == 3)
    {
        color = pow(max(color, vec3(0.0)), vec3(0.88, 1.08, 1.16));
    }
    else if (topic == 4)
    {
        float pulse = abs(sin(t * 1.6 + radius * 9.0));
        color += vec3(0.14, 0.06, 0.03) * pulse;
    }
    else if (topic == 5)
    {
        float scan = 0.5 + 0.5 * sin(t * 2.0 + uv.y * 30.0);
        color += vec3(0.08, 0.05, 0.10) * scan;
    }
    else if (topic == 6)
    {
        float rim = pow(clamp(1.0 - radius * 1.9, 0.0, 1.0), 2.2);
        color = mix(color, color * vec3(0.80, 0.92, 1.0), 0.25);
        color += vec3(0.18, 0.30, 0.55) * rim * 0.35;
    }
    else if (topic == 7)
    {
        float shadow = smoothstep(0.95, 0.15, uv.y);
        color *= max(0.25, shadow);
    }
    else if (topic == 8)
    {
        vec2 l = normalize(vec2(-0.55, 0.75));
        float ndl = clamp(dot(normalize(uv - 0.5), l), 0.0, 1.0);
        float spec = pow(ndl, 32.0);
        color = color * (0.25 + 0.75 * ndl) + vec3(spec * 0.85);
    }
    else if (topic == 9)
    {
        float bloom = exp(-9.0 * radius);
        color += bloom * vec3(0.95, 0.75, 0.42);
        color = vec3(1.0) - exp(-color * 1.45);
    }
    else if (topic == 10)
    {
        vec2 p = uv * 120.0 + vec2(t * 14.0, -t * 11.0);
        float spark = step(0.988, hash12(floor(p)));
        color += vec3(1.00, 0.80, 0.30) * spark;
    }
    else if (topic == 11)
    {
        vec2 g = floor(uv * vec2(14.0, 10.0));
        float checker = mod(g.x + g.y, 2.0);
        color = mix(color, color * vec3(0.78, 1.10, 0.92), checker * 0.45);
    }
    else if (topic == 12)
    {
        float stepped = floor(t * 8.0) / 8.0;
        color *= 0.86 + 0.14 * sin(stepped * 6.0);
    }
    else if (topic == 13)
    {
        float refl = smoothstep(1.0, 0.0, radius);
        color = mix(color, vec3(0.35, 0.58, 0.88) * refl + color * 0.70, 0.42);
    }
    else if (topic == 14)
    {
        vec2 pix = floor(uv * vec2(22.0, 14.0)) / vec2(22.0, 14.0);
        color = mix(color, vec3(pix.x, pix.y, 1.0 - pix.x) * 0.85, 0.36);
    }
    else if (topic == 15)
    {
        float heat = smoothstep(0.2, 1.0, abs(sin(t * 2.2 + uv.x * 7.0 + uv.y * 9.0)));
        color = mix(color, vec3(heat, 0.18, 1.0 - heat), 0.46);
    }
    else if (topic == 16)
    {
        vec2 tile = floor(uv * vec2(18.0, 10.0));
        vec3 tileColor = 0.45 + 0.55 * vec3(
            hash12(tile + vec2(1.7, 2.3)),
            hash12(tile + vec2(8.3, 3.1)),
            hash12(tile + vec2(15.1, 4.7)));
        float grid = max(step(0.96, fract(uv.x * 18.0)),
                         step(0.94, fract(uv.y * 10.0)));
        float moving = 0.5 + 0.5 * sin(t * 1.3 + uv.x * 11.0 + uv.y * 7.0);
        color = color * (0.62 + moving * 0.48) + tileColor * (0.16 + grid * 0.26);
    }
    else if (topic == 17)
    {
        float horizon = clamp(1.0 - abs(uv.y - 0.5) * 2.0, 0.0, 1.0);
        float cavity = 1.0 - smoothstep(0.08, 0.85, abs(sin(uv.x * 25.0) * cos(uv.y * 19.0)));
        float ao = clamp(0.52 + horizon * 0.34 - cavity * 0.22, 0.0, 1.0);
        color *= ao;
        color = mix(color, color * vec3(0.70, 0.82, 1.0), 0.18);
    }
    else if (topic == 18)
    {
        vec2 centered = uv * 2.0 - 1.0;
        float fresnel = pow(clamp(length(centered), 0.0, 1.0), 2.4);
        float bands = 0.5 + 0.5 * sin((centered.x + centered.y) * 18.0 - t * 1.8);
        vec3 reflection = mix(vec3(0.08, 0.18, 0.30), vec3(0.55, 0.80, 1.0), bands);
        color = mix(color, reflection + color * 0.55, fresnel * 0.72);
        color += fresnel * vec3(0.15, 0.28, 0.48);
    }
    else if (topic == 19)
    {
        vec2 coarseUv = floor(uv * vec2(20.0, 12.0)) / vec2(20.0, 12.0);
        float lane = hash12(coarseUv * 31.0);
        float shadeRate = mix(0.55, 1.15, step(0.36, lane));
        float grid = max(step(0.96, fract(uv.x * 20.0)),
                         step(0.94, fract(uv.y * 12.0)));
        vec3 rateTint = lane < 0.36 ? vec3(0.25, 0.55, 1.00) : vec3(1.00, 0.62, 0.25);
        color = color * shadeRate;
        color = mix(color, rateTint, 0.20 + grid * 0.32);
    }
    else if (topic == 20)
    {
        float distanceApprox = length(uv - 0.5) * 2.0;
        float lod = clamp((distanceApprox - 0.25) / 0.75, 0.0, 1.0);
        float quant = floor(lod * 4.0) / 4.0;
        float stripes = step(0.82, fract((uv.x + uv.y) * mix(14.0, 5.0, quant)));
        vec3 lodColor = mix(vec3(0.30, 1.00, 0.55), vec3(1.00, 0.45, 0.20), quant);
        color = mix(color, color * lodColor, 0.22);
        color += stripes * lodColor * (0.10 + quant * 0.18);
    }

    return max(color, vec3(0.0));
}

vec3 applyTopicDxStyle(int topic, vec3 color, vec3 normal, vec3 worldPos, float t)
{
    float radius = length(worldPos.xz) * 0.2;
    vec3 cameraPos = vec3(0.0, 1.4, -4.5);

    if (topic == 1)
    {
        color *= vec3(1.05, 1.00, 0.95);
    }
    else if (topic == 2)
    {
        vec3 swizzled = vec3(color.b, color.r, color.g);
        color = mix(color, swizzled, 0.35 + 0.20 * sin(t * 1.3));
    }
    else if (topic == 3)
    {
        color = pow(max(color, vec3(0.0)), vec3(0.90, 1.10, 1.20));
    }
    else if (topic == 4)
    {
        color += vec3(0.12 * abs(sin(t * 1.5 + worldPos.x * 2.0)));
    }
    else if (topic == 5)
    {
        color += vec3(0.08 * sin(t + worldPos.x * 2.0),
                      0.05 * cos(t * 1.4 + worldPos.y * 2.0),
                      0.06 * sin(t * 1.8));
    }
    else if (topic == 6)
    {
        vec3 N = normalize(normal);
        vec3 V = normalize(cameraPos - worldPos);
        float rim = pow(clamp(1.0 - abs(dot(N, V)), 0.0, 1.0), 3.0);
        float faceTint = 0.08 * clamp(N.y * 0.5 + 0.5, 0.0, 1.0);
        color = mix(color, color * vec3(0.82, 0.90, 1.0), 0.18);
        color += (rim * 0.22 + faceTint) * vec3(0.65, 0.82, 1.0);
    }
    else if (topic == 7)
    {
        float shadow = clamp(0.72 - (worldPos.y + 0.5) * 0.55, 0.0, 1.0);
        color *= max(shadow, 0.25);
    }
    else if (topic == 8)
    {
        vec3 N = normalize(normal);
        vec3 L = normalize(vec3(-0.35, 0.6, 0.7));
        vec3 V = vec3(0.0, 0.0, 1.0);
        vec3 H = normalize(L + V);
        float ndl = clamp(dot(N, L), 0.0, 1.0);
        float ndh = clamp(dot(N, H), 0.0, 1.0);
        float rough = 0.35;
        float spec = pow(ndh, mix(16.0, 96.0, 1.0 - rough));
        color = color * (0.25 + 0.75 * ndl) + vec3(spec * 0.80);
    }
    else if (topic == 9)
    {
        float bloom = exp(-9.0 * radius);
        color += bloom * vec3(0.90, 0.75, 0.45);
        color = vec3(1.0) - exp(-color * 1.5);
    }
    else if (topic == 10)
    {
        vec2 p = worldPos.xz * 4.0 + vec2(t * 3.0, -t * 1.7);
        float spark = step(0.965, hash12(floor(p)));
        color += vec3(1.0, 0.8, 0.35) * spark * 0.9;
    }
    else if (topic == 11)
    {
        vec2 uv = worldPos.xz * 0.25 + 0.5;
        float checker = step(0.5, fract(uv.x * 10.0)) * step(0.5, fract(uv.y * 10.0));
        color = mix(color, color * vec3(0.75, 1.1, 0.9), checker * 0.45);
    }
    else if (topic == 12)
    {
        float stepped = floor(t * 8.0) / 8.0;
        color *= 0.88 + 0.12 * sin(stepped * 6.0);
    }
    else if (topic == 13)
    {
        float refl = smoothstep(1.0, 0.0, radius);
        color = mix(color, vec3(0.35, 0.55, 0.85) * refl + color * 0.72, 0.4);
    }
    else if (topic == 14)
    {
        vec2 uv = worldPos.xz * 0.20 + 0.5;
        vec2 pix = floor(uv * vec2(24.0, 24.0)) / vec2(24.0, 24.0);
        color = mix(color, vec3(pix.x, pix.y, 1.0 - pix.x) * 0.85, 0.38);
    }
    else if (topic == 15)
    {
        float heat = smoothstep(0.2, 1.0, abs(sin(t * 2.0 + worldPos.x * 3.0 + worldPos.z * 2.0)));
        color = mix(color, vec3(heat, 0.18, 1.0 - heat), 0.42);
    }
    else if (topic == 16)
    {
        vec2 tile = floor((worldPos.xz + 4.0) * 1.25);
        float tileId = hash12(tile);
        vec3 tileColor = 0.45 + 0.55 * vec3(
            hash12(tile + vec2(1.7)),
            hash12(tile + vec2(8.3)),
            hash12(tile + vec2(15.1)));
        float movingLight = 0.0;
        for (int i = 0; i < 4; ++i)
        {
            float fi = float(i);
            vec3 lp = vec3(sin(t * (0.55 + fi * 0.18) + fi * 1.9) * 3.2,
                           1.15 + 0.35 * sin(t * 0.7 + fi),
                           cos(t * (0.42 + fi * 0.13) + fi * 2.4) * 2.7);
            float d = length(worldPos - lp);
            movingLight += clamp(1.0 - d / (2.4 + fi * 0.2), 0.0, 1.0);
        }
        float grid = max(step(0.94, fract((worldPos.x + 4.0) * 1.25)),
                         step(0.94, fract((worldPos.z + 4.0) * 1.25)));
        color = color * (0.55 + movingLight * 0.55) + tileColor * (0.12 + grid * 0.28 + tileId * 0.05);
    }
    else if (topic == 17)
    {
        vec3 N = normalize(normal);
        float horizon = clamp(N.y * 0.5 + 0.5, 0.0, 1.0);
        float cavity = 1.0 - smoothstep(0.18, 0.85, abs(N.x) + abs(N.y) + abs(N.z) - 1.0);
        float contact = smoothstep(-0.95, 0.20, -worldPos.y);
        float ao = clamp(0.52 + horizon * 0.34 - cavity * 0.18 - contact * 0.20, 0.0, 1.0);
        color *= ao;
        color = mix(color, color * vec3(0.70, 0.82, 1.0), 0.18);
    }
    else if (topic == 18)
    {
        vec3 N = normalize(normal);
        vec3 V = normalize(cameraPos - worldPos);
        float fresnel = pow(clamp(1.0 - dot(N, V), 0.0, 1.0), 4.0);
        vec2 reflectedUv = reflect(-V, N).xz * 0.22 + worldPos.xz * 0.04 + t * vec2(0.03, -0.02);
        float bands = 0.5 + 0.5 * sin((reflectedUv.x + reflectedUv.y) * 38.0);
        vec3 reflection = mix(vec3(0.08, 0.18, 0.28), vec3(0.55, 0.80, 1.0), bands);
        color = mix(color, reflection + color * 0.55, fresnel * 0.72);
        color += fresnel * vec3(0.35, 0.55, 0.85);
    }
    else if (topic == 19)
    {
        vec2 screenUv = worldPos.xy * 0.18 + 0.5;
        vec2 coarse = floor(screenUv * 12.0) / 12.0;
        float lane = hash12(coarse);
        float shadeRate = mix(0.55, 1.15, step(0.36, lane));
        float debugGrid = max(step(0.92, fract(screenUv.x * 12.0)), step(0.92, fract(screenUv.y * 12.0)));
        vec3 rateColor = lane < 0.36 ? vec3(0.25, 0.55, 1.0) : vec3(1.0, 0.62, 0.25);
        color = color * shadeRate;
        color = mix(color, rateColor, 0.22 + debugGrid * 0.30);
    }
    else if (topic == 20)
    {
        float distanceToCamera = length(cameraPos - worldPos);
        float lod = clamp((distanceToCamera - 3.0) / 5.0, 0.0, 1.0);
        float quant = floor(lod * 4.0) / 4.0;
        float stripes = step(0.82, fract((worldPos.x + worldPos.y + worldPos.z) * mix(8.0, 2.5, quant)));
        vec3 lodColor = mix(vec3(0.30, 1.00, 0.55), vec3(1.00, 0.45, 0.20), quant);
        color = mix(color, color * lodColor, 0.22);
        color += stripes * lodColor * (0.10 + quant * 0.18);
    }

    return max(color, vec3(0.0));
}

void main()
{
    int topic = int(clamp(floor(inTopic + 0.5), 1.0, 20.0));

    vec3 color = inColor;
    if (inMode > 0.5)
    {
        vec3 N = normalize(inNormal);
        vec3 L = normalize(vec3(-0.38, 0.68, 0.63));
        vec3 V = normalize(vec3(0.0, 1.4, -4.5) - inWorldPos);
        float ndl = max(dot(N, L), 0.0);
        color = color * (0.18 + 0.82 * ndl);
        color = applyTopicDxStyle(topic, color, N, inWorldPos, inTime);
    }
    else
    {
        color = applyTopic(topic, color, inUv, inTime);
    }

    if (inErrorFlag > 0.5)
    {
        if (inMode > 0.5)
        {
            float radius = length(inWorldPos.xz) * 0.25;
            float jitter = 0.92 + 0.35 * abs(sin(inTime * 2.7 + radius * 9.0));
            color = pow(max(color * jitter, vec3(0.0)), vec3(0.78));
            if (topic == 9 || topic == 14)
            {
                color += vec3(0.34, 0.16, 0.05);
            }
        }
        else
        {
            float jitter = 0.92 + 0.35 * abs(sin(inTime * 2.7 + inUv.x * 12.0));
            color = pow(max(color * jitter, vec3(0.0)), vec3(0.80));
            color += vec3(0.03, 0.0, -0.01);
        }
    }

    outColor = vec4(clamp(color, vec3(0.0), vec3(1.0)), 1.0);
}
