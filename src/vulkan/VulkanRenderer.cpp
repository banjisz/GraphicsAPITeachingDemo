#include "VulkanRenderer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "DXTeachingShared.h"
#include "DiagnosticsLog.h"

namespace dxteaching
{

namespace
{

constexpr std::array<const char *, 1> kDeviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME};

struct Vertex
{
    float position[3];
    float normal[3];
    float color[3];
};

constexpr std::array<Vertex, 3> kTriangleVertices = {
    Vertex{{0.0f, -0.60f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.00f, 0.35f, 0.20f}},
    Vertex{{0.65f, 0.55f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.20f, 0.75f, 1.00f}},
    Vertex{{-0.65f, 0.55f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.30f, 1.00f, 0.45f}},
};

std::vector<Vertex> BuildCubeVertices()
{
    const float s = 0.8f;
    const std::array<std::array<float, 3>, 8> p = {
        std::array<float, 3>{-s, -s, -s},
        std::array<float, 3>{s, -s, -s},
        std::array<float, 3>{s, s, -s},
        std::array<float, 3>{-s, s, -s},
        std::array<float, 3>{-s, -s, s},
        std::array<float, 3>{s, -s, s},
        std::array<float, 3>{s, s, s},
        std::array<float, 3>{-s, s, s},
    };

    struct FaceDesc
    {
        int i0;
        int i1;
        int i2;
        int i3;
        std::array<float, 3> normal;
        std::array<float, 3> color;
    };

    const std::array<FaceDesc, 6> faces = {
        FaceDesc{4, 5, 6, 7, {0.0f, 0.0f, 1.0f}, {1.00f, 0.40f, 0.25f}},   // front
        FaceDesc{1, 0, 3, 2, {0.0f, 0.0f, -1.0f}, {0.25f, 0.85f, 1.00f}},  // back
        FaceDesc{0, 4, 7, 3, {-1.0f, 0.0f, 0.0f}, {0.30f, 1.00f, 0.50f}},  // left
        FaceDesc{5, 1, 2, 6, {1.0f, 0.0f, 0.0f}, {1.00f, 0.85f, 0.25f}},   // right
        FaceDesc{3, 7, 6, 2, {0.0f, 1.0f, 0.0f}, {0.80f, 0.45f, 1.00f}},   // top
        FaceDesc{0, 1, 5, 4, {0.0f, -1.0f, 0.0f}, {0.30f, 0.65f, 1.00f}},  // bottom
    };

    std::vector<Vertex> vertices;
    vertices.reserve(36);

    auto pushVertex = [&vertices, &p](int idx, const std::array<float, 3> &n, const std::array<float, 3> &c) {
        vertices.push_back(Vertex{
            {p[static_cast<size_t>(idx)][0], p[static_cast<size_t>(idx)][1], p[static_cast<size_t>(idx)][2]},
            {n[0], n[1], n[2]},
            {c[0], c[1], c[2]}});
    };

    for (const auto &face : faces)
    {
        // tri 1
        pushVertex(face.i0, face.normal, face.color);
        pushVertex(face.i1, face.normal, face.color);
        pushVertex(face.i2, face.normal, face.color);
        // tri 2
        pushVertex(face.i0, face.normal, face.color);
        pushVertex(face.i2, face.normal, face.color);
        pushVertex(face.i3, face.normal, face.color);
    }

    return vertices;
}

bool FileExists(const std::string &path)
{
    const DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

VkShaderModule CreateShaderModule(VkDevice device, const std::vector<char> &code)
{
    if (code.empty() || (code.size() % sizeof(uint32_t)) != 0)
    {
        return VK_NULL_HANDLE;
    }

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t *>(code.data());

    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &module) != VK_SUCCESS)
    {
        return VK_NULL_HANDLE;
    }

    return module;
}

} // namespace

VulkanRenderer::~VulkanRenderer()
{
    Shutdown();
}

