#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <d3d11_1.h>
#include <dxgi.h>
#include <wrl/client.h>

#include "IRendererBackend.h"

namespace dxteaching
{

class DX11Renderer final : public IRendererBackend
{
public:
    DX11Renderer() = default;
    ~DX11Renderer() override;

    bool Initialize(HWND hwnd, uint32_t width, uint32_t height) override;
    void Resize(uint32_t width, uint32_t height) override;
    void Render(const FrameSettings &settings) override;
    void Shutdown() override;
    const char *BackendName() const override;

private:
    bool CreateSwapChainAndDevice();
    bool CreatePipelineShaders();
    bool CreateGeometryBuffers();
    bool CreateMaterialTextures();
    bool CreateConstantBuffers();
    bool CreateStatesAndSamplers();
    bool CreateSizeDependentResources();

    bool CreateBackBufferRTV();
    bool CompileShader(const char *entryPoint,
                       const char *target,
                       Microsoft::WRL::ComPtr<ID3DBlob> &shaderBlob) const;

    void ReleaseSizeDependentResources();
    void UpdateViewports();

    void BeginEvent(const wchar_t *label) const;
    void EndEvent() const;

private:
    HWND hwnd_ = nullptr;
    uint32_t width_ = 1;
    uint32_t height_ = 1;
    uint32_t renderWidth_ = 1;
    uint32_t renderHeight_ = 1;

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGISwapChain> swapChain_;
    Microsoft::WRL::ComPtr<ID3DUserDefinedAnnotation> annotation_;

    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> backBufferRTV_;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> sceneColorMSAATex_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> sceneColorMSAARTV_;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> sceneColorTex_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> sceneColorRTV_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> sceneColorSRV_;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> postColorTex_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> postColorRTV_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> postColorSRV_;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> historyTex_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> historyRTV_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> historySRV_;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> bloomATex_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> bloomARTV_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> bloomASRV_;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> bloomBTex_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> bloomBRTV_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> bloomBSRV_;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> particleTex_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> particleSRV_;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> particleUAV_;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> edgeTex_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> edgeSRV_;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> edgeUAV_;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> albedoTex_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> albedoSRV_;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> normalTex_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> normalSRV_;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> sceneDepthTex_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> sceneDepthDSV_;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> shadowDepthTex_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> shadowDepthDSV_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shadowDepthSRV_;

    Microsoft::WRL::ComPtr<ID3D11VertexShader> sceneVS_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> scenePS_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> shadowVS_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> fullscreenVS_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> brightExtractPS_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> blurHPS_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> blurVPS_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> compositePS_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> copyPS_;
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> particleCS_;
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> edgeDetectCS_;

    Microsoft::WRL::ComPtr<ID3D11InputLayout> sceneInputLayout_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> vertexBuffer_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> indexBuffer_;
    UINT indexCount_ = 0;

    Microsoft::WRL::ComPtr<ID3D11Buffer> sceneCB_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> postCB_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> blurCB_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> particleCB_;

    Microsoft::WRL::ComPtr<ID3D11SamplerState> linearClampSampler_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> pointClampSampler_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> shadowSampler_;

    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> sceneDepthState_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> shadowDepthState_;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> sceneRasterState_;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> shadowRasterState_;

    D3D11_VIEWPORT fullViewport_{};
    D3D11_VIEWPORT halfViewport_{};
    D3D11_VIEWPORT presentViewport_{};
    D3D11_VIEWPORT shadowViewport_{};

    bool historyValid_ = false;
    int historyTopic_ = 0;
};

} // namespace dxteaching
