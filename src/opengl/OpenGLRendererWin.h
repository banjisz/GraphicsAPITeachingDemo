#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>

#include "IRendererBackend.h"

namespace dxteaching
{

class OpenGLRenderer : public IRendererBackend
{
public:
    OpenGLRenderer();
    ~OpenGLRenderer() override;

    bool Initialize(HWND hwnd, uint32_t width, uint32_t height) override;
    void Resize(uint32_t width, uint32_t height) override;
    void Render(const FrameSettings &settings) override;
    void Shutdown() override;
    const char *BackendName() const override;

private:
    bool CreateChildWindow();
    bool CreateGLContext();
    bool LoadGLFunctions();
    bool CompileShaders();
    void CreateGeometry();
    void DestroyGeometry();
    void DestroyShaders();
    void UpdateMatrices(const FrameSettings &settings);

    HWND parentHwnd_ = nullptr;
    HWND glHwnd_ = nullptr;
    HDC hdc_ = nullptr;
    HGLRC hglrc_ = nullptr;

    uint32_t width_ = 0;
    uint32_t height_ = 0;

    uint32_t sceneProgram_ = 0;
    uint32_t vao_ = 0;
    uint32_t vbo_ = 0;
    uint32_t ibo_ = 0;
    uint32_t indexCount_ = 0;

    int locWorld_ = -1;
    int locViewProj_ = -1;
    int locLightDir_ = -1;
    int locCameraPosTime_ = -1;
    int locTopicFlags_ = -1;

    static bool classRegistered_;
};

} // namespace dxteaching
