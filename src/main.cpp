#include <vulkan/vk_platform.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_structs.hpp>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <tl/expected.hpp>

#include <SDL2/SDL.h>
#include <SDL2/SDL_video.h>
#include <SDL2/SDL_vulkan.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>


#include "helpers.hpp"

// CONSTANTS

const uint32_t DEFAULT_WIDTH = 1024;
const uint32_t DEFAULT_HEIGHT = 768;

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
        cleanup();
    }

private:
    bool initWindow() {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
            std::cout << "Failed to initialize the SDL2\n";
            std::cout << "SDL2 Error: " << SDL_GetError() << "\n";
            return false;
        }
        window = SDL_CreateWindow("Vulkan Window", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                  static_cast<int>(screenWidth), static_cast<int>(screenHeight),
                                  SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN);
        if (!window) {
            std::cout << "Failed to create window\n";
            std::cout << "SDL2 Error: " << SDL_GetError() << "\n";
            return false;
        }

        windowSurface = SDL_GetWindowSurface(window);

        if (!windowSurface) {
            std::cout << "Failed to get the surface from the window\n";
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
        createGraphicsPipeline();
        createFramebuffers();
        createCommandPool();
        createCommandBuffer();

        return true;
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
        if (!SDL_Vulkan_GetInstanceExtensions(window, &sdlExtensionCount, nullptr)) {
            return tl::unexpected(std::string("Can't query instance extension count\n"));
        }
        std::vector<const char *> sdlExtensionNames(sdlExtensionCount);
        if (!SDL_Vulkan_GetInstanceExtensions(window, &sdlExtensionCount, sdlExtensionNames.data())) {
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
        physicalDevice = VK_NULL_HANDLE;
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
        SDL_bool state = SDL_Vulkan_CreateSurface(window, instance.get(), &sdlSurface);
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
            SDL_GL_GetDrawableSize(window, &width, &height);

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
        createInfo.oldSwapchain = VK_NULL_HANDLE;

        swapchain = device->createSwapchainKHRUnique(createInfo);
        swapChainImages = device->getSwapchainImagesKHR(swapchain.get());
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

    void createGraphicsPipeline() {
        std::vector<char> vertShaderCode = readFile("shaders/vert.spv");
        std::vector<char> fragShaderCode = readFile("shaders/frag.spv");
        vk::UniqueShaderModule vertShaderModule = createShaderModule(vertShaderCode);
        vk::UniqueShaderModule fragShaderModule = createShaderModule(fragShaderCode);
        vk::PipelineShaderStageCreateInfo vertShaderStageInfo{
            vk::PipelineShaderStageCreateFlags{}, vk::ShaderStageFlagBits::eVertex, vertShaderModule.get(), "main"};
        vk::PipelineShaderStageCreateInfo fragShaderStageInfo{
            vk::PipelineShaderStageCreateFlags{}, vk::ShaderStageFlagBits::eFragment, fragShaderModule.get(), "main"};
        vk::PipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

        vk::PipelineVertexInputStateCreateInfo vertexInputInfo{vk::PipelineVertexInputStateCreateFlags{}, 0, nullptr, 0,
                                                               nullptr};
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
                                                            vk::FrontFace::eClockwise,
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

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{vk::PipelineLayoutCreateFlags{}, {}, {}};
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
        vk::RenderPassCreateInfo renderPassInfo{
            vk::RenderPassCreateFlags{},
            /*attachmentCount*/ 1,       &colorAttachment,
            /*subpassCount*/ 1,          &subpass,
        };
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

    void createCommandBuffer() {
        vk::CommandBufferAllocateInfo allocInfo{commandPool.get(), vk::CommandBufferLevel::ePrimary,
                                                /*commandBufferCount*/ 1};
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
        commandBuffer.draw(3, 1, 0, 0);
        commandBuffer.endRenderPass();
        commandBuffer.end();
    }

    void mainLoop() {
        bool keep_window_open = true;
        while (keep_window_open) {
            SDL_Event e;
            while (SDL_PollEvent(&e) > 0) {
                switch (e.type) {
                case SDL_QUIT:
                    keep_window_open = false;
                    break;
                }
                SDL_UpdateWindowSurface(window);
            }
        }
    }

    void cleanup() { SDL_DestroyWindow(window); }

private:
    unsigned int screenWidth = DEFAULT_WIDTH, screenHeight = DEFAULT_HEIGHT;
    SDL_Window *window;
    SDL_Surface *windowSurface;
    vk::UniqueInstance instance;
    vk::DebugUtilsMessengerEXT debugMessenger;
    vk::PhysicalDevice physicalDevice;
    vk::UniqueDevice device;
    vk::UniqueSurfaceKHR surface;
    vk::Queue graphicsQueue;
    vk::Queue presentQueue;
    vk::UniqueSwapchainKHR swapchain;
    std::vector<vk::Image> swapChainImages;
    vk::Format swapChainImageFormat;
    vk::Extent2D swapChainExtent;
    std::vector<vk::UniqueImageView> swapChainImageViews;
    vk::UniqueRenderPass renderPass;
    vk::UniquePipelineLayout pipelineLayout;
    vk::UniquePipeline graphicsPipeline;
    std::vector<vk::UniqueFramebuffer> swapChainFramebuffers;
    vk::UniqueCommandPool commandPool;
    std::vector<vk::UniqueCommandBuffer> commandBuffers;
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