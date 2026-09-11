#pragma once

#include "VulkanPresentationSupport.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace owl::vulkan
{
    namespace detail
    {
        inline constexpr std::string_view ValidationLayerName = "VK_LAYER_KHRONOS_validation";

        struct InstanceConfiguration
        {
            std::vector<const char*> enabledExtensions;
            PresentationSupportCapabilities presentationSupport;
            bool validationEnabled = false;
            bool debugMessengerEnabled = false;
            std::string validationWarning;
        };

        [[nodiscard]] std::optional<InstanceConfiguration>
        SelectInstanceConfiguration(std::uint32_t loaderApiVersion,
                                    std::span<const char* const> requiredExtensions,
                                    std::span<const VkExtensionProperties> availableExtensions,
                                    std::span<const VkLayerProperties> availableLayers,
                                    bool requestValidation, std::string& error);

        [[nodiscard]] std::string
        FormatDebugMessage(VkDebugUtilsMessageTypeFlagsEXT messageTypes,
                           const VkDebugUtilsMessengerCallbackDataEXT* callbackData);
    } // namespace detail

    class VulkanInstance
    {
    public:
        VulkanInstance() noexcept = default;
        ~VulkanInstance();

        VulkanInstance(const VulkanInstance&) = delete;
        VulkanInstance& operator=(const VulkanInstance&) = delete;

        VulkanInstance(VulkanInstance&& other) noexcept;
        VulkanInstance& operator=(VulkanInstance&& other) noexcept;

        [[nodiscard]] static std::optional<VulkanInstance> Create(std::string& error);

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] VkInstance Get() const noexcept;
        [[nodiscard]] bool ValidationEnabled() const noexcept;
        [[nodiscard]] const detail::PresentationSupportCapabilities&
        PresentationSupport() const noexcept;

    private:
        VulkanInstance(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger,
                       PFN_vkDestroyDebugUtilsMessengerEXT destroyDebugMessenger,
                       bool validationEnabled,
                       detail::PresentationSupportCapabilities presentationSupport) noexcept;
        void Reset() noexcept;

        VkInstance instance_ = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
        PFN_vkDestroyDebugUtilsMessengerEXT destroyDebugMessenger_ = nullptr;
        bool validationEnabled_ = false;
        detail::PresentationSupportCapabilities presentationSupport_{};
    };
} // namespace owl::vulkan