bool VulkanRenderer::Initialize(HWND hwnd, uint32_t width, uint32_t height)
{
    LogLine("VK", "Initialize begin size=%ux%u", width, height);

    hwnd_ = hwnd;
    width_ = std::max<uint32_t>(1u, width);
    height_ = std::max<uint32_t>(1u, height);

    if (!CreateInstance())
    {
        LogLine("VK", "CreateInstance failed");
        Shutdown();
        return false;
    }

    if (!CreateSurface())
    {
        LogLine("VK", "CreateSurface failed");
        Shutdown();
        return false;
    }

    if (!PickPhysicalDevice())
    {
        LogLine("VK", "PickPhysicalDevice failed");
        Shutdown();
        return false;
    }

    if (!CreateLogicalDevice())
    {
        LogLine("VK", "CreateLogicalDevice failed");
        Shutdown();
        return false;
    }

    if (!CreateCommandPool())
    {
        LogLine("VK", "CreateCommandPool failed");
        Shutdown();
        return false;
    }

    if (!CreateVertexBuffer())
    {
        LogLine("VK", "CreateVertexBuffer failed");
        Shutdown();
        return false;
    }

    if (!CreateSwapchainResources())
    {
        LogLine("VK", "CreateSwapchainResources failed");
        Shutdown();
        return false;
    }

    if (!CreateSyncObjects())
    {
        LogLine("VK", "CreateSyncObjects failed");
        Shutdown();
        return false;
    }

    LogLine("VK", "Initialize success");
    return true;
}

void VulkanRenderer::Resize(uint32_t width, uint32_t height)
{
    width_ = width;
    height_ = height;

    if (device_ == VK_NULL_HANDLE || width_ == 0 || height_ == 0)
    {
        return;
    }

    if (!RecreateSwapchain())
    {
        LogLine("VK", "Resize recreate swapchain failed");
    }
}

void VulkanRenderer::Render(const FrameSettings &settings)
{
    if (device_ == VK_NULL_HANDLE || swapchain_ == VK_NULL_HANDLE || width_ == 0 || height_ == 0)
    {
        return;
    }

    const VkFence inFlight = inFlightFences_[currentFrame_];
    vkWaitForFences(device_, 1, &inFlight, VK_TRUE, UINT64_MAX);

    uint32_t imageIndex = 0;
    VkResult acquireResult = vkAcquireNextImageKHR(
        device_,
        swapchain_,
        UINT64_MAX,
        imageAvailableSemaphores_[currentFrame_],
        VK_NULL_HANDLE,
        &imageIndex);

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
    {
        RecreateSwapchain();
        return;
    }

    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR)
    {
        LogLine("VK", "vkAcquireNextImageKHR failed result=%d", static_cast<int>(acquireResult));
        return;
    }

    if (imageIndex >= imagesInFlight_.size())
    {
        LogLine("VK", "Acquire returned invalid image index=%u", imageIndex);
        return;
    }

    if (imagesInFlight_[imageIndex] != VK_NULL_HANDLE)
    {
        vkWaitForFences(device_, 1, &imagesInFlight_[imageIndex], VK_TRUE, UINT64_MAX);
    }
    imagesInFlight_[imageIndex] = inFlight;

    vkResetFences(device_, 1, &inFlight);

    vkResetCommandBuffer(commandBuffers_[imageIndex], 0);
    if (!RecordCommandBuffer(commandBuffers_[imageIndex], imageIndex, settings))
    {
        LogLine("VK", "RecordCommandBuffer failed");
        return;
    }

    const VkSemaphore waitSemaphores[] = {imageAvailableSemaphores_[currentFrame_]};
    const VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    const VkSemaphore signalSemaphores[] = {renderFinishedSemaphores_[currentFrame_]};

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers_[imageIndex];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(graphicsQueue_, 1, &submitInfo, inFlight) != VK_SUCCESS)
    {
        LogLine("VK", "vkQueueSubmit failed");
        return;
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain_;
    presentInfo.pImageIndices = &imageIndex;

    VkResult presentResult = vkQueuePresentKHR(presentQueue_, &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR)
    {
        RecreateSwapchain();
    }
    else if (presentResult != VK_SUCCESS)
    {
        LogLine("VK", "vkQueuePresentKHR failed result=%d", static_cast<int>(presentResult));
    }

    currentFrame_ = (currentFrame_ + 1u) % kMaxFramesInFlight;
}

