#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "IRendererBackend.h"

namespace dxteaching
{

class VulkanRenderer final : public IRendererBackend
{
public:
    enum class RenderMode
    {
        Triangle = 0,
        Cube = 1
    };

    VulkanRenderer() = default;
    ~VulkanRenderer() override;

    bool Initialize(HWND hwnd, uint32_t width, uint32_t height) override;
    void Resize(uint32_t width, uint32_t height) override;
    void Render(const FrameSettings &settings) override;
    void Shutdown() override;
    const char *BackendName() const override;

    void SetRenderMode(RenderMode mode);
    RenderMode GetRenderMode() const;

private:
    struct QueueFamilyIndices
    {
        std::optional<uint32_t> graphicsFamily;
        std::optional<uint32_t> presentFamily;

        bool Complete() const
        {
            return graphicsFamily.has_value() && presentFamily.has_value();
        }
    };

    struct SwapchainSupportDetails
    {
        VkSurfaceCapabilitiesKHR capabilities{};
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    struct PushConstants
    {
        std::array<float, 4> params0{};
        std::array<float, 4> params1{};
    };

    bool CreateInstance();
    bool CreateSurface();
    bool PickPhysicalDevice();
    bool CreateLogicalDevice();
    bool CreateCommandPool();
    bool CreateSwapchainResources();
    bool CreateSwapchain();
    bool CreateImageViews();
    bool CreateRenderPass();
    bool CreatePipeline();
    bool CreateDepthResources();
    bool CreateFramebuffers();
    bool AllocateCommandBuffers();
    bool CreateVertexBuffer();
    bool CreateMaterialResources();
    bool CreateMaterialDescriptorSetLayout();
    bool CreateMaterialTextures();
    bool CreateMaterialDescriptorSet();
    bool CreateSyncObjects();
    void CleanupSwapchainResources();
    bool RecreateSwapchain();

    QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device) const;
    SwapchainSupportDetails QuerySwapchainSupport(VkPhysicalDevice device) const;
    VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &formats) const;
    VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR> &presentModes) const;
    VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities) const;
    bool CheckDeviceExtensionSupport(VkPhysicalDevice device) const;
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
    bool CreateBuffer(VkDeviceSize size,
                      VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags properties,
                      VkBuffer &buffer,
                      VkDeviceMemory &memory) const;
    bool CreateTextureImage(const std::vector<uint32_t> &pixels,
                            VkImage &image,
                            VkDeviceMemory &memory);
    VkCommandBuffer BeginSingleTimeCommands() const;
    bool EndSingleTimeCommands(VkCommandBuffer commandBuffer) const;
    bool TransitionImageLayout(VkImage image,
                               VkImageLayout oldLayout,
                               VkImageLayout newLayout) const;
    bool CopyBufferToImage(VkBuffer buffer,
                           VkImage image,
                           uint32_t width,
                           uint32_t height) const;
    bool RecordCommandBuffer(VkCommandBuffer commandBuffer,
                             uint32_t imageIndex,
                             const FrameSettings &settings);
    VkFormat FindDepthFormat() const;
    VkFormat FindSupportedFormat(const std::vector<VkFormat> &candidates,
                                 VkImageTiling tiling,
                                 VkFormatFeatureFlags features) const;
    bool CreateImage(uint32_t width,
                     uint32_t height,
                     VkFormat format,
                     VkImageTiling tiling,
                     VkImageUsageFlags usage,
                     VkMemoryPropertyFlags properties,
                     VkImage &image,
                     VkDeviceMemory &memory,
                     VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT) const;
    VkImageView CreateImageView(VkImage image,
                                VkFormat format,
                                VkImageAspectFlags aspectMask) const;
    std::vector<char> ReadBinaryFile(const std::string &path) const;
    std::string ResolveShaderPath(const char *fileName) const;
    VkSampleCountFlagBits GetMaxUsableSampleCount() const;
    bool CreateMSAAResources();
    bool CreateComputePipeline();
    void CleanupComputeResources();

private:
    static constexpr uint32_t kMaxFramesInFlight = 2;

    HWND hwnd_ = nullptr;
    uint32_t width_ = 1;
    uint32_t height_ = 1;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamily_ = 0;
    uint32_t presentQueueFamily_ = 0;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapchainImageFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchainExtent_{};
    std::vector<VkImage> swapchainImages_;
    std::vector<VkImageView> swapchainImageViews_;

    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;

    VkPipelineLayout computePipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline computePipeline_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout computeDescriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSet computeDescriptorSet_ = VK_NULL_HANDLE;
    VkImage computeStorageImage_ = VK_NULL_HANDLE;
    VkDeviceMemory computeStorageMemory_ = VK_NULL_HANDLE;
    VkImageView computeStorageImageView_ = VK_NULL_HANDLE;
    VkImage depthImage_ = VK_NULL_HANDLE;
    VkDeviceMemory depthImageMemory_ = VK_NULL_HANDLE;
    VkImageView depthImageView_ = VK_NULL_HANDLE;
    VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;
    std::vector<VkFramebuffer> framebuffers_;

    VkSampleCountFlagBits msaaSamples_ = VK_SAMPLE_COUNT_1_BIT;
    VkImage msaaColorImage_ = VK_NULL_HANDLE;
    VkDeviceMemory msaaColorMemory_ = VK_NULL_HANDLE;
    VkImageView msaaColorImageView_ = VK_NULL_HANDLE;
    VkImage msaaDepthImage_ = VK_NULL_HANDLE;
    VkDeviceMemory msaaDepthMemory_ = VK_NULL_HANDLE;
    VkImageView msaaDepthImageView_ = VK_NULL_HANDLE;

    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers_;

    VkBuffer vertexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory vertexBufferMemory_ = VK_NULL_HANDLE;
    VkBuffer indexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory indexBufferMemory_ = VK_NULL_HANDLE;
    uint32_t cubeIndexCount_ = 0;
    VkImage albedoImage_ = VK_NULL_HANDLE;
    VkDeviceMemory albedoImageMemory_ = VK_NULL_HANDLE;
    VkImageView albedoImageView_ = VK_NULL_HANDLE;
    VkImage normalImage_ = VK_NULL_HANDLE;
    VkDeviceMemory normalImageMemory_ = VK_NULL_HANDLE;
    VkImageView normalImageView_ = VK_NULL_HANDLE;
    VkSampler materialSampler_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;
    uint32_t triangleFirstVertex_ = 0;
    uint32_t triangleVertexCount_ = 0;
    uint32_t cubeFirstVertex_ = 0;
    uint32_t cubeVertexCount_ = 0;
    RenderMode renderMode_ = RenderMode::Triangle;

    std::array<VkSemaphore, kMaxFramesInFlight> imageAvailableSemaphores_{};
    std::array<VkSemaphore, kMaxFramesInFlight> renderFinishedSemaphores_{};
    std::array<VkFence, kMaxFramesInFlight> inFlightFences_{};
    std::vector<VkFence> imagesInFlight_;
    uint32_t currentFrame_ = 0;
};

} // namespace dxteaching
