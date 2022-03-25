#include <SDL2/SDL_video.h>
#include <vulkan/vk_platform.h>
#include <vulkan/vulkan.hpp>
#include <GLFW/glfw3.h>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_structs.hpp>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
#include <tl/expected.hpp>

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <optional>

#include "helpers.hpp"

// CONSTANTS

const uint32_t DEFAULT_WIDTH = 1024;
const uint32_t DEFAULT_HEIGHT = 768;

const std::vector<const char*> gValidationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

#ifdef NDEBUG
    static constexpr bool gEnableValidationLayers = false;
#else
    static constexpr bool gEnableValidationLayers = true;
#endif

// UTILS

#define UNUSED(expr) (void)(expr)

// DEBUG DEFINITIONS

std::unordered_map< VkInstance, PFN_vkCreateDebugUtilsMessengerEXT > CreateDebugUtilsMessengerEXTDispatchTable;
std::unordered_map< VkInstance, PFN_vkDestroyDebugUtilsMessengerEXT > DestroyDebugUtilsMessengerEXTDispatchTable;
std::unordered_map< VkInstance, PFN_vkSubmitDebugUtilsMessageEXT > SubmitDebugUtilsMessageEXTDispatchTable;

void loadDebugUtilsCommands( VkInstance instance ){
	PFN_vkVoidFunction temp_fp;

	temp_fp = vkGetInstanceProcAddr( instance, "vkCreateDebugUtilsMessengerEXT" );
	if( !temp_fp ) throw "Failed to load vkCreateDebugUtilsMessengerEXT"; // check shouldn't be necessary (based on spec)
	CreateDebugUtilsMessengerEXTDispatchTable[instance] = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>( temp_fp );

	temp_fp = vkGetInstanceProcAddr( instance, "vkDestroyDebugUtilsMessengerEXT" );
	if( !temp_fp ) throw "Failed to load vkDestroyDebugUtilsMessengerEXT"; // check shouldn't be necessary (based on spec)
	DestroyDebugUtilsMessengerEXTDispatchTable[instance] = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>( temp_fp );

	temp_fp = vkGetInstanceProcAddr( instance, "vkSubmitDebugUtilsMessageEXT" );
	if( !temp_fp ) throw "Failed to load vkSubmitDebugUtilsMessageEXT"; // check shouldn't be necessary (based on spec)
	SubmitDebugUtilsMessageEXTDispatchTable[instance] = reinterpret_cast<PFN_vkSubmitDebugUtilsMessageEXT>( temp_fp );
}

void unloadDebugUtilsCommands( VkInstance instance ){
	CreateDebugUtilsMessengerEXTDispatchTable.erase( instance );
	DestroyDebugUtilsMessengerEXTDispatchTable.erase( instance );
	SubmitDebugUtilsMessageEXTDispatchTable.erase( instance );
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDebugUtilsMessengerEXT(
	VkInstance instance,
	const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
	const VkAllocationCallbacks* pAllocator,
	VkDebugUtilsMessengerEXT* pMessenger
){
	auto dispatched_cmd = CreateDebugUtilsMessengerEXTDispatchTable.at( instance );
	return dispatched_cmd( instance, pCreateInfo, pAllocator, pMessenger );
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDebugUtilsMessengerEXT(
	VkInstance instance,
	VkDebugUtilsMessengerEXT messenger,
	const VkAllocationCallbacks* pAllocator
){
	auto dispatched_cmd = DestroyDebugUtilsMessengerEXTDispatchTable.at( instance );
	return dispatched_cmd( instance, messenger, pAllocator );
}

VKAPI_ATTR void VKAPI_CALL vkSubmitDebugUtilsMessageEXT(
	VkInstance instance,
	VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT messageTypes,
	const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData
){
	auto dispatched_cmd = SubmitDebugUtilsMessageEXTDispatchTable.at( instance );
	return dispatched_cmd( instance, messageSeverity, messageTypes, pCallbackData );
}


// CORE DEFINITIONS

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData)
{
    UNUSED(messageSeverity);
    UNUSED(messageType);
    UNUSED(pCallbackData);
    UNUSED(pUserData);
    std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;
    return VK_FALSE;
}

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;

    bool isComplete() {
        return graphicsFamily.has_value();
    }
};