void VulkanRenderer::Shutdown()
{
    if (instance_ == VK_NULL_HANDLE)
    {
        return;
    }

    LogLine("VK", "Shutdown begin");

    if (device_ != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(device_);
    }

    CleanupSwapchainResources();

    if (vertexBuffer_ != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(device_, vertexBuffer_, nullptr);
        vertexBuffer_ = VK_NULL_HANDLE;
    }
    if (vertexBufferMemory_ != VK_NULL_HANDLE)
    {
        vkFreeMemory(device_, vertexBufferMemory_, nullptr);
        vertexBufferMemory_ = VK_NULL_HANDLE;
    }

    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i)
    {
        if (imageAvailableSemaphores_[i] != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(device_, imageAvailableSemaphores_[i], nullptr);
            imageAvailableSemaphores_[i] = VK_NULL_HANDLE;
        }
        if (renderFinishedSemaphores_[i] != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(device_, renderFinishedSemaphores_[i], nullptr);
            renderFinishedSemaphores_[i] = VK_NULL_HANDLE;
        }
        if (inFlightFences_[i] != VK_NULL_HANDLE)
        {
            vkDestroyFence(device_, inFlightFences_[i], nullptr);
            inFlightFences_[i] = VK_NULL_HANDLE;
        }
    }
    imagesInFlight_.clear();

    if (commandPool_ != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(device_, commandPool_, nullptr);
        commandPool_ = VK_NULL_HANDLE;
    }

    if (device_ != VK_NULL_HANDLE)
    {
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }

    if (surface_ != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }

    vkDestroyInstance(instance_, nullptr);
    instance_ = VK_NULL_HANDLE;

    physicalDevice_ = VK_NULL_HANDLE;
    graphicsQueue_ = VK_NULL_HANDLE;
    presentQueue_ = VK_NULL_HANDLE;
    currentFrame_ = 0;

    LogLine("VK", "Shutdown done");
}

const char *VulkanRenderer::BackendName() const
{
    return "Vulkan";
}

void VulkanRenderer::SetRenderMode(RenderMode mode)
{
    renderMode_ = mode;
    LogLine("VK", "SetRenderMode mode=%s", renderMode_ == RenderMode::Triangle ? "Triangle" : "Cube");
}

VulkanRenderer::RenderMode VulkanRenderer::GetRenderMode() const
{
    return renderMode_;
}

bool VulkanRenderer::CreateInstance()
{
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Graphics API Teaching Demo";
    appInfo.applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
    appInfo.pEngineName = "TeachingEngine";
    appInfo.engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    const std::array<const char *, 2> extensions = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
    };

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    createInfo.enabledLayerCount = 0;

    if (vkCreateInstance(&createInfo, nullptr, &instance_) != VK_SUCCESS)
    {
        return false;
    }

    return true;
}

bool VulkanRenderer::CreateSurface()
{
    VkWin32SurfaceCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    createInfo.hinstance = GetModuleHandleA(nullptr);
    createInfo.hwnd = hwnd_;

    return vkCreateWin32SurfaceKHR(instance_, &createInfo, nullptr, &surface_) == VK_SUCCESS;
}

bool VulkanRenderer::PickPhysicalDevice()
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr);
    if (deviceCount == 0)
    {
        LogLine("VK", "No Vulkan physical device found");
        return false;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data());

    for (VkPhysicalDevice candidate : devices)
    {
        const QueueFamilyIndices indices = FindQueueFamilies(candidate);
        const bool extensionSupported = CheckDeviceExtensionSupport(candidate);
        bool swapchainAdequate = false;
        if (extensionSupported)
        {
            const SwapchainSupportDetails details = QuerySwapchainSupport(candidate);
            swapchainAdequate = !details.formats.empty() && !details.presentModes.empty();
        }

        if (indices.Complete() && extensionSupported && swapchainAdequate)
        {
            physicalDevice_ = candidate;
            graphicsQueueFamily_ = indices.graphicsFamily.value();
            presentQueueFamily_ = indices.presentFamily.value();
            break;
        }
    }

    if (physicalDevice_ == VK_NULL_HANDLE)
    {
        return false;
    }

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice_, &properties);
    LogLine("VK", "Using device: %s", properties.deviceName);
    return true;
}

