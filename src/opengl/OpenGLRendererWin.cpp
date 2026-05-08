#include "OpenGLRendererWin.h"

#include <GL/gl.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "DiagnosticsLog.h"
#include "DXTeachingShared.h"

#pragma comment(lib, "opengl32.lib")

// WGL extension constants
#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB 0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
#define WGL_DRAW_TO_WINDOW_ARB 0x2001
#define WGL_SUPPORT_OPENGL_ARB 0x2010
#define WGL_DOUBLE_BUFFER_ARB 0x2011
#define WGL_PIXEL_TYPE_ARB 0x2013
#define WGL_COLOR_BITS_ARB 0x2014
#define WGL_DEPTH_BITS_ARB 0x2022
#define WGL_STENCIL_BITS_ARB 0x2023
#define WGL_TYPE_RGBA_ARB 0x202B

// GL constants not in Windows gl.h
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4

typedef char GLchar;
typedef intptr_t GLsizeiptr;

// GL function pointer types
typedef GLuint(APIENTRY *PFNGLCREATESHADERPROC)(GLenum type);
typedef void(APIENTRY *PFNGLSHADERSOURCEPROC)(GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length);
typedef void(APIENTRY *PFNGLCOMPILESHADERPROC)(GLuint shader);
typedef void(APIENTRY *PFNGLGETSHADERIVPROC)(GLuint shader, GLenum pname, GLint *params);
typedef void(APIENTRY *PFNGLGETSHADERINFOLOGPROC)(GLuint shader, GLsizei maxLength, GLsizei *length, GLchar *infoLog);
typedef GLuint(APIENTRY *PFNGLCREATEPROGRAMPROC)();
typedef void(APIENTRY *PFNGLATTACHSHADERPROC)(GLuint program, GLuint shader);
typedef void(APIENTRY *PFNGLLINKPROGRAMPROC)(GLuint program);
typedef void(APIENTRY *PFNGLGETPROGRAMIVPROC)(GLuint program, GLenum pname, GLint *params);
typedef void(APIENTRY *PFNGLGETPROGRAMINFOLOGPROC)(GLuint program, GLsizei maxLength, GLsizei *length, GLchar *infoLog);
typedef void(APIENTRY *PFNGLDELETESHADERPROC)(GLuint shader);
typedef void(APIENTRY *PFNGLUSEPROGRAMPROC)(GLuint program);
typedef void(APIENTRY *PFNGLDELETEPROGRAMPROC)(GLuint program);
typedef GLint(APIENTRY *PFNGLGETUNIFORMLOCATIONPROC)(GLuint program, const GLchar *name);
typedef void(APIENTRY *PFNGLUNIFORMMATRIX4FVPROC)(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
typedef void(APIENTRY *PFNGLUNIFORM4FPROC)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
typedef void(APIENTRY *PFNGLGENVERTEXARRAYSPROC)(GLsizei n, GLuint *arrays);
typedef void(APIENTRY *PFNGLBINDVERTEXARRAYPROC)(GLuint array);
typedef void(APIENTRY *PFNGLDELETEVERTEXARRAYSPROC)(GLsizei n, const GLuint *arrays);
typedef void(APIENTRY *PFNGLGENBUFFERSPROC)(GLsizei n, GLuint *buffers);
typedef void(APIENTRY *PFNGLBINDBUFFERPROC)(GLenum target, GLuint buffer);
typedef void(APIENTRY *PFNGLBUFFERDATAPROC)(GLenum target, GLsizeiptr size, const void *data, GLenum usage);
typedef void(APIENTRY *PFNGLDELETEBUFFERSPROC)(GLsizei n, const GLuint *buffers);
typedef void(APIENTRY *PFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint index);
typedef void(APIENTRY *PFNGLVERTEXATTRIBPOINTERPROC)(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer);

typedef HGLRC(WINAPI *PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC hDC, HGLRC hShareContext, const int *attribList);
typedef BOOL(WINAPI *PFNWGLCHOOSEPIXELFORMATARBPROC)(HDC hdc, const int *piAttribIList, const FLOAT *pfAttribFList, UINT nMaxFormats, int *piFormats, UINT *nNumFormats);
typedef BOOL(WINAPI *PFNWGLSWAPINTERVALEXTPROC)(int interval);

// GL function pointers (file-local)
static PFNGLCREATESHADERPROC glCreateShader_;
static PFNGLSHADERSOURCEPROC glShaderSource_;
static PFNGLCOMPILESHADERPROC glCompileShader_;
static PFNGLGETSHADERIVPROC glGetShaderiv_;
static PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog_;
static PFNGLCREATEPROGRAMPROC glCreateProgram_;
static PFNGLATTACHSHADERPROC glAttachShader_;
static PFNGLLINKPROGRAMPROC glLinkProgram_;
static PFNGLGETPROGRAMIVPROC glGetProgramiv_;
static PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog_;
static PFNGLDELETESHADERPROC glDeleteShader_;
static PFNGLUSEPROGRAMPROC glUseProgram_;
static PFNGLDELETEPROGRAMPROC glDeleteProgram_;
static PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation_;
static PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv_;
static PFNGLUNIFORM4FPROC glUniform4f_;
static PFNGLGENVERTEXARRAYSPROC glGenVertexArrays_;
static PFNGLBINDVERTEXARRAYPROC glBindVertexArray_;
static PFNGLDELETEVERTEXARRAYSPROC glDeleteVertexArrays_;
static PFNGLGENBUFFERSPROC glGenBuffers_;
static PFNGLBINDBUFFERPROC glBindBuffer_;
static PFNGLBUFFERDATAPROC glBufferData_;
static PFNGLDELETEBUFFERSPROC glDeleteBuffers_;
static PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray_;
static PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer_;

static PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB_;
static PFNWGLCHOOSEPIXELFORMATARBPROC wglChoosePixelFormatARB_;
static PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT_;

namespace dxteaching
{

static const char *kVertexShaderSource = R"(
#version 330 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;

uniform mat4 uWorld;
uniform mat4 uViewProj;

out vec3 vWorldPos;
out vec3 vNormal;
out vec3 vColor;

void main()
{
    vec4 worldPos = uWorld * vec4(aPosition, 1.0);
    gl_Position = uViewProj * worldPos;
    vWorldPos = worldPos.xyz;
    vNormal = normalize((uWorld * vec4(aNormal, 0.0)).xyz);
    vColor = aColor;
}
)";

static const char *kFragmentShaderSource = R"(
#version 330 core

in vec3 vWorldPos;
in vec3 vNormal;
in vec3 vColor;

uniform vec4 uLightDir;
uniform vec4 uCameraPosTime;
uniform vec4 uTopicFlags;

out vec4 fragColor;

float hash12(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

vec3 topicColorAdjust(int topic, vec3 color, vec3 normal, vec3 worldPos, float t)
{
    float radius = length(worldPos.xz) * 0.2;

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
        color += 0.12 * abs(sin(t * 1.5 + worldPos.x * 2.0));
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
        vec3 V = normalize(uCameraPosTime.xyz - worldPos);
        float rim = pow(clamp(1.0 - abs(dot(N, V)), 0.0, 1.0), 3.0);
        color = mix(color, color * vec3(0.82, 0.90, 1.0), 0.18);
        color += rim * 0.22 * vec3(0.65, 0.82, 1.0);
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
        float ndl = max(dot(N, L), 0.0);
        float ndh = max(dot(N, H), 0.0);
        float spec = pow(ndh, 48.0);
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
        vec2 pix = floor(uv * 24.0) / 24.0;
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
        vec3 tileColor = 0.45 + 0.55 * vec3(
            hash12(tile + 1.7),
            hash12(tile + 8.3),
            hash12(tile + 15.1));
        float grid = max(step(0.94, fract((worldPos.x + 4.0) * 1.25)),
                         step(0.94, fract((worldPos.z + 4.0) * 1.25)));
        color = color * 0.75 + tileColor * (0.12 + grid * 0.28);
    }
    else if (topic == 17)
    {
        vec3 N = normalize(normal);
        float horizon = clamp(N.y * 0.5 + 0.5, 0.0, 1.0);
        float ao = clamp(0.52 + horizon * 0.34, 0.0, 1.0);
        color *= ao;
        color = mix(color, color * vec3(0.70, 0.82, 1.0), 0.18);
    }
    else if (topic == 18)
    {
        vec3 N = normalize(normal);
        vec3 V = normalize(uCameraPosTime.xyz - worldPos);
        float fresnel = pow(clamp(1.0 - dot(N, V), 0.0, 1.0), 4.0);
        color += fresnel * vec3(0.35, 0.55, 0.85);
    }
    else if (topic == 19)
    {
        vec2 screenUv = worldPos.xy * 0.18 + 0.5;
        vec2 coarse = floor(screenUv * 12.0) / 12.0;
        float lane = hash12(coarse);
        float debugGrid = max(step(0.92, fract(screenUv.x * 12.0)), step(0.92, fract(screenUv.y * 12.0)));
        vec3 rateColor = lane < 0.36 ? vec3(0.25, 0.55, 1.0) : vec3(1.0, 0.62, 0.25);
        color = mix(color, rateColor, 0.22 + debugGrid * 0.30);
    }
    else if (topic == 20)
    {
        float distanceToCamera = length(uCameraPosTime.xyz - worldPos);
        float lod = clamp((distanceToCamera - 3.0) / 5.0, 0.0, 1.0);
        float quant = floor(lod * 4.0) / 4.0;
        vec3 lodColor = mix(vec3(0.30, 1.00, 0.55), vec3(1.00, 0.45, 0.20), quant);
        color = mix(color, color * lodColor, 0.22);
    }

    return max(color, vec3(0.0));
}

void main()
{
    int topic = int(round(uTopicFlags.x));
    bool errorEnabled = uTopicFlags.y > 0.5;

    vec3 N = normalize(vNormal);
    vec3 lightDir = normalize(-uLightDir.xyz);
    float ndl = max(dot(N, lightDir), 0.0);

    vec3 litColor = vColor * (0.20 + 0.80 * ndl);

    float t = uCameraPosTime.w;
    vec3 color = topicColorAdjust(topic, litColor, N, vWorldPos, t);

    if (errorEnabled)
    {
        float radius = length(vWorldPos.xz) * 0.25;
        float jitter = 0.92 + 0.35 * abs(sin(t * 2.7 + radius * 9.0));
        color = pow(max(color * jitter, vec3(0.0)), vec3(0.78));
    }

    fragColor = vec4(max(color, vec3(0.0)), 1.0);
}
)";