QueueFamilyIndices findQueueFamilies(vk::PhysicalDevice device) {
    QueueFamilyIndices indices;
    
    std::vector<vk::QueueFamilyProperties> queueFamilies = device.getQueueFamilyProperties();
    int i = 0;
    for (const auto& queueFamily : queueFamilies) {
        if (queueFamily.queueFlags & vk::QueueFlagBits::eGraphics) {
            indices.graphicsFamily = i;
        }
        if (indices.isComplete()) {
            break;
        }
        ++i;
    }

    return indices;
}

class HelloTriangleApplication {
public:
    void run() {
        if (!initWindow()) {
            throw std::runtime_error("Failed to init window");
        }
        if (!initVulkan()) {
            throw std::runtime_error("Failed to init vulkan");
        }
        mainLoop();
        cleanup();
    }

private:
    bool initWindow()
    {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
            std::cout << "Failed to initialize the SDL2\n";
            std::cout << "SDL2 Error: " << SDL_GetError() << "\n";
            return false;
        }
        window = SDL_CreateWindow("Vulkan Window",
                                    SDL_WINDOWPOS_CENTERED,
                                    SDL_WINDOWPOS_CENTERED,
                                    static_cast<int>(screenWidth),
                                    static_cast<int>(screenHeight),
                                    SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN);
        if(!window)
        {
            std::cout << "Failed to create window\n";
            std::cout << "SDL2 Error: " << SDL_GetError() << "\n";
            return false;
        }

        windowSurface = SDL_GetWindowSurface(window);