bool VulkanRenderer::CreateLogicalDevice()
{
    std::set<uint32_t> uniqueQueueFamilies = {graphicsQueueFamily_, presentQueueFamily_};
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    queueCreateInfos.reserve(uniqueQueueFamilies.size());

    const float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies)
    {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures deviceFeatures{};

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(kDeviceExtensions.size());
    createInfo.ppEnabledExtensionNames = kDeviceExtensions.data();

    if (vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_) != VK_SUCCESS)
    {
        return false;
    }

    vkGetDeviceQueue(device_, graphicsQueueFamily_, 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, presentQueueFamily_, 0, &presentQueue_);
    return true;
}

bool VulkanRenderer::CreateCommandPool()
{
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = graphicsQueueFamily_;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    return vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_) == VK_SUCCESS;
}

bool VulkanRenderer::CreateSwapchainResources()
{
    if (!CreateSwapchain())
    {
        return false;
    }
    if (!CreateImageViews())
    {
        return false;
    }
    if (!CreateRenderPass())
    {
        return false;
    }
    if (!CreatePipeline())
    {
        return false;
    }
    if (!CreateDepthResources())
    {
        return false;
    }
    if (!CreateFramebuffers())
    {
        return false;
    }
    if (!AllocateCommandBuffers())
    {
        return false;
    }

    imagesInFlight_.assign(swapchainImages_.size(), VK_NULL_HANDLE);
    return true;
}

void VulkanRenderer::CleanupSwapchainResources()
{
    if (device_ == VK_NULL_HANDLE)
    {
        return;
    }

    if (!commandBuffers_.empty())
    {
        vkFreeCommandBuffers(device_,
                             commandPool_,
                             static_cast<uint32_t>(commandBuffers_.size()),
                             commandBuffers_.data());
        commandBuffers_.clear();
    }

    for (VkFramebuffer framebuffer : framebuffers_)
    {
        vkDestroyFramebuffer(device_, framebuffer, nullptr);
    }
    framebuffers_.clear();

    if (graphicsPipeline_ != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device_, graphicsPipeline_, nullptr);
        graphicsPipeline_ = VK_NULL_HANDLE;
    }
    if (pipelineLayout_ != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
    }
    if (renderPass_ != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(device_, renderPass_, nullptr);
        renderPass_ = VK_NULL_HANDLE;
    }

    if (depthImageView_ != VK_NULL_HANDLE)
    {
        vkDestroyImageView(device_, depthImageView_, nullptr);
        depthImageView_ = VK_NULL_HANDLE;
    }
    if (depthImage_ != VK_NULL_HANDLE)
    {
        vkDestroyImage(device_, depthImage_, nullptr);
        depthImage_ = VK_NULL_HANDLE;
    }
    if (depthImageMemory_ != VK_NULL_HANDLE)
    {
        vkFreeMemory(device_, depthImageMemory_, nullptr);
        depthImageMemory_ = VK_NULL_HANDLE;
    }
    depthFormat_ = VK_FORMAT_UNDEFINED;

    for (VkImageView imageView : swapchainImageViews_)
    {
        vkDestroyImageView(device_, imageView, nullptr);
    }
    swapchainImageViews_.clear();

    if (swapchain_ != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }

    swapchainImages_.clear();
    imagesInFlight_.clear();
}

bool VulkanRenderer::RecreateSwapchain()
{
    if (device_ == VK_NULL_HANDLE || width_ == 0 || height_ == 0)
    {
        return true;
    }

    vkDeviceWaitIdle(device_);
    CleanupSwapchainResources();
    const bool ok = CreateSwapchainResources();
    if (ok)
    {
        LogLine("VK", "Swapchain recreated size=%ux%u", width_, height_);
    }
    return ok;
}

VulkanRenderer::QueueFamilyIndices VulkanRenderer::FindQueueFamilies(VkPhysicalDevice device) const
{
    QueueFamilyIndices indices{};

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    for (uint32_t i = 0; i < queueFamilyCount; ++i)
    {
        if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
        {
            indices.graphicsFamily = i;
        }

        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface_, &presentSupport);
        if (presentSupport == VK_TRUE)
        {
            indices.presentFamily = i;
        }

        if (indices.Complete())
        {
            break;
        }
    }

    return indices;
}

