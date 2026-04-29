#include "DX11Renderer.h"

#include <d3dcompiler.h>

#include <DirectXMath.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <vector>

#include "DiagnosticsLog.h"

namespace dxteaching
{

namespace
{

constexpr UINT kShaderCompileFlagsBase = D3DCOMPILE_ENABLE_STRICTNESS;
constexpr UINT kTextureSlotCount = 10;
constexpr UINT kSamplerCount = 3;
constexpr UINT kComputeGroupSize = 8;

UINT Align16(UINT value)
{
    return (value + 15u) & ~15u;
}

void StoreMatrixTransposed(const DirectX::XMMATRIX &matrix, float out[16])
{
    DirectX::XMFLOAT4X4 temp{};
    DirectX::XMStoreFloat4x4(&temp, DirectX::XMMatrixTranspose(matrix));
    std::memcpy(out, &temp, sizeof(temp));
}

template <typename T>
void UpdateDynamicBuffer(ID3D11DeviceContext *context, ID3D11Buffer *buffer, const T &value)
{
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        std::memcpy(mapped.pData, &value, sizeof(T));
        context->Unmap(buffer, 0);
    }
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

std::vector<uint32_t> BuildMaterialAlbedoPixels()
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

std::vector<uint32_t> BuildMaterialNormalPixels()
{
    std::vector<uint32_t> pixels(static_cast<size_t>(kMaterialTextureSize) * kMaterialTextureSize);
    for (uint32_t y = 0; y < kMaterialTextureSize; ++y)
    {
        for (uint32_t x = 0; x < kMaterialTextureSize; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(kMaterialTextureSize - 1u) * 2.0f - 1.0f;
            const float v = static_cast<float>(y) / static_cast<float>(kMaterialTextureSize - 1u) * 2.0f - 1.0f;
            const float nx = std::sinf(u * 9.0f) * 0.35f;
            const float ny = std::cosf(v * 9.0f) * 0.35f;
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

} // namespace

DX11Renderer::~DX11Renderer()
{
    Shutdown();
}

bool DX11Renderer::Initialize(HWND hwnd, uint32_t width, uint32_t height)
{
    LogLine("DX11", "Initialize begin width=%u height=%u", width, height);
    Shutdown();

    hwnd_ = hwnd;
    width_ = std::max<uint32_t>(1u, width);
    height_ = std::max<uint32_t>(1u, height);
    renderWidth_ = ScaledRenderDimension(width_);
    renderHeight_ = ScaledRenderDimension(height_);

    if (!CreateSwapChainAndDevice() ||
        !CreatePipelineShaders() ||
        !CreateGeometryBuffers() ||
        !CreateMaterialTextures() ||
        !CreateConstantBuffers() ||
        !CreateStatesAndSamplers() ||
        !CreateSizeDependentResources())
    {
        LogLine("DX11", "Initialize failed during resource creation");
        Shutdown();
        return false;
    }

    context_.As(&annotation_);
    UpdateViewports();
    historyValid_ = false;
    historyTopic_ = 0;
    LogLine("DX11", "Initialize success");
    return true;
}

void DX11Renderer::Resize(uint32_t width, uint32_t height)
{
    LogLine("DX11", "Resize requested width=%u height=%u", width, height);
    if (!swapChain_ || width == 0 || height == 0)
    {
        LogLine("DX11", "Resize skipped swapChain=%d", swapChain_ ? 1 : 0);
        return;
    }

    width_ = width;
    height_ = height;
    renderWidth_ = ScaledRenderDimension(width_);
    renderHeight_ = ScaledRenderDimension(height_);
    historyValid_ = false;
    historyTopic_ = 0;

    context_->OMSetRenderTargets(0, nullptr, nullptr);

    ReleaseSizeDependentResources();

    if (FAILED(swapChain_->ResizeBuffers(0, width_, height_, DXGI_FORMAT_UNKNOWN, 0)))
    {
        LogLine("DX11", "ResizeBuffers failed");
        return;
    }

    if (!CreateSizeDependentResources())
    {
        LogLine("DX11", "CreateSizeDependentResources failed after resize");
        return;
    }

    UpdateViewports();
    LogLine("DX11", "Resize success width=%u height=%u", width_, height_);
}

void DX11Renderer::Render(const FrameSettings &settings)
{
    static uint64_t frameCounter = 0;
    ++frameCounter;

    if (!context_ ||
        !swapChain_ ||
        !backBufferRTV_ ||
        !sceneColorMSAARTV_ ||
        !sceneColorRTV_ ||
        !sceneDepthDSV_ ||
        !shadowDepthDSV_ ||
        !postColorRTV_ ||
        !historyRTV_ ||
        !bloomARTV_ ||
        !bloomBRTV_ ||
        !sceneVS_ ||
        !scenePS_ ||
        !shadowVS_ ||
        !fullscreenVS_ ||
        !brightExtractPS_ ||
        !blurHPS_ ||
        !blurVPS_ ||
        !compositePS_ ||
        !copyPS_ ||
        !particleCS_ ||
        !edgeDetectCS_ ||
        !edgeSRV_ ||
        !edgeUAV_ ||
        !albedoSRV_ ||
        !normalSRV_)
    {
        if ((frameCounter % 120u) == 0u)
        {
            LogLine("DX11", "Render skipped due to missing resources");
        }
        return;
    }

    if ((frameCounter % 120u) == 0u)
    {
        LogLine("DX11", "Render heartbeat frame=%llu topic=%d size=%ux%u", static_cast<unsigned long long>(frameCounter), settings.topic, width_, height_);
    }

    const int clampedTopic = ClampTopic(settings.topic);
    const TopicRuntimeProfile profile = BuildTopicRuntimeProfile(clampedTopic, settings.errorExampleEnabled);
    if (historyTopic_ != clampedTopic)
    {
        historyValid_ = false;
        historyTopic_ = clampedTopic;
    }
    const uint32_t objectCount = std::max<uint32_t>(1u, std::min<uint32_t>(profile.objectCount, 10u));

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

    PostConstantBuffer postCB{};
    postCB.postParams0[0] = profile.exposure;
    postCB.postParams0[1] = profile.enableBloom ? profile.bloomStrength : 0.0f;
    postCB.postParams0[2] = profile.enableParticles ? profile.particleStrength : 0.0f;
    postCB.postParams0[3] = (profile.enableTemporal && historyValid_) ? profile.temporalBlend : 0.0f;
    postCB.postParams1[0] = static_cast<float>(clampedTopic);
    postCB.postParams1[1] = settings.elapsedSeconds;
    postCB.postParams1[2] = profile.edgeStrength;
    postCB.postParams1[3] = profile.brightThreshold;
    UpdateDynamicBuffer(context_.Get(), postCB_.Get(), postCB);

    ParticleConstantBuffer particleCB{};
    particleCB.particleParams0[0] = settings.elapsedSeconds;
    particleCB.particleParams0[1] = static_cast<float>(clampedTopic);
    particleCB.particleParams0[2] = settings.errorExampleEnabled ? 1.0f : 0.0f;
    particleCB.particleParams0[3] = profile.enableParticles ? profile.particleStrength : 0.0f;
    particleCB.particleParams1[0] = static_cast<float>(particleWidth);
    particleCB.particleParams1[1] = static_cast<float>(particleHeight);
    particleCB.particleParams1[2] = 1.0f / static_cast<float>(particleWidth);
    particleCB.particleParams1[3] = 1.0f / static_cast<float>(particleHeight);
    UpdateDynamicBuffer(context_.Get(), particleCB_.Get(), particleCB);

    auto updateSceneCB = [&](const XMMATRIX &world) {
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

        UpdateDynamicBuffer(context_.Get(), sceneCB_.Get(), sceneCB);
    };

    float clearColor[4] = {0.08f, 0.10f, 0.12f, 1.0f};
    ComputeClearColor(settings, clearColor);

    const float zeroColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    UINT stride = sizeof(VertexPC);
    UINT offset = 0;
    ID3D11Buffer *vb = vertexBuffer_.Get();
    ID3D11Buffer *ib = indexBuffer_.Get();

    context_->IASetInputLayout(sceneInputLayout_.Get());
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    context_->IASetIndexBuffer(ib, DXGI_FORMAT_R16_UINT, 0);

    ID3D11SamplerState *samplers[kSamplerCount] = {
        linearClampSampler_.Get(),
        pointClampSampler_.Get(),
        shadowSampler_.Get()};
    context_->PSSetSamplers(0, kSamplerCount, samplers);

    ID3D11ShaderResourceView *nullSrvs[kTextureSlotCount] = {};
    context_->PSSetShaderResources(0, kTextureSlotCount, nullSrvs);

    ID3D11UnorderedAccessView *nullUavs[2] = {nullptr, nullptr};
    UINT initialCounts[2] = {0, 0};
    context_->CSSetUnorderedAccessViews(0, 2, nullUavs, initialCounts);

    BeginEvent(L"Shadow Pass");
    {
        context_->RSSetViewports(1, &shadowViewport_);
        context_->RSSetState(shadowRasterState_.Get());
        context_->OMSetDepthStencilState(shadowDepthState_.Get(), 0);
        context_->OMSetRenderTargets(0, nullptr, shadowDepthDSV_.Get());
        context_->ClearDepthStencilView(shadowDepthDSV_.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);

        context_->VSSetShader(shadowVS_.Get(), nullptr, 0);
        context_->PSSetShader(nullptr, nullptr, 0);

        ID3D11Buffer *sceneCBBuffer = sceneCB_.Get();
        context_->VSSetConstantBuffers(0, 1, &sceneCBBuffer);

        for (uint32_t i = 0; i < objectCount; ++i)
        {
            updateSceneCB(BuildWorldMatrix(settings.elapsedSeconds, i, objectCount, profile.rotationSpeed));
            context_->DrawIndexed(indexCount_, 0, 0);
        }
    }
    EndEvent();

    BeginEvent(L"Scene HDR Pass");
    {
        context_->RSSetViewports(1, &fullViewport_);
        context_->RSSetState(sceneRasterState_.Get());
        context_->OMSetDepthStencilState(sceneDepthState_.Get(), 0);
        context_->OMSetRenderTargets(1, sceneColorMSAARTV_.GetAddressOf(), sceneDepthDSV_.Get());
        context_->ClearRenderTargetView(sceneColorMSAARTV_.Get(), clearColor);
        context_->ClearDepthStencilView(sceneDepthDSV_.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);

        context_->VSSetShader(sceneVS_.Get(), nullptr, 0);
        context_->PSSetShader(scenePS_.Get(), nullptr, 0);

        ID3D11ShaderResourceView *scenePassSrvs[kTextureSlotCount] = {};
        scenePassSrvs[0] = shadowDepthSRV_.Get();
        scenePassSrvs[8] = albedoSRV_.Get();
        scenePassSrvs[9] = normalSRV_.Get();
        context_->PSSetShaderResources(0, kTextureSlotCount, scenePassSrvs);

        ID3D11Buffer *sceneCBBuffer = sceneCB_.Get();
        context_->VSSetConstantBuffers(0, 1, &sceneCBBuffer);
        context_->PSSetConstantBuffers(0, 1, &sceneCBBuffer);

        for (uint32_t i = 0; i < objectCount; ++i)
        {
            updateSceneCB(BuildWorldMatrix(settings.elapsedSeconds, i, objectCount, profile.rotationSpeed));
            context_->DrawIndexed(indexCount_, 0, 0);
        }

        context_->PSSetShaderResources(0, kTextureSlotCount, nullSrvs);
    }
    EndEvent();

    context_->OMSetRenderTargets(0, nullptr, nullptr);
    context_->ResolveSubresource(sceneColorTex_.Get(),
                                 0,
                                 sceneColorMSAATex_.Get(),
                                 0,
                                 DXGI_FORMAT_R16G16B16A16_FLOAT);

    BeginEvent(L"Edge Detect Compute");
    {
        ID3D11ShaderResourceView *edgeSrvs[kTextureSlotCount] = {};
        edgeSrvs[1] = sceneColorSRV_.Get();
        ID3D11UnorderedAccessView *edgeUav = edgeUAV_.Get();
        context_->CSSetShader(edgeDetectCS_.Get(), nullptr, 0);
        context_->CSSetShaderResources(0, kTextureSlotCount, edgeSrvs);
        context_->CSSetUnorderedAccessViews(1, 1, &edgeUav, nullptr);

        const UINT groupsX = (renderWidth_ + (kComputeGroupSize - 1)) / kComputeGroupSize;
        const UINT groupsY = (renderHeight_ + (kComputeGroupSize - 1)) / kComputeGroupSize;
        context_->Dispatch(groupsX, groupsY, 1);

        context_->CSSetShaderResources(0, kTextureSlotCount, nullSrvs);
        context_->CSSetUnorderedAccessViews(1, 1, nullUavs, initialCounts);
        context_->CSSetShader(nullptr, nullptr, 0);
    }
    EndEvent();

    BeginEvent(L"Particle Compute");
    {
        context_->ClearUnorderedAccessViewFloat(particleUAV_.Get(), zeroColor);

        ID3D11Buffer *particleCBBuffer = particleCB_.Get();
        ID3D11UnorderedAccessView *uav = particleUAV_.Get();

        context_->CSSetShader(particleCS_.Get(), nullptr, 0);
        context_->CSSetConstantBuffers(3, 1, &particleCBBuffer);
        context_->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);

        const UINT groupsX = (particleWidth + (kComputeGroupSize - 1)) / kComputeGroupSize;
        const UINT groupsY = (particleHeight + (kComputeGroupSize - 1)) / kComputeGroupSize;
        context_->Dispatch(groupsX, groupsY, 1);

        context_->CSSetUnorderedAccessViews(0, 1, nullUavs, initialCounts);
        context_->CSSetShader(nullptr, nullptr, 0);
    }
    EndEvent();

    context_->OMSetDepthStencilState(nullptr, 0);
    context_->VSSetShader(fullscreenVS_.Get(), nullptr, 0);
    context_->IASetInputLayout(nullptr);

    if (profile.enableBloom)
    {
        BeginEvent(L"Bloom Extract");
        {
            context_->RSSetViewports(1, &halfViewport_);
            context_->OMSetRenderTargets(1, bloomARTV_.GetAddressOf(), nullptr);
            context_->ClearRenderTargetView(bloomARTV_.Get(), zeroColor);
            context_->PSSetShader(brightExtractPS_.Get(), nullptr, 0);

            ID3D11Buffer *postBuffer = postCB_.Get();
            context_->PSSetConstantBuffers(1, 1, &postBuffer);

            ID3D11ShaderResourceView *extractSrvs[kTextureSlotCount] = {};
            extractSrvs[1] = sceneColorSRV_.Get();
            context_->PSSetShaderResources(0, kTextureSlotCount, extractSrvs);

            context_->Draw(3, 0);
            context_->PSSetShaderResources(0, kTextureSlotCount, nullSrvs);
        }
        EndEvent();

        const uint32_t blurPasses = std::max<uint32_t>(1u, std::min<uint32_t>(profile.blurPassCount, 6u));
        for (uint32_t i = 0; i < blurPasses; ++i)
        {
            BeginEvent(L"Bloom Blur H");
            {
                BlurConstantBuffer blurCB{};
                blurCB.blurParams[0] = 1.0f / safeHalfWidth;
                blurCB.blurParams[1] = 1.0f / safeHalfHeight;
                blurCB.blurParams[2] = 1.0f;
                blurCB.blurParams[3] = 0.0f;
                UpdateDynamicBuffer(context_.Get(), blurCB_.Get(), blurCB);

                ID3D11Buffer *blurBuffer = blurCB_.Get();
                context_->PSSetConstantBuffers(2, 1, &blurBuffer);

                context_->RSSetViewports(1, &halfViewport_);
                context_->OMSetRenderTargets(1, bloomBRTV_.GetAddressOf(), nullptr);
                context_->ClearRenderTargetView(bloomBRTV_.Get(), zeroColor);
                context_->PSSetShader(blurHPS_.Get(), nullptr, 0);

                ID3D11ShaderResourceView *blurSrvs[kTextureSlotCount] = {};
                blurSrvs[2] = bloomASRV_.Get();
                context_->PSSetShaderResources(0, kTextureSlotCount, blurSrvs);

                context_->Draw(3, 0);
                context_->PSSetShaderResources(0, kTextureSlotCount, nullSrvs);
            }
            EndEvent();

            BeginEvent(L"Bloom Blur V");
            {
                BlurConstantBuffer blurCB{};
                blurCB.blurParams[0] = 1.0f / safeHalfWidth;
                blurCB.blurParams[1] = 1.0f / safeHalfHeight;
                blurCB.blurParams[2] = 0.0f;
                blurCB.blurParams[3] = 1.0f;
                UpdateDynamicBuffer(context_.Get(), blurCB_.Get(), blurCB);

                ID3D11Buffer *blurBuffer = blurCB_.Get();
                context_->PSSetConstantBuffers(2, 1, &blurBuffer);

                context_->RSSetViewports(1, &halfViewport_);
                context_->OMSetRenderTargets(1, bloomARTV_.GetAddressOf(), nullptr);
                context_->PSSetShader(blurVPS_.Get(), nullptr, 0);

                ID3D11ShaderResourceView *blurSrvs[kTextureSlotCount] = {};
                blurSrvs[5] = bloomBSRV_.Get();
                context_->PSSetShaderResources(0, kTextureSlotCount, blurSrvs);

                context_->Draw(3, 0);
                context_->PSSetShaderResources(0, kTextureSlotCount, nullSrvs);
            }
            EndEvent();
        }
    }
    else
    {
        context_->ClearRenderTargetView(bloomARTV_.Get(), zeroColor);
    }

    BeginEvent(L"Composite Pass");
    {
        context_->RSSetViewports(1, &fullViewport_);
        context_->OMSetRenderTargets(1, postColorRTV_.GetAddressOf(), nullptr);
        context_->ClearRenderTargetView(postColorRTV_.Get(), zeroColor);
        context_->PSSetShader(compositePS_.Get(), nullptr, 0);

        ID3D11Buffer *postBuffer = postCB_.Get();
        context_->PSSetConstantBuffers(1, 1, &postBuffer);

        ID3D11ShaderResourceView *compositeSrvs[kTextureSlotCount] = {};
        compositeSrvs[1] = sceneColorSRV_.Get();
        compositeSrvs[2] = bloomASRV_.Get();
        compositeSrvs[3] = particleSRV_.Get();
        compositeSrvs[4] = historySRV_.Get();
        compositeSrvs[7] = edgeSRV_.Get();
        context_->PSSetShaderResources(0, kTextureSlotCount, compositeSrvs);

        context_->Draw(3, 0);
        context_->PSSetShaderResources(0, kTextureSlotCount, nullSrvs);
    }
    EndEvent();

    // CopyResource requires the source and destination resources to be unbound from the pipeline.
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    context_->CopyResource(historyTex_.Get(), postColorTex_.Get());
    historyValid_ = true;

    BeginEvent(L"Present Pass");
    {
        context_->RSSetViewports(1, &presentViewport_);
        context_->OMSetRenderTargets(1, backBufferRTV_.GetAddressOf(), nullptr);
        context_->PSSetShader(copyPS_.Get(), nullptr, 0);

        ID3D11ShaderResourceView *copySrvs[kTextureSlotCount] = {};
        copySrvs[6] = postColorSRV_.Get();
        context_->PSSetShaderResources(0, kTextureSlotCount, copySrvs);

        context_->Draw(3, 0);
        context_->PSSetShaderResources(0, kTextureSlotCount, nullSrvs);
    }
    EndEvent();

    const HRESULT presentHr = swapChain_->Present(1, 0);
    if (presentHr == DXGI_STATUS_OCCLUDED)
    {
        LogLine("DX11", "Present status occluded");
        return;
    }

    if (FAILED(presentHr))
    {
        LogLine("DX11", "Present failed hr=%s", FormatHResult(presentHr).c_str());
        if (presentHr == DXGI_ERROR_DEVICE_REMOVED ||
            presentHr == DXGI_ERROR_DEVICE_RESET ||
            presentHr == DXGI_ERROR_DRIVER_INTERNAL_ERROR)
        {
            HRESULT removedReason = S_OK;
            if (device_)
            {
                removedReason = device_->GetDeviceRemovedReason();
            }
            LogLine("DX11", "Device lost, reason=%s, reinitializing", FormatHResult(removedReason).c_str());
            Initialize(hwnd_, width_, height_);
        }
        return;
    }
}

void DX11Renderer::Shutdown()
{
    LogLine("DX11", "Shutdown begin");
    if (context_)
    {
        context_->ClearState();
        context_->Flush();
    }

    ReleaseSizeDependentResources();

    shadowRasterState_.Reset();
    sceneRasterState_.Reset();
    shadowDepthState_.Reset();
    sceneDepthState_.Reset();

    shadowSampler_.Reset();
    pointClampSampler_.Reset();
    linearClampSampler_.Reset();

    particleCB_.Reset();
    blurCB_.Reset();
    postCB_.Reset();
    sceneCB_.Reset();

    normalSRV_.Reset();
    normalTex_.Reset();
    albedoSRV_.Reset();
    albedoTex_.Reset();

    indexBuffer_.Reset();
    vertexBuffer_.Reset();
    sceneInputLayout_.Reset();

    edgeDetectCS_.Reset();
    particleCS_.Reset();
    copyPS_.Reset();
    compositePS_.Reset();
    blurVPS_.Reset();
    blurHPS_.Reset();
    brightExtractPS_.Reset();
    fullscreenVS_.Reset();
    shadowVS_.Reset();
    scenePS_.Reset();
    sceneVS_.Reset();

    annotation_.Reset();
    swapChain_.Reset();
    context_.Reset();
    device_.Reset();

    hwnd_ = nullptr;
    width_ = 1;
    height_ = 1;
    renderWidth_ = 1;
    renderHeight_ = 1;
    historyValid_ = false;
    historyTopic_ = 0;
    LogLine("DX11", "Shutdown done");
}

const char *DX11Renderer::BackendName() const
{
    return "DX11";
}

bool DX11Renderer::CreateSwapChainAndDevice()
{
    DXGI_SWAP_CHAIN_DESC swapChainDesc{};
    swapChainDesc.BufferCount = kSwapChainBufferCount;
    swapChainDesc.BufferDesc.Width = width_;
    swapChainDesc.BufferDesc.Height = height_;
    swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.OutputWindow = hwnd_;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.Windowed = TRUE;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    D3D_FEATURE_LEVEL requestedLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0};

    UINT createFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
    createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL createdLevel = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr,
                                               D3D_DRIVER_TYPE_HARDWARE,
                                               nullptr,
                                               createFlags,
                                               requestedLevels,
                                               ARRAYSIZE(requestedLevels),
                                               D3D11_SDK_VERSION,
                                               &swapChainDesc,
                                               swapChain_.GetAddressOf(),
                                               device_.GetAddressOf(),
                                               &createdLevel,
                                               context_.GetAddressOf());

#if defined(_DEBUG)
    if (FAILED(hr))
    {
        createFlags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = D3D11CreateDeviceAndSwapChain(nullptr,
                                           D3D_DRIVER_TYPE_HARDWARE,
                                           nullptr,
                                           createFlags,
                                           requestedLevels,
                                           ARRAYSIZE(requestedLevels),
                                           D3D11_SDK_VERSION,
                                           &swapChainDesc,
                                           swapChain_.GetAddressOf(),
                                           device_.GetAddressOf(),
                                           &createdLevel,
                                           context_.GetAddressOf());
    }
#endif

