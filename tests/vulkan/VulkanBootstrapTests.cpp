#include <catch2/catch_test_macros.hpp>

#include "VulkanInstance.h"
#include "VulkanSurface.h"

#include <owl/foundation/Log.h>
#include <owl/platform/Platform.h>

#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <array>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace
{
VkExtensionProperties MakeExtension(const std::string_view name)
{
    VkExtensionProperties property{};
    REQUIRE(name.size() < std::size(property.extensionName));
    std::copy(name.begin(), name.end(), property.extensionName);
    property.extensionName[name.size()] = '\0';
    return property;
}

VkLayerProperties MakeLayer(const std::string_view name)
{
    VkLayerProperties property{};
    REQUIRE(name.size() < std::size(property.layerName));
    std::copy(name.begin(), name.end(), property.layerName);
    property.layerName[name.size()] = '\0';
    return property;
}

bool ContainsExtension(const owl::vulkan::detail::InstanceConfiguration& configuration,
                       const std::string_view name)
{
    return std::ranges::any_of(configuration.enabledExtensions,
                               [name](const char* extension) { return extension == name; });
}

class LoggingScope
{
  public:
    LoggingScope()
    {
        owl::foundation::InitializeLogging();
    }
    ~LoggingScope()
    {
        owl::foundation::ShutdownLogging();
    }

    LoggingScope(const LoggingScope&) = delete;
    LoggingScope& operator=(const LoggingScope&) = delete;
};
} // namespace

static_assert(!std::is_copy_constructible_v<owl::vulkan::VulkanInstance>);
static_assert(std::is_move_constructible_v<owl::vulkan::VulkanInstance>);
static_assert(!std::is_copy_constructible_v<owl::vulkan::VulkanSurface>);
static_assert(std::is_move_constructible_v<owl::vulkan::VulkanSurface>);

TEST_CASE("Instance configuration rejects loaders below Vulkan 1.3", "[vulkan]")
{
    const std::array requiredExtensions{VK_KHR_SURFACE_EXTENSION_NAME};
    const std::array availableExtensions{MakeExtension(VK_KHR_SURFACE_EXTENSION_NAME)};
    const std::array<VkLayerProperties, 0> availableLayers{};
    std::string error;

    const auto configuration = owl::vulkan::detail::SelectInstanceConfiguration(
        VK_API_VERSION_1_2, requiredExtensions, availableExtensions, availableLayers, false, error);

    CHECK_FALSE(configuration.has_value());
    CHECK(error.find("1.2.0") != std::string::npos);
    CHECK(error.find("1.3") != std::string::npos);
}

TEST_CASE("Instance configuration requires every SDL extension", "[vulkan]")
{
    constexpr std::string_view missingExtension = "VK_TEST_required_surface";
    const std::array requiredExtensions{VK_KHR_SURFACE_EXTENSION_NAME, missingExtension.data()};
    const std::array availableExtensions{MakeExtension(VK_KHR_SURFACE_EXTENSION_NAME)};
    const std::array<VkLayerProperties, 0> availableLayers{};
    std::string error;

    const auto configuration = owl::vulkan::detail::SelectInstanceConfiguration(
        VK_API_VERSION_1_3, requiredExtensions, availableExtensions, availableLayers, false, error);

    CHECK_FALSE(configuration.has_value());
    CHECK(error.find(missingExtension) != std::string::npos);
}

TEST_CASE("Newer loaders are accepted and duplicate SDL extensions are removed", "[vulkan]")
{
    const std::array requiredExtensions{VK_KHR_SURFACE_EXTENSION_NAME,
                                        VK_KHR_SURFACE_EXTENSION_NAME};
    const std::array availableExtensions{MakeExtension(VK_KHR_SURFACE_EXTENSION_NAME)};
    const std::array<VkLayerProperties, 0> availableLayers{};
    std::string error;

    const auto configuration = owl::vulkan::detail::SelectInstanceConfiguration(
        VK_API_VERSION_1_4, requiredExtensions, availableExtensions, availableLayers, false, error);

    REQUIRE(configuration.has_value());
    REQUIRE(configuration->enabledExtensions.size() == 1);
    CHECK(std::string_view{configuration->enabledExtensions.front()} ==
          VK_KHR_SURFACE_EXTENSION_NAME);
    CHECK(error.empty());
}

TEST_CASE("Validation is disabled when the layer is unavailable", "[vulkan]")
{
    const std::array requiredExtensions{VK_KHR_SURFACE_EXTENSION_NAME};
    const std::array availableExtensions{MakeExtension(VK_KHR_SURFACE_EXTENSION_NAME),
                                         MakeExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)};
    const std::array<VkLayerProperties, 0> availableLayers{};
    std::string error;

    const auto configuration = owl::vulkan::detail::SelectInstanceConfiguration(
        VK_API_VERSION_1_3, requiredExtensions, availableExtensions, availableLayers, true, error);

    REQUIRE(configuration.has_value());
    CHECK_FALSE(configuration->validationEnabled);
    CHECK_FALSE(configuration->debugMessengerEnabled);
    CHECK_FALSE(configuration->validationWarning.empty());
    CHECK_FALSE(ContainsExtension(*configuration, VK_EXT_DEBUG_UTILS_EXTENSION_NAME));
    CHECK(error.empty());
}