VulkanRenderer::SwapchainSupportDetails VulkanRenderer::QuerySwapchainSupport(VkPhysicalDevice device) const
{
    SwapchainSupportDetails details{};

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface_, &details.capabilities);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &formatCount, nullptr);
    if (formatCount > 0)
    {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &presentModeCount, nullptr);
    if (presentModeCount > 0)
    {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &presentModeCount, details.presentModes.data());
    }

    return details;
}

VkSurfaceFormatKHR VulkanRenderer::ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &formats) const
{
    for (const auto &format : formats)
    {
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return format;
        }
    }

    return formats.front();
}

VkPresentModeKHR VulkanRenderer::ChooseSwapPresentMode(const std::vector<VkPresentModeKHR> &presentModes) const
{
    // Prefer FIFO for maximum compatibility and stable behavior while switching
    // from other backends at runtime.
    for (VkPresentModeKHR mode : presentModes)
    {
        if (mode == VK_PRESENT_MODE_FIFO_KHR)
        {
            return mode;
        }
    }

    // Fallback to first mode if FIFO is not reported (should be rare).
    if (!presentModes.empty())
    {
        return presentModes.front();
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanRenderer::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities) const
{
    if (capabilities.currentExtent.width != UINT32_MAX)
    {
        return capabilities.currentExtent;
    }

    VkExtent2D actualExtent{};
    actualExtent.width = std::clamp(width_, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    actualExtent.height = std::clamp(height_, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    return actualExtent;
}

bool VulkanRenderer::CheckDeviceExtensionSupport(VkPhysicalDevice device) const
{
    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions(kDeviceExtensions.begin(), kDeviceExtensions.end());
    for (const auto &extension : availableExtensions)
    {
        requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty();
}

uint32_t VulkanRenderer::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const
{
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memoryProperties);

    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
    {
        if ((typeFilter & (1u << i)) != 0 &&
            (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }

    return UINT32_MAX;
}

VkFormat VulkanRenderer::FindSupportedFormat(const std::vector<VkFormat> &candidates,
                                             VkImageTiling tiling,
                                             VkFormatFeatureFlags features) const
{
    for (VkFormat format : candidates)
    {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice_, format, &properties);

        if (tiling == VK_IMAGE_TILING_LINEAR &&
            (properties.linearTilingFeatures & features) == features)
        {
            return format;
        }
        if (tiling == VK_IMAGE_TILING_OPTIMAL &&
            (properties.optimalTilingFeatures & features) == features)
        {
            return format;
        }
    }

    return VK_FORMAT_UNDEFINED;
}

VkFormat VulkanRenderer::FindDepthFormat() const
{
    return FindSupportedFormat(
        {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

bool VulkanRenderer::CreateImage(uint32_t width,
                                 uint32_t height,
                                 VkFormat format,
                                 VkImageTiling tiling,
                                 VkImageUsageFlags usage,
                                 VkMemoryPropertyFlags properties,
                                 VkImage &image,
                                 VkDeviceMemory &memory) const
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device_, &imageInfo, nullptr, &image) != VK_SUCCESS)
    {
        return false;
    }

    VkMemoryRequirements memRequirements{};
    vkGetImageMemoryRequirements(device_, image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties);
    if (allocInfo.memoryTypeIndex == UINT32_MAX)
    {
        return false;
    }

    if (vkAllocateMemory(device_, &allocInfo, nullptr, &memory) != VK_SUCCESS)
    {
        return false;
    }

    if (vkBindImageMemory(device_, image, memory, 0) != VK_SUCCESS)
    {
        return false;
    }

    return true;
}

VkImageView VulkanRenderer::CreateImageView(VkImage image,
                                            VkFormat format,
                                            VkImageAspectFlags aspectMask) const
{
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectMask;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView = VK_NULL_HANDLE;
    if (vkCreateImageView(device_, &viewInfo, nullptr, &imageView) != VK_SUCCESS)
    {
        return VK_NULL_HANDLE;
    }

    return imageView;
}

bool VulkanRenderer::CreateBuffer(VkDeviceSize size,
                                  VkBufferUsageFlags usage,
                                  VkMemoryPropertyFlags properties,
                                  VkBuffer &buffer,
                                  VkDeviceMemory &memory) const
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device_, &bufferInfo, nullptr, &buffer) != VK_SUCCESS)
    {
        return false;
    }

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device_, buffer, &requirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = requirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(requirements.memoryTypeBits, properties);
    if (allocInfo.memoryTypeIndex == UINT32_MAX)
    {
        return false;
    }

    if (vkAllocateMemory(device_, &allocInfo, nullptr, &memory) != VK_SUCCESS)
    {
        return false;
    }

    if (vkBindBufferMemory(device_, buffer, memory, 0) != VK_SUCCESS)
    {
        return false;
    }

    return true;
}

bool VulkanRenderer::CreateSwapchain()
{
    const SwapchainSupportDetails swapchainSupport = QuerySwapchainSupport(physicalDevice_);
    if (swapchainSupport.formats.empty() || swapchainSupport.presentModes.empty())
    {
        LogLine("VK", "Swapchain support insufficient");
        return false;
    }

    const VkSurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(swapchainSupport.formats);
    const VkPresentModeKHR presentMode = ChooseSwapPresentMode(swapchainSupport.presentModes);
    const VkExtent2D extent = ChooseSwapExtent(swapchainSupport.capabilities);

    uint32_t imageCount = swapchainSupport.capabilities.minImageCount + 1;
    if (swapchainSupport.capabilities.maxImageCount > 0 &&
        imageCount > swapchainSupport.capabilities.maxImageCount)
    {
        imageCount = swapchainSupport.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface_;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    const uint32_t queueFamilyIndices[] = {graphicsQueueFamily_, presentQueueFamily_};
    if (graphicsQueueFamily_ != presentQueueFamily_)
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    }
    else
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = swapchainSupport.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(device_, &createInfo, nullptr, &swapchain_) != VK_SUCCESS)
    {
        return false;
    }

    vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, nullptr);
    swapchainImages_.resize(imageCount);
    vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, swapchainImages_.data());

    swapchainImageFormat_ = surfaceFormat.format;
    swapchainExtent_ = extent;
    LogLine("VK", "Swapchain created images=%u extent=%ux%u mode=%d",
            imageCount,
            swapchainExtent_.width,
            swapchainExtent_.height,
            static_cast<int>(presentMode));
    return true;
}

