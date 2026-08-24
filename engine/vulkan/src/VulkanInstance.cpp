#include "VulkanInstance.h"

#include <owl/foundation/Log.h>

#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace owl::vulkan
{
    namespace
    {
        constexpr std::uint32_t RequiredApiVersion = VK_API_VERSION_1_3;

        [[nodiscard]] std::string ApiVersionString(const std::uint32_t version)
        {
            return std::to_string(VK_API_VERSION_MAJOR(version)) + "." +
                   std::to_string(VK_API_VERSION_MINOR(version)) + "." +
                   std::to_string(VK_API_VERSION_PATCH(version));
        }

        [[nodiscard]] std::string VulkanError(const std::string_view operation,
                                              const VkResult result)
        {
            return std::string{operation} + " failed with VkResult " +
                   std::to_string(static_cast<int>(result));
        }

        [[nodiscard]] bool
        HasExtension(const std::span<const VkExtensionProperties> availableExtensions,
                     const std::string_view requestedName)
        {
            return std::ranges::any_of(
                availableExtensions, [requestedName](const auto& extension)
                { return std::string_view{extension.extensionName} == requestedName; });
        }

        [[nodiscard]] bool HasLayer(const std::span<const VkLayerProperties> availableLayers,
                                    const std::string_view requestedName)
        {
            return std::ranges::any_of(
                availableLayers, [requestedName](const auto& layer)
                { return std::string_view{layer.layerName} == requestedName; });
        }

        void AppendUniqueExtension(std::vector<const char*>& extensions, const char* extension)
        {
            const std::string_view requestedName{extension};
            const bool alreadyEnabled = std::ranges::any_of(
                extensions, [requestedName](const char* enabledExtension)
                { return std::string_view{enabledExtension} == requestedName; });
            if (!alreadyEnabled)
            {
                extensions.push_back(extension);
            }
        }

        [[nodiscard]] bool
        EnumerateInstanceExtensions(std::vector<VkExtensionProperties>& extensions,
                                    std::string& error)
        {
            std::uint32_t count = 0;
            VkResult result = vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
            if (result != VK_SUCCESS)
            {
                error = VulkanError("vkEnumerateInstanceExtensionProperties(count)", result);
                return false;
            }

            do
            {
                extensions.resize(count);
                VkExtensionProperties* data = count > 0 ? extensions.data() : nullptr;
                result = vkEnumerateInstanceExtensionProperties(nullptr, &count, data);
            } while (result == VK_INCOMPLETE);

            if (result != VK_SUCCESS)
            {
                error = VulkanError("vkEnumerateInstanceExtensionProperties(data)", result);
                return false;
            }

            extensions.resize(count);
            return true;
        }

        [[nodiscard]] bool EnumerateInstanceLayers(std::vector<VkLayerProperties>& layers,
                                                   std::string& error)
        {
            std::uint32_t count = 0;
            VkResult result = vkEnumerateInstanceLayerProperties(&count, nullptr);
            if (result != VK_SUCCESS)
            {
                error = VulkanError("vkEnumerateInstanceLayerProperties(count)", result);
                return false;
            }

            do
            {
                layers.resize(count);
                VkLayerProperties* data = count > 0 ? layers.data() : nullptr;
                result = vkEnumerateInstanceLayerProperties(&count, data);
            } while (result == VK_INCOMPLETE);

            if (result != VK_SUCCESS)
            {
                error = VulkanError("vkEnumerateInstanceLayerProperties(data)", result);
                return false;
            }

            layers.resize(count);
            return true;
        }

        [[nodiscard]] bool QueryLoaderApiVersion(std::uint32_t& version, std::string& error)
        {
            version = VK_API_VERSION_1_0;
            const auto enumerateInstanceVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
                vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"));
            if (enumerateInstanceVersion == nullptr)
            {
                return true;
            }

            const VkResult result = enumerateInstanceVersion(&version);
            if (result != VK_SUCCESS)
            {
                error = VulkanError("vkEnumerateInstanceVersion", result);
                return false;
            }

            return true;
        }

        [[nodiscard]] owl::foundation::LogLevel
        ToLogLevel(const VkDebugUtilsMessageSeverityFlagBitsEXT severity)
        {
            if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0)
            {
                return owl::foundation::LogLevel::Error;
            }
            if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0)
            {
                return owl::foundation::LogLevel::Warning;
            }
            if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) != 0)
            {
                return owl::foundation::LogLevel::Info;
            }
            return owl::foundation::LogLevel::Trace;
        }

        VKAPI_ATTR VkBool32 VKAPI_CALL
        DebugCallback(const VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                      const VkDebugUtilsMessageTypeFlagsEXT messageTypes,
                      const VkDebugUtilsMessengerCallbackDataEXT* callbackData, void* userData)
        {
            static_cast<void>(userData);
            try
            {
                owl::foundation::LogMessage(ToLogLevel(messageSeverity), "VulkanValidation",
                                            detail::FormatDebugMessage(messageTypes, callbackData));
            }
            catch (...)
            {
            }
            return VK_FALSE;
        }

        [[nodiscard]] VkDebugUtilsMessengerCreateInfoEXT DebugMessengerCreateInfo()
        {
            VkDebugUtilsMessengerCreateInfoEXT createInfo{
                .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
                .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                   VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                                   VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                   VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
                .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
                .pfnUserCallback = DebugCallback,
            };
            return createInfo;
        }
    } // namespace

    namespace detail
    {
        std::optional<InstanceConfiguration> SelectInstanceConfiguration(
            const std::uint32_t loaderApiVersion,
            const std::span<const char* const> requiredExtensions,
            const std::span<const VkExtensionProperties> availableExtensions,
            const std::span<const VkLayerProperties> availableLayers, const bool requestValidation,
            std::string& error)
        {
            error.clear();
            if (loaderApiVersion < RequiredApiVersion)
            {
                error = "Vulkan Loader API " + ApiVersionString(loaderApiVersion) +
                        " is below the required Vulkan 1.3 baseline";
                return std::nullopt;
            }

            InstanceConfiguration configuration;
            for (const char* requiredExtension : requiredExtensions)
            {
                if (requiredExtension == nullptr || requiredExtension[0] == '\0')
                {
                    error = "SDL reported an empty required Vulkan instance extension";
                    return std::nullopt;
                }
                if (!HasExtension(availableExtensions, requiredExtension))
                {
                    error = "Required Vulkan instance extension is unavailable: " +
                            std::string{requiredExtension};
                    return std::nullopt;
                }
                AppendUniqueExtension(configuration.enabledExtensions, requiredExtension);
            }

            if (!requestValidation)
            {
                return configuration;
            }

            if (!HasLayer(availableLayers, ValidationLayerName))
            {
                configuration.validationWarning =
                    "Validation requested but VK_LAYER_KHRONOS_validation is unavailable";
                return configuration;
            }

            configuration.validationEnabled = true;
            if (!HasExtension(availableExtensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
            {
                configuration.validationWarning =
                    "Validation enabled without a debug messenger because "
                    "VK_EXT_debug_utils is unavailable";
                return configuration;
            }

            configuration.debugMessengerEnabled = true;
            AppendUniqueExtension(configuration.enabledExtensions,
                                  VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            return configuration;
        }

        std::string FormatDebugMessage(const VkDebugUtilsMessageTypeFlagsEXT messageTypes,
                                       const VkDebugUtilsMessengerCallbackDataEXT* callbackData)
        {
            std::ostringstream stream;
            stream << "types=0x" << std::hex << messageTypes << std::dec;
            if (callbackData == nullptr)
            {
                return stream.str();
            }

            stream << " id=";
            if (callbackData->pMessageIdName != nullptr)
            {
                stream << callbackData->pMessageIdName;
            }
            else
            {
                stream << "unknown";
            }
            stream << "(" << callbackData->messageIdNumber << ")";

            if (callbackData->pMessage != nullptr)
            {
                stream << " message=" << callbackData->pMessage;
            }

            if (callbackData->objectCount > 0 && callbackData->pObjects != nullptr)
            {
                stream << " objects=[";
                for (std::uint32_t index = 0; index < callbackData->objectCount; ++index)
                {
                    if (index > 0)
                    {
                        stream << ", ";
                    }

                    const VkDebugUtilsObjectNameInfoEXT& object = callbackData->pObjects[index];
                    stream << "{type=" << static_cast<int>(object.objectType) << ", handle=0x"
                           << std::hex << object.objectHandle << std::dec;
                    if (object.pObjectName != nullptr)
                    {
                        stream << ", name=" << object.pObjectName;
                    }
                    stream << "}";
                }
                stream << "]";
            }

            return stream.str();
        }
    } // namespace detail

    VulkanInstance::~VulkanInstance()
    {
        Reset();
    }

    VulkanInstance::VulkanInstance(VulkanInstance&& other) noexcept
        : instance_(std::exchange(other.instance_, VK_NULL_HANDLE)),
          debugMessenger_(std::exchange(other.debugMessenger_, VK_NULL_HANDLE)),
          destroyDebugMessenger_(std::exchange(other.destroyDebugMessenger_, nullptr)),
          validationEnabled_(std::exchange(other.validationEnabled_, false))
    {
    }

    VulkanInstance& VulkanInstance::operator=(VulkanInstance&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            instance_ = std::exchange(other.instance_, VK_NULL_HANDLE);
            debugMessenger_ = std::exchange(other.debugMessenger_, VK_NULL_HANDLE);
            destroyDebugMessenger_ = std::exchange(other.destroyDebugMessenger_, nullptr);
            validationEnabled_ = std::exchange(other.validationEnabled_, false);
        }
        return *this;
    }

    std::optional<VulkanInstance> VulkanInstance::Create(std::string& error)
    {
        if (SDL_Vulkan_GetVkGetInstanceProcAddr() == nullptr)
        {
            error = "SDL Vulkan Loader is unavailable; create a WindowSurfaceApi::Vulkan window "
                    "first: " +
                    std::string{SDL_GetError()};
            return std::nullopt;
        }

        std::uint32_t loaderApiVersion = VK_API_VERSION_1_0;
        if (!QueryLoaderApiVersion(loaderApiVersion, error))
        {
            return std::nullopt;
        }

        std::uint32_t requiredExtensionCount = 0;
        const char* const* requiredExtensionNames =
            SDL_Vulkan_GetInstanceExtensions(&requiredExtensionCount);
        if (requiredExtensionNames == nullptr)
        {
            error = "SDL_Vulkan_GetInstanceExtensions failed: " + std::string{SDL_GetError()};
            return std::nullopt;
        }

        std::vector<VkExtensionProperties> availableExtensions;
        if (!EnumerateInstanceExtensions(availableExtensions, error))
        {
            return std::nullopt;
        }

        std::vector<VkLayerProperties> availableLayers;
        if (!EnumerateInstanceLayers(availableLayers, error))
        {
            return std::nullopt;
        }

#if defined(OWL_ENABLE_VULKAN_VALIDATION)
        constexpr bool requestValidation = true;
#else
        constexpr bool requestValidation = false;
#endif

        const std::span<const char* const> requiredExtensions{requiredExtensionNames,
                                                              requiredExtensionCount};
        auto configuration = detail::SelectInstanceConfiguration(
            loaderApiVersion, requiredExtensions, availableExtensions, availableLayers,
            requestValidation, error);
        if (!configuration)
        {
            return std::nullopt;
        }

        owl::foundation::LogMessage(owl::foundation::LogLevel::Info, "Vulkan",
                                    "Loader API version: " + ApiVersionString(loaderApiVersion));
        if (!configuration->validationWarning.empty())
        {
            owl::foundation::LogMessage(owl::foundation::LogLevel::Warning, "Vulkan",
                                        configuration->validationWarning);
        }

        const VkApplicationInfo applicationInfo{
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pApplicationName = "OwlEngine M1 Vulkan Bootstrap",
            .applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
            .pEngineName = "OwlEngine",
            .engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
            .apiVersion = RequiredApiVersion,
        };

        const char* validationLayer = detail::ValidationLayerName.data();
        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = DebugMessengerCreateInfo();
        const VkInstanceCreateInfo instanceCreateInfo{
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pNext = configuration->debugMessengerEnabled ? &debugCreateInfo : nullptr,
            .pApplicationInfo = &applicationInfo,
            .enabledLayerCount = configuration->validationEnabled ? 1U : 0U,
            .ppEnabledLayerNames = configuration->validationEnabled ? &validationLayer : nullptr,
            .enabledExtensionCount =
                static_cast<std::uint32_t>(configuration->enabledExtensions.size()),
            .ppEnabledExtensionNames = configuration->enabledExtensions.data(),
        };

        VkInstance instance = VK_NULL_HANDLE;
        const VkResult createResult = vkCreateInstance(&instanceCreateInfo, nullptr, &instance);
        if (createResult != VK_SUCCESS)
        {
            error = VulkanError("vkCreateInstance", createResult);
            return std::nullopt;
        }

        VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
        PFN_vkDestroyDebugUtilsMessengerEXT destroyDebugMessenger = nullptr;
        if (configuration->debugMessengerEnabled)
        {
            const auto createDebugMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
            destroyDebugMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
            if (createDebugMessenger == nullptr || destroyDebugMessenger == nullptr)
            {
                vkDestroyInstance(instance, nullptr);
                error = "VK_EXT_debug_utils entry points are unavailable after instance creation";
                return std::nullopt;
            }

            const VkResult debugResult =
                createDebugMessenger(instance, &debugCreateInfo, nullptr, &debugMessenger);
            if (debugResult != VK_SUCCESS)
            {
                vkDestroyInstance(instance, nullptr);
                error = VulkanError("vkCreateDebugUtilsMessengerEXT", debugResult);
                return std::nullopt;
            }
        }

        error.clear();
        return VulkanInstance{instance, debugMessenger, destroyDebugMessenger,
                              configuration->validationEnabled};
    }

    bool VulkanInstance::IsValid() const noexcept
    {
        return instance_ != VK_NULL_HANDLE;
    }

    VkInstance VulkanInstance::Get() const noexcept
    {
        return instance_;
    }

    bool VulkanInstance::ValidationEnabled() const noexcept
    {
        return validationEnabled_;
    }

    VulkanInstance::VulkanInstance(const VkInstance instance,
                                   const VkDebugUtilsMessengerEXT debugMessenger,
                                   const PFN_vkDestroyDebugUtilsMessengerEXT destroyDebugMessenger,
                                   const bool validationEnabled) noexcept
        : instance_(instance), debugMessenger_(debugMessenger),
          destroyDebugMessenger_(destroyDebugMessenger), validationEnabled_(validationEnabled)
    {
    }

    void VulkanInstance::Reset() noexcept
    {
        if (instance_ != VK_NULL_HANDLE && debugMessenger_ != VK_NULL_HANDLE &&
            destroyDebugMessenger_ != nullptr)
        {
            destroyDebugMessenger_(instance_, debugMessenger_, nullptr);
        }
        if (instance_ != VK_NULL_HANDLE)
        {
            vkDestroyInstance(instance_, nullptr);
        }

        instance_ = VK_NULL_HANDLE;
        debugMessenger_ = VK_NULL_HANDLE;
        destroyDebugMessenger_ = nullptr;
        validationEnabled_ = false;
    }
} // namespace owl::vulkan
