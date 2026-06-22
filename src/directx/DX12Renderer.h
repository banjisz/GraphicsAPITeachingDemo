#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>

#include "IRendererBackend.h"

namespace dxteaching
{

class DX12Renderer final : public IRendererBackend
{
public:
    DX12Renderer() = default;
    ~DX12Renderer() override;

    bool Initialize(HWND hwnd, uint32_t width, uint32_t height) override;
    void Resize(uint32_t width, uint32_t height) override;
    void Render(const FrameSettings &settings) override;
    void Shutdown() override;
    const char *BackendName() const override;

private:
    bool CreateDeviceAndCommandQueue(IDXGIFactory4 *factory);
    bool CreateSwapChain(IDXGIFactory4 *factory);
    bool CreateDescriptorHeaps();
    bool CreatePipelineStates();
    bool CreateGeometryResources();
    bool CreateMaterialTextures();
    bool CreateConstantBufferResources();
    bool CreateFrameResources();
    bool CreateSyncObjects();

    void ReleaseFrameResources();
    bool UploadTexture2D(const uint32_t *pixels,
                         Microsoft::WRL::ComPtr<ID3D12Resource> &texture,
                         D3D12_CPU_DESCRIPTOR_HANDLE srvHandle);

    void WaitForGpu();
    void WaitForCurrentFrame();

    void TransitionResource(ID3D12Resource *resource,
                            D3D12_RESOURCE_STATES &currentState,
                            D3D12_RESOURCE_STATES targetState);

    D3D12_CPU_DESCRIPTOR_HANDLE RtvHandle(UINT index) const;
    D3D12_CPU_DESCRIPTOR_HANDLE DsvHandle(UINT index) const;
    D3D12_CPU_DESCRIPTOR_HANDLE SrvUavCpuHandle(UINT index) const;
    D3D12_GPU_DESCRIPTOR_HANDLE SrvUavGpuHandle(UINT index) const;

    UINT64 SceneCBAddress(UINT objectIndex) const;
    UINT64 PostCBAddress() const;
    UINT64 BlurCBAddress() const;
    UINT64 ParticleCBAddress() const;

    void BeginEvent(const char *label);
    void EndEvent();

private:
    static constexpr UINT kMaxObjectCount = 12;
    static constexpr UINT kRtvCount = kSwapChainBufferCount + 5;
    static constexpr UINT kDsvCount = 2;
    static constexpr UINT kSrvUavCount = 12;

    enum RtvIndex : UINT
    {
        kRtvBackBuffer0 = 0,
        kRtvBackBuffer1 = 1,
        kRtvScene = 2,
        kRtvBloomA = 3,
        kRtvBloomB = 4,
        kRtvPost = 5,
        kRtvHistory = 6,
    };

    enum DsvIndex : UINT
    {
        kDsvScene = 0,
        kDsvShadow = 1,
    };

    enum SrvUavIndex : UINT
    {
        kSrvShadow = 0,
        kSrvScene = 1,
        kSrvBloomA = 2,
        kSrvParticle = 3,
        kSrvHistory = 4,
        kSrvBloomB = 5,
        kSrvPost = 6,
        kSrvEdge = 7,
        kSrvAlbedo = 8,
        kSrvNormal = 9,
        kUavParticle = 10,
        kUavEdge = 11,
    };

    HWND hwnd_ = nullptr;
    uint32_t width_ = 1;
    uint32_t height_ = 1;
    uint32_t renderWidth_ = 1;
    uint32_t renderHeight_ = 1;

    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue_;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> swapChain_;

    std::array<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>, kSwapChainBufferCount> commandAllocators_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvUavHeap_;
    UINT rtvDescriptorSize_ = 0;
    UINT dsvDescriptorSize_ = 0;
    UINT srvUavDescriptorSize_ = 0;

    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kSwapChainBufferCount> backBuffers_;
    std::array<D3D12_RESOURCE_STATES, kSwapChainBufferCount> backBufferStates_{};

    Microsoft::WRL::ComPtr<ID3D12Resource> sceneColor_;
    D3D12_RESOURCE_STATES sceneColorState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    Microsoft::WRL::ComPtr<ID3D12Resource> sceneColorMSAA_;
    D3D12_RESOURCE_STATES sceneColorMSAAState_ = D3D12_RESOURCE_STATE_RENDER_TARGET;

    Microsoft::WRL::ComPtr<ID3D12Resource> bloomA_;
    D3D12_RESOURCE_STATES bloomAState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    Microsoft::WRL::ComPtr<ID3D12Resource> bloomB_;
    D3D12_RESOURCE_STATES bloomBState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    Microsoft::WRL::ComPtr<ID3D12Resource> postColor_;
    D3D12_RESOURCE_STATES postColorState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    Microsoft::WRL::ComPtr<ID3D12Resource> history_;
    D3D12_RESOURCE_STATES historyState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    Microsoft::WRL::ComPtr<ID3D12Resource> particle_;
    D3D12_RESOURCE_STATES particleState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    Microsoft::WRL::ComPtr<ID3D12Resource> edge_;
    D3D12_RESOURCE_STATES edgeState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    Microsoft::WRL::ComPtr<ID3D12Resource> albedoTexture_;
    Microsoft::WRL::ComPtr<ID3D12Resource> normalTexture_;

    Microsoft::WRL::ComPtr<ID3D12Resource> sceneDepth_;
    D3D12_RESOURCE_STATES sceneDepthState_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;

    Microsoft::WRL::ComPtr<ID3D12Resource> shadowDepth_;
    D3D12_RESOURCE_STATES shadowDepthState_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> graphicsRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> computeRootSignature_;

    Microsoft::WRL::ComPtr<ID3D12PipelineState> scenePSO_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> tessPSO_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> wireframePSO_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> shadowPSO_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> brightExtractPSO_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> blurHPSO_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> blurVPSO_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> compositePSO_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> copyPSO_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> particleCSPSO_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> edgeDetectCSPSO_;

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    D3D12_INDEX_BUFFER_VIEW indexBufferView_{};
    UINT indexCount_ = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource> sceneConstantBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> postConstantBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> blurConstantBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> particleConstantBuffer_;

    uint8_t *mappedSceneCB_ = nullptr;
    uint8_t *mappedPostCB_ = nullptr;
    uint8_t *mappedBlurCB_ = nullptr;
    uint8_t *mappedParticleCB_ = nullptr;

    UINT sceneCBStride_ = 0;
    UINT postCBStride_ = 0;
    UINT blurCBStride_ = 0;
    UINT particleCBStride_ = 0;
    UINT sceneCBFrameStride_ = 0;

    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    UINT64 fenceValue_ = 0;
    std::array<UINT64, kSwapChainBufferCount> frameFenceValues_{};
    HANDLE fenceEvent_ = nullptr;

    UINT frameIndex_ = 0;
    bool historyValid_ = false;
    int historyTopic_ = 0;
};

} // namespace dxteaching
