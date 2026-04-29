#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "DXTeachingShared.h"

namespace dxteaching
{

enum class BackendType
{
    DX11,
    DX12
};

class IRendererBackend
{
public:
    virtual ~IRendererBackend() = default;

    virtual bool Initialize(HWND hwnd, uint32_t width, uint32_t height) = 0;
    virtual void Resize(uint32_t width, uint32_t height) = 0;
    virtual void Render(const FrameSettings &settings) = 0;
    virtual void Shutdown() = 0;
    virtual const char *BackendName() const = 0;
};

} // namespace dxteaching