        if(!windowSurface)
        {
            std::cout << "Failed to get the surface from the window\n";
            std::cout << "SDL2 Error: " << SDL_GetError() << "\n";
            return false;
        }
        return true;
    }
    bool initVulkan()
    {
        if (!createInstance()) {
            return false;
        }
        setupDebugMessenger();
        pickPhysicalDevice();
        createLogicalDevice();


        return true;
    }

    void populateDebugMessengerCreateInfo(vk::DebugUtilsMessengerCreateInfoEXT& createInfo)
    {
        createInfo.sType = vk::StructureType::eDebugUtilsMessengerCreateInfoEXT;
        createInfo.messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
        createInfo.messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
        createInfo.pfnUserCallback = debugCallback;
        createInfo.pUserData = nullptr;
    }

    void setupDebugMessenger()
    {
        if (!gEnableValidationLayers) return;

        auto dldi = vk::DispatchLoaderDynamic(instance.get(), vkGetInstanceProcAddr);

        vk::DebugUtilsMessengerCreateInfoEXT createInfo{};
        populateDebugMessengerCreateInfo(createInfo);

        instance->createDebugUtilsMessengerEXTUnique(createInfo);
        
    }

    bool checkValidationLayerSupport() {
        std::vector<vk::LayerProperties> availableLayers = vk::enumerateInstanceLayerProperties();

        for (const char* layerName : gValidationLayers) {
            bool layerFound = false;

            for (const auto& layerProperties : availableLayers) {
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

    tl::expected<std::vector<const char*>, std::string> getRequiredExtensions() {
        uint32_t sdlExtensionCount = 0;
        if (!SDL_Vulkan_GetInstanceExtensions(window, &sdlExtensionCount, nullptr))
        {
            return tl::unexpected(std::string("Can't query instance extension count\n"));
        }
        std::vector<const char*> sdlExtensionNames(sdlExtensionCount);
        if (!SDL_Vulkan_GetInstanceExtensions(window, &sdlExtensionCount, sdlExtensionNames.data()))
        {
            return tl::unexpected(std::string("Can't query instance extension names\n"));
        }

        sdlExtensionNames.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        ++sdlExtensionCount;
        std::cout << "required extensions:\n";
        for (const auto& extensionName : sdlExtensionNames)
        {
            std::cout << '\t' << extensionName << '\n';
            //    sdlVulkanExtensions.emplace_back(sdlExtensionNames[i]);
        }

        std::vector<vk::ExtensionProperties> extensions = vk::enumerateInstanceExtensionProperties(nullptr);
        std::cout << "available extensions:\n";
        for (const auto& extension : extensions) {
            std::cout << '\t' << extension.extensionName << '\n';
        }

        return sdlExtensionNames;
    }

    bool createInstance() {
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
        std::vector<const char*> extensions;
        if (maybeExtensions.has_value()) {
            extensions = maybeExtensions.value();
            createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
            createInfo.ppEnabledExtensionNames = extensions.data();
        }
        else {
            throw std::runtime_error(maybeExtensions.error());
        }

        instance = vk::createInstanceUnique(createInfo);

        loadDebugUtilsCommands(instance.get());

        return true;
    }

    bool isDeviceSuitable(vk::PhysicalDevice device) {
        vk::PhysicalDeviceProperties deviceProperties = device.getProperties();
        vk::PhysicalDeviceFeatures deviceFeatures = device.getFeatures();
        QueueFamilyIndices indices = findQueueFamilies(device);

        return deviceProperties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu &&
            deviceFeatures.geometryShader && indices.isComplete();
    }

    void pickPhysicalDevice() {
        physicalDevice = VK_NULL_HANDLE;
        std::vector<vk::PhysicalDevice> physicalDevices = instance->enumeratePhysicalDevices();
        if (physicalDevices.empty()) {
            throw std::runtime_error("failed to find GPUs with Vulkan support!");
        }
        for (const auto& device : physicalDevices) {
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

        vk::DeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = vk::StructureType::eDeviceQueueCreateInfo;
        queueCreateInfo.queueFamilyIndex = indices.graphicsFamily.value();
        queueCreateInfo.queueCount = 1;
        float queuePriority = 1.0f;
        queueCreateInfo.pQueuePriorities = &queuePriority;

        vk::PhysicalDeviceFeatures deviceFeatures{};

        vk::DeviceCreateInfo createInfo{};
        createInfo.sType = vk::StructureType::eDeviceCreateInfo;
        createInfo.pQueueCreateInfos = &queueCreateInfo;
        createInfo.queueCreateInfoCount = 1;
        createInfo.pEnabledFeatures = &deviceFeatures;
        createInfo.enabledExtensionCount = 0;

        if (gEnableValidationLayers) {
            createInfo.enabledLayerCount = static_cast<uint32_t>(gValidationLayers.size());
            createInfo.ppEnabledLayerNames = gValidationLayers.data();
        } else {
            createInfo.enabledLayerCount = 0;
        }

        device = physicalDevice.createDeviceUnique(createInfo);
        graphicsQueue = device->getQueue(indices.graphicsFamily.value(), 0);
    }

    void mainLoop() {
        bool keep_window_open = true;
        while(keep_window_open)
        {
            SDL_Event e;
            while(SDL_PollEvent(&e) > 0)
            {
                switch(e.type)
                {
                    case SDL_QUIT:
                        keep_window_open = false;
                        break;
                }
                SDL_UpdateWindowSurface(window);
            }
        }
    }

    void cleanup() {
        SDL_DestroyWindow(window);
    }
private:
    unsigned int screenWidth = DEFAULT_WIDTH, screenHeight = DEFAULT_HEIGHT;
    SDL_Window* window;
    SDL_Surface* windowSurface;
    vk::UniqueHandle<vk::Instance, vk::DispatchLoaderStatic> instance;
    // std::vector<std::string> sdlVulkanExtensions;
    vk::DebugUtilsMessengerEXT debugMessenger;
    vk::PhysicalDevice physicalDevice;
    vk::UniqueHandle<vk::Device, vk::DispatchLoaderStatic> device;
    vk::Queue graphicsQueue;
};

int main() {
    HelloTriangleApplication app;

    try {
        app.run();
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}