#include "DX12Renderer.h"

#include <d3dcompiler.h>

#include <DirectXMath.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <vector>

namespace dxteaching
{

namespace
{

constexpr UINT kShaderCompileFlagsBase = D3DCOMPILE_ENABLE_STRICTNESS;
constexpr UINT kComputeGroupSize = 8;

UINT Align256(UINT value)
{
    return (value + 255u) & ~255u;
}

bool CompileShader(const char *entryPoint,
                   const char *target,
                   Microsoft::WRL::ComPtr<ID3DBlob> &shaderBlob)
{
    UINT compileFlags = kShaderCompileFlagsBase;
#if defined(_DEBUG)
    compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    const char *source = TeachingShaderSource();
    const HRESULT hr = D3DCompile(source,
                                  std::strlen(source),
                                  nullptr,
                                  nullptr,
                                  nullptr,
                                  entryPoint,
                                  target,
                                  compileFlags,
                                  0,
                                  shaderBlob.GetAddressOf(),
                                  errors.GetAddressOf());

    if (FAILED(hr))
    {
        if (errors)
        {
            OutputDebugStringA(static_cast<const char *>(errors->GetBufferPointer()));
        }
        return false;
    }

    return true;
}

void StoreMatrixTransposed(const DirectX::XMMATRIX &matrix, float out[16])
{
    DirectX::XMFLOAT4X4 temp{};
    DirectX::XMStoreFloat4x4(&temp, DirectX::XMMatrixTranspose(matrix));
    std::memcpy(out, &temp, sizeof(temp));
}

DirectX::XMMATRIX BuildWorldMatrix(float timeSeconds,
                                   uint32_t objectIndex,
                                   uint32_t objectCount,
                                   float rotationSpeed)
{
    using namespace DirectX;

    const float center = 0.5f * static_cast<float>(objectCount - 1);
    const float offsetX = (static_cast<float>(objectIndex) - center) * 1.6f;
    const float phase = static_cast<float>(objectIndex) * 0.75f;
    const bool singleObject = objectCount == 1;
    const float scaleValue = singleObject ? 0.95f : 0.65f;
    const float wobbleY = singleObject ? 0.0f : 0.18f * std::sinf(timeSeconds * 1.35f + phase);

    const XMMATRIX scale = XMMatrixScaling(scaleValue, scaleValue, scaleValue);
    const XMMATRIX rotation =
        singleObject
            ? XMMatrixRotationX(XMConvertToRadians(-14.0f) + timeSeconds * 0.32f) *
                  XMMatrixRotationY(XMConvertToRadians(28.0f) + timeSeconds * rotationSpeed)
            : XMMatrixRotationY(timeSeconds * (rotationSpeed + 0.08f * static_cast<float>(objectIndex)) + phase);
    const XMMATRIX wobble = XMMatrixTranslation(offsetX, wobbleY, 0.0f);
    return scale * rotation * wobble;
}

} // namespace

DX12Renderer::~DX12Renderer()
{
    Shutdown();
}

bool DX12Renderer::Initialize(HWND hwnd, uint32_t width, uint32_t height)
{
    Shutdown();

    hwnd_ = hwnd;
    width_ = std::max<uint32_t>(1u, width);
    height_ = std::max<uint32_t>(1u, height);
    renderWidth_ = ScaledRenderDimension(width_);
    renderHeight_ = ScaledRenderDimension(height_);

    UINT factoryFlags = 0;
#if defined(_DEBUG)
    Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(debugController.GetAddressOf()))))
    {
        debugController->EnableDebugLayer();
        factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    }
#endif

    Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
    if (FAILED(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(factory.GetAddressOf()))))
    {
        return false;
    }

    if (!CreateDeviceAndCommandQueue(factory.Get()) ||
        !CreateSwapChain(factory.Get()) ||
        !CreateDescriptorHeaps() ||
        !CreateMaterialTextures() ||
        !CreatePipelineStates() ||
        !CreateGeometryResources() ||
        !CreateConstantBufferResources() ||
        !CreateFrameResources() ||
        !CreateSyncObjects())
    {
        Shutdown();
        return false;
    }

    historyValid_ = false;
    historyTopic_ = 0;
    return true;
}

void DX12Renderer::Resize(uint32_t width, uint32_t height)
{
    if (!swapChain_ || width == 0 || height == 0)
    {
        return;
    }

    width_ = width;
    height_ = height;
    renderWidth_ = ScaledRenderDimension(width_);
    renderHeight_ = ScaledRenderDimension(height_);

    WaitForGpu();
    ReleaseFrameResources();

    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(swapChain_->GetDesc(&desc)))
    {
        return;
    }

    if (FAILED(swapChain_->ResizeBuffers(kSwapChainBufferCount,
                                         width_,
                                         height_,
                                         desc.BufferDesc.Format,
                                         desc.Flags)))
    {
        return;
    }

    frameIndex_ = swapChain_->GetCurrentBackBufferIndex();
    frameFenceValues_.fill(0);

    if (!CreateFrameResources())
    {
        return;
    }

    historyValid_ = false;
    historyTopic_ = 0;
}