    return SUCCEEDED(hr);
}

bool DX11Renderer::CreateBackBufferRTV()
{
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    if (FAILED(swapChain_->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()))))
    {
        return false;
    }

    return SUCCEEDED(device_->CreateRenderTargetView(backBuffer.Get(), nullptr, backBufferRTV_.GetAddressOf()));
}

bool DX11Renderer::CreateSizeDependentResources()
{
    if (!CreateBackBufferRTV())
    {
        return false;
    }

    const UINT halfWidth = std::max<UINT>(1u, renderWidth_ / 2u);
    const UINT halfHeight = std::max<UINT>(1u, renderHeight_ / 2u);

    auto createColorTarget = [&](UINT textureWidth,
                                 UINT textureHeight,
                                 DXGI_FORMAT format,
                                 UINT bindFlags,
                                 UINT sampleCount,
                                 Microsoft::WRL::ComPtr<ID3D11Texture2D> &texture,
                                 Microsoft::WRL::ComPtr<ID3D11RenderTargetView> *rtv,
                                 Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> *srv,
                                 Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> *uav) -> bool {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = textureWidth;
        desc.Height = textureHeight;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = format;
        desc.SampleDesc.Count = sampleCount;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = bindFlags;

        if (FAILED(device_->CreateTexture2D(&desc, nullptr, texture.GetAddressOf())))
        {
            return false;
        }

        if (rtv != nullptr)
        {
            if (FAILED(device_->CreateRenderTargetView(texture.Get(), nullptr, rtv->GetAddressOf())))
            {
                return false;
            }
        }

        if (srv != nullptr)
        {
            if (FAILED(device_->CreateShaderResourceView(texture.Get(), nullptr, srv->GetAddressOf())))
            {
                return false;
            }
        }

        if (uav != nullptr)
        {
            if (FAILED(device_->CreateUnorderedAccessView(texture.Get(), nullptr, uav->GetAddressOf())))
            {
                return false;
            }
        }

        return true;
    };

    if (!createColorTarget(renderWidth_,
                           renderHeight_,
                           DXGI_FORMAT_R16G16B16A16_FLOAT,
                           D3D11_BIND_RENDER_TARGET,
                           kSceneSampleCount,
                           sceneColorMSAATex_,
                           &sceneColorMSAARTV_,
                           nullptr,
                           nullptr))
    {
        return false;
    }

    if (!createColorTarget(renderWidth_,
                           renderHeight_,
                           DXGI_FORMAT_R16G16B16A16_FLOAT,
                           D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
                           1,
                           sceneColorTex_,
                           &sceneColorRTV_,
                           &sceneColorSRV_,
                           nullptr))
    {
        return false;
    }

    if (!createColorTarget(renderWidth_,
                           renderHeight_,
                           DXGI_FORMAT_R16G16B16A16_FLOAT,
                           D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
                           1,
                           postColorTex_,
                           &postColorRTV_,
                           &postColorSRV_,
                           nullptr))
    {
        return false;
    }

    if (!createColorTarget(renderWidth_,
                           renderHeight_,
                           DXGI_FORMAT_R16G16B16A16_FLOAT,
                           D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
                           1,
                           historyTex_,
                           &historyRTV_,
                           &historySRV_,
                           nullptr))
    {
        return false;
    }

    if (!createColorTarget(halfWidth,
                           halfHeight,
                           DXGI_FORMAT_R16G16B16A16_FLOAT,
                           D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
                           1,
                           bloomATex_,
                           &bloomARTV_,
                           &bloomASRV_,
                           nullptr))
    {
        return false;
    }

    if (!createColorTarget(renderWidth_,
                           renderHeight_,
                           DXGI_FORMAT_R8G8B8A8_UNORM,
                           D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
                           1,
                           edgeTex_,
                           nullptr,
                           &edgeSRV_,
                           &edgeUAV_))
    {
        return false;
    }

    if (!createColorTarget(halfWidth,
                           halfHeight,
                           DXGI_FORMAT_R16G16B16A16_FLOAT,
                           D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
                           1,
                           bloomBTex_,
                           &bloomBRTV_,
                           &bloomBSRV_,
                           nullptr))
    {
        return false;
    }

    if (!createColorTarget(halfWidth,
                           halfHeight,
                           DXGI_FORMAT_R16G16B16A16_FLOAT,
                           D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
                           1,
                           particleTex_,
                           nullptr,
                           &particleSRV_,
                           &particleUAV_))
    {
        return false;
    }

    D3D11_TEXTURE2D_DESC sceneDepthDesc{};
    sceneDepthDesc.Width = renderWidth_;
    sceneDepthDesc.Height = renderHeight_;
    sceneDepthDesc.MipLevels = 1;
    sceneDepthDesc.ArraySize = 1;
    sceneDepthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    sceneDepthDesc.SampleDesc.Count = kSceneSampleCount;
    sceneDepthDesc.Usage = D3D11_USAGE_DEFAULT;
    sceneDepthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    if (FAILED(device_->CreateTexture2D(&sceneDepthDesc, nullptr, sceneDepthTex_.GetAddressOf())))
    {
        return false;
    }

    if (FAILED(device_->CreateDepthStencilView(sceneDepthTex_.Get(), nullptr, sceneDepthDSV_.GetAddressOf())))
    {
        return false;
    }

    D3D11_TEXTURE2D_DESC shadowDesc{};
    shadowDesc.Width = kShadowMapSize;
    shadowDesc.Height = kShadowMapSize;
    shadowDesc.MipLevels = 1;
    shadowDesc.ArraySize = 1;
    shadowDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    shadowDesc.SampleDesc.Count = 1;
    shadowDesc.Usage = D3D11_USAGE_DEFAULT;
    shadowDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

    if (FAILED(device_->CreateTexture2D(&shadowDesc, nullptr, shadowDepthTex_.GetAddressOf())))
    {
        return false;
    }

    D3D11_DEPTH_STENCIL_VIEW_DESC shadowDsvDesc{};
    shadowDsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    shadowDsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    shadowDsvDesc.Texture2D.MipSlice = 0;

    if (FAILED(device_->CreateDepthStencilView(shadowDepthTex_.Get(), &shadowDsvDesc, shadowDepthDSV_.GetAddressOf())))
    {
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC shadowSrvDesc{};
    shadowSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    shadowSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    shadowSrvDesc.Texture2D.MostDetailedMip = 0;
    shadowSrvDesc.Texture2D.MipLevels = 1;

    if (FAILED(device_->CreateShaderResourceView(shadowDepthTex_.Get(), &shadowSrvDesc, shadowDepthSRV_.GetAddressOf())))
    {
        return false;
    }

    const float zeroColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    context_->ClearRenderTargetView(historyRTV_.Get(), zeroColor);
    return true;
}

void DX11Renderer::ReleaseSizeDependentResources()
{
    shadowDepthSRV_.Reset();
    shadowDepthDSV_.Reset();
    shadowDepthTex_.Reset();

    sceneDepthDSV_.Reset();
    sceneDepthTex_.Reset();

    particleUAV_.Reset();
    particleSRV_.Reset();
    particleTex_.Reset();

    edgeUAV_.Reset();
    edgeSRV_.Reset();
    edgeTex_.Reset();

    bloomBSRV_.Reset();
    bloomBRTV_.Reset();
    bloomBTex_.Reset();

    bloomASRV_.Reset();
    bloomARTV_.Reset();
    bloomATex_.Reset();

    historySRV_.Reset();
    historyRTV_.Reset();
    historyTex_.Reset();

    postColorSRV_.Reset();
    postColorRTV_.Reset();
    postColorTex_.Reset();

    sceneColorSRV_.Reset();
    sceneColorRTV_.Reset();
    sceneColorTex_.Reset();

    sceneColorMSAARTV_.Reset();
    sceneColorMSAATex_.Reset();

    backBufferRTV_.Reset();
}

void DX11Renderer::UpdateViewports()
{
    fullViewport_.TopLeftX = 0.0f;
    fullViewport_.TopLeftY = 0.0f;
    fullViewport_.Width = static_cast<float>(renderWidth_);
    fullViewport_.Height = static_cast<float>(renderHeight_);
    fullViewport_.MinDepth = 0.0f;
    fullViewport_.MaxDepth = 1.0f;

    halfViewport_.TopLeftX = 0.0f;
    halfViewport_.TopLeftY = 0.0f;
    halfViewport_.Width = static_cast<float>(std::max<uint32_t>(1u, renderWidth_ / 2u));
    halfViewport_.Height = static_cast<float>(std::max<uint32_t>(1u, renderHeight_ / 2u));
    halfViewport_.MinDepth = 0.0f;
    halfViewport_.MaxDepth = 1.0f;

    presentViewport_.TopLeftX = 0.0f;
    presentViewport_.TopLeftY = 0.0f;
    presentViewport_.Width = static_cast<float>(width_);
    presentViewport_.Height = static_cast<float>(height_);
    presentViewport_.MinDepth = 0.0f;
    presentViewport_.MaxDepth = 1.0f;

    shadowViewport_.TopLeftX = 0.0f;
    shadowViewport_.TopLeftY = 0.0f;
    shadowViewport_.Width = static_cast<float>(kShadowMapSize);
    shadowViewport_.Height = static_cast<float>(kShadowMapSize);
    shadowViewport_.MinDepth = 0.0f;
    shadowViewport_.MaxDepth = 1.0f;
}

bool DX11Renderer::CompileShader(const char *entryPoint,
                                 const char *target,
                                 Microsoft::WRL::ComPtr<ID3DBlob> &shaderBlob) const
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

bool DX11Renderer::CreatePipelineShaders()
{
    Microsoft::WRL::ComPtr<ID3DBlob> sceneVsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> scenePsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> shadowVsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> fullscreenVsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> brightPsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> blurHBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> blurVBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> compositeBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> copyBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> particleBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> edgeBlob;

    if (!CompileShader("SceneVS", "vs_5_0", sceneVsBlob) ||
        !CompileShader("ScenePS", "ps_5_0", scenePsBlob) ||
        !CompileShader("ShadowVS", "vs_5_0", shadowVsBlob) ||
        !CompileShader("FullscreenVS", "vs_5_0", fullscreenVsBlob) ||
        !CompileShader("BrightExtractPS", "ps_5_0", brightPsBlob) ||
        !CompileShader("BlurHPS", "ps_5_0", blurHBlob) ||
        !CompileShader("BlurVPS", "ps_5_0", blurVBlob) ||
        !CompileShader("CompositePS", "ps_5_0", compositeBlob) ||
        !CompileShader("CopyPS", "ps_5_0", copyBlob) ||
        !CompileShader("ParticleCS", "cs_5_0", particleBlob) ||
        !CompileShader("EdgeDetectCS", "cs_5_0", edgeBlob))
    {
        return false;
    }

    if (FAILED(device_->CreateVertexShader(sceneVsBlob->GetBufferPointer(),
                                            sceneVsBlob->GetBufferSize(),
                                            nullptr,
                                            sceneVS_.GetAddressOf())) ||
        FAILED(device_->CreatePixelShader(scenePsBlob->GetBufferPointer(),
                                          scenePsBlob->GetBufferSize(),
                                          nullptr,
                                          scenePS_.GetAddressOf())) ||
        FAILED(device_->CreateVertexShader(shadowVsBlob->GetBufferPointer(),
                                           shadowVsBlob->GetBufferSize(),
                                           nullptr,
                                           shadowVS_.GetAddressOf())) ||
        FAILED(device_->CreateVertexShader(fullscreenVsBlob->GetBufferPointer(),
                                           fullscreenVsBlob->GetBufferSize(),
                                           nullptr,
                                           fullscreenVS_.GetAddressOf())) ||
        FAILED(device_->CreatePixelShader(brightPsBlob->GetBufferPointer(),
                                          brightPsBlob->GetBufferSize(),
                                          nullptr,
                                          brightExtractPS_.GetAddressOf())) ||
        FAILED(device_->CreatePixelShader(blurHBlob->GetBufferPointer(),
                                          blurHBlob->GetBufferSize(),
                                          nullptr,
                                          blurHPS_.GetAddressOf())) ||
        FAILED(device_->CreatePixelShader(blurVBlob->GetBufferPointer(),
                                          blurVBlob->GetBufferSize(),
                                          nullptr,
                                          blurVPS_.GetAddressOf())) ||
        FAILED(device_->CreatePixelShader(compositeBlob->GetBufferPointer(),
                                          compositeBlob->GetBufferSize(),
                                          nullptr,
                                          compositePS_.GetAddressOf())) ||
        FAILED(device_->CreatePixelShader(copyBlob->GetBufferPointer(),
                                          copyBlob->GetBufferSize(),
                                          nullptr,
                                          copyPS_.GetAddressOf())) ||
        FAILED(device_->CreateComputeShader(particleBlob->GetBufferPointer(),
                                            particleBlob->GetBufferSize(),
                                            nullptr,
                                            particleCS_.GetAddressOf())) ||
        FAILED(device_->CreateComputeShader(edgeBlob->GetBufferPointer(),
                                            edgeBlob->GetBufferSize(),
                                            nullptr,
                                            edgeDetectCS_.GetAddressOf())))
    {
        return false;
    }

    const D3D11_INPUT_ELEMENT_DESC inputLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0}};

    return SUCCEEDED(device_->CreateInputLayout(inputLayout,
                                                 ARRAYSIZE(inputLayout),
                                                 sceneVsBlob->GetBufferPointer(),
                                                 sceneVsBlob->GetBufferSize(),
                                                 sceneInputLayout_.GetAddressOf()));
}