TEST_CASE("Validation enables its layer and debug-utils extension together", "[vulkan]")
{
    const std::array requiredExtensions{VK_KHR_SURFACE_EXTENSION_NAME};
    const std::array availableExtensions{MakeExtension(VK_KHR_SURFACE_EXTENSION_NAME),
                                         MakeExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)};
    const std::array availableLayers{MakeLayer(owl::vulkan::detail::ValidationLayerName)};
    std::string error;

    const auto configuration = owl::vulkan::detail::SelectInstanceConfiguration(
        VK_API_VERSION_1_3, requiredExtensions, availableExtensions, availableLayers, true, error);

    REQUIRE(configuration.has_value());
    CHECK(configuration->validationEnabled);
    CHECK(configuration->debugMessengerEnabled);
    CHECK(configuration->validationWarning.empty());
    CHECK(ContainsExtension(*configuration, VK_EXT_DEBUG_UTILS_EXTENSION_NAME));
    CHECK(error.empty());
}

TEST_CASE("Validation remains enabled without a debug messenger extension", "[vulkan]")
{
    const std::array requiredExtensions{VK_KHR_SURFACE_EXTENSION_NAME};
    const std::array availableExtensions{MakeExtension(VK_KHR_SURFACE_EXTENSION_NAME)};
    const std::array availableLayers{MakeLayer(owl::vulkan::detail::ValidationLayerName)};
    std::string error;

    const auto configuration = owl::vulkan::detail::SelectInstanceConfiguration(
        VK_API_VERSION_1_3, requiredExtensions, availableExtensions, availableLayers, true, error);

    REQUIRE(configuration.has_value());
    CHECK(configuration->validationEnabled);
    CHECK_FALSE(configuration->debugMessengerEnabled);
    CHECK_FALSE(configuration->validationWarning.empty());
    CHECK_FALSE(ContainsExtension(*configuration, VK_EXT_DEBUG_UTILS_EXTENSION_NAME));
    CHECK(error.empty());
}

TEST_CASE("Validation remains disabled when it was not requested", "[vulkan]")
{
    const std::array requiredExtensions{VK_KHR_SURFACE_EXTENSION_NAME};
    const std::array availableExtensions{MakeExtension(VK_KHR_SURFACE_EXTENSION_NAME),
                                         MakeExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)};
    const std::array availableLayers{MakeLayer(owl::vulkan::detail::ValidationLayerName)};
    std::string error;

    const auto configuration = owl::vulkan::detail::SelectInstanceConfiguration(
        VK_API_VERSION_1_3, requiredExtensions, availableExtensions, availableLayers, false, error);

    REQUIRE(configuration.has_value());
    CHECK_FALSE(configuration->validationEnabled);
    CHECK_FALSE(configuration->debugMessengerEnabled);
    CHECK_FALSE(ContainsExtension(*configuration, VK_EXT_DEBUG_UTILS_EXTENSION_NAME));
    CHECK(error.empty());
}

TEST_CASE("Debug messages include IDs and Vulkan object context", "[vulkan]")
{
    const VkDebugUtilsObjectNameInfoEXT object{
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .objectType = VK_OBJECT_TYPE_BUFFER,
        .objectHandle = 0x1234,
        .pObjectName = "UploadBuffer",
    };
    const VkDebugUtilsMessengerCallbackDataEXT callbackData{
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CALLBACK_DATA_EXT,
        .pMessageIdName = "VUID-Test-00001",
        .messageIdNumber = 17,
        .pMessage = "Synthetic validation message",
        .objectCount = 1,
        .pObjects = &object,
    };

    const std::string message = owl::vulkan::detail::FormatDebugMessage(
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT, &callbackData);

    CHECK(message.find("VUID-Test-00001") != std::string::npos);
    CHECK(message.find("17") != std::string::npos);
    CHECK(message.find("Synthetic validation message") != std::string::npos);
    CHECK(message.find("UploadBuffer") != std::string::npos);
    CHECK(message.find("1234") != std::string::npos);
}

TEST_CASE("Empty Vulkan owners remain invalid after moves", "[vulkan]")
{
    owl::vulkan::VulkanInstance instanceSource;
    owl::vulkan::VulkanInstance instanceDestination{std::move(instanceSource)};
    CHECK_FALSE(instanceSource.IsValid());
    CHECK_FALSE(instanceDestination.IsValid());

    owl::vulkan::VulkanSurface surfaceSource;
    owl::vulkan::VulkanSurface surfaceDestination{std::move(surfaceSource)};
    CHECK_FALSE(surfaceSource.IsValid());
    CHECK_FALSE(surfaceDestination.IsValid());
}

TEST_CASE("Vulkan instance and SDL surface bootstrap locally", "[vulkan][integration]")
{
    const char* enabled = SDL_getenv("OWL_RUN_VULKAN_BOOTSTRAP_TEST");
    if (enabled == nullptr || std::string_view{enabled} != "1")
    {
        SKIP("Set OWL_RUN_VULKAN_BOOTSTRAP_TEST=1 to run the local GPU bootstrap test");
    }

    LoggingScope logging;
    std::string error;
    auto platform = owl::platform::Platform::Create(error);
    INFO(error);
    REQUIRE(platform.has_value());

    const owl::platform::WindowDesc windowDesc{
        .title = "OwlEngine - Vulkan Bootstrap Test",
        .width = 320,
        .height = 180,
        .resizable = false,
        .surfaceApi = owl::platform::WindowSurfaceApi::Vulkan,
    };
    auto window = platform->CreateWindow(windowDesc, error);
    INFO(error);
    REQUIRE(window.has_value());

    auto instance = owl::vulkan::VulkanInstance::Create(error);
    INFO(error);
    REQUIRE(instance.has_value());

    auto surface = owl::vulkan::VulkanSurface::Create(*instance, *window, error);
    INFO(error);
    REQUIRE(surface.has_value());
    CHECK(surface->IsValid());
}