void DX12Renderer::Render(const FrameSettings &settings)
{
    if (!device_ ||
        !swapChain_ ||
        !commandList_ ||
        !scenePSO_ ||
        !shadowPSO_ ||
        !brightExtractPSO_ ||
        !blurHPSO_ ||
        !blurVPSO_ ||
        !compositePSO_ ||
        !copyPSO_ ||
        !particleCSPSO_ ||
        !graphicsRootSignature_ ||
        !computeRootSignature_ ||
        !sceneColor_ ||
        !sceneColorMSAA_ ||
        !bloomA_ ||
        !bloomB_ ||
        !postColor_ ||
        !history_ ||
        !particle_ ||
        !edge_ ||
        !albedoTexture_ ||
        !normalTexture_ ||
        !sceneDepth_ ||
        !shadowDepth_ ||
        !edgeDetectCSPSO_)
    {
        return;
    }

    WaitForCurrentFrame();

    if (FAILED(commandAllocators_[frameIndex_]->Reset()))
    {
        return;
    }

    if (FAILED(commandList_->Reset(commandAllocators_[frameIndex_].Get(), nullptr)))
    {
        return;
    }

    const int clampedTopic = ClampTopic(settings.topic);
    const TopicRuntimeProfile profile = BuildTopicRuntimeProfile(clampedTopic, settings.errorExampleEnabled);
    if (historyTopic_ != clampedTopic)
    {
        historyValid_ = false;
        historyTopic_ = clampedTopic;
    }
    const uint32_t objectCount = std::max<uint32_t>(1u, std::min<uint32_t>(profile.objectCount, kMaxObjectCount));

    const float safeWidth = static_cast<float>(std::max<uint32_t>(1u, renderWidth_));
    const float safeHeight = static_cast<float>(std::max<uint32_t>(1u, renderHeight_));
    const float safeHalfWidth = static_cast<float>(std::max<uint32_t>(1u, renderWidth_ / 2u));
    const float safeHalfHeight = static_cast<float>(std::max<uint32_t>(1u, renderHeight_ / 2u));
    const uint32_t particleWidth = std::max<uint32_t>(1u, renderWidth_ / 2u);
    const uint32_t particleHeight = std::max<uint32_t>(1u, renderHeight_ / 2u);

    using namespace DirectX;

    const XMVECTOR eye = XMVectorSet(0.0f, 0.7f, -5.2f, 1.0f);
    const XMVECTOR target = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
    const XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    const XMMATRIX view = XMMatrixLookAtLH(eye, target, up);
    const XMMATRIX projection = XMMatrixPerspectiveFovLH(XMConvertToRadians(65.0f), safeWidth / safeHeight, 0.1f, 120.0f);
    const XMMATRIX viewProj = view * projection;

    const XMVECTOR lightPosition = XMVectorSet(-6.5f, 7.0f, -4.5f, 1.0f);
    const XMVECTOR lightTarget = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
    const XMVECTOR lightDirection = XMVector3Normalize(lightTarget - lightPosition);
    const XMMATRIX lightView = XMMatrixLookAtLH(lightPosition, lightTarget, up);
    const XMMATRIX lightProjection = XMMatrixOrthographicLH(18.0f, 18.0f, 0.1f, 35.0f);
    const XMMATRIX lightViewProj = lightView * lightProjection;

    DirectX::XMFLOAT3 cameraPos{};
    DirectX::XMFLOAT3 lightDir{};
    XMStoreFloat3(&cameraPos, eye);
    XMStoreFloat3(&lightDir, lightDirection);

    const float temporalBlend = (profile.enableTemporal && historyValid_) ? profile.temporalBlend : 0.0f;

    PostConstantBuffer postCB{};
    postCB.postParams0[0] = profile.exposure;
    postCB.postParams0[1] = profile.enableBloom ? profile.bloomStrength : 0.0f;
    postCB.postParams0[2] = profile.enableParticles ? profile.particleStrength : 0.0f;
    postCB.postParams0[3] = temporalBlend;
    postCB.postParams1[0] = static_cast<float>(clampedTopic);
    postCB.postParams1[1] = settings.elapsedSeconds;
    postCB.postParams1[2] = profile.edgeStrength;
    postCB.postParams1[3] = profile.brightThreshold;
    std::memcpy(mappedPostCB_ + static_cast<size_t>(frameIndex_) * static_cast<size_t>(postCBStride_), &postCB, sizeof(postCB));

    ParticleConstantBuffer particleCB{};
    particleCB.particleParams0[0] = settings.elapsedSeconds;
    particleCB.particleParams0[1] = static_cast<float>(clampedTopic);
    particleCB.particleParams0[2] = settings.errorExampleEnabled ? 1.0f : 0.0f;
    particleCB.particleParams0[3] = profile.enableParticles ? profile.particleStrength : 0.0f;
    particleCB.particleParams1[0] = static_cast<float>(particleWidth);
    particleCB.particleParams1[1] = static_cast<float>(particleHeight);
    particleCB.particleParams1[2] = 1.0f / static_cast<float>(particleWidth);
    particleCB.particleParams1[3] = 1.0f / static_cast<float>(particleHeight);
    std::memcpy(mappedParticleCB_ + static_cast<size_t>(frameIndex_) * static_cast<size_t>(particleCBStride_), &particleCB, sizeof(particleCB));

    auto uploadSceneCB = [&](UINT objectIndex, const XMMATRIX &world) {
        SceneConstantBuffer sceneCB{};
        StoreMatrixTransposed(world, sceneCB.world);
        StoreMatrixTransposed(viewProj, sceneCB.viewProj);
        StoreMatrixTransposed(lightViewProj, sceneCB.lightViewProj);

        sceneCB.lightDirAndStrength[0] = lightDir.x;
        sceneCB.lightDirAndStrength[1] = lightDir.y;
        sceneCB.lightDirAndStrength[2] = lightDir.z;
        sceneCB.lightDirAndStrength[3] = profile.useShadow ? profile.shadowStrength : 0.0f;

        sceneCB.cameraPosAndTime[0] = cameraPos.x;
        sceneCB.cameraPosAndTime[1] = cameraPos.y;
        sceneCB.cameraPosAndTime[2] = cameraPos.z;
        sceneCB.cameraPosAndTime[3] = settings.elapsedSeconds;

        sceneCB.topicAndFlags[0] = static_cast<float>(clampedTopic);
        sceneCB.topicAndFlags[1] = settings.errorExampleEnabled ? 1.0f : 0.0f;
        sceneCB.topicAndFlags[2] = profile.usePBR ? 1.0f : 0.0f;
        sceneCB.topicAndFlags[3] = profile.useShadow ? 1.0f : 0.0f;

        sceneCB.sceneParams[0] = profile.exposure;
        sceneCB.sceneParams[1] = profile.bloomStrength;
        sceneCB.sceneParams[2] = profile.particleStrength;
        sceneCB.sceneParams[3] = profile.edgeStrength;

        sceneCB.screenInfo[0] = safeWidth;
        sceneCB.screenInfo[1] = safeHeight;
        sceneCB.screenInfo[2] = 1.0f / safeWidth;
        sceneCB.screenInfo[3] = static_cast<float>(objectCount);

        uint8_t *dst = mappedSceneCB_ +
                       static_cast<size_t>(frameIndex_) * static_cast<size_t>(sceneCBFrameStride_) +
                       static_cast<size_t>(objectIndex) * static_cast<size_t>(sceneCBStride_);
        std::memcpy(dst, &sceneCB, sizeof(sceneCB));
    };

    D3D12_VIEWPORT fullViewport{};
    fullViewport.TopLeftX = 0.0f;
    fullViewport.TopLeftY = 0.0f;
    fullViewport.Width = safeWidth;
    fullViewport.Height = safeHeight;
    fullViewport.MinDepth = 0.0f;
    fullViewport.MaxDepth = 1.0f;

    D3D12_VIEWPORT halfViewport{};
    halfViewport.TopLeftX = 0.0f;
    halfViewport.TopLeftY = 0.0f;
    halfViewport.Width = safeHalfWidth;
    halfViewport.Height = safeHalfHeight;
    halfViewport.MinDepth = 0.0f;
    halfViewport.MaxDepth = 1.0f;

    D3D12_VIEWPORT shadowViewport{};
    shadowViewport.TopLeftX = 0.0f;
    shadowViewport.TopLeftY = 0.0f;
    shadowViewport.Width = static_cast<float>(kShadowMapSize);
    shadowViewport.Height = static_cast<float>(kShadowMapSize);
    shadowViewport.MinDepth = 0.0f;
    shadowViewport.MaxDepth = 1.0f;

    D3D12_RECT fullScissor{0, 0, static_cast<LONG>(renderWidth_), static_cast<LONG>(renderHeight_)};
    D3D12_RECT halfScissor{0, 0, static_cast<LONG>(std::max<uint32_t>(1u, renderWidth_ / 2u)), static_cast<LONG>(std::max<uint32_t>(1u, renderHeight_ / 2u))};
    D3D12_RECT shadowScissor{0, 0, static_cast<LONG>(kShadowMapSize), static_cast<LONG>(kShadowMapSize)};
    D3D12_VIEWPORT presentViewport{};
    presentViewport.TopLeftX = 0.0f;
    presentViewport.TopLeftY = 0.0f;
    presentViewport.Width = static_cast<float>(width_);
    presentViewport.Height = static_cast<float>(height_);
    presentViewport.MinDepth = 0.0f;
    presentViewport.MaxDepth = 1.0f;
    D3D12_RECT presentScissor{0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_)};

    ID3D12DescriptorHeap *heaps[] = {srvUavHeap_.Get()};
    commandList_->SetDescriptorHeaps(1, heaps);

    commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList_->IASetVertexBuffers(0, 1, &vertexBufferView_);
    commandList_->IASetIndexBuffer(&indexBufferView_);

    BeginEvent("Shadow Pass");
    {
        TransitionResource(shadowDepth_.Get(), shadowDepthState_, D3D12_RESOURCE_STATE_DEPTH_WRITE);

        commandList_->RSSetViewports(1, &shadowViewport);
        commandList_->RSSetScissorRects(1, &shadowScissor);

        const D3D12_CPU_DESCRIPTOR_HANDLE shadowDsv = DsvHandle(kDsvShadow);
        commandList_->OMSetRenderTargets(0, nullptr, FALSE, &shadowDsv);
        commandList_->ClearDepthStencilView(shadowDsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

        commandList_->SetGraphicsRootSignature(graphicsRootSignature_.Get());
        commandList_->SetGraphicsRootDescriptorTable(3, SrvUavGpuHandle(kSrvShadow));
        commandList_->SetPipelineState(shadowPSO_.Get());

        for (uint32_t i = 0; i < objectCount; ++i)
        {
            uploadSceneCB(i, BuildWorldMatrix(settings.elapsedSeconds, i, objectCount, profile.rotationSpeed));
            commandList_->SetGraphicsRootConstantBufferView(0, SceneCBAddress(i));
            commandList_->DrawIndexedInstanced(indexCount_, 1, 0, 0, 0);
        }
    }
    EndEvent();

    BeginEvent("Scene HDR Pass");
    {
        TransitionResource(sceneColorMSAA_.Get(), sceneColorMSAAState_, D3D12_RESOURCE_STATE_RENDER_TARGET);
        TransitionResource(sceneDepth_.Get(), sceneDepthState_, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        TransitionResource(shadowDepth_.Get(), shadowDepthState_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        commandList_->RSSetViewports(1, &fullViewport);
        commandList_->RSSetScissorRects(1, &fullScissor);

        const D3D12_CPU_DESCRIPTOR_HANDLE sceneRtv = RtvHandle(kRtvScene);
        const D3D12_CPU_DESCRIPTOR_HANDLE sceneDsv = DsvHandle(kDsvScene);
        commandList_->OMSetRenderTargets(1, &sceneRtv, FALSE, &sceneDsv);

        float clearColor[4] = {0.08f, 0.10f, 0.12f, 1.0f};
        ComputeClearColor(settings, clearColor);
        commandList_->ClearRenderTargetView(sceneRtv, clearColor, 0, nullptr);
        commandList_->ClearDepthStencilView(sceneDsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

        commandList_->SetGraphicsRootSignature(graphicsRootSignature_.Get());
        commandList_->SetGraphicsRootDescriptorTable(3, SrvUavGpuHandle(kSrvShadow));

        bool useTessellation = profile.usePBR;
        if (useTessellation)
        {
            commandList_->SetPipelineState(tessPSO_.Get());
            commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
        }
        else
        {
            commandList_->SetPipelineState(scenePSO_.Get());
        }

        for (uint32_t i = 0; i < objectCount; ++i)
        {
            uploadSceneCB(i, BuildWorldMatrix(settings.elapsedSeconds, i, objectCount, profile.rotationSpeed));
            commandList_->SetGraphicsRootConstantBufferView(0, SceneCBAddress(i));
            commandList_->DrawIndexedInstanced(indexCount_, 1, 0, 0, 0);
        }

        if (useTessellation)
        {
            commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        }

        TransitionResource(shadowDepth_.Get(), shadowDepthState_, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    }
    EndEvent();

    BeginEvent("Scene MSAA Resolve");
    {
        TransitionResource(sceneColorMSAA_.Get(), sceneColorMSAAState_, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
        TransitionResource(sceneColor_.Get(), sceneColorState_, D3D12_RESOURCE_STATE_RESOLVE_DEST);
        commandList_->ResolveSubresource(sceneColor_.Get(),
                                         0,
                                         sceneColorMSAA_.Get(),
                                         0,
                                         DXGI_FORMAT_R16G16B16A16_FLOAT);
        TransitionResource(sceneColor_.Get(), sceneColorState_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    EndEvent();

    BeginEvent("Particle Compute");
    {
        TransitionResource(particle_.Get(), particleState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        commandList_->SetComputeRootSignature(computeRootSignature_.Get());
        commandList_->SetPipelineState(particleCSPSO_.Get());
        commandList_->SetComputeRootConstantBufferView(0, ParticleCBAddress());
        commandList_->SetComputeRootDescriptorTable(1, SrvUavGpuHandle(0));

        const UINT groupsX = (particleWidth + (kComputeGroupSize - 1)) / kComputeGroupSize;
        const UINT groupsY = (particleHeight + (kComputeGroupSize - 1)) / kComputeGroupSize;
        commandList_->Dispatch(groupsX, groupsY, 1);

        TransitionResource(particle_.Get(), particleState_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    EndEvent();

    BeginEvent("Edge Detect Compute");
    {
        TransitionResource(edge_.Get(), edgeState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        commandList_->SetComputeRootSignature(computeRootSignature_.Get());
        commandList_->SetPipelineState(edgeDetectCSPSO_.Get());
        commandList_->SetComputeRootConstantBufferView(0, ParticleCBAddress());
        commandList_->SetComputeRootDescriptorTable(1, SrvUavGpuHandle(0));

        const UINT groupsX = (renderWidth_ + (kComputeGroupSize - 1)) / kComputeGroupSize;
        const UINT groupsY = (renderHeight_ + (kComputeGroupSize - 1)) / kComputeGroupSize;
        commandList_->Dispatch(groupsX, groupsY, 1);

        TransitionResource(edge_.Get(), edgeState_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    EndEvent();

    const float zeroColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    if (profile.enableBloom)
    {
        BeginEvent("Bloom Extract");
        {
            TransitionResource(bloomA_.Get(), bloomAState_, D3D12_RESOURCE_STATE_RENDER_TARGET);

            commandList_->RSSetViewports(1, &halfViewport);
            commandList_->RSSetScissorRects(1, &halfScissor);

            const D3D12_CPU_DESCRIPTOR_HANDLE bloomArtv = RtvHandle(kRtvBloomA);
            commandList_->OMSetRenderTargets(1, &bloomArtv, FALSE, nullptr);
            commandList_->ClearRenderTargetView(bloomArtv, zeroColor, 0, nullptr);

            commandList_->SetGraphicsRootSignature(graphicsRootSignature_.Get());
            commandList_->SetGraphicsRootDescriptorTable(3, SrvUavGpuHandle(kSrvShadow));
            commandList_->SetGraphicsRootConstantBufferView(1, PostCBAddress());
            commandList_->SetPipelineState(brightExtractPSO_.Get());
            commandList_->DrawInstanced(3, 1, 0, 0);

            TransitionResource(bloomA_.Get(), bloomAState_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }
        EndEvent();

        const uint32_t blurPasses = std::max<uint32_t>(1u, std::min<uint32_t>(profile.blurPassCount, 6u));
        for (uint32_t i = 0; i < blurPasses; ++i)
        {
            BeginEvent("Bloom Blur H");
            {
                BlurConstantBuffer blurCB{};
                blurCB.blurParams[0] = 1.0f / safeHalfWidth;
                blurCB.blurParams[1] = 1.0f / safeHalfHeight;
                blurCB.blurParams[2] = 1.0f;
                blurCB.blurParams[3] = 0.0f;
                std::memcpy(mappedBlurCB_ + static_cast<size_t>(frameIndex_) * static_cast<size_t>(blurCBStride_), &blurCB, sizeof(blurCB));

                TransitionResource(bloomB_.Get(), bloomBState_, D3D12_RESOURCE_STATE_RENDER_TARGET);

                commandList_->RSSetViewports(1, &halfViewport);
                commandList_->RSSetScissorRects(1, &halfScissor);

                const D3D12_CPU_DESCRIPTOR_HANDLE bloomBrtv = RtvHandle(kRtvBloomB);
                commandList_->OMSetRenderTargets(1, &bloomBrtv, FALSE, nullptr);
                commandList_->ClearRenderTargetView(bloomBrtv, zeroColor, 0, nullptr);

                commandList_->SetGraphicsRootSignature(graphicsRootSignature_.Get());
                commandList_->SetGraphicsRootDescriptorTable(3, SrvUavGpuHandle(kSrvShadow));
                commandList_->SetGraphicsRootConstantBufferView(2, BlurCBAddress());
                commandList_->SetPipelineState(blurHPSO_.Get());
                commandList_->DrawInstanced(3, 1, 0, 0);

                TransitionResource(bloomB_.Get(), bloomBState_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            }
            EndEvent();

            BeginEvent("Bloom Blur V");
            {
                BlurConstantBuffer blurCB{};
                blurCB.blurParams[0] = 1.0f / safeHalfWidth;
                blurCB.blurParams[1] = 1.0f / safeHalfHeight;
                blurCB.blurParams[2] = 0.0f;
                blurCB.blurParams[3] = 1.0f;
                std::memcpy(mappedBlurCB_ + static_cast<size_t>(frameIndex_) * static_cast<size_t>(blurCBStride_), &blurCB, sizeof(blurCB));

                TransitionResource(bloomA_.Get(), bloomAState_, D3D12_RESOURCE_STATE_RENDER_TARGET);

                commandList_->RSSetViewports(1, &halfViewport);
                commandList_->RSSetScissorRects(1, &halfScissor);

                const D3D12_CPU_DESCRIPTOR_HANDLE bloomArtv = RtvHandle(kRtvBloomA);
                commandList_->OMSetRenderTargets(1, &bloomArtv, FALSE, nullptr);

                commandList_->SetGraphicsRootSignature(graphicsRootSignature_.Get());
                commandList_->SetGraphicsRootDescriptorTable(3, SrvUavGpuHandle(kSrvShadow));
                commandList_->SetGraphicsRootConstantBufferView(2, BlurCBAddress());
                commandList_->SetPipelineState(blurVPSO_.Get());
                commandList_->DrawInstanced(3, 1, 0, 0);

                TransitionResource(bloomA_.Get(), bloomAState_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            }
            EndEvent();
        }
    }
    else
    {
        TransitionResource(bloomA_.Get(), bloomAState_, D3D12_RESOURCE_STATE_RENDER_TARGET);
        const D3D12_CPU_DESCRIPTOR_HANDLE bloomArtv = RtvHandle(kRtvBloomA);
        commandList_->ClearRenderTargetView(bloomArtv, zeroColor, 0, nullptr);
        TransitionResource(bloomA_.Get(), bloomAState_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    BeginEvent("Composite Pass");
    {
        TransitionResource(postColor_.Get(), postColorState_, D3D12_RESOURCE_STATE_RENDER_TARGET);

        commandList_->RSSetViewports(1, &fullViewport);
        commandList_->RSSetScissorRects(1, &fullScissor);

        const D3D12_CPU_DESCRIPTOR_HANDLE postRtv = RtvHandle(kRtvPost);
        commandList_->OMSetRenderTargets(1, &postRtv, FALSE, nullptr);
        commandList_->ClearRenderTargetView(postRtv, zeroColor, 0, nullptr);

        commandList_->SetGraphicsRootSignature(graphicsRootSignature_.Get());
        commandList_->SetGraphicsRootDescriptorTable(3, SrvUavGpuHandle(kSrvShadow));
        commandList_->SetGraphicsRootConstantBufferView(1, PostCBAddress());
        commandList_->SetPipelineState(compositePSO_.Get());
        commandList_->DrawInstanced(3, 1, 0, 0);

        TransitionResource(postColor_.Get(), postColorState_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    EndEvent();

    BeginEvent("History Copy");
    {
        TransitionResource(history_.Get(), historyState_, D3D12_RESOURCE_STATE_COPY_DEST);
        TransitionResource(postColor_.Get(), postColorState_, D3D12_RESOURCE_STATE_COPY_SOURCE);
        commandList_->CopyResource(history_.Get(), postColor_.Get());
        TransitionResource(postColor_.Get(), postColorState_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        TransitionResource(history_.Get(), historyState_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        historyValid_ = true;
    }
    EndEvent();

    BeginEvent("Present Pass");
    {
        TransitionResource(backBuffers_[frameIndex_].Get(), backBufferStates_[frameIndex_], D3D12_RESOURCE_STATE_RENDER_TARGET);

        commandList_->RSSetViewports(1, &presentViewport);
        commandList_->RSSetScissorRects(1, &presentScissor);

        const D3D12_CPU_DESCRIPTOR_HANDLE backRtv = RtvHandle(frameIndex_ == 0 ? kRtvBackBuffer0 : kRtvBackBuffer1);
        commandList_->OMSetRenderTargets(1, &backRtv, FALSE, nullptr);

        commandList_->SetGraphicsRootSignature(graphicsRootSignature_.Get());
        commandList_->SetGraphicsRootDescriptorTable(3, SrvUavGpuHandle(kSrvShadow));
        commandList_->SetPipelineState(copyPSO_.Get());
        commandList_->DrawInstanced(3, 1, 0, 0);

        TransitionResource(backBuffers_[frameIndex_].Get(), backBufferStates_[frameIndex_], D3D12_RESOURCE_STATE_PRESENT);
    }
    EndEvent();

    if (FAILED(commandList_->Close()))
    {
        return;
    }

    ID3D12CommandList *lists[] = {commandList_.Get()};
    commandQueue_->ExecuteCommandLists(1, lists);

    if (FAILED(swapChain_->Present(1, 0)))
    {
        return;
    }

    const UINT64 signalValue = ++fenceValue_;
    if (FAILED(commandQueue_->Signal(fence_.Get(), signalValue)))
    {
        return;
    }

    frameFenceValues_[frameIndex_] = signalValue;
    frameIndex_ = swapChain_->GetCurrentBackBufferIndex();
}

void DX12Renderer::Shutdown()
{
    WaitForGpu();

    if (sceneConstantBuffer_ && mappedSceneCB_)
    {
        sceneConstantBuffer_->Unmap(0, nullptr);
    }
    if (postConstantBuffer_ && mappedPostCB_)
    {
        postConstantBuffer_->Unmap(0, nullptr);
    }
    if (blurConstantBuffer_ && mappedBlurCB_)
    {
        blurConstantBuffer_->Unmap(0, nullptr);
    }
    if (particleConstantBuffer_ && mappedParticleCB_)
    {
        particleConstantBuffer_->Unmap(0, nullptr);
    }

    mappedSceneCB_ = nullptr;
    mappedPostCB_ = nullptr;
    mappedBlurCB_ = nullptr;
    mappedParticleCB_ = nullptr;

    if (fenceEvent_)
    {
        CloseHandle(fenceEvent_);
        fenceEvent_ = nullptr;
    }

    ReleaseFrameResources();

    particleConstantBuffer_.Reset();
    blurConstantBuffer_.Reset();
    postConstantBuffer_.Reset();
    sceneConstantBuffer_.Reset();

    indexBuffer_.Reset();
    vertexBuffer_.Reset();

    normalTexture_.Reset();
    albedoTexture_.Reset();

    edgeDetectCSPSO_.Reset();
    particleCSPSO_.Reset();
    copyPSO_.Reset();
    compositePSO_.Reset();
    blurVPSO_.Reset();
    blurHPSO_.Reset();
    brightExtractPSO_.Reset();
    shadowPSO_.Reset();
    scenePSO_.Reset();

    computeRootSignature_.Reset();
    graphicsRootSignature_.Reset();

    for (auto &allocator : commandAllocators_)
    {
        allocator.Reset();
    }
    commandList_.Reset();

    srvUavHeap_.Reset();
    dsvHeap_.Reset();
    rtvHeap_.Reset();

    fence_.Reset();
    swapChain_.Reset();
    commandQueue_.Reset();
    device_.Reset();

    frameFenceValues_.fill(0);
    backBufferStates_.fill(D3D12_RESOURCE_STATE_PRESENT);
    fenceValue_ = 0;
    frameIndex_ = 0;

    sceneCBStride_ = 0;
    postCBStride_ = 0;
    blurCBStride_ = 0;
    particleCBStride_ = 0;
    sceneCBFrameStride_ = 0;

    rtvDescriptorSize_ = 0;
    dsvDescriptorSize_ = 0;
    srvUavDescriptorSize_ = 0;

    historyValid_ = false;
    historyTopic_ = 0;

    hwnd_ = nullptr;
    width_ = 1;
    height_ = 1;
    renderWidth_ = 1;
    renderHeight_ = 1;
}

const char *DX12Renderer::BackendName() const
{
    return "DX12";
}

bool DX12Renderer::CreateDeviceAndCommandQueue(IDXGIFactory4 *factory)
{
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;

    for (UINT adapterIndex = 0;; ++adapterIndex)
    {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> candidate;
        if (factory->EnumAdapters1(adapterIndex, candidate.GetAddressOf()) == DXGI_ERROR_NOT_FOUND)
        {
            break;
        }

        DXGI_ADAPTER_DESC1 desc{};
        if (FAILED(candidate->GetDesc1(&desc)))
        {
            continue;
        }

        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
        {
            continue;
        }

        if (SUCCEEDED(D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
        {
            adapter = candidate;
            break;
        }
    }

    if (!adapter)
    {
        if (FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(adapter.GetAddressOf()))))
        {
            return false;
        }
    }

    if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(device_.GetAddressOf()))))
    {
        return false;
    }

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

    if (FAILED(device_->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(commandQueue_.GetAddressOf()))))
    {
        return false;
    }

    for (UINT i = 0; i < kSwapChainBufferCount; ++i)
    {
        if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                   IID_PPV_ARGS(commandAllocators_[i].GetAddressOf()))))
        {
            return false;
        }
    }

    if (FAILED(device_->CreateCommandList(0,
                                          D3D12_COMMAND_LIST_TYPE_DIRECT,
                                          commandAllocators_[0].Get(),
                                          nullptr,
                                          IID_PPV_ARGS(commandList_.GetAddressOf()))))
    {
        return false;
    }

    return SUCCEEDED(commandList_->Close());
}

bool DX12Renderer::CreateSwapChain(IDXGIFactory4 *factory)
{
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.BufferCount = kSwapChainBufferCount;
    desc.Width = width_;
    desc.Height = height_;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.SampleDesc.Count = 1;

    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain1;
    if (FAILED(factory->CreateSwapChainForHwnd(commandQueue_.Get(),
                                               hwnd_,
                                               &desc,
                                               nullptr,
                                               nullptr,
                                               swapChain1.GetAddressOf())))
    {
        return false;
    }

    if (FAILED(factory->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER)))
    {
        return false;
    }

    if (FAILED(swapChain1.As(&swapChain_)))
    {
        return false;
    }

    frameIndex_ = swapChain_->GetCurrentBackBufferIndex();
    return true;
}

bool DX12Renderer::CreateDescriptorHeaps()
{
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
    rtvDesc.NumDescriptors = kRtvCount;
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    if (FAILED(device_->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(rtvHeap_.GetAddressOf()))))
    {
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC dsvDesc{};
    dsvDesc.NumDescriptors = kDsvCount;
    dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    if (FAILED(device_->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(dsvHeap_.GetAddressOf()))))
    {
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};
    srvDesc.NumDescriptors = kSrvUavCount;
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    if (FAILED(device_->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(srvUavHeap_.GetAddressOf()))))
    {
        return false;
    }

    rtvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    dsvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    srvUavDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    return true;
}

bool DX12Renderer::UploadTexture2D(const uint32_t *pixels,
                                   Microsoft::WRL::ComPtr<ID3D12Resource> &texture,
                                   D3D12_CPU_DESCRIPTOR_HANDLE srvHandle)
{
    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC textureDesc{};
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDesc.Width = kMaterialTextureSize;
    textureDesc.Height = kMaterialTextureSize;
    textureDesc.DepthOrArraySize = 1;
    textureDesc.MipLevels = 1;
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    if (FAILED(device_->CreateCommittedResource(&defaultHeap,
                                                D3D12_HEAP_FLAG_NONE,
                                                &textureDesc,
                                                D3D12_RESOURCE_STATE_COPY_DEST,
                                                nullptr,
                                                IID_PPV_ARGS(texture.GetAddressOf()))))
    {
        return false;
    }

    const UINT64 rowPitch = (static_cast<UINT64>(kMaterialTextureSize) * sizeof(uint32_t) + 255ull) & ~255ull;
    const UINT64 uploadSize = rowPitch * kMaterialTextureSize;

    D3D12_HEAP_PROPERTIES uploadHeap{};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC uploadDesc{};
    uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadDesc.Width = uploadSize;
    uploadDesc.Height = 1;
    uploadDesc.DepthOrArraySize = 1;
    uploadDesc.MipLevels = 1;
    uploadDesc.Format = DXGI_FORMAT_UNKNOWN;
    uploadDesc.SampleDesc.Count = 1;
    uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    Microsoft::WRL::ComPtr<ID3D12Resource> upload;
    if (FAILED(device_->CreateCommittedResource(&uploadHeap,
                                                D3D12_HEAP_FLAG_NONE,
                                                &uploadDesc,
                                                D3D12_RESOURCE_STATE_GENERIC_READ,
                                                nullptr,
                                                IID_PPV_ARGS(upload.GetAddressOf()))))
    {
        return false;
    }

    uint8_t *mapped = nullptr;
    D3D12_RANGE readRange{0, 0};
    if (FAILED(upload->Map(0, &readRange, reinterpret_cast<void **>(&mapped))))
    {
        return false;
    }

    for (uint32_t y = 0; y < kMaterialTextureSize; ++y)
    {
        std::memcpy(mapped + static_cast<size_t>(y) * static_cast<size_t>(rowPitch),
                    pixels + static_cast<size_t>(y) * kMaterialTextureSize,
                    kMaterialTextureSize * sizeof(uint32_t));
    }
    upload->Unmap(0, nullptr);

    if (FAILED(commandAllocators_[0]->Reset()) ||
        FAILED(commandList_->Reset(commandAllocators_[0].Get(), nullptr)))
    {
        return false;
    }

    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = texture.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = upload.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Offset = 0;
    src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    src.PlacedFootprint.Footprint.Width = kMaterialTextureSize;
    src.PlacedFootprint.Footprint.Height = kMaterialTextureSize;
    src.PlacedFootprint.Footprint.Depth = 1;
    src.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(rowPitch);

    commandList_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = texture.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList_->ResourceBarrier(1, &barrier);

    if (FAILED(commandList_->Close()))
    {
        return false;
    }

    ID3D12CommandList *lists[] = {commandList_.Get()};
    commandQueue_->ExecuteCommandLists(1, lists);

    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    if (FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.GetAddressOf()))))
    {
        return false;
    }

    HANDLE eventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!eventHandle)
    {
        return false;
    }

    constexpr UINT64 signalValue = 1;
    const HRESULT signalHr = commandQueue_->Signal(fence.Get(), signalValue);
    const HRESULT eventHr = fence->SetEventOnCompletion(signalValue, eventHandle);
    if (SUCCEEDED(signalHr) && SUCCEEDED(eventHr))
    {
        WaitForSingleObject(eventHandle, INFINITE);
    }
    CloseHandle(eventHandle);

    if (FAILED(signalHr) || FAILED(eventHr))
    {
        return false;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(texture.Get(), &srvDesc, srvHandle);
    return true;
}

bool DX12Renderer::CreateMaterialTextures()
{
    const auto albedo = BuildMaterialAlbedoTexels();
    const auto normal = BuildMaterialNormalTexels();
    return UploadTexture2D(albedo.data(), albedoTexture_, SrvUavCpuHandle(kSrvAlbedo)) &&
           UploadTexture2D(normal.data(), normalTexture_, SrvUavCpuHandle(kSrvNormal));
}

bool DX12Renderer::CreatePipelineStates()
{
    Microsoft::WRL::ComPtr<ID3DBlob> sceneVS;
    Microsoft::WRL::ComPtr<ID3DBlob> scenePS;
    Microsoft::WRL::ComPtr<ID3DBlob> shadowVS;
    Microsoft::WRL::ComPtr<ID3DBlob> fullscreenVS;
    Microsoft::WRL::ComPtr<ID3DBlob> brightPS;
    Microsoft::WRL::ComPtr<ID3DBlob> blurHPS;
    Microsoft::WRL::ComPtr<ID3DBlob> blurVPS;
    Microsoft::WRL::ComPtr<ID3DBlob> compositePS;
    Microsoft::WRL::ComPtr<ID3DBlob> copyPS;
    Microsoft::WRL::ComPtr<ID3DBlob> particleCS;
    Microsoft::WRL::ComPtr<ID3DBlob> edgeCS;
    Microsoft::WRL::ComPtr<ID3DBlob> tessVS;
    Microsoft::WRL::ComPtr<ID3DBlob> tessHS;
    Microsoft::WRL::ComPtr<ID3DBlob> tessDS;
    Microsoft::WRL::ComPtr<ID3DBlob> wireframeGS;
    Microsoft::WRL::ComPtr<ID3DBlob> wireframePS;

    if (!CompileShader("SceneVS", "vs_5_0", sceneVS) ||
        !CompileShader("ScenePS", "ps_5_0", scenePS) ||
        !CompileShader("ShadowVS", "vs_5_0", shadowVS) ||
        !CompileShader("FullscreenVS", "vs_5_0", fullscreenVS) ||
        !CompileShader("BrightExtractPS", "ps_5_0", brightPS) ||
        !CompileShader("BlurHPS", "ps_5_0", blurHPS) ||
        !CompileShader("BlurVPS", "ps_5_0", blurVPS) ||
        !CompileShader("CompositePS", "ps_5_0", compositePS) ||
        !CompileShader("CopyPS", "ps_5_0", copyPS) ||
        !CompileShader("ParticleCS", "cs_5_0", particleCS) ||
        !CompileShader("EdgeDetectCS", "cs_5_0", edgeCS) ||
        !CompileShader("TessVS", "vs_5_0", tessVS) ||
        !CompileShader("TessHS", "hs_5_0", tessHS) ||
        !CompileShader("TessDS", "ds_5_0", tessDS) ||
        !CompileShader("WireframeGS", "gs_5_0", wireframeGS) ||
        !CompileShader("WireframePS", "ps_5_0", wireframePS))
    {
        return false;
    }

    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 10;
    srvRange.BaseShaderRegister = 0;
    srvRange.RegisterSpace = 0;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[4] = {};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].Descriptor.ShaderRegister = 0;
    rootParameters[0].Descriptor.RegisterSpace = 0;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[1].Descriptor.ShaderRegister = 1;
    rootParameters[1].Descriptor.RegisterSpace = 0;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[2].Descriptor.ShaderRegister = 2;
    rootParameters[2].Descriptor.RegisterSpace = 0;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[3].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[3].DescriptorTable.pDescriptorRanges = &srvRange;
    rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC staticSamplers[3] = {};
    staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[0].MipLODBias = 0.0f;
    staticSamplers[0].MaxAnisotropy = 1;
    staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    staticSamplers[0].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
    staticSamplers[0].MinLOD = 0.0f;
    staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[0].ShaderRegister = 0;
    staticSamplers[0].RegisterSpace = 0;
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    staticSamplers[1] = staticSamplers[0];
    staticSamplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    staticSamplers[1].ShaderRegister = 1;

    staticSamplers[2] = staticSamplers[0];
    staticSamplers[2].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    staticSamplers[2].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    staticSamplers[2].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    staticSamplers[2].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    staticSamplers[2].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    staticSamplers[2].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    staticSamplers[2].ShaderRegister = 2;

    D3D12_ROOT_SIGNATURE_DESC graphicsRootDesc{};
    graphicsRootDesc.NumParameters = static_cast<UINT>(std::size(rootParameters));
    graphicsRootDesc.pParameters = rootParameters;
    graphicsRootDesc.NumStaticSamplers = static_cast<UINT>(std::size(staticSamplers));
    graphicsRootDesc.pStaticSamplers = staticSamplers;
    graphicsRootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    Microsoft::WRL::ComPtr<ID3DBlob> serializedRoot;
    Microsoft::WRL::ComPtr<ID3DBlob> rootErrors;
    if (FAILED(D3D12SerializeRootSignature(&graphicsRootDesc,
                                           D3D_ROOT_SIGNATURE_VERSION_1,
                                           serializedRoot.GetAddressOf(),
                                           rootErrors.GetAddressOf())))
    {
        if (rootErrors)
        {
            OutputDebugStringA(static_cast<const char *>(rootErrors->GetBufferPointer()));
        }
        return false;
    }

    if (FAILED(device_->CreateRootSignature(0,
                                            serializedRoot->GetBufferPointer(),
                                            serializedRoot->GetBufferSize(),
                                            IID_PPV_ARGS(graphicsRootSignature_.GetAddressOf()))))
    {
        return false;
    }

    D3D12_DESCRIPTOR_RANGE computeRanges[2] = {};
    computeRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    computeRanges[0].NumDescriptors = 10;
    computeRanges[0].BaseShaderRegister = 0;
    computeRanges[0].RegisterSpace = 0;
    computeRanges[0].OffsetInDescriptorsFromTableStart = 0;

    D3D12_DESCRIPTOR_RANGE &uavRange = computeRanges[1];
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = 2;
    uavRange.BaseShaderRegister = 0;
    uavRange.RegisterSpace = 0;
    uavRange.OffsetInDescriptorsFromTableStart = 10;

    D3D12_ROOT_PARAMETER computeParams[2] = {};
    computeParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    computeParams[0].Descriptor.ShaderRegister = 3;
    computeParams[0].Descriptor.RegisterSpace = 0;
    computeParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    computeParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    computeParams[1].DescriptorTable.NumDescriptorRanges = static_cast<UINT>(std::size(computeRanges));
    computeParams[1].DescriptorTable.pDescriptorRanges = computeRanges;
    computeParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC computeRootDesc{};
    computeRootDesc.NumParameters = static_cast<UINT>(std::size(computeParams));
    computeRootDesc.pParameters = computeParams;
    computeRootDesc.NumStaticSamplers = 0;
    computeRootDesc.pStaticSamplers = nullptr;
    computeRootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    serializedRoot.Reset();
    rootErrors.Reset();
    if (FAILED(D3D12SerializeRootSignature(&computeRootDesc,
                                           D3D_ROOT_SIGNATURE_VERSION_1,
                                           serializedRoot.GetAddressOf(),
                                           rootErrors.GetAddressOf())))
    {
        if (rootErrors)
        {
            OutputDebugStringA(static_cast<const char *>(rootErrors->GetBufferPointer()));
        }
        return false;
    }

    if (FAILED(device_->CreateRootSignature(0,
                                            serializedRoot->GetBufferPointer(),
                                            serializedRoot->GetBufferSize(),
                                            IID_PPV_ARGS(computeRootSignature_.GetAddressOf()))))
    {
        return false;
    }

    D3D12_INPUT_ELEMENT_DESC sceneInputLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};

    D3D12_RASTERIZER_DESC defaultRasterizer{};
    defaultRasterizer.FillMode = D3D12_FILL_MODE_SOLID;
    defaultRasterizer.CullMode = D3D12_CULL_MODE_BACK;
    defaultRasterizer.FrontCounterClockwise = FALSE;
    defaultRasterizer.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    defaultRasterizer.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    defaultRasterizer.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    defaultRasterizer.DepthClipEnable = TRUE;
    defaultRasterizer.MultisampleEnable = TRUE;
    defaultRasterizer.AntialiasedLineEnable = FALSE;
    defaultRasterizer.ForcedSampleCount = 0;
    defaultRasterizer.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    D3D12_RASTERIZER_DESC shadowRasterizer = defaultRasterizer;
    shadowRasterizer.DepthBias = 1500;
    shadowRasterizer.SlopeScaledDepthBias = 1.4f;

    D3D12_BLEND_DESC blendDesc{};
    blendDesc.AlphaToCoverageEnable = FALSE;
    blendDesc.IndependentBlendEnable = FALSE;
    D3D12_RENDER_TARGET_BLEND_DESC rtBlend{};
    rtBlend.BlendEnable = FALSE;
    rtBlend.LogicOpEnable = FALSE;
    rtBlend.SrcBlend = D3D12_BLEND_ONE;
    rtBlend.DestBlend = D3D12_BLEND_ZERO;
    rtBlend.BlendOp = D3D12_BLEND_OP_ADD;
    rtBlend.SrcBlendAlpha = D3D12_BLEND_ONE;
    rtBlend.DestBlendAlpha = D3D12_BLEND_ZERO;
    rtBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    rtBlend.LogicOp = D3D12_LOGIC_OP_NOOP;
    rtBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blendDesc.RenderTarget[0] = rtBlend;

    D3D12_DEPTH_STENCIL_DESC depthEnabled{};
    depthEnabled.DepthEnable = TRUE;
    depthEnabled.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depthEnabled.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    depthEnabled.StencilEnable = FALSE;

    D3D12_DEPTH_STENCIL_DESC depthDisabled{};
    depthDisabled.DepthEnable = FALSE;
    depthDisabled.StencilEnable = FALSE;

    auto createGraphicsPSO = [&](const D3D12_GRAPHICS_PIPELINE_STATE_DESC &desc,
                                 Microsoft::WRL::ComPtr<ID3D12PipelineState> &pso) -> bool {
        return SUCCEEDED(device_->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(pso.GetAddressOf())));
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC sceneDesc{};
    sceneDesc.pRootSignature = graphicsRootSignature_.Get();
    sceneDesc.VS = {sceneVS->GetBufferPointer(), sceneVS->GetBufferSize()};
    sceneDesc.PS = {scenePS->GetBufferPointer(), scenePS->GetBufferSize()};
    sceneDesc.BlendState = blendDesc;
    sceneDesc.SampleMask = UINT_MAX;
    sceneDesc.RasterizerState = defaultRasterizer;
    sceneDesc.DepthStencilState = depthEnabled;
    sceneDesc.InputLayout = {sceneInputLayout, static_cast<UINT>(std::size(sceneInputLayout))};
    sceneDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    sceneDesc.NumRenderTargets = 1;
    sceneDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    sceneDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    sceneDesc.SampleDesc.Count = kSceneSampleCount;

    if (!createGraphicsPSO(sceneDesc, scenePSO_))
    {
        return false;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC tessDesc = sceneDesc;
    tessDesc.VS = {tessVS->GetBufferPointer(), tessVS->GetBufferSize()};
    tessDesc.HS = {tessHS->GetBufferPointer(), tessHS->GetBufferSize()};
    tessDesc.DS = {tessDS->GetBufferPointer(), tessDS->GetBufferSize()};
    tessDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;

    if (!createGraphicsPSO(tessDesc, tessPSO_))
    {
        return false;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC wireframeDesc = sceneDesc;
    wireframeDesc.GS = {wireframeGS->GetBufferPointer(), wireframeGS->GetBufferSize()};
    wireframeDesc.PS = {wireframePS->GetBufferPointer(), wireframePS->GetBufferSize()};

    if (!createGraphicsPSO(wireframeDesc, wireframePSO_))
    {
        return false;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowDesc = sceneDesc;
    shadowDesc.VS = {shadowVS->GetBufferPointer(), shadowVS->GetBufferSize()};
    shadowDesc.PS = {nullptr, 0};
    shadowDesc.RasterizerState = shadowRasterizer;
    shadowDesc.NumRenderTargets = 0;
    shadowDesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
    shadowDesc.SampleDesc.Count = 1;

    if (!createGraphicsPSO(shadowDesc, shadowPSO_))
    {
        return false;
    }

    auto buildFullscreenDesc = [&](ID3DBlob *psBlob, DXGI_FORMAT rtvFormat) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = graphicsRootSignature_.Get();
        desc.VS = {fullscreenVS->GetBufferPointer(), fullscreenVS->GetBufferSize()};
        desc.PS = {psBlob->GetBufferPointer(), psBlob->GetBufferSize()};
        desc.BlendState = blendDesc;
        desc.SampleMask = UINT_MAX;
        D3D12_RASTERIZER_DESC raster = defaultRasterizer;
        raster.CullMode = D3D12_CULL_MODE_NONE;
        desc.RasterizerState = raster;
        desc.DepthStencilState = depthDisabled;
        desc.InputLayout = {nullptr, 0};
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets = 1;
        desc.RTVFormats[0] = rtvFormat;
        desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        return desc;
    };

    if (!createGraphicsPSO(buildFullscreenDesc(brightPS.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT), brightExtractPSO_) ||
        !createGraphicsPSO(buildFullscreenDesc(blurHPS.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT), blurHPSO_) ||
        !createGraphicsPSO(buildFullscreenDesc(blurVPS.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT), blurVPSO_) ||
        !createGraphicsPSO(buildFullscreenDesc(compositePS.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT), compositePSO_) ||
        !createGraphicsPSO(buildFullscreenDesc(copyPS.Get(), DXGI_FORMAT_R8G8B8A8_UNORM), copyPSO_))
    {
        return false;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC computeDesc{};
    computeDesc.pRootSignature = computeRootSignature_.Get();
    computeDesc.CS = {particleCS->GetBufferPointer(), particleCS->GetBufferSize()};
    if (FAILED(device_->CreateComputePipelineState(&computeDesc, IID_PPV_ARGS(particleCSPSO_.GetAddressOf()))))
    {
        return false;
    }

    computeDesc.CS = {edgeCS->GetBufferPointer(), edgeCS->GetBufferSize()};
    return SUCCEEDED(device_->CreateComputePipelineState(&computeDesc, IID_PPV_ARGS(edgeDetectCSPSO_.GetAddressOf())));
}

bool DX12Renderer::CreateGeometryResources()
{
    const VertexPC vertices[] = {
        {{-1.0f, -1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.45f, 0.30f}},
        {{1.0f, -1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.45f, 0.30f}},
        {{1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.45f, 0.30f}},
        {{-1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.45f, 0.30f}},

        {{1.0f, -1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {0.25f, 0.85f, 1.0f}},
        {{-1.0f, -1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {0.25f, 0.85f, 1.0f}},
        {{-1.0f, 1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {0.25f, 0.85f, 1.0f}},
        {{1.0f, 1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {0.25f, 0.85f, 1.0f}},

        {{-1.0f, -1.0f, -1.0f}, {-1.0f, 0.0f, 0.0f}, {0.30f, 1.0f, 0.45f}},
        {{-1.0f, -1.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}, {0.30f, 1.0f, 0.45f}},
        {{-1.0f, 1.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}, {0.30f, 1.0f, 0.45f}},
        {{-1.0f, 1.0f, -1.0f}, {-1.0f, 0.0f, 0.0f}, {0.30f, 1.0f, 0.45f}},

        {{1.0f, -1.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {0.95f, 0.9f, 0.35f}},
        {{1.0f, -1.0f, -1.0f}, {1.0f, 0.0f, 0.0f}, {0.95f, 0.9f, 0.35f}},
        {{1.0f, 1.0f, -1.0f}, {1.0f, 0.0f, 0.0f}, {0.95f, 0.9f, 0.35f}},
        {{1.0f, 1.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {0.95f, 0.9f, 0.35f}},

        {{-1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {0.65f, 0.65f, 1.0f}},
        {{1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {0.65f, 0.65f, 1.0f}},
        {{1.0f, 1.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {0.65f, 0.65f, 1.0f}},
        {{-1.0f, 1.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {0.65f, 0.65f, 1.0f}},

        {{-1.0f, -1.0f, -1.0f}, {0.0f, -1.0f, 0.0f}, {0.45f, 0.55f, 0.95f}},
        {{1.0f, -1.0f, -1.0f}, {0.0f, -1.0f, 0.0f}, {0.45f, 0.55f, 0.95f}},
        {{1.0f, -1.0f, 1.0f}, {0.0f, -1.0f, 0.0f}, {0.45f, 0.55f, 0.95f}},
        {{-1.0f, -1.0f, 1.0f}, {0.0f, -1.0f, 0.0f}, {0.45f, 0.55f, 0.95f}},
    };

    const uint16_t indices[] = {
        0, 1, 2, 0, 2, 3,
        4, 5, 6, 4, 6, 7,
        8, 9, 10, 8, 10, 11,
        12, 13, 14, 12, 14, 15,
        16, 17, 18, 16, 18, 19,
        20, 21, 22, 20, 22, 23};

    indexCount_ = static_cast<UINT>(sizeof(indices) / sizeof(indices[0]));

    auto createUploadBuffer = [&](UINT64 size,
                                  Microsoft::WRL::ComPtr<ID3D12Resource> &resource,
                                  const void *initialData) -> bool {
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = size;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        if (FAILED(device_->CreateCommittedResource(&heapProps,
                                                    D3D12_HEAP_FLAG_NONE,
                                                    &desc,
                                                    D3D12_RESOURCE_STATE_GENERIC_READ,
                                                    nullptr,
                                                    IID_PPV_ARGS(resource.GetAddressOf()))))
        {
            return false;
        }

        void *mapped = nullptr;
        D3D12_RANGE readRange{0, 0};
        if (FAILED(resource->Map(0, &readRange, &mapped)))
        {
            return false;
        }

        std::memcpy(mapped, initialData, static_cast<size_t>(size));
        resource->Unmap(0, nullptr);
        return true;
    };

    if (!createUploadBuffer(sizeof(vertices), vertexBuffer_, vertices) ||
        !createUploadBuffer(sizeof(indices), indexBuffer_, indices))
    {
        return false;
    }

    vertexBufferView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
    vertexBufferView_.StrideInBytes = sizeof(VertexPC);
    vertexBufferView_.SizeInBytes = sizeof(vertices);

    indexBufferView_.BufferLocation = indexBuffer_->GetGPUVirtualAddress();
    indexBufferView_.Format = DXGI_FORMAT_R16_UINT;
    indexBufferView_.SizeInBytes = sizeof(indices);

    return true;
}

bool DX12Renderer::CreateConstantBufferResources()
{
    sceneCBStride_ = Align256(static_cast<UINT>(sizeof(SceneConstantBuffer)));
    postCBStride_ = Align256(static_cast<UINT>(sizeof(PostConstantBuffer)));
    blurCBStride_ = Align256(static_cast<UINT>(sizeof(BlurConstantBuffer)));
    particleCBStride_ = Align256(static_cast<UINT>(sizeof(ParticleConstantBuffer)));
    sceneCBFrameStride_ = sceneCBStride_ * kMaxObjectCount;

    auto createMappedUpload = [&](UINT64 size,
                                  Microsoft::WRL::ComPtr<ID3D12Resource> &resource,
                                  uint8_t *&mappedPtr) -> bool {
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = size;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        if (FAILED(device_->CreateCommittedResource(&heapProps,
                                                    D3D12_HEAP_FLAG_NONE,
                                                    &desc,
                                                    D3D12_RESOURCE_STATE_GENERIC_READ,
                                                    nullptr,
                                                    IID_PPV_ARGS(resource.GetAddressOf()))))
        {
            return false;
        }

        D3D12_RANGE readRange{0, 0};
        return SUCCEEDED(resource->Map(0, &readRange, reinterpret_cast<void **>(&mappedPtr)));
    };

    if (!createMappedUpload(static_cast<UINT64>(sceneCBFrameStride_) * kSwapChainBufferCount,
                            sceneConstantBuffer_,
                            mappedSceneCB_) ||
        !createMappedUpload(static_cast<UINT64>(postCBStride_) * kSwapChainBufferCount,
                            postConstantBuffer_,
                            mappedPostCB_) ||
        !createMappedUpload(static_cast<UINT64>(blurCBStride_) * kSwapChainBufferCount,
                            blurConstantBuffer_,
                            mappedBlurCB_) ||
        !createMappedUpload(static_cast<UINT64>(particleCBStride_) * kSwapChainBufferCount,
                            particleConstantBuffer_,
                            mappedParticleCB_))
    {
        return false;
    }

    return true;
}

bool DX12Renderer::CreateFrameResources()
{
    for (UINT i = 0; i < kSwapChainBufferCount; ++i)
    {
        if (FAILED(swapChain_->GetBuffer(i, IID_PPV_ARGS(backBuffers_[i].GetAddressOf()))))
        {
            return false;
        }
        device_->CreateRenderTargetView(backBuffers_[i].Get(), nullptr, RtvHandle(i == 0 ? kRtvBackBuffer0 : kRtvBackBuffer1));
        backBufferStates_[i] = D3D12_RESOURCE_STATE_PRESENT;
    }

    auto createTexture2D = [&](UINT textureWidth,
                               UINT textureHeight,
                               DXGI_FORMAT format,
                               D3D12_RESOURCE_FLAGS flags,
                               D3D12_RESOURCE_STATES initialState,
                               UINT sampleCount,
                               const D3D12_CLEAR_VALUE *clearValue,
                               Microsoft::WRL::ComPtr<ID3D12Resource> &resource) -> bool {
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = textureWidth;
        desc.Height = textureHeight;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = format;
        desc.SampleDesc.Count = sampleCount;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = flags;

        return SUCCEEDED(device_->CreateCommittedResource(&heapProps,
                                                          D3D12_HEAP_FLAG_NONE,
                                                          &desc,
                                                          initialState,
                                                          clearValue,
                                                          IID_PPV_ARGS(resource.GetAddressOf())));
    };

    const UINT halfWidth = std::max<UINT>(1u, renderWidth_ / 2u);
    const UINT halfHeight = std::max<UINT>(1u, renderHeight_ / 2u);

    D3D12_CLEAR_VALUE hdrClear{};
    hdrClear.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    hdrClear.Color[0] = 0.0f;
    hdrClear.Color[1] = 0.0f;
    hdrClear.Color[2] = 0.0f;
    hdrClear.Color[3] = 1.0f;

    if (!createTexture2D(renderWidth_, renderHeight_, DXGI_FORMAT_R16G16B16A16_FLOAT,
                         D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                         D3D12_RESOURCE_STATE_RENDER_TARGET,
                         kSceneSampleCount,
                         &hdrClear,
                         sceneColorMSAA_) ||
        !createTexture2D(renderWidth_, renderHeight_, DXGI_FORMAT_R16G16B16A16_FLOAT,
                         D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                         D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                         1,
                         &hdrClear,
                         sceneColor_) ||
        !createTexture2D(halfWidth, halfHeight, DXGI_FORMAT_R16G16B16A16_FLOAT,
                         D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                         D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                         1,
                         &hdrClear,
                         bloomA_) ||
        !createTexture2D(halfWidth, halfHeight, DXGI_FORMAT_R16G16B16A16_FLOAT,
                         D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                         D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                         1,
                         &hdrClear,
                         bloomB_) ||
        !createTexture2D(renderWidth_, renderHeight_, DXGI_FORMAT_R16G16B16A16_FLOAT,
                         D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                         D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                         1,
                         &hdrClear,
                         postColor_) ||
        !createTexture2D(renderWidth_, renderHeight_, DXGI_FORMAT_R16G16B16A16_FLOAT,
                         D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                         D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                         1,
                         &hdrClear,
                         history_))
    {
        return false;
    }

    if (!createTexture2D(halfWidth, halfHeight, DXGI_FORMAT_R16G16B16A16_FLOAT,
                         D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                         D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                         1,
                         nullptr,
                         particle_))
    {
        return false;
    }

    if (!createTexture2D(renderWidth_, renderHeight_, DXGI_FORMAT_R8G8B8A8_UNORM,
                         D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                         1,
                         nullptr,
                         edge_))
    {
        return false;
    }

    D3D12_CLEAR_VALUE depthClear{};
    depthClear.Format = DXGI_FORMAT_D32_FLOAT;
    depthClear.DepthStencil.Depth = 1.0f;
    depthClear.DepthStencil.Stencil = 0;

    if (!createTexture2D(renderWidth_, renderHeight_, DXGI_FORMAT_D32_FLOAT,
                         D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
                         D3D12_RESOURCE_STATE_DEPTH_WRITE,
                         kSceneSampleCount,
                         &depthClear,
                         sceneDepth_))
    {
        return false;
    }

    if (!createTexture2D(kShadowMapSize, kShadowMapSize, DXGI_FORMAT_R32_TYPELESS,
                         D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
                         D3D12_RESOURCE_STATE_DEPTH_WRITE,
                         1,
                         &depthClear,
                         shadowDepth_))
    {
        return false;
    }

    sceneColorMSAAState_ = D3D12_RESOURCE_STATE_RENDER_TARGET;
    sceneColorState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    bloomAState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    bloomBState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    postColorState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    historyState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    particleState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    edgeState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    sceneDepthState_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    shadowDepthState_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;

    device_->CreateRenderTargetView(sceneColorMSAA_.Get(), nullptr, RtvHandle(kRtvScene));
    device_->CreateRenderTargetView(bloomA_.Get(), nullptr, RtvHandle(kRtvBloomA));
    device_->CreateRenderTargetView(bloomB_.Get(), nullptr, RtvHandle(kRtvBloomB));
    device_->CreateRenderTargetView(postColor_.Get(), nullptr, RtvHandle(kRtvPost));
    device_->CreateRenderTargetView(history_.Get(), nullptr, RtvHandle(kRtvHistory));

    D3D12_DEPTH_STENCIL_VIEW_DESC sceneDsvDesc{};
    sceneDsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    sceneDsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
    sceneDsvDesc.Flags = D3D12_DSV_FLAG_NONE;
    device_->CreateDepthStencilView(sceneDepth_.Get(), &sceneDsvDesc, DsvHandle(kDsvScene));

    D3D12_DEPTH_STENCIL_VIEW_DESC shadowDsvDesc{};
    shadowDsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    shadowDsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    shadowDsvDesc.Flags = D3D12_DSV_FLAG_NONE;
    device_->CreateDepthStencilView(shadowDepth_.Get(), &shadowDsvDesc, DsvHandle(kDsvShadow));

    D3D12_SHADER_RESOURCE_VIEW_DESC shadowSrvDesc{};
    shadowSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    shadowSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    shadowSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    shadowSrvDesc.Texture2D.MostDetailedMip = 0;
    shadowSrvDesc.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(shadowDepth_.Get(), &shadowSrvDesc, SrvUavCpuHandle(kSrvShadow));

    auto createColorSrv = [&](ID3D12Resource *resource, UINT index) {
        D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.Texture2D.MostDetailedMip = 0;
        desc.Texture2D.MipLevels = 1;
        device_->CreateShaderResourceView(resource, &desc, SrvUavCpuHandle(index));
    };

    createColorSrv(sceneColor_.Get(), kSrvScene);
    createColorSrv(bloomA_.Get(), kSrvBloomA);
    createColorSrv(particle_.Get(), kSrvParticle);
    createColorSrv(history_.Get(), kSrvHistory);
    createColorSrv(bloomB_.Get(), kSrvBloomB);
    createColorSrv(postColor_.Get(), kSrvPost);

    D3D12_SHADER_RESOURCE_VIEW_DESC edgeSrvDesc{};
    edgeSrvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    edgeSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    edgeSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    edgeSrvDesc.Texture2D.MostDetailedMip = 0;
    edgeSrvDesc.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(edge_.Get(), &edgeSrvDesc, SrvUavCpuHandle(kSrvEdge));

    D3D12_UNORDERED_ACCESS_VIEW_DESC particleUavDesc{};
    particleUavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    particleUavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    particleUavDesc.Texture2D.MipSlice = 0;
    device_->CreateUnorderedAccessView(particle_.Get(), nullptr, &particleUavDesc, SrvUavCpuHandle(kUavParticle));

    D3D12_UNORDERED_ACCESS_VIEW_DESC edgeUavDesc{};
    edgeUavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    edgeUavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    edgeUavDesc.Texture2D.MipSlice = 0;
    device_->CreateUnorderedAccessView(edge_.Get(), nullptr, &edgeUavDesc, SrvUavCpuHandle(kUavEdge));

    return true;
}

bool DX12Renderer::CreateSyncObjects()
{
    if (FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence_.GetAddressOf()))))
    {
        return false;
    }

    fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent_)
    {
        return false;
    }

    fenceValue_ = 0;
    frameFenceValues_.fill(0);
    return true;
}

void DX12Renderer::ReleaseFrameResources()
{
    for (auto &bb : backBuffers_)
    {
        bb.Reset();
    }

    sceneColorMSAA_.Reset();
    sceneColor_.Reset();
    bloomA_.Reset();
    bloomB_.Reset();
    postColor_.Reset();
    history_.Reset();
    particle_.Reset();
    edge_.Reset();
    sceneDepth_.Reset();
    shadowDepth_.Reset();

    backBufferStates_.fill(D3D12_RESOURCE_STATE_PRESENT);
    sceneColorMSAAState_ = D3D12_RESOURCE_STATE_RENDER_TARGET;
    edgeState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
}

void DX12Renderer::WaitForGpu()
{
    if (!commandQueue_ || !fence_ || !fenceEvent_)
    {
        return;
    }

    const UINT64 signalValue = ++fenceValue_;
    if (FAILED(commandQueue_->Signal(fence_.Get(), signalValue)))
    {
        return;
    }

    if (FAILED(fence_->SetEventOnCompletion(signalValue, fenceEvent_)))
    {
        return;
    }

    WaitForSingleObject(fenceEvent_, INFINITE);
    frameFenceValues_.fill(signalValue);
}

void DX12Renderer::WaitForCurrentFrame()
{
    if (!fence_ || !fenceEvent_)
    {
        return;
    }

    const UINT64 waitValue = frameFenceValues_[frameIndex_];
    if (waitValue == 0)
    {
        return;
    }

    if (fence_->GetCompletedValue() >= waitValue)
    {
        return;
    }

    if (SUCCEEDED(fence_->SetEventOnCompletion(waitValue, fenceEvent_)))
    {
        WaitForSingleObject(fenceEvent_, INFINITE);
    }
}

void DX12Renderer::TransitionResource(ID3D12Resource *resource,
                                      D3D12_RESOURCE_STATES &currentState,
                                      D3D12_RESOURCE_STATES targetState)
{
    if (!resource || currentState == targetState)
    {
        return;
    }

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = currentState;
    barrier.Transition.StateAfter = targetState;

    commandList_->ResourceBarrier(1, &barrier);
    currentState = targetState;
}

D3D12_CPU_DESCRIPTOR_HANDLE DX12Renderer::RtvHandle(UINT index) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(index) * static_cast<SIZE_T>(rtvDescriptorSize_);
    return handle;
}

D3D12_CPU_DESCRIPTOR_HANDLE DX12Renderer::DsvHandle(UINT index) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(index) * static_cast<SIZE_T>(dsvDescriptorSize_);
    return handle;
}

D3D12_CPU_DESCRIPTOR_HANDLE DX12Renderer::SrvUavCpuHandle(UINT index) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = srvUavHeap_->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(index) * static_cast<SIZE_T>(srvUavDescriptorSize_);
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE DX12Renderer::SrvUavGpuHandle(UINT index) const
{
    D3D12_GPU_DESCRIPTOR_HANDLE handle = srvUavHeap_->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<UINT64>(index) * static_cast<UINT64>(srvUavDescriptorSize_);
    return handle;
}

UINT64 DX12Renderer::SceneCBAddress(UINT objectIndex) const
{
    const UINT64 base = sceneConstantBuffer_->GetGPUVirtualAddress() +
                        static_cast<UINT64>(frameIndex_) * static_cast<UINT64>(sceneCBFrameStride_);
    return base + static_cast<UINT64>(objectIndex) * static_cast<UINT64>(sceneCBStride_);
}

UINT64 DX12Renderer::PostCBAddress() const
{
    return postConstantBuffer_->GetGPUVirtualAddress() +
           static_cast<UINT64>(frameIndex_) * static_cast<UINT64>(postCBStride_);
}

UINT64 DX12Renderer::BlurCBAddress() const
{
    return blurConstantBuffer_->GetGPUVirtualAddress() +
           static_cast<UINT64>(frameIndex_) * static_cast<UINT64>(blurCBStride_);
}

UINT64 DX12Renderer::ParticleCBAddress() const
{
    return particleConstantBuffer_->GetGPUVirtualAddress() +
           static_cast<UINT64>(frameIndex_) * static_cast<UINT64>(particleCBStride_);
}

void DX12Renderer::BeginEvent(const char *label)
{
    if (!commandList_ || label == nullptr)
    {
        return;
    }
    commandList_->BeginEvent(0, label, static_cast<UINT>(std::strlen(label)));
}

void DX12Renderer::EndEvent()
{
    if (commandList_)
    {
        commandList_->EndEvent();
    }
}

} // namespace dxteaching
