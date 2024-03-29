#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <SDL2/SDL.h>
#include <SDL2/SDL_video.h>
#include <SDL2/SDL_vulkan.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <tl/expected.hpp>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_structs.hpp>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "common.h"
#include "helpers.hpp"
#include "mock.h"

// CONSTANTS

const uint32_t DEFAULT_WIDTH = 1024;
const uint32_t DEFAULT_HEIGHT = 768;
const uint32_t MAX_FRAMES_IN_FLIGHT = 2;

const std::vector<const char *> gValidationLayers = {"VK_LAYER_KHRONOS_validation"};

const std::vector<const char *> gDeviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

#ifdef NDEBUG
static constexpr bool gEnableValidationLayers = false;
#else
static constexpr bool gEnableValidationLayers = true;
#endif

// UTILS

#define UNUSED(expr) (void)(expr)

// DEBUG DEFINITIONS

std::unordered_map<VkInstance, PFN_vkCreateDebugUtilsMessengerEXT> CreateDebugUtilsMessengerEXTDispatchTable;
std::unordered_map<VkInstance, PFN_vkDestroyDebugUtilsMessengerEXT> DestroyDebugUtilsMessengerEXTDispatchTable;
std::unordered_map<VkInstance, PFN_vkSubmitDebugUtilsMessageEXT> SubmitDebugUtilsMessageEXTDispatchTable;

void loadDebugUtilsCommands(VkInstance instance) {
    PFN_vkVoidFunction temp_fp;

    temp_fp = vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (!temp_fp) {
        throw "Failed to load vkCreateDebugUtilsMessengerEXT"; // check shouldn't be necessary (based on spec)
    }
    CreateDebugUtilsMessengerEXTDispatchTable[instance] = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(temp_fp);

    temp_fp = vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (!temp_fp) {
        throw "Failed to load vkDestroyDebugUtilsMessengerEXT"; // check shouldn't be necessary (based on spec)
    }
    DestroyDebugUtilsMessengerEXTDispatchTable[instance] =
        reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(temp_fp);

    temp_fp = vkGetInstanceProcAddr(instance, "vkSubmitDebugUtilsMessageEXT");
    if (!temp_fp) {
        throw "Failed to load vkSubmitDebugUtilsMessageEXT"; // check shouldn't be necessary (based on spec)
    }
    SubmitDebugUtilsMessageEXTDispatchTable[instance] = reinterpret_cast<PFN_vkSubmitDebugUtilsMessageEXT>(temp_fp);
}