bool VulkanRenderer::CreateImageViews()
{
    swapchainImageViews_.resize(swapchainImages_.size());

    for (size_t i = 0; i < swapchainImages_.size(); ++i)
    {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = swapchainImages_[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = swapchainImageFormat_;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device_, &createInfo, nullptr, &swapchainImageViews_[i]) != VK_SUCCESS)
        {
            return false;
        }
    }

    return true;
}

bool VulkanRenderer::CreateRenderPass()
{
    depthFormat_ = FindDepthFormat();
    if (depthFormat_ == VK_FORMAT_UNDEFINED)
    {
        LogLine("VK", "No supported depth format");
        return false;
    }

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapchainImageFormat_;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = depthFormat_;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    return vkCreateRenderPass(device_, &renderPassInfo, nullptr, &renderPass_) == VK_SUCCESS;
}

bool VulkanRenderer::CreatePipeline()
{
    const std::string vertexPath = ResolveShaderPath("teaching.vert.spv");
    const std::string fragmentPath = ResolveShaderPath("teaching.frag.spv");
    const std::vector<char> vertexCode = ReadBinaryFile(vertexPath);
    const std::vector<char> fragmentCode = ReadBinaryFile(fragmentPath);

    if (vertexCode.empty() || fragmentCode.empty())
    {
        LogLine("VK", "Shader load failed vert=%s frag=%s", vertexPath.c_str(), fragmentPath.c_str());
        return false;
    }

    VkShaderModule vertexShaderModule = CreateShaderModule(device_, vertexCode);
    VkShaderModule fragmentShaderModule = CreateShaderModule(device_, fragmentCode);
    if (vertexShaderModule == VK_NULL_HANDLE || fragmentShaderModule == VK_NULL_HANDLE)
    {
        if (vertexShaderModule != VK_NULL_HANDLE)
        {
            vkDestroyShaderModule(device_, vertexShaderModule, nullptr);
        }
        if (fragmentShaderModule != VK_NULL_HANDLE)
        {
            vkDestroyShaderModule(device_, fragmentShaderModule, nullptr);
        }
        return false;
    }

    VkPipelineShaderStageCreateInfo vertexStageInfo{};
    vertexStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertexStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertexStageInfo.module = vertexShaderModule;
    vertexStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragmentStageInfo{};
    fragmentStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragmentStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragmentStageInfo.module = fragmentShaderModule;
    fragmentStageInfo.pName = "main";

    const VkPipelineShaderStageCreateInfo shaderStages[] = {vertexStageInfo, fragmentStageInfo};

    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(Vertex, position);

    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(Vertex, normal);

    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[2].offset = offsetof(Vertex, color);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;

    if (vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS)
    {
        vkDestroyShaderModule(device_, vertexShaderModule, nullptr);
        vkDestroyShaderModule(device_, fragmentShaderModule, nullptr);
        return false;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout_;
    pipelineInfo.renderPass = renderPass_;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    const bool success = vkCreateGraphicsPipelines(
                             device_,
                             VK_NULL_HANDLE,
                             1,
                             &pipelineInfo,
                             nullptr,
                             &graphicsPipeline_) == VK_SUCCESS;

    vkDestroyShaderModule(device_, vertexShaderModule, nullptr);
    vkDestroyShaderModule(device_, fragmentShaderModule, nullptr);

    if (!success)
    {
        return false;
    }

    LogLine("VK", "Pipeline created");
    return true;
}

bool VulkanRenderer::CreateDepthResources()
{
    if (depthFormat_ == VK_FORMAT_UNDEFINED)
    {
        depthFormat_ = FindDepthFormat();
        if (depthFormat_ == VK_FORMAT_UNDEFINED)
        {
            return false;
        }
    }

    if (!CreateImage(swapchainExtent_.width,
                     swapchainExtent_.height,
                     depthFormat_,
                     VK_IMAGE_TILING_OPTIMAL,
                     VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     depthImage_,
                     depthImageMemory_))
    {
        return false;
    }

    VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (depthFormat_ == VK_FORMAT_D32_SFLOAT_S8_UINT || depthFormat_ == VK_FORMAT_D24_UNORM_S8_UINT)
    {
        aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }

    depthImageView_ = CreateImageView(depthImage_, depthFormat_, aspectMask);
    if (depthImageView_ == VK_NULL_HANDLE)
    {
        return false;
    }

    return true;
}

bool VulkanRenderer::CreateFramebuffers()
{
    framebuffers_.resize(swapchainImageViews_.size());
    for (size_t i = 0; i < swapchainImageViews_.size(); ++i)
    {
        const std::array<VkImageView, 2> attachments = {swapchainImageViews_[i], depthImageView_};

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass_;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = swapchainExtent_.width;
        framebufferInfo.height = swapchainExtent_.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(device_, &framebufferInfo, nullptr, &framebuffers_[i]) != VK_SUCCESS)
        {
            return false;
        }
    }

    return true;
}

bool VulkanRenderer::AllocateCommandBuffers()
{
    if (swapchainImages_.empty())
    {
        return false;
    }

    commandBuffers_.resize(swapchainImages_.size());
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers_.size());

    return vkAllocateCommandBuffers(device_, &allocInfo, commandBuffers_.data()) == VK_SUCCESS;
}

bool VulkanRenderer::CreateVertexBuffer()
{
    const std::vector<Vertex> cubeVertices = BuildCubeVertices();
    std::vector<Vertex> allVertices;
    allVertices.reserve(kTriangleVertices.size() + cubeVertices.size());

    triangleFirstVertex_ = 0;
    triangleVertexCount_ = static_cast<uint32_t>(kTriangleVertices.size());
    for (const Vertex &v : kTriangleVertices)
    {
        allVertices.push_back(v);
    }

    cubeFirstVertex_ = static_cast<uint32_t>(allVertices.size());
    cubeVertexCount_ = static_cast<uint32_t>(cubeVertices.size());
    allVertices.insert(allVertices.end(), cubeVertices.begin(), cubeVertices.end());

    const VkDeviceSize bufferSize = static_cast<VkDeviceSize>(allVertices.size() * sizeof(Vertex));

    if (!CreateBuffer(bufferSize,
                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      vertexBuffer_,
                      vertexBufferMemory_))
    {
        return false;
    }

    void *mappedData = nullptr;
    if (vkMapMemory(device_, vertexBufferMemory_, 0, bufferSize, 0, &mappedData) != VK_SUCCESS)
    {
        return false;
    }

    std::memcpy(mappedData, allVertices.data(), static_cast<size_t>(bufferSize));
    vkUnmapMemory(device_, vertexBufferMemory_);

    LogLine("VK", "Geometry uploaded triangle=%u cube=%u", triangleVertexCount_, cubeVertexCount_);
    return true;
}

bool VulkanRenderer::CreateSyncObjects()
{
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i)
    {
        if (vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &imageAvailableSemaphores_[i]) != VK_SUCCESS ||
            vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &renderFinishedSemaphores_[i]) != VK_SUCCESS ||
            vkCreateFence(device_, &fenceInfo, nullptr, &inFlightFences_[i]) != VK_SUCCESS)
        {
            return false;
        }
    }

    return true;
}

bool VulkanRenderer::RecordCommandBuffer(VkCommandBuffer commandBuffer,
                                         uint32_t imageIndex,
                                         const FrameSettings &settings)
{
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
    {
        return false;
    }

    float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    ComputeClearColor(settings, clearColor);

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {clearColor[0], clearColor[1], clearColor[2], clearColor[3]};
    clearValues[1].depthStencil = {1.0f, 0u};

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass_;
    renderPassInfo.framebuffer = framebuffers_[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapchainExtent_;
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline_);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapchainExtent_.width);
    viewport.height = static_cast<float>(swapchainExtent_.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapchainExtent_;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    const VkBuffer vertexBuffers[] = {vertexBuffer_};
    constexpr VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

    PushConstants push{};
    const int clampedTopic = std::min(20, std::max(1, settings.topic));
    push.params0[0] = settings.elapsedSeconds;
    push.params0[1] = static_cast<float>(clampedTopic);
    push.params0[2] = settings.errorExampleEnabled ? 1.0f : 0.0f;
    push.params0[3] = (renderMode_ == RenderMode::Triangle) ? 0.0f : 1.0f;
    push.params1[0] = static_cast<float>(swapchainExtent_.width) / std::max(1.0f, static_cast<float>(swapchainExtent_.height));
    push.params1[1] = static_cast<float>(swapchainExtent_.width);
    push.params1[2] = static_cast<float>(swapchainExtent_.height);
    push.params1[3] = 0.0f;

    vkCmdPushConstants(commandBuffer,
                       pipelineLayout_,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0,
                       sizeof(PushConstants),
                       &push);

    if (renderMode_ == RenderMode::Triangle)
    {
        vkCmdDraw(commandBuffer, triangleVertexCount_, 1, triangleFirstVertex_, 0);
    }
    else
    {
        vkCmdDraw(commandBuffer, cubeVertexCount_, 1, cubeFirstVertex_, 0);
    }
    vkCmdEndRenderPass(commandBuffer);

    return vkEndCommandBuffer(commandBuffer) == VK_SUCCESS;
}

std::vector<char> VulkanRenderer::ReadBinaryFile(const std::string &path) const
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open())
    {
        LogLine("VK", "Failed to open shader: %s", path.c_str());
        return {};
    }

    const std::streamsize fileSize = file.tellg();
    if (fileSize <= 0)
    {
        return {};
    }

    std::vector<char> buffer(static_cast<size_t>(fileSize));
    file.seekg(0, std::ios::beg);
    file.read(buffer.data(), fileSize);
    return buffer;
}

std::string VulkanRenderer::ResolveShaderPath(const char *fileName) const
{
    char modulePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, modulePath, MAX_PATH);
    std::string exePath(modulePath);
    const size_t slashPos = exePath.find_last_of("\\/");
    std::string exeDir = (slashPos == std::string::npos) ? "." : exePath.substr(0, slashPos);

    std::array<std::string, 3> candidates = {
        exeDir + "\\shaders\\" + fileName,
        exeDir + "\\..\\shaders\\" + fileName,
        std::string("shaders\\") + fileName,
    };

    for (const std::string &candidate : candidates)
    {
        if (FileExists(candidate))
        {
            return candidate;
        }
    }

    return candidates[0];
}

} // namespace dxteaching