// Simple matrix math helpers
static void Mat4Identity(float m[16])
{
    std::memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void Mat4Multiply(float out[16], const float a[16], const float b[16])
{
    float tmp[16];
    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 4; ++col)
        {
            tmp[col * 4 + row] =
                a[0 * 4 + row] * b[col * 4 + 0] +
                a[1 * 4 + row] * b[col * 4 + 1] +
                a[2 * 4 + row] * b[col * 4 + 2] +
                a[3 * 4 + row] * b[col * 4 + 3];
        }
    }
    std::memcpy(out, tmp, 16 * sizeof(float));
}

static void Mat4RotateY(float m[16], float radians)
{
    Mat4Identity(m);
    float c = std::cos(radians);
    float s = std::sin(radians);
    m[0] = c;
    m[2] = -s;
    m[8] = s;
    m[10] = c;
}

static void Mat4Perspective(float m[16], float fovY, float aspect, float nearZ, float farZ)
{
    std::memset(m, 0, 16 * sizeof(float));
    float tanHalfFov = std::tan(fovY * 0.5f);
    m[0] = 1.0f / (aspect * tanHalfFov);
    m[5] = 1.0f / tanHalfFov;
    m[10] = -(farZ + nearZ) / (farZ - nearZ);
    m[11] = -1.0f;
    m[14] = -(2.0f * farZ * nearZ) / (farZ - nearZ);
}

static void Mat4LookAt(float m[16], const float eye[3], const float center[3], const float up[3])
{
    float f[3] = {center[0] - eye[0], center[1] - eye[1], center[2] - eye[2]};
    float fLen = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    f[0] /= fLen;
    f[1] /= fLen;
    f[2] /= fLen;

    float s[3] = {f[1] * up[2] - f[2] * up[1], f[2] * up[0] - f[0] * up[2], f[0] * up[1] - f[1] * up[0]};
    float sLen = std::sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
    s[0] /= sLen;
    s[1] /= sLen;
    s[2] /= sLen;

    float u[3] = {s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0]};

    Mat4Identity(m);
    m[0] = s[0];
    m[4] = s[1];
    m[8] = s[2];
    m[1] = u[0];
    m[5] = u[1];
    m[9] = u[2];
    m[2] = -f[0];
    m[6] = -f[1];
    m[10] = -f[2];
    m[12] = -(s[0] * eye[0] + s[1] * eye[1] + s[2] * eye[2]);
    m[13] = -(u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2]);
    m[14] = (f[0] * eye[0] + f[1] * eye[1] + f[2] * eye[2]);
}

// Geometry generation: a sphere-like icosphere approximation
struct MeshVertex
{
    float pos[3];
    float normal[3];
    float color[3];
};

static void GenerateTeachingMesh(std::vector<MeshVertex> &vertices, std::vector<uint32_t> &indices)
{
    constexpr int stacks = 24;
    constexpr int slices = 32;
    constexpr float radius = 1.0f;
    constexpr float pi = 3.14159265f;

    vertices.clear();
    indices.clear();

    for (int i = 0; i <= stacks; ++i)
    {
        float phi = pi * static_cast<float>(i) / static_cast<float>(stacks);
        float y = radius * std::cos(phi);
        float ringRadius = radius * std::sin(phi);

        for (int j = 0; j <= slices; ++j)
        {
            float theta = 2.0f * pi * static_cast<float>(j) / static_cast<float>(slices);
            float x = ringRadius * std::cos(theta);
            float z = ringRadius * std::sin(theta);

            MeshVertex v{};
            v.pos[0] = x;
            v.pos[1] = y;
            v.pos[2] = z;
            float len = std::sqrt(x * x + y * y + z * z);
            v.normal[0] = x / len;
            v.normal[1] = y / len;
            v.normal[2] = z / len;

            float t0 = static_cast<float>(j) / static_cast<float>(slices);
            float t1 = static_cast<float>(i) / static_cast<float>(stacks);
            v.color[0] = 0.4f + 0.6f * t0;
            v.color[1] = 0.3f + 0.4f * t1;
            v.color[2] = 0.5f + 0.5f * (1.0f - t0);

            vertices.push_back(v);
        }
    }

    for (int i = 0; i < stacks; ++i)
    {
        for (int j = 0; j < slices; ++j)
        {
            uint32_t row0 = static_cast<uint32_t>(i * (slices + 1) + j);
            uint32_t row1 = static_cast<uint32_t>((i + 1) * (slices + 1) + j);
            indices.push_back(row0);
            indices.push_back(row1);
            indices.push_back(row0 + 1);
            indices.push_back(row0 + 1);
            indices.push_back(row1);
            indices.push_back(row1 + 1);
        }
    }
}

bool OpenGLRenderer::classRegistered_ = false;

OpenGLRenderer::OpenGLRenderer() = default;

OpenGLRenderer::~OpenGLRenderer()
{
    Shutdown();
}

bool OpenGLRenderer::Initialize(HWND hwnd, uint32_t width, uint32_t height)
{
    LogLine("OGL", "Initialize begin size=%ux%u", width, height);
    parentHwnd_ = hwnd;
    width_ = width;
    height_ = height;

    if (!CreateChildWindow())
    {
        LogLine("OGL", "CreateChildWindow failed");
        return false;
    }

    if (!CreateGLContext())
    {
        LogLine("OGL", "CreateGLContext failed");
        return false;
    }

    if (!LoadGLFunctions())
    {
        LogLine("OGL", "LoadGLFunctions failed");
        Shutdown();
        return false;
    }

    if (!CompileShaders())
    {
        LogLine("OGL", "CompileShaders failed");
        Shutdown();
        return false;
    }

    CreateGeometry();

    glEnable(GL_DEPTH_TEST);
    // Mesh winding between DX and GL paths can differ on some drivers/platforms.
    // Disable culling to guarantee visible output while switching backends.
    glDisable(GL_CULL_FACE);

    LogLine("OGL", "Initialize success, GL_VERSION=%s", reinterpret_cast<const char *>(glGetString(GL_VERSION)));
    return true;
}

void OpenGLRenderer::Resize(uint32_t width, uint32_t height)
{
    width_ = width;
    height_ = height;
    if (glHwnd_)
    {
        MoveWindow(glHwnd_, 0, 0, static_cast<int>(width_), static_cast<int>(height_), FALSE);
    }
    if (hglrc_)
    {
        wglMakeCurrent(hdc_, hglrc_);
        glViewport(0, 0, static_cast<GLsizei>(width_), static_cast<GLsizei>(height_));
    }
}

void OpenGLRenderer::Render(const FrameSettings &settings)
{
    if (!hglrc_ || !sceneProgram_ || !hdc_)
    {
        return;
    }

    if (!wglMakeCurrent(hdc_, hglrc_))
    {
        LogLine("OGL", "wglMakeCurrent failed in Render, error=%lu", GetLastError());
        return;
    }

    float clearColor[4];
    ComputeClearColor(settings, clearColor);
    glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glViewport(0, 0, static_cast<GLsizei>(width_), static_cast<GLsizei>(height_));

    glUseProgram_(sceneProgram_);

    UpdateMatrices(settings);

    float lightDir[4] = {0.0f, -1.0f, 0.0f, 0.7f};
    glUniform4f_(locLightDir_, lightDir[0], lightDir[1], lightDir[2], lightDir[3]);

    float cameraPos[4] = {0.0f, 1.4f, -4.5f, settings.elapsedSeconds};
    glUniform4f_(locCameraPosTime_, cameraPos[0], cameraPos[1], cameraPos[2], cameraPos[3]);

    float topicFlags[4] = {
        static_cast<float>(settings.topic),
        settings.errorExampleEnabled ? 1.0f : 0.0f,
        0.0f,
        0.0f};
    glUniform4f_(locTopicFlags_, topicFlags[0], topicFlags[1], topicFlags[2], topicFlags[3]);

    glBindVertexArray_(vao_);

    TopicRuntimeProfile profile = BuildTopicRuntimeProfile(settings.topic, settings.errorExampleEnabled);
    uint32_t objectCount = profile.objectCount;

    for (uint32_t obj = 0; obj < objectCount; ++obj)
    {
        float angle = settings.elapsedSeconds * profile.rotationSpeed +
                      static_cast<float>(obj) * 6.28318f / static_cast<float>(objectCount);
        float offsetX = (objectCount > 1) ? 2.2f * std::cos(angle) : 0.0f;
        float offsetZ = (objectCount > 1) ? 2.2f * std::sin(angle) : 0.0f;

        float world[16];
        float rotation[16];
        Mat4RotateY(rotation, angle);

        Mat4Identity(world);
        world[12] = offsetX;
        world[14] = offsetZ;
        Mat4Multiply(world, world, rotation);

        glUniformMatrix4fv_(locWorld_, 1, GL_FALSE, world);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexCount_), GL_UNSIGNED_INT, nullptr);
    }

    glBindVertexArray_(0);
    glUseProgram_(0);

    glFlush();
    if (!SwapBuffers(hdc_))
    {
        LogLine("OGL", "SwapBuffers failed, error=%lu", GetLastError());
    }
}

void OpenGLRenderer::Shutdown()
{
    LogLine("OGL", "Shutdown begin");

    if (hglrc_)
    {
        wglMakeCurrent(hdc_, hglrc_);
        DestroyGeometry();
        DestroyShaders();
        glFinish();
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(hglrc_);
        hglrc_ = nullptr;
    }

    if (hdc_ && glHwnd_)
    {
        ReleaseDC(glHwnd_, hdc_);
        hdc_ = nullptr;
    }

    if (glHwnd_)
    {
        DestroyWindow(glHwnd_);
        glHwnd_ = nullptr;
    }

    LogLine("OGL", "Shutdown done");
}

const char *OpenGLRenderer::BackendName() const
{
    return "OpenGL";
}

bool OpenGLRenderer::CreateChildWindow()
{
    if (!classRegistered_)
    {
        WNDCLASSEXA wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_OWNDC;
        wc.lpfnWndProc = DefWindowProcA;
        wc.hInstance = GetModuleHandleA(nullptr);
        wc.lpszClassName = "OpenGLChildWindow";
        if (!RegisterClassExA(&wc))
        {
            LogLine("OGL", "RegisterClassExA failed for child window, error=%lu", GetLastError());
            return false;
        }
        classRegistered_ = true;
    }

    glHwnd_ = CreateWindowExA(
        0,
        "OpenGLChildWindow",
        nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        0, 0,
        static_cast<int>(width_), static_cast<int>(height_),
        parentHwnd_,
        nullptr,
        GetModuleHandleA(nullptr),
        nullptr);

    if (!glHwnd_)
    {
        LogLine("OGL", "CreateWindowExA child failed, error=%lu", GetLastError());
        return false;
    }

    ShowWindow(glHwnd_, SW_SHOW);
    SetWindowPos(glHwnd_, HWND_TOP, 0, 0, static_cast<int>(width_), static_cast<int>(height_),
                 SWP_NOMOVE | SWP_SHOWWINDOW);
    UpdateWindow(glHwnd_);

    LogLine("OGL", "Child window created");
    return true;
}

bool OpenGLRenderer::CreateGLContext()
{
    hdc_ = GetDC(glHwnd_);
    if (!hdc_)
    {
        LogLine("OGL", "GetDC failed");
        return false;
    }

    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;

    int pixelFormat = ChoosePixelFormat(hdc_, &pfd);
    if (pixelFormat == 0)
    {
        LogLine("OGL", "ChoosePixelFormat failed, error=%lu", GetLastError());
        return false;
    }

    if (!SetPixelFormat(hdc_, pixelFormat, &pfd))
    {
        LogLine("OGL", "SetPixelFormat failed, error=%lu", GetLastError());
        return false;
    }
    LogLine("OGL", "Pixel format set: %d", pixelFormat);

    HGLRC tempContext = wglCreateContext(hdc_);
    if (!tempContext)
    {
        LogLine("OGL", "wglCreateContext (temp) failed");
        return false;
    }

    if (!wglMakeCurrent(hdc_, tempContext))
    {
        wglDeleteContext(tempContext);
        LogLine("OGL", "wglMakeCurrent (temp) failed");
        return false;
    }

    wglCreateContextAttribsARB_ = reinterpret_cast<PFNWGLCREATECONTEXTATTRIBSARBPROC>(
        wglGetProcAddress("wglCreateContextAttribsARB"));
    wglSwapIntervalEXT_ = reinterpret_cast<PFNWGLSWAPINTERVALEXTPROC>(
        wglGetProcAddress("wglSwapIntervalEXT"));

    if (wglCreateContextAttribsARB_)
    {
        int contextAttribs[] = {
            WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
            WGL_CONTEXT_MINOR_VERSION_ARB, 3,
            WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
            0};

        hglrc_ = wglCreateContextAttribsARB_(hdc_, nullptr, contextAttribs);
    }

    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(tempContext);

    if (!hglrc_)
    {
        LogLine("OGL", "wglCreateContextAttribsARB failed, GL 3.3 core not available");
        return false;
    }

    if (!wglMakeCurrent(hdc_, hglrc_))
    {
        wglDeleteContext(hglrc_);
        hglrc_ = nullptr;
        LogLine("OGL", "wglMakeCurrent (final) failed");
        return false;
    }

    if (wglSwapIntervalEXT_)
    {
        wglSwapIntervalEXT_(1);
        LogLine("OGL", "wglSwapIntervalEXT set to 1 (vsync on)");
    }

    return true;
}

bool OpenGLRenderer::LoadGLFunctions()
{
#define LOAD_GL(name, type)                                                        \
    name##_ = reinterpret_cast<type>(wglGetProcAddress(#name));                    \
    if (!name##_)                                                                  \
    {                                                                              \
        LogLine("OGL", "Failed to load GL function: %s", #name);                  \
        return false;                                                              \
    }

    LOAD_GL(glCreateShader, PFNGLCREATESHADERPROC)
    LOAD_GL(glShaderSource, PFNGLSHADERSOURCEPROC)
    LOAD_GL(glCompileShader, PFNGLCOMPILESHADERPROC)
    LOAD_GL(glGetShaderiv, PFNGLGETSHADERIVPROC)
    LOAD_GL(glGetShaderInfoLog, PFNGLGETSHADERINFOLOGPROC)
    LOAD_GL(glCreateProgram, PFNGLCREATEPROGRAMPROC)
    LOAD_GL(glAttachShader, PFNGLATTACHSHADERPROC)
    LOAD_GL(glLinkProgram, PFNGLLINKPROGRAMPROC)
    LOAD_GL(glGetProgramiv, PFNGLGETPROGRAMIVPROC)
    LOAD_GL(glGetProgramInfoLog, PFNGLGETPROGRAMINFOLOGPROC)
    LOAD_GL(glDeleteShader, PFNGLDELETESHADERPROC)
    LOAD_GL(glUseProgram, PFNGLUSEPROGRAMPROC)
    LOAD_GL(glDeleteProgram, PFNGLDELETEPROGRAMPROC)
    LOAD_GL(glGetUniformLocation, PFNGLGETUNIFORMLOCATIONPROC)
    LOAD_GL(glUniformMatrix4fv, PFNGLUNIFORMMATRIX4FVPROC)
    LOAD_GL(glUniform4f, PFNGLUNIFORM4FPROC)
    LOAD_GL(glGenVertexArrays, PFNGLGENVERTEXARRAYSPROC)
    LOAD_GL(glBindVertexArray, PFNGLBINDVERTEXARRAYPROC)
    LOAD_GL(glDeleteVertexArrays, PFNGLDELETEVERTEXARRAYSPROC)
    LOAD_GL(glGenBuffers, PFNGLGENBUFFERSPROC)
    LOAD_GL(glBindBuffer, PFNGLBINDBUFFERPROC)
    LOAD_GL(glBufferData, PFNGLBUFFERDATAPROC)
    LOAD_GL(glDeleteBuffers, PFNGLDELETEBUFFERSPROC)
    LOAD_GL(glEnableVertexAttribArray, PFNGLENABLEVERTEXATTRIBARRAYPROC)
    LOAD_GL(glVertexAttribPointer, PFNGLVERTEXATTRIBPOINTERPROC)

#undef LOAD_GL

    return true;
}

bool OpenGLRenderer::CompileShaders()
{
    auto compileShader = [](GLenum type, const char *source) -> GLuint
    {
        GLuint shader = glCreateShader_(type);
        glShaderSource_(shader, 1, &source, nullptr);
        glCompileShader_(shader);

        GLint success = 0;
        glGetShaderiv_(shader, GL_COMPILE_STATUS, &success);
        if (!success)
        {
            GLint logLen = 0;
            glGetShaderiv_(shader, GL_INFO_LOG_LENGTH, &logLen);
            std::vector<char> log(static_cast<size_t>(logLen) + 1, '\0');
            glGetShaderInfoLog_(shader, logLen, nullptr, log.data());
            LogLine("OGL", "Shader compile error: %s", log.data());
            glDeleteShader_(shader);
            return 0;
        }
        return shader;
    };

    GLuint vs = compileShader(GL_VERTEX_SHADER, kVertexShaderSource);
    if (!vs)
    {
        return false;
    }

    GLuint fs = compileShader(GL_FRAGMENT_SHADER, kFragmentShaderSource);
    if (!fs)
    {
        glDeleteShader_(vs);
        return false;
    }

    sceneProgram_ = glCreateProgram_();
    glAttachShader_(sceneProgram_, vs);
    glAttachShader_(sceneProgram_, fs);
    glLinkProgram_(sceneProgram_);

    glDeleteShader_(vs);
    glDeleteShader_(fs);

    GLint linkSuccess = 0;
    glGetProgramiv_(sceneProgram_, GL_LINK_STATUS, &linkSuccess);
    if (!linkSuccess)
    {
        GLint logLen = 0;
        glGetProgramiv_(sceneProgram_, GL_INFO_LOG_LENGTH, &logLen);
        std::vector<char> log(static_cast<size_t>(logLen) + 1, '\0');
        glGetProgramInfoLog_(sceneProgram_, logLen, nullptr, log.data());
        LogLine("OGL", "Program link error: %s", log.data());
        glDeleteProgram_(sceneProgram_);
        sceneProgram_ = 0;
        return false;
    }

    locWorld_ = glGetUniformLocation_(sceneProgram_, "uWorld");
    locViewProj_ = glGetUniformLocation_(sceneProgram_, "uViewProj");
    locLightDir_ = glGetUniformLocation_(sceneProgram_, "uLightDir");
    locCameraPosTime_ = glGetUniformLocation_(sceneProgram_, "uCameraPosTime");
    locTopicFlags_ = glGetUniformLocation_(sceneProgram_, "uTopicFlags");

    LogLine("OGL", "Shaders compiled, uniforms: world=%d viewProj=%d light=%d camera=%d topic=%d",
            locWorld_, locViewProj_, locLightDir_, locCameraPosTime_, locTopicFlags_);
    return true;
}

void OpenGLRenderer::CreateGeometry()
{
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;
    GenerateTeachingMesh(vertices, indices);
    indexCount_ = static_cast<uint32_t>(indices.size());

    glGenVertexArrays_(1, &vao_);
    glBindVertexArray_(vao_);

    glGenBuffers_(1, &vbo_);
    glBindBuffer_(GL_ARRAY_BUFFER, vbo_);
    glBufferData_(GL_ARRAY_BUFFER,
                  static_cast<GLsizeiptr>(vertices.size() * sizeof(MeshVertex)),
                  vertices.data(),
                  GL_STATIC_DRAW);

    glGenBuffers_(1, &ibo_);
    glBindBuffer_(GL_ELEMENT_ARRAY_BUFFER, ibo_);
    glBufferData_(GL_ELEMENT_ARRAY_BUFFER,
                  static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)),
                  indices.data(),
                  GL_STATIC_DRAW);

    // position: location 0
    glEnableVertexAttribArray_(0);
    glVertexAttribPointer_(0, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex),
                           reinterpret_cast<const void *>(offsetof(MeshVertex, pos)));

    // normal: location 1
    glEnableVertexAttribArray_(1);
    glVertexAttribPointer_(1, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex),
                           reinterpret_cast<const void *>(offsetof(MeshVertex, normal)));

    // color: location 2
    glEnableVertexAttribArray_(2);
    glVertexAttribPointer_(2, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex),
                           reinterpret_cast<const void *>(offsetof(MeshVertex, color)));

    glBindVertexArray_(0);
    LogLine("OGL", "Geometry created: %u vertices, %u indices",
            static_cast<uint32_t>(vertices.size()), indexCount_);
}

void OpenGLRenderer::DestroyGeometry()
{
    if (vao_)
    {
        glDeleteVertexArrays_(1, &vao_);
        vao_ = 0;
    }
    if (vbo_)
    {
        glDeleteBuffers_(1, &vbo_);
        vbo_ = 0;
    }
    if (ibo_)
    {
        glDeleteBuffers_(1, &ibo_);
        ibo_ = 0;
    }
}

void OpenGLRenderer::DestroyShaders()
{
    if (sceneProgram_)
    {
        glDeleteProgram_(sceneProgram_);
        sceneProgram_ = 0;
    }
}

void OpenGLRenderer::UpdateMatrices(const FrameSettings &settings)
{
    float aspect = (height_ > 0) ? static_cast<float>(width_) / static_cast<float>(height_) : 1.0f;
    float proj[16];
    Mat4Perspective(proj, 0.7854f, aspect, 0.1f, 100.0f);

    float eye[3] = {0.0f, 1.4f, -4.5f};
    float center[3] = {0.0f, 0.0f, 0.0f};
    float up[3] = {0.0f, 1.0f, 0.0f};
    float view[16];
    Mat4LookAt(view, eye, center, up);

    float viewProj[16];
    Mat4Multiply(viewProj, proj, view);

    glUniformMatrix4fv_(locViewProj_, 1, GL_FALSE, viewProj);
}

} // namespace dxteaching