void unloadDebugUtilsCommands(VkInstance instance) {
    CreateDebugUtilsMessengerEXTDispatchTable.erase(instance);
    DestroyDebugUtilsMessengerEXTDispatchTable.erase(instance);
    SubmitDebugUtilsMessageEXTDispatchTable.erase(instance);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDebugUtilsMessengerEXT(VkInstance instance,
                                                              const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo,
                                                              const VkAllocationCallbacks *pAllocator,
                                                              VkDebugUtilsMessengerEXT *pMessenger) {
    auto dispatched_cmd = CreateDebugUtilsMessengerEXTDispatchTable.at(instance);
    return dispatched_cmd(instance, pCreateInfo, pAllocator, pMessenger);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT messenger,
                                                           const VkAllocationCallbacks *pAllocator) {
    auto dispatched_cmd = DestroyDebugUtilsMessengerEXTDispatchTable.at(instance);
    return dispatched_cmd(instance, messenger, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL vkSubmitDebugUtilsMessageEXT(VkInstance instance,
                                                        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                                        VkDebugUtilsMessageTypeFlagsEXT messageTypes,
                                                        const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData) {
    auto dispatched_cmd = SubmitDebugUtilsMessageEXTDispatchTable.at(instance);
    return dispatched_cmd(instance, messageSeverity, messageTypes, pCallbackData);
}

// CORE DEFINITIONS

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                                    VkDebugUtilsMessageTypeFlagsEXT messageType,
                                                    const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
                                                    void *pUserData) {
    UNUSED(messageSeverity);
    UNUSED(messageType);
    UNUSED(pCallbackData);
    UNUSED(pUserData);
    std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;
    return VK_FALSE;
}

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    bool isComplete() { return graphicsFamily.has_value() && presentFamily.has_value(); }
};

struct SwapChainSupportDetails {
    vk::SurfaceCapabilitiesKHR capabilities;
    std::vector<vk::SurfaceFormatKHR> formats;
    std::vector<vk::PresentModeKHR> presentModes;
};

class HelloTriangleApplication {
public:
    void run() {
        if (!initWindow()) {
            throw std::runtime_error("Failed to init window");
        }
        initVulkan();
        mainLoop();
    }

    ~HelloTriangleApplication() { SDL_DestroyWindow(pWindow); }

private:
    bool initWindow() {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
            std::cout << "Failed to initialize the SDL2\n";
            std::cout << "SDL2 Error: " << SDL_GetError() << "\n";
            return false;
        }
        pWindow = SDL_CreateWindow("Vulkan Window", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                   static_cast<int>(screenWidth), static_cast<int>(screenHeight),
                                   SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
        if (!pWindow) {
            std::cout << "Failed to create window\n";
            std::cout << "SDL2 Error: " << SDL_GetError() << "\n";
            return false;
        }
        return true;
    }
    bool initVulkan() {
        createInstance();
        setupDebugMessenger();
        createSurface();
        pickPhysicalDevice();
        createLogicalDevice();
        createSwapChain();
        createImageViews();
        createRenderPass();
        createDescriptorSetLayout();
        createGraphicsPipeline();
        createFramebuffers();
        createCommandPool();
        createVertexBuffer();
        createIndexBuffer();
        createUniformBuffers();
        createDescriptorPool();
        createDescriptorSets();
        createCommandBuffers();
        createSyncObjects();

        return true;
    }

    void framebufferResized(int width, int height) {
        UNUSED(width);
        UNUSED(height);
        bFramebufferResized = true;
    }

    void populateDebugMessengerCreateInfo(vk::DebugUtilsMessengerCreateInfoEXT &createInfo) {
        createInfo.sType = vk::StructureType::eDebugUtilsMessengerCreateInfoEXT;
        createInfo.messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
                                     vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                                     vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
        createInfo.messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                                 vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                                 vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
        createInfo.pfnUserCallback = debugCallback;
        createInfo.pUserData = nullptr;
    }

    void setupDebugMessenger() {
        if (!gEnableValidationLayers)
            return;

        auto dldi = vk::DispatchLoaderDynamic(instance.get(), vkGetInstanceProcAddr);

        vk::DebugUtilsMessengerCreateInfoEXT createInfo{};
        populateDebugMessengerCreateInfo(createInfo);

        instance->createDebugUtilsMessengerEXTUnique(createInfo);
    }

    bool checkValidationLayerSupport() {
        std::vector<vk::LayerProperties> availableLayers = vk::enumerateInstanceLayerProperties();

        for (const char *layerName : gValidationLayers) {
            bool layerFound = false;

            for (const auto &layerProperties : availableLayers) {
                if (strcmp(layerName, layerProperties.layerName) == 0) {
                    layerFound = true;
                    break;
                }
            }

            if (!layerFound) {
                return false;
            }
        }

        return true;
    }

    tl::expected<std::vector<const char *>, std::string> getRequiredExtensions() {
        uint32_t sdlExtensionCount = 0;
        if (!SDL_Vulkan_GetInstanceExtensions(pWindow, &sdlExtensionCount, nullptr)) {
            return tl::unexpected(std::string("Can't query instance extension count\n"));
        }
        std::vector<const char *> sdlExtensionNames(sdlExtensionCount);
        if (!SDL_Vulkan_GetInstanceExtensions(pWindow, &sdlExtensionCount, sdlExtensionNames.data())) {
            return tl::unexpected(std::string("Can't query instance extension names\n"));
        }

        sdlExtensionNames.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        ++sdlExtensionCount;
        std::cout << "required extensions:\n";
        for (const auto &extensionName : sdlExtensionNames) {
            std::cout << '\t' << extensionName << '\n';
        }

        std::vector<vk::ExtensionProperties> extensions = vk::enumerateInstanceExtensionProperties(nullptr);
        std::cout << "available extensions:\n";
        for (const auto &extension : extensions) {
            std::cout << '\t' << extension.extensionName << '\n';
        }

        return sdlExtensionNames;
    }

    void createInstance() {
        if (gEnableValidationLayers && !checkValidationLayerSupport()) {
            throw std::runtime_error("validation layers requested, but not available!");
        }
        vk::ApplicationInfo appInfo{};
        appInfo.sType = vk::StructureType::eApplicationInfo;
        appInfo.pApplicationName = "Vulkan Triangle";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "PONS2";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_0;

        vk::InstanceCreateInfo createInfo{};
        createInfo.sType = vk::StructureType::eInstanceCreateInfo;
        createInfo.pApplicationInfo = &appInfo;
        if (gEnableValidationLayers) {
            createInfo.enabledLayerCount = static_cast<uint32_t>(gValidationLayers.size());
            createInfo.ppEnabledLayerNames = gValidationLayers.data();
        } else {
            createInfo.enabledLayerCount = 0;
        }
        auto maybeExtensions = getRequiredExtensions();
        std::vector<const char *> extensions;
        if (maybeExtensions.has_value()) {
            extensions = maybeExtensions.value();
            createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
            createInfo.ppEnabledExtensionNames = extensions.data();
        } else {
            throw std::runtime_error(maybeExtensions.error());
        }

        instance = vk::createInstanceUnique(createInfo);

        loadDebugUtilsCommands(instance.get());
    }

    vk::Bool32 presentSupport = false;
    QueueFamilyIndices findQueueFamilies(vk::PhysicalDevice device) {
        QueueFamilyIndices indices;

        std::vector<vk::QueueFamilyProperties> queueFamilies = device.getQueueFamilyProperties();
        uint32_t i = 0;
        for (const auto &queueFamily : queueFamilies) {
            if (queueFamily.queueFlags & vk::QueueFlagBits::eGraphics) {
                indices.graphicsFamily = i;
            }
            vk::Result res = device.getSurfaceSupportKHR(i, surface.get(), &presentSupport);
            if (res != vk::Result::eSuccess) {
                throw std::runtime_error("can't get surface support value");
            }
            if (presentSupport) {
                indices.presentFamily = presentSupport;
            }
            if (indices.isComplete()) {
                break;
            }
            ++i;
        }

        return indices;
    }

    bool isDeviceSuitable(vk::PhysicalDevice device) {
        vk::PhysicalDeviceProperties deviceProperties = device.getProperties();
        vk::PhysicalDeviceFeatures deviceFeatures = device.getFeatures();
        QueueFamilyIndices indices = findQueueFamilies(device);
        bool extensionsSupported = checkDeviceExtensionSupport(device);
        bool swapChainAdequate = false;

        if (extensionsSupported) {
            SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device);
            swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
        }

        return deviceProperties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu && deviceFeatures.geometryShader &&
               indices.isComplete() && extensionsSupported && swapChainAdequate;
    }

    bool checkDeviceExtensionSupport(vk::PhysicalDevice device) {
        std::vector<vk::ExtensionProperties> availableExtensions = device.enumerateDeviceExtensionProperties();

        std::set<std::string> requiredExtensions(gDeviceExtensions.begin(), gDeviceExtensions.end());
        for (const auto &extension : availableExtensions) {
            requiredExtensions.erase(extension.extensionName);
        }

        return requiredExtensions.empty();
    }

    void pickPhysicalDevice() {
        physicalDevice = nullptr;
        std::vector<vk::PhysicalDevice> physicalDevices = instance->enumeratePhysicalDevices();
        if (physicalDevices.empty()) {
            throw std::runtime_error("failed to find GPUs with Vulkan support!");
        }
        for (const auto &device : physicalDevices) {
            if (isDeviceSuitable(device)) {
                physicalDevice = device;
                break;
            }
        }
        if (!physicalDevice) {
            throw std::runtime_error("failed to find a suitable GPU!");
        }
    }

    void createLogicalDevice() {
        QueueFamilyIndices indices = findQueueFamilies(physicalDevice);

        std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
        std::set<uint32_t> uniqueQueueFamilites = {indices.graphicsFamily.value(), indices.presentFamily.value()};

        float queuePriority = 1.0f;
        for (uint32_t queueFamily : uniqueQueueFamilites) {
            vk::DeviceQueueCreateInfo queueCreateInfo{};
            queueCreateInfo.sType = vk::StructureType::eDeviceQueueCreateInfo;
            queueCreateInfo.queueFamilyIndex = queueFamily;
            queueCreateInfo.queueCount = 1;
            queueCreateInfo.pQueuePriorities = &queuePriority;
            queueCreateInfos.push_back(queueCreateInfo);
        }

        vk::PhysicalDeviceFeatures deviceFeatures{};

        vk::DeviceCreateInfo createInfo{};
        createInfo.sType = vk::StructureType::eDeviceCreateInfo;
        createInfo.pQueueCreateInfos = queueCreateInfos.data();
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
        createInfo.pEnabledFeatures = &deviceFeatures;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(gDeviceExtensions.size());
        createInfo.ppEnabledExtensionNames = gDeviceExtensions.data();

        if (gEnableValidationLayers) {
            createInfo.enabledLayerCount = static_cast<uint32_t>(gValidationLayers.size());
            createInfo.ppEnabledLayerNames = gValidationLayers.data();
        } else {
            createInfo.enabledLayerCount = 0;
        }

        device = physicalDevice.createDeviceUnique(createInfo);
        graphicsQueue = device->getQueue(indices.graphicsFamily.value(), 0);
        presentQueue = device->getQueue(indices.presentFamily.value(), 0);
    }

    void createSurface() {
        VkSurfaceKHR sdlSurface;
        SDL_bool state = SDL_Vulkan_CreateSurface(pWindow, instance.get(), &sdlSurface);
        if (!state) {
            throw std::runtime_error("failed to create window surface");
        }
        surface = vk::UniqueSurfaceKHR{sdlSurface, instance.get()};
    }

    SwapChainSupportDetails querySwapChainSupport(vk::PhysicalDevice device) {
        vk::SurfaceCapabilitiesKHR capabilities = device.getSurfaceCapabilitiesKHR(surface.get());
        std::vector<vk::SurfaceFormatKHR> formats = device.getSurfaceFormatsKHR(surface.get());
        std::vector<vk::PresentModeKHR> surfacePresentModes = device.getSurfacePresentModesKHR(surface.get());

        return SwapChainSupportDetails{capabilities, formats, surfacePresentModes};
    }

    vk::SurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR> &availableFormats) {
        for (const auto &availableFormat : availableFormats) {
            if (availableFormat.format == vk::Format::eB8G8R8A8Srgb &&
                availableFormat.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
                return availableFormat;
            }
        }

        // default
        return availableFormats.at(0);
    }

    vk::PresentModeKHR chooseSwapPresentMode(const std::vector<vk::PresentModeKHR> &availablePresentModes) {
        for (const auto &presentMode : availablePresentModes) {
            if (presentMode == vk::PresentModeKHR::eMailbox) {
                return presentMode;
            }
        }

        // default
        return vk::PresentModeKHR::eFifo;
    }

    vk::Extent2D chooseSwapExtent(const vk::SurfaceCapabilitiesKHR &capabilities) {
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            return capabilities.currentExtent;
        } else {
            int width, height;
            SDL_GL_GetDrawableSize(pWindow, &width, &height);

            vk::Extent2D actualExtent = {
                static_cast<uint32_t>(width),
                static_cast<uint32_t>(height),
            };

            actualExtent.width =
                std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            actualExtent.height =
                std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

            return actualExtent;
        }
    }

    void createSwapChain() {
        SwapChainSupportDetails swapChainSupport = querySwapChainSupport(physicalDevice);

        vk::SurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
        vk::PresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
        vk::Extent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

        uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
        if (swapChainSupport.capabilities.maxImageCount > 0 &&
            imageCount > swapChainSupport.capabilities.maxImageCount) {
            imageCount = swapChainSupport.capabilities.maxImageCount;
        }

        vk::SwapchainCreateInfoKHR createInfo{};
        createInfo.sType = vk::StructureType::eSwapchainCreateInfoKHR;
        createInfo.surface = surface.get();
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;

        QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
        uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(), indices.presentFamily.value()};

        if (indices.graphicsFamily != indices.presentFamily) {
            createInfo.imageSharingMode = vk::SharingMode::eConcurrent;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        } else {
            createInfo.imageSharingMode = vk::SharingMode::eExclusive;
            createInfo.queueFamilyIndexCount = 0;
            createInfo.pQueueFamilyIndices = nullptr;
        }
        createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
        createInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;
        createInfo.oldSwapchain = nullptr;

        swapChain = device->createSwapchainKHRUnique(createInfo);
        swapChainImages = device->getSwapchainImagesKHR(swapChain.get());
        swapChainImageFormat = surfaceFormat.format;
        swapChainExtent = extent;
    }

    void createImageViews() {
        swapChainImageViews.reserve(swapChainImages.size());
        for (auto image : swapChainImages) {
            vk::ImageViewCreateInfo imageViewCreateInfo(
                vk::ImageViewCreateFlags{}, image, vk::ImageViewType::e2D, swapChainImageFormat,
                vk::ComponentMapping{vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eG, vk::ComponentSwizzle::eB,
                                     vk::ComponentSwizzle::eA},
                vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
            swapChainImageViews.push_back(device->createImageViewUnique(imageViewCreateInfo));
        }
    }

    void createDescriptorSetLayout() {
        vk::DescriptorSetLayoutBinding uboLayoutBinding{/*binding*/ 0, vk::DescriptorType::eUniformBuffer,
                                                        /*descriptorCount*/ 1, vk::ShaderStageFlagBits::eVertex,
                                                        nullptr};
        vk::DescriptorSetLayoutCreateInfo layoutInfo{vk::DescriptorSetLayoutCreateFlags{},
                                                     /*bindingCount*/ 1, &uboLayoutBinding};
        descriptorSetLayout = device->createDescriptorSetLayoutUnique(layoutInfo);
    }

    void createGraphicsPipeline() {
        std::vector<char> vertShaderCode =
            readFile("/home/modbrin/projects/pons2/shaders/bin/vert.spv"); // FIXME: find a better way to handle
                                                                           // relative paths hell during debug
        std::vector<char> fragShaderCode = readFile("/home/modbrin/projects/pons2/shaders/bin/frag.spv");
        vk::UniqueShaderModule vertShaderModule = createShaderModule(vertShaderCode);
        vk::UniqueShaderModule fragShaderModule = createShaderModule(fragShaderCode);
        vk::PipelineShaderStageCreateInfo vertShaderStageInfo{
            vk::PipelineShaderStageCreateFlags{}, vk::ShaderStageFlagBits::eVertex, vertShaderModule.get(), "main"};
        vk::PipelineShaderStageCreateInfo fragShaderStageInfo{
            vk::PipelineShaderStageCreateFlags{}, vk::ShaderStageFlagBits::eFragment, fragShaderModule.get(), "main"};
        vk::PipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

        auto bindingDescription = Vertex::getBindingDescription();
        auto attributeDescription = Vertex::getAttributeDescriptions();

        vk::PipelineVertexInputStateCreateInfo vertexInputInfo{vk::PipelineVertexInputStateCreateFlags{},
                                                               bindingDescription, attributeDescription};
        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{vk::PipelineInputAssemblyStateCreateFlags{},
                                                               vk::PrimitiveTopology::eTriangleList, false};
        vk::Viewport viewport{
            0.0f, 0.0f, static_cast<float>(swapChainExtent.width), static_cast<float>(swapChainExtent.height),
            0.0f, 1.0f};
        vk::Rect2D scissor{{0, 0}, swapChainExtent};
        vk::PipelineViewportStateCreateInfo viewportState{vk::PipelineViewportStateCreateFlags{}, 1, &viewport, 1,
                                                          &scissor};
        vk::PipelineRasterizationStateCreateInfo rasterizer{vk::PipelineRasterizationStateCreateFlags{},
                                                            /*depthClamp*/ false,
                                                            /*rasterizeDiscard*/ false,
                                                            vk::PolygonMode::eFill,
                                                            vk::CullModeFlagBits::eBack,
                                                            vk::FrontFace::eCounterClockwise,
                                                            /*depthBias*/ false,
                                                            /*depthBiasConstantFactor*/ 0.0f,
                                                            /*depthBiasClamp*/ 0.0f,
                                                            /*depthBiasSlopeFactor*/ 0.0f,
                                                            /*lineWidth*/ 1.0f};
        vk::PipelineMultisampleStateCreateInfo multisampling{
            vk::PipelineMultisampleStateCreateFlags{},
            vk::SampleCountFlagBits::e1,
            /*sampleShadingEnable*/ false,
            1.0f,
            /*pSampleMask*/ nullptr,
            /*alphaToCoverageEnable*/ false,
            /*alphaToOneEnable*/ false,
        };
        vk::PipelineColorBlendAttachmentState colorBlendAttachment{
            /*blend*/ false,
            vk::BlendFactor::eOne,
            vk::BlendFactor::eZero,
            vk::BlendOp::eAdd,
            vk::BlendFactor::eOne,
            vk::BlendFactor::eZero,
            vk::BlendOp::eAdd,
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB |
                vk::ColorComponentFlagBits::eA};
        vk::PipelineColorBlendStateCreateInfo colorBlending{vk::PipelineColorBlendStateCreateFlags{},
                                                            /*logicOpEnable*/ false,
                                                            vk::LogicOp::eCopy,
                                                            /*attachmentCount*/ 1,
                                                            &colorBlendAttachment,
                                                            {0.0f, 0.0f, 0.0f, 0.0f}};
        std::vector<vk::DynamicState> dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eLineWidth};
        vk::PipelineDynamicStateCreateInfo dynamicState{
            vk::PipelineDynamicStateCreateFlags{}, static_cast<uint32_t>(dynamicStates.size()), dynamicStates.data()};

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{vk::PipelineLayoutCreateFlags{}, descriptorSetLayout.get(), {}};
        pipelineLayout = device->createPipelineLayoutUnique(pipelineLayoutInfo);

        vk::GraphicsPipelineCreateInfo pipelineInfo{vk::PipelineCreateFlags{},
                                                    /*stageCount*/ 2,
                                                    shaderStages,
                                                    &vertexInputInfo,
                                                    &inputAssembly,
                                                    /*pTessellationState*/ nullptr,
                                                    &viewportState,
                                                    &rasterizer,
                                                    &multisampling,
                                                    /*pDepthStencilState*/ nullptr,
                                                    &colorBlending,
                                                    /*pDynamicState*/ nullptr,
                                                    pipelineLayout.get(),
                                                    renderPass.get(),
                                                    /*subpass*/ 0,
                                                    /*basePipelineHandle*/ nullptr,
                                                    /*basePipelineIndex*/ -1};
        graphicsPipeline = device->createGraphicsPipelineUnique(nullptr, pipelineInfo).value;
    }

    vk::UniqueShaderModule createShaderModule(const std::vector<char> &code) {
        vk::ShaderModuleCreateInfo createInfo{vk::ShaderModuleCreateFlags{}, code.size(),
                                              reinterpret_cast<const uint32_t *>(code.data())};
        vk::UniqueShaderModule shaderModule = device->createShaderModuleUnique(createInfo);
        return shaderModule;
    }

    static std::vector<char> readFile(const std::string &filename) {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("failed to open file!");
        }
        size_t fileSize = static_cast<size_t>(file.tellg());
        std::vector<char> buffer(fileSize);
        file.seekg(0);
        file.read(buffer.data(), static_cast<std::streamsize>(fileSize));
        file.close();

        return buffer;
    }

    void createRenderPass() {
        vk::AttachmentDescription colorAttachment{
            vk::AttachmentDescriptionFlags{}, swapChainImageFormat,          vk::SampleCountFlagBits::e1,
            vk::AttachmentLoadOp::eClear,     vk::AttachmentStoreOp::eStore, vk::AttachmentLoadOp::eDontCare,
            vk::AttachmentStoreOp::eDontCare, vk::ImageLayout::eUndefined,   vk::ImageLayout::ePresentSrcKHR};
        vk::AttachmentReference colorAttachmentRef{/*attachment*/ 0, vk::ImageLayout::eColorAttachmentOptimal};
        vk::SubpassDescription subpass{
            vk::SubpassDescriptionFlags{}, vk::PipelineBindPoint::eGraphics,
            /*inputAttachmentCount*/ 0,
            /*pInputAttachments*/ nullptr,
            /*colorAttachmentCount*/ 1,    &colorAttachmentRef,
        };
        vk::SubpassDependency dependency{
            VK_SUBPASS_EXTERNAL,
            /*dstSubpass*/ 0,
            vk::PipelineStageFlagBits::eColorAttachmentOutput,
            vk::PipelineStageFlagBits::eColorAttachmentOutput,
            vk::AccessFlags{},
            vk::AccessFlagBits::eColorAttachmentWrite,

        };
        vk::RenderPassCreateInfo renderPassInfo{vk::RenderPassCreateFlags{},
                                                /*attachmentCount*/ 1,       &colorAttachment,
                                                /*subpassCount*/ 1,          &subpass,
                                                /*dependencyCount*/ 1,       &dependency};
        renderPass = device->createRenderPassUnique(renderPassInfo);
    }

    void createFramebuffers() {
        swapChainFramebuffers.reserve(swapChainImageViews.size());
        for (const auto &imageView : swapChainImageViews) {
            vk::ImageView attachments[] = {imageView.get()};
            vk::FramebufferCreateInfo framebufferInfo{vk::FramebufferCreateFlags{},
                                                      renderPass.get(),
                                                      /*attachmentCount*/ 1,
                                                      attachments,
                                                      swapChainExtent.width,
                                                      swapChainExtent.height,
                                                      /*layers*/ 1};
            swapChainFramebuffers.emplace_back(device->createFramebufferUnique(framebufferInfo));
        }
    }

    void createCommandPool() {
        QueueFamilyIndices queueFamilyIndices = findQueueFamilies(physicalDevice);
        vk::CommandPoolCreateInfo poolInfo{vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
                                           queueFamilyIndices.graphicsFamily.value()};
        commandPool = device->createCommandPoolUnique(poolInfo);
    }

    void createCommandBuffers() {
        vk::CommandBufferAllocateInfo allocInfo{commandPool.get(), vk::CommandBufferLevel::ePrimary,
                                                /*commandBufferCount*/ MAX_FRAMES_IN_FLIGHT};
        commandBuffers = device->allocateCommandBuffersUnique(allocInfo);
    }

    void recordCommandBuffer(vk::CommandBuffer commandBuffer, uint32_t imageIndex) {
        vk::CommandBufferBeginInfo beginInfo{vk::CommandBufferUsageFlags{},
                                             /*pInheritanceInfo*/ nullptr};
        commandBuffer.begin(beginInfo);
        vk::ClearColorValue clearColorValue{};
        clearColorValue.setFloat32({0.0f, 0.0f, 0.0f, 0.0f});
        vk::ClearValue clearColor{clearColorValue};
        vk::RenderPassBeginInfo renderPassInfo{renderPass.get(), swapChainFramebuffers.at(imageIndex).get(),
                                               vk::Rect2D{{0, 0}, swapChainExtent},
                                               /*clearValueCount*/ 1, &clearColor};
        commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline.get());
        vk::Buffer vertexBuffers[] = {vertexBuffer.get()};
        vk::DeviceSize offsets[] = {0};
        commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);
        commandBuffer.bindIndexBuffer(indexBuffer.get(), 0, vk::IndexType::eUint16);
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout.get(), 0, 1,
                                         &descriptorSets.at(currentFrame), 0, nullptr);
        commandBuffer.drawIndexed(static_cast<uint32_t>(mockIndices.size()), 1, 0, 0, 0);
        commandBuffer.endRenderPass();
        commandBuffer.end();
    }

    void createSyncObjects() {
        imageAvailableSemaphores.reserve(MAX_FRAMES_IN_FLIGHT);
        renderFinishedSemaphores.reserve(MAX_FRAMES_IN_FLIGHT);
        inFlightFences.reserve(MAX_FRAMES_IN_FLIGHT);

        vk::SemaphoreCreateInfo semaphoreInfo{vk::SemaphoreCreateFlags{}};
        vk::FenceCreateInfo fenceInfo{vk::FenceCreateFlagBits::eSignaled};
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            imageAvailableSemaphores.emplace_back(device->createSemaphoreUnique(semaphoreInfo));
            renderFinishedSemaphores.emplace_back(device->createSemaphoreUnique(semaphoreInfo));
            inFlightFences.emplace_back(device->createFenceUnique(fenceInfo));
        }
    }

    void cleanupSwapChain() {
        for (auto &framebuffer : swapChainFramebuffers) {
            device->destroyFramebuffer(framebuffer.release());
        }
        swapChainFramebuffers.clear();
        device->destroyPipeline(graphicsPipeline.release());
        device->destroyPipelineLayout(pipelineLayout.release());
        device->destroyRenderPass(renderPass.release());
        for (auto &imageView : swapChainImageViews) {
            device->destroyImageView(imageView.release());
        }
        swapChainImageViews.clear();
        device->destroySwapchainKHR(swapChain.release());

        // TODO: is this necessary?
        // for (auto &buffer : uniformBuffers) {
        //     device->destroyBuffer(buffer.release());
        // }
        // uniformBuffers.clear();
        // for (auto &bufferMemory : uniformBuffersMemory) {
        //     device->freeMemory(bufferMemory.release());
        // }
        // uniformBuffersMemory.clear();
    }

    void recreateSwapChain() {
        int width, height;
        SDL_GL_GetDrawableSize(pWindow, &width, &height);
        while (width == 0 || height == 0) {
            // TODO: investigate, this should be probably done differently in sdl
            SDL_GL_GetDrawableSize(pWindow, &width, &height);
            SDL_WaitEvent(nullptr);
        }
        device->waitIdle();

        cleanupSwapChain();
        createSwapChain();
        createImageViews();
        createRenderPass();
        createGraphicsPipeline();
        createFramebuffers();

        // TODO: is this necessary?
        // createUniformBuffers();
        // createCommandBuffers();
        // createDescriptorPool();
        // createDescriptorSets();
    }

    std::tuple<vk::UniqueBuffer, vk::UniqueDeviceMemory> createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage,
                                                                      vk::MemoryPropertyFlags properties) {
        vk::BufferCreateInfo bufferInfo{vk::BufferCreateFlags{}, size, usage, vk::SharingMode::eExclusive};
        vk::UniqueBuffer buffer = device->createBufferUnique(bufferInfo);

        vk::MemoryRequirements memRequirements = device->getBufferMemoryRequirements(buffer.get());
        vk::MemoryAllocateInfo allocInfo{memRequirements.size,
                                         findMemoryType(memRequirements.memoryTypeBits, properties)};
        vk::UniqueDeviceMemory bufferMemory = device->allocateMemoryUnique(allocInfo);
        device->bindBufferMemory(buffer.get(), bufferMemory.get(), 0);
        return std::forward_as_tuple(std::move(buffer), std::move(bufferMemory));
    }

    void createVertexBuffer() {
        vk::DeviceSize bufferSize = sizeof(mockVertices[0]) * mockVertices.size();
        auto [stagingBuffer, stagingBufferMemory] =
            createBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferSrc,
                         vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        void *data;
        vk::Result result = device->mapMemory(stagingBufferMemory.get(), 0, bufferSize, vk::MemoryMapFlags{}, &data);
        if (result != vk::Result::eSuccess) {
            throw std::runtime_error("failed to map stagingBuffer memory");
        }
        memcpy(data, mockVertices.data(), static_cast<size_t>(bufferSize));
        device->unmapMemory(stagingBufferMemory.get());
        std::tie(vertexBuffer, vertexBufferMemory) =
            createBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer,
                         vk::MemoryPropertyFlagBits::eDeviceLocal);
        copyBuffer(stagingBuffer.get(), vertexBuffer.get(), bufferSize);
    }

    void createIndexBuffer() {
        vk::DeviceSize bufferSize = sizeof(mockIndices[0]) * mockIndices.size();
        auto [stagingBuffer, stagingBufferMemory] =
            createBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferSrc,
                         vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        void *data;
        vk::Result result = device->mapMemory(stagingBufferMemory.get(), 0, bufferSize, vk::MemoryMapFlags{}, &data);
        if (result != vk::Result::eSuccess) {
            throw std::runtime_error("failed to map stagingBuffer memory");
        }
        memcpy(data, mockIndices.data(), static_cast<size_t>(bufferSize));
        device->unmapMemory(stagingBufferMemory.get());
        std::tie(indexBuffer, indexBufferMemory) =
            createBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer,
                         vk::MemoryPropertyFlagBits::eDeviceLocal);
        copyBuffer(stagingBuffer.get(), indexBuffer.get(), bufferSize);
    }

    void copyBuffer(vk::Buffer srcBuffer, vk::Buffer dstBuffer, vk::DeviceSize size) {
        vk::CommandBufferAllocateInfo allocInfo{commandPool.get(), vk::CommandBufferLevel::ePrimary,
                                                /*commandBufferCount*/ 1};
        vk::UniqueCommandBuffer commandBuffer = std::move(device->allocateCommandBuffersUnique(allocInfo)[0]);
        vk::CommandBufferBeginInfo beginInfo{vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
        commandBuffer->begin(beginInfo);
        vk::BufferCopy copyRegion{/*srcOffset*/ 0,
                                  /*dstOffset*/ 0, size};
        commandBuffer->copyBuffer(srcBuffer, dstBuffer, 1, &copyRegion);
        commandBuffer->end();
        vk::SubmitInfo submitInfo{
            /*waitSemaphoreCount*/ 0,
            /*pWaitSemaphores*/ nullptr,
            /*pWaitDstStageMask*/ nullptr,
            /*commandBufferCount*/ 1,      &commandBuffer.get(),
        };
        graphicsQueue.submit(submitInfo, nullptr);
        graphicsQueue.waitIdle();
    }

    uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) {
        vk::PhysicalDeviceMemoryProperties memProperties = physicalDevice.getMemoryProperties();
        for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
            if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }
        throw std::runtime_error("failed to find suitable memory type");
    }

    void createUniformBuffers() {
        vk::DeviceSize bufferSize = sizeof(UniformBufferObject);
        uniformBuffers.reserve(MAX_FRAMES_IN_FLIGHT);
        uniformBuffersMemory.reserve(MAX_FRAMES_IN_FLIGHT);
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            auto [buffer, bufferMemory] =
                createBuffer(bufferSize, vk::BufferUsageFlagBits::eUniformBuffer,
                             vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            uniformBuffers.emplace_back(std::move(buffer));
            uniformBuffersMemory.emplace_back(std::move(bufferMemory));
        }
    }

    void updateUniformBuffer(uint32_t currentImage) {
        static auto startTime = std::chrono::high_resolution_clock::now();
        auto currentTime = std::chrono::high_resolution_clock::now();
        float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

        UniformBufferObject ubo{
            .model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
            .view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
            .proj = glm::perspective(glm::radians(45.0f),
                                     swapChainExtent.width / static_cast<float>(swapChainExtent.height), 0.1f, 10.0f)};
        ubo.proj[1][1] *= -1.0f; // flip Y coordinate
        void *data;
        vk::Result result =
            device->mapMemory(uniformBuffersMemory[currentImage].get(), 0, sizeof(ubo), vk::MemoryMapFlags{}, &data);
        if (result != vk::Result::eSuccess) {
            throw std::runtime_error("failed to map memory of uniform buffer");
        }
        memcpy(data, &ubo, sizeof(ubo));
        device->unmapMemory(uniformBuffersMemory[currentImage].get());
    }

    void createDescriptorPool() {
        vk::DescriptorPoolSize poolSize{vk::DescriptorType::eUniformBuffer,
                                        /*descriptorCount*/ static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT)};
        vk::DescriptorPoolCreateInfo poolInfo{vk::DescriptorPoolCreateFlags{},
                                              /*maxSets*/ static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT), poolSize};
        descriptorPool = device->createDescriptorPoolUnique(poolInfo);
    }

    void createDescriptorSets() {
        std::vector<vk::DescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, descriptorSetLayout.get());
        vk::DescriptorSetAllocateInfo allocInfo{descriptorPool.get(), layouts};
        descriptorSets = device->allocateDescriptorSets(allocInfo);
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            vk::DescriptorBufferInfo bufferInfo{uniformBuffers[i].get(),
                                                /*offset*/ 0, sizeof(UniformBufferObject)};
            vk::WriteDescriptorSet descriptorWrite{
                descriptorSets[i],
                /*dstBinding*/ 0,
                /*dstArrayElement*/ 0,
                /*descriptorCount*/ 1, vk::DescriptorType::eUniformBuffer, nullptr, &bufferInfo, nullptr};
            device->updateDescriptorSets(1, &descriptorWrite, 0, nullptr);
        }
    }

    void handleEvents() {
        SDL_Event event;
        while (SDL_PollEvent(&event) > 0) {
            switch (event.type) {
            case SDL_QUIT:
                bKeepWindowOpen = false;
                break;
            case SDL_WINDOWEVENT:
                switch (event.window.event) {
                case SDL_WINDOWEVENT_SIZE_CHANGED:
                    framebufferResized(event.window.data1, event.window.data2);
                    break;
                case SDL_WINDOWEVENT_RESTORED:
                    bIsWindowMinimized = false;
                    break;
                case SDL_WINDOWEVENT_MINIMIZED:
                    bIsWindowMinimized = true;
                    break;
                }
                break;
            }
        }
    }

    void drawFrame() {
        auto waitResult = device->waitForFences(inFlightFences[currentFrame].get(), true, UINT64_MAX);
        if (waitResult != vk::Result::eSuccess) {
            throw std::runtime_error("error while waiting for inFlightFence");
        }

        vk::ResultValue<uint32_t> acquireImageResult = device->acquireNextImageKHR(
            swapChain.get(), UINT64_MAX, imageAvailableSemaphores[currentFrame].get(), nullptr);
        if (acquireImageResult.result == vk::Result::eErrorOutOfDateKHR) {
            recreateSwapChain();
            return;
        } else if (acquireImageResult.result != vk::Result::eSuccess &&
                   acquireImageResult.result != vk::Result::eSuboptimalKHR) {
            throw std::runtime_error("failed to acquire swap chain image");
        }
        device->resetFences(inFlightFences[currentFrame].get());

        vk::CommandBuffer commandBuffer = commandBuffers[currentFrame].get();
        commandBuffer.reset(vk::CommandBufferResetFlags{});
        recordCommandBuffer(commandBuffer, acquireImageResult.value);
        std::vector<vk::Semaphore> waitSemaphores = {imageAvailableSemaphores[currentFrame].get()};
        std::vector<vk::Semaphore> signalSemaphores = {renderFinishedSemaphores[currentFrame].get()};
        vk::PipelineStageFlags waitStages = {vk::PipelineStageFlagBits::eColorAttachmentOutput};
        updateUniformBuffer(currentFrame);
        vk::SubmitInfo submitInfo{waitSemaphores, waitStages, commandBuffer, signalSemaphores};
        graphicsQueue.submit(submitInfo, inFlightFences[currentFrame].get());
        vk::SwapchainKHR swapChains = {swapChain.get()};
        vk::PresentInfoKHR presentInfo{signalSemaphores, swapChains, acquireImageResult.value, nullptr};
        vk::Result presentResult = presentQueue.presentKHR(presentInfo);
        if (presentResult == vk::Result::eErrorOutOfDateKHR || presentResult == vk::Result::eSuboptimalKHR ||
            bFramebufferResized) {
            bFramebufferResized = false;
            recreateSwapChain();
        } else if (presentResult != vk::Result::eSuccess) {
            throw std::runtime_error("failed to invoke presentKHR");
        }
        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    void mainLoop() {
        while (bKeepWindowOpen) {
            handleEvents();
            drawFrame();
        }
        device->waitIdle();
    }

private:
    uint32_t currentFrame = 0;
    bool bKeepWindowOpen = true;
    bool bFramebufferResized = false;
    bool bIsWindowMinimized = false;
    unsigned int screenWidth = DEFAULT_WIDTH, screenHeight = DEFAULT_HEIGHT;
    SDL_Window *pWindow;
    vk::UniqueInstance instance;
    vk::DebugUtilsMessengerEXT debugMessenger;
    vk::PhysicalDevice physicalDevice;
    vk::UniqueDevice device;
    vk::UniqueSurfaceKHR surface;
    vk::Queue graphicsQueue;
    vk::Queue presentQueue;
    vk::UniqueSwapchainKHR swapChain;
    std::vector<vk::Image> swapChainImages;
    vk::Format swapChainImageFormat;
    vk::Extent2D swapChainExtent;
    std::vector<vk::UniqueImageView> swapChainImageViews;
    std::vector<vk::UniqueSemaphore> imageAvailableSemaphores;
    std::vector<vk::UniqueSemaphore> renderFinishedSemaphores;
    std::vector<vk::UniqueFence> inFlightFences;
    vk::UniqueRenderPass renderPass;
    vk::UniqueDescriptorSetLayout descriptorSetLayout;
    vk::UniquePipelineLayout pipelineLayout;
    vk::UniquePipeline graphicsPipeline;
    std::vector<vk::UniqueFramebuffer> swapChainFramebuffers;
    vk::UniqueCommandPool commandPool;
    std::vector<vk::UniqueCommandBuffer> commandBuffers;
    vk::UniqueDeviceMemory vertexBufferMemory;
    vk::UniqueBuffer vertexBuffer;
    vk::UniqueDeviceMemory indexBufferMemory;
    vk::UniqueBuffer indexBuffer;
    std::vector<vk::UniqueBuffer> uniformBuffers;
    std::vector<vk::UniqueDeviceMemory> uniformBuffersMemory;
    std::vector<vk::DescriptorSet> descriptorSets; // freed with descriptorPool
    vk::UniqueDescriptorPool descriptorPool;
};

int main() {
    HelloTriangleApplication app;

    try {
        app.run();
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}