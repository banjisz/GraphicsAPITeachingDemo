#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;

layout(location = 0) out vec3 outColor;
layout(location = 1) out vec2 outUv;
layout(location = 2) out float outTime;
layout(location = 3) out float outTopic;
layout(location = 4) out float outErrorFlag;
layout(location = 5) out vec3 outWorldPos;
layout(location = 6) out vec3 outNormal;
layout(location = 7) out float outMode;
layout(location = 8) out vec3 outLocalPos;
layout(location = 9) out vec3 outLocalNormal;

layout(push_constant) uniform PushConstants
{
    vec4 params0; // x=time, y=topic, z=error flag, w=mode (0 triangle, 1 cube)
    vec4 params1; // x=aspect, y=width, z=height, w=reserved
} pc;

mat3 rotationX(float a)
{
    float c = cos(a);
    float s = sin(a);
    return mat3(1.0, 0.0, 0.0,
                0.0, c, -s,
                0.0, s, c);
}

mat3 rotationY(float a)
{
    float c = cos(a);
    float s = sin(a);
    return mat3(c, 0.0, s,
                0.0, 1.0, 0.0,
                -s, 0.0, c);
}

void main()
{
    float t = pc.params0.x;
    float topic = floor(pc.params0.y + 0.5);
    float errorFlag = pc.params0.z;
    float mode = pc.params0.w;
    float aspect = max(0.1, pc.params1.x);

    vec3 worldPos = inPosition;
    vec3 worldNormal = normalize(inNormal);
    vec3 localPos = inPosition;
    vec3 localNormal = normalize(inNormal);
    vec2 uv = inPosition.xy * 0.5 + 0.5;

    if (mode < 0.5)
    {
        float speed = 0.55 + 0.03 * topic;
        float angle = t * speed;
        mat2 rot = mat2(cos(angle), -sin(angle),
                        sin(angle),  cos(angle));

        float wobble = 0.95 + 0.08 * sin(t * 0.75 + topic * 0.15);
        vec2 p = rot * (inPosition.xy * wobble);

        if (errorFlag > 0.5)
        {
            p += vec2(0.02 * sin(t * 6.0 + inPosition.y * 8.0),
                      0.02 * cos(t * 5.0 + inPosition.x * 7.0));
        }

        gl_Position = vec4(p, 0.0, 1.0);
        worldPos = vec3(p, 0.0);
        worldNormal = vec3(0.0, 0.0, 1.0);
        uv = p * 0.5 + 0.5;
    }
    else
    {
        float angleY = t * (0.85 + 0.03 * topic);
        float angleX = t * (0.55 + 0.02 * topic);
        mat3 rot = rotationY(angleY) * rotationX(angleX);

        worldPos = rot * inPosition;
        worldNormal = normalize(rot * inNormal);

        if (errorFlag > 0.5)
        {
            worldPos += worldNormal * (0.03 * sin(t * 7.0 + inPosition.x * 4.0 + inPosition.y * 3.0));
        }

        vec3 viewPos = worldPos + vec3(0.0, 0.0, 3.2);
        float viewZ = max(0.05, viewPos.z);
        const float nearPlane = 0.1;
        const float farPlane = 20.0;
        const float fovY = 1.04719755; // 60 deg
        float focal = 1.0 / tan(fovY * 0.5);
        float clipZ = (viewZ * farPlane - nearPlane * farPlane) / (farPlane - nearPlane);
        gl_Position = vec4((viewPos.x / aspect) * focal, viewPos.y * focal, clipZ, viewZ);

        uv = worldPos.xy * 0.5 + 0.5;
    }

    outColor = inColor;
    outUv = uv;
    outTime = t;
    outTopic = topic;
    outErrorFlag = errorFlag;
    outWorldPos = worldPos;
    outNormal = worldNormal;
    outMode = mode;
    outLocalPos = localPos;
    outLocalNormal = localNormal;
}