bool DX11Renderer::CreateMaterialTextures()
{
    auto createTexture = [&](const uint32_t *pixels,
                             Microsoft::WRL::ComPtr<ID3D11Texture2D> &texture,
                             Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> &srv) -> bool {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = kMaterialTextureSize;
        desc.Height = kMaterialTextureSize;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA data{};
        data.pSysMem = pixels;
        data.SysMemPitch = kMaterialTextureSize * sizeof(uint32_t);

        if (FAILED(device_->CreateTexture2D(&desc, &data, texture.GetAddressOf())))
        {
            return false;
        }

        return SUCCEEDED(device_->CreateShaderResourceView(texture.Get(), nullptr, srv.GetAddressOf()));
    };

    const auto albedo = BuildMaterialAlbedoPixels();
    const auto normal = BuildMaterialNormalPixels();
    return createTexture(albedo.data(), albedoTex_, albedoSRV_) &&
           createTexture(normal.data(), normalTex_, normalSRV_);
}

bool DX11Renderer::CreateGeometryBuffers()
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

    D3D11_BUFFER_DESC vbDesc{};
    vbDesc.ByteWidth = static_cast<UINT>(sizeof(vertices));
    vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA vbData{};
    vbData.pSysMem = vertices;

    if (FAILED(device_->CreateBuffer(&vbDesc, &vbData, vertexBuffer_.GetAddressOf())))
    {
        return false;
    }

    D3D11_BUFFER_DESC ibDesc{};
    ibDesc.ByteWidth = static_cast<UINT>(sizeof(indices));
    ibDesc.Usage = D3D11_USAGE_IMMUTABLE;
    ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

    D3D11_SUBRESOURCE_DATA ibData{};
    ibData.pSysMem = indices;

    return SUCCEEDED(device_->CreateBuffer(&ibDesc, &ibData, indexBuffer_.GetAddressOf()));
}

bool DX11Renderer::CreateConstantBuffers()
{
    auto createCB = [&](UINT sizeInBytes, Microsoft::WRL::ComPtr<ID3D11Buffer> &buffer) -> bool {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = Align16(sizeInBytes);
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        return SUCCEEDED(device_->CreateBuffer(&desc, nullptr, buffer.GetAddressOf()));
    };

    return createCB(static_cast<UINT>(sizeof(SceneConstantBuffer)), sceneCB_) &&
           createCB(static_cast<UINT>(sizeof(PostConstantBuffer)), postCB_) &&
           createCB(static_cast<UINT>(sizeof(BlurConstantBuffer)), blurCB_) &&
           createCB(static_cast<UINT>(sizeof(ParticleConstantBuffer)), particleCB_);
}

bool DX11Renderer::CreateStatesAndSamplers()
{
    D3D11_SAMPLER_DESC linearDesc{};
    linearDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    linearDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    linearDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    linearDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    linearDesc.MaxLOD = D3D11_FLOAT32_MAX;

    if (FAILED(device_->CreateSamplerState(&linearDesc, linearClampSampler_.GetAddressOf())))
    {
        return false;
    }

    D3D11_SAMPLER_DESC pointDesc = linearDesc;
    pointDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    if (FAILED(device_->CreateSamplerState(&pointDesc, pointClampSampler_.GetAddressOf())))
    {
        return false;
    }

    D3D11_SAMPLER_DESC shadowDesc{};
    shadowDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    shadowDesc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
    shadowDesc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
    shadowDesc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
    shadowDesc.BorderColor[0] = 1.0f;
    shadowDesc.BorderColor[1] = 1.0f;
    shadowDesc.BorderColor[2] = 1.0f;
    shadowDesc.BorderColor[3] = 1.0f;
    shadowDesc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
    shadowDesc.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device_->CreateSamplerState(&shadowDesc, shadowSampler_.GetAddressOf())))
    {
        return false;
    }

    D3D11_DEPTH_STENCIL_DESC sceneDepthDesc{};
    sceneDepthDesc.DepthEnable = TRUE;
    sceneDepthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    sceneDepthDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    if (FAILED(device_->CreateDepthStencilState(&sceneDepthDesc, sceneDepthState_.GetAddressOf())))
    {
        return false;
    }

    D3D11_DEPTH_STENCIL_DESC shadowDepthDesc = sceneDepthDesc;
    if (FAILED(device_->CreateDepthStencilState(&shadowDepthDesc, shadowDepthState_.GetAddressOf())))
    {
        return false;
    }

    D3D11_RASTERIZER_DESC sceneRasterDesc{};
    sceneRasterDesc.FillMode = D3D11_FILL_SOLID;
    sceneRasterDesc.CullMode = D3D11_CULL_BACK;
    sceneRasterDesc.DepthClipEnable = TRUE;
    sceneRasterDesc.MultisampleEnable = TRUE;
    if (FAILED(device_->CreateRasterizerState(&sceneRasterDesc, sceneRasterState_.GetAddressOf())))
    {
        return false;
    }

    D3D11_RASTERIZER_DESC shadowRasterDesc = sceneRasterDesc;
    shadowRasterDesc.DepthBias = 1500;
    shadowRasterDesc.SlopeScaledDepthBias = 1.4f;
    shadowRasterDesc.DepthBiasClamp = 0.0f;
    if (FAILED(device_->CreateRasterizerState(&shadowRasterDesc, shadowRasterState_.GetAddressOf())))
    {
        return false;
    }

    return true;
}

void DX11Renderer::BeginEvent(const wchar_t *label) const
{
    if (annotation_)
    {
        annotation_->BeginEvent(label);
    }
}

void DX11Renderer::EndEvent() const
{
    if (annotation_)
    {
        annotation_->EndEvent();
    }
}

} // namespace dxteaching
