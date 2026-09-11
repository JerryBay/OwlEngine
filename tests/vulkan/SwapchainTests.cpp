#include <catch2/catch_test_macros.hpp>

#include "SDLWindowAccess.h"
#include "VulkanDevice.h"
#include "VulkanInstance.h"
#include "VulkanSurface.h"
#include "VulkanSwapchain.h"

#include <owl/foundation/Log.h>
#include <owl/platform/Platform.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>

#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace
{
    using owl::vulkan::SwapchainUpdateResult;

    struct SurfaceFixture
    {
        VkSurfaceCapabilitiesKHR capabilities{
            .minImageCount = 2,
            .maxImageCount = 0,
            .currentExtent = {UINT32_MAX, UINT32_MAX},
            .minImageExtent = {64, 64},
            .maxImageExtent = {1920, 1080},
            .maxImageArrayLayers = 1,
            .supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
            .currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
            .supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            .supportedUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        };
        std::array<VkSurfaceFormatKHR, 1> formats{{
            {VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
        }};
        std::array<VkPresentModeKHR, 2> presentModes{VK_PRESENT_MODE_FIFO_KHR,
                                                     VK_PRESENT_MODE_MAILBOX_KHR};
        owl::vulkan::detail::QueueFamilySelection queues{.graphicsFamily = 0, .presentFamily = 0};

        owl::vulkan::detail::SwapchainSupport Support() const
        {
            return {capabilities, formats, presentModes};
        }
    };

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

static_assert(!std::is_copy_constructible_v<owl::vulkan::VulkanSwapchain>);
static_assert(!std::is_copy_assignable_v<owl::vulkan::VulkanSwapchain>);
static_assert(std::is_nothrow_move_constructible_v<owl::vulkan::VulkanSwapchain>);
static_assert(std::is_nothrow_move_assignable_v<owl::vulkan::VulkanSwapchain>);

TEST_CASE("Swapchain image count respects bounded and unbounded surface limits",
          "[vulkan][swapchain]")
{
    SurfaceFixture fixture;
    std::uint32_t expected = 3;
    SECTION("unbounded maximum") {}
    SECTION("bounded maximum")
    {
        fixture.capabilities.maxImageCount = 2;
        expected = 2;
    }
    SECTION("minimum cannot overflow")
    {
        fixture.capabilities.minImageCount = std::numeric_limits<std::uint32_t>::max();
        expected = std::numeric_limits<std::uint32_t>::max();
    }
    owl::vulkan::detail::SwapchainConfiguration configuration;
    std::string error = "stale";
    REQUIRE(owl::vulkan::detail::SelectSwapchainConfiguration(fixture.Support(), fixture.queues,
                                                              {320, 180}, configuration, error) ==
            SwapchainUpdateResult::Ready);
    CHECK(configuration.minImageCount == expected);
    CHECK(error.empty());
}

TEST_CASE("Swapchain sharing uses only distinct graphics and present families",
          "[vulkan][swapchain]")
{
    SurfaceFixture fixture;
    SECTION("unified") {}
    SECTION("separate")
    {
        fixture.queues.presentFamily = 3;
    }
    owl::vulkan::detail::SwapchainConfiguration configuration;
    std::string error;
    REQUIRE(owl::vulkan::detail::SelectSwapchainConfiguration(fixture.Support(), fixture.queues,
                                                              {320, 180}, configuration, error) ==
            SwapchainUpdateResult::Ready);
    if (fixture.queues.presentFamily == 0)
    {
        CHECK(configuration.sharingMode == VK_SHARING_MODE_EXCLUSIVE);
        CHECK(configuration.queueFamilyIndexCount == 0);
    }
    else
    {
        CHECK(configuration.sharingMode == VK_SHARING_MODE_CONCURRENT);
        CHECK(configuration.queueFamilyIndexCount == 2);
        CHECK(configuration.queueFamilyIndices[0] == 0);
        CHECK(configuration.queueFamilyIndices[1] == 3);
    }
}

TEST_CASE("Swapchain configuration uses fresh surface format mode transform and alpha",
          "[vulkan][swapchain]")
{
    SurfaceFixture fixture;
    fixture.formats[0] = {VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    fixture.presentModes.fill(VK_PRESENT_MODE_FIFO_KHR);
    fixture.capabilities.currentExtent = {640, 360};
    fixture.capabilities.supportedTransforms = VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR;
    fixture.capabilities.currentTransform = VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR;
    fixture.capabilities.supportedCompositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    owl::vulkan::detail::SwapchainConfiguration configuration;
    std::string error;
    REQUIRE(owl::vulkan::detail::SelectSwapchainConfiguration(fixture.Support(), fixture.queues,
                                                              {320, 180}, configuration, error) ==
            SwapchainUpdateResult::Ready);
    CHECK(configuration.surfaceFormat.format == VK_FORMAT_R8G8B8A8_UNORM);
    CHECK(configuration.presentMode == VK_PRESENT_MODE_FIFO_KHR);
    CHECK(configuration.preTransform == VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR);
    CHECK(configuration.compositeAlpha == VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR);
    CHECK(configuration.extent.width == 640);
    CHECK(configuration.extent.height == 360);
}

TEST_CASE("Swapchain configuration defers zero extents before clamping", "[vulkan][swapchain]")
{
    SurfaceFixture fixture;
    VkExtent2D requested{320, 180};
    SECTION("zero width despite nonzero fixed extent")
    {
        requested.width = 0;
        fixture.capabilities.currentExtent = {320, 180};
    }
    SECTION("zero height")
    {
        requested.height = 0;
    }
    SECTION("surface became minimized")
    {
        fixture.capabilities.currentExtent = {0, 0};
    }
    SECTION("variable extent with zero maximum")
    {
        fixture.capabilities.minImageExtent = {0, 0};
        fixture.capabilities.maxImageExtent = {0, 0};
    }
    owl::vulkan::detail::SwapchainConfiguration configuration;
    configuration.extent = {999, 999};
    std::string error = "stale";
    CHECK(owl::vulkan::detail::SelectSwapchainConfiguration(fixture.Support(), fixture.queues,
                                                            requested, configuration, error) ==
          SwapchainUpdateResult::Deferred);
    CHECK(error.empty());
    CHECK(configuration.extent.width == 0);
    CHECK(configuration.extent.height == 0);
}

TEST_CASE("Swapchain configuration clamps dimensions and prefers opaque alpha",
          "[vulkan][swapchain]")
{
    SurfaceFixture fixture;
    fixture.capabilities.supportedCompositeAlpha |= VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    owl::vulkan::detail::SwapchainConfiguration configuration;
    std::string error;
    REQUIRE(owl::vulkan::detail::SelectSwapchainConfiguration(fixture.Support(), fixture.queues,
                                                              {4000, 1}, configuration, error) ==
            SwapchainUpdateResult::Ready);
    CHECK(configuration.extent.width == 1920);
    CHECK(configuration.extent.height == 64);
    CHECK(configuration.compositeAlpha == VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR);
    CHECK(configuration.surfaceFormat.format == VK_FORMAT_B8G8R8A8_SRGB);
    CHECK(configuration.presentMode == VK_PRESENT_MODE_MAILBOX_KHR);
}

TEST_CASE("Swapchain configuration rejects missing required surface capabilities",
          "[vulkan][swapchain]")
{
    SurfaceFixture fixture;
    auto support = fixture.Support();
    std::string missing;
    SECTION("color attachment")
    {
        support.capabilities.supportedUsageFlags = 0;
        missing = "COLOR_ATTACHMENT";
    }
    SECTION("alpha")
    {
        support.capabilities.supportedCompositeAlpha = 0;
        missing = "alpha";
    }
    SECTION("formats")
    {
        support.formats = {};
        missing = "format";
    }
    SECTION("modes")
    {
        support.presentModes = {};
        missing = "mode";
    }
    SECTION("queues")
    {
        fixture.queues.presentFamily.reset();
        missing = "queue";
    }
    SECTION("invalid extent limits")
    {
        support.capabilities.minImageExtent.width = 2000;
        missing = "limits";
    }
    SECTION("invalid image count limits")
    {
        support.capabilities.maxImageCount = 1;
        missing = "limits";
    }
    SECTION("no image layers")
    {
        support.capabilities.maxImageArrayLayers = 0;
        missing = "limits";
    }
    SECTION("unsupported transform")
    {
        support.capabilities.supportedTransforms = 0;
        missing = "transform";
    }
    owl::vulkan::detail::SwapchainConfiguration configuration;
    std::string error;
    CHECK(owl::vulkan::detail::SelectSwapchainConfiguration(support, fixture.queues, {320, 180},
                                                            configuration, error) ==
          SwapchainUpdateResult::Failed);
    CHECK(error.find(missing) != std::string::npos);
}

TEST_CASE("Empty swapchains reject missing parents and remain empty after moves",
          "[vulkan][swapchain]")
{
    owl::vulkan::VulkanSwapchain source;
    owl::vulkan::VulkanDevice device;
    owl::vulkan::VulkanSurface surface;
    std::string error;
    CHECK(source.CreateOrRecreate(device, surface, {320, 180}, error) ==
          SwapchainUpdateResult::Failed);
    CHECK(error.find("device") != std::string::npos);
    owl::vulkan::VulkanSwapchain moved{std::move(source)};
    owl::vulkan::VulkanSwapchain assigned;
    assigned = std::move(moved);
    for (const auto* swapchain : {&source, &moved, &assigned})
    {
        CHECK_FALSE(swapchain->IsValid());
        CHECK(swapchain->Get() == VK_NULL_HANDLE);
        CHECK(swapchain->Images().empty());
        CHECK(swapchain->ImageViews().empty());
        CHECK(swapchain->Extent().width == 0);
        CHECK(swapchain->Extent().height == 0);
    }
}

TEST_CASE("Vulkan swapchain lifecycle and resize locally", "[vulkan][integration][swapchain]")
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
    const owl::platform::WindowDesc desc{
        .title = "OwlEngine - Vulkan Swapchain Test",
        .width = 320,
        .height = 180,
        .resizable = true,
        .surfaceApi = owl::platform::WindowSurfaceApi::Vulkan,
    };
    auto window = platform->CreateWindow(desc, error);
    REQUIRE(window.has_value());
    auto instance = owl::vulkan::VulkanInstance::Create(error);
    REQUIRE(instance.has_value());
    auto surface = owl::vulkan::VulkanSurface::Create(*instance, *window, error);
    REQUIRE(surface.has_value());
    auto selection =
        owl::vulkan::VulkanDeviceSelection::Select(instance->Get(), surface->Get(), error);
    REQUIRE(selection.has_value());
    auto device = owl::vulkan::VulkanDevice::Create(*selection, error);
    REQUIRE(device.has_value());
    const VkDevice originalDevice = device->Get();
    const VkSurfaceKHR originalSurface = surface->Get();
    const VkQueue originalQueue = device->GraphicsQueue();
    owl::vulkan::VulkanSwapchain swapchain;
    error = "stale";
    REQUIRE(swapchain.CreateOrRecreate(*device, *surface, {0, 0}, error) ==
            SwapchainUpdateResult::Deferred);
    CHECK(error.empty());
    CHECK_FALSE(swapchain.IsValid());

    const auto createAtWindowExtent = [&](owl::vulkan::VulkanSwapchain& target)
    {
        const auto pixels = window->GetFramebufferExtent();
        REQUIRE(pixels.has_value());
        REQUIRE(pixels->width > 0);
        REQUIRE(pixels->height > 0);
        const VkExtent2D extent{static_cast<std::uint32_t>(pixels->width),
                                static_cast<std::uint32_t>(pixels->height)};
        const auto result = target.CreateOrRecreate(*device, *surface, extent, error);
        INFO(error);
        REQUIRE(result == SwapchainUpdateResult::Ready);
        REQUIRE(target.IsValid());
        CHECK(target.Extent().width == extent.width);
        CHECK(target.Extent().height == extent.height);
        REQUIRE_FALSE(target.Images().empty());
        REQUIRE(target.Images().size() == target.ImageViews().size());
        for (std::size_t i = 0; i < target.Images().size(); ++i)
        {
            CHECK(target.Images()[i] != VK_NULL_HANDLE);
            CHECK(target.ImageViews()[i] != VK_NULL_HANDLE);
        }
        CHECK(device->Get() == originalDevice);
        CHECK(surface->Get() == originalSurface);
        CHECK(device->GraphicsQueue() == originalQueue);
        CHECK(error.empty());
    };
    createAtWindowExtent(swapchain);
    const VkSwapchainKHR first = swapchain.Get();
    const VkImageView firstView = swapchain.ImageViews().front();
    {
        owl::vulkan::VulkanSurface emptySurface;
        CHECK(swapchain.CreateOrRecreate(*device, emptySurface, {320, 180}, error) ==
              SwapchainUpdateResult::Failed);
        CHECK(error.find("surface") != std::string::npos);
        CHECK(swapchain.Get() == first);
        CHECK(swapchain.ImageViews().front() == firstView);
        auto otherDevice = owl::vulkan::VulkanDevice::Create(*selection, error);
        REQUIRE(otherDevice.has_value());
        CHECK(swapchain.CreateOrRecreate(*otherDevice, *surface, {320, 180}, error) ==
              SwapchainUpdateResult::Failed);
        CHECK(error.find("Cannot change") != std::string::npos);
        CHECK(swapchain.Get() == first);
        CHECK(swapchain.ImageViews().front() == firstView);
        auto otherWindow = platform->CreateWindow(desc, error);
        REQUIRE(otherWindow.has_value());
        auto otherSurface = owl::vulkan::VulkanSurface::Create(*instance, *otherWindow, error);
        REQUIRE(otherSurface.has_value());
        CHECK(swapchain.CreateOrRecreate(*device, *otherSurface, {320, 180}, error) ==
              SwapchainUpdateResult::Failed);
        CHECK(error.find("Cannot change") != std::string::npos);
        CHECK(swapchain.Get() == first);
        CHECK(swapchain.ImageViews().front() == firstView);
        owl::vulkan::VulkanSwapchain destination;
        REQUIRE(destination.CreateOrRecreate(*device, *otherSurface, {320, 180}, error) ==
                SwapchainUpdateResult::Ready);
        REQUIRE(destination.IsValid());
        destination = std::move(swapchain);
        CHECK_FALSE(swapchain.IsValid());
        CHECK(swapchain.Images().empty());
        CHECK(swapchain.ImageViews().empty());
        CHECK(destination.Get() == first);
        CHECK(destination.ImageViews().front() == firstView);
        swapchain = std::move(destination);
        CHECK_FALSE(destination.IsValid());
        CHECK(destination.Images().empty());
        CHECK(destination.ImageViews().empty());
    }
    owl::vulkan::VulkanSwapchain moved{std::move(swapchain)};
    CHECK_FALSE(swapchain.IsValid());
    CHECK(swapchain.Images().empty());
    CHECK(swapchain.ImageViews().empty());
    CHECK(moved.Get() == first);
    CHECK(moved.ImageViews().front() == firstView);
    swapchain = std::move(moved);
    CHECK_FALSE(moved.IsValid());
    CHECK(moved.Images().empty());
    CHECK(moved.ImageViews().empty());

    SDL_Window* nativeWindow = owl::platform::SDLWindowAccess::Get(*window);
    REQUIRE(nativeWindow != nullptr);
    for (const auto size : {VkExtent2D{640, 360}, VkExtent2D{480, 270}})
    {
        REQUIRE(SDL_SetWindowSize(nativeWindow, static_cast<int>(size.width),
                                  static_cast<int>(size.height)));
        REQUIRE(SDL_SyncWindow(nativeWindow));
        SDL_PumpEvents();
        int width = 0;
        int height = 0;
        REQUIRE(SDL_GetWindowSize(nativeWindow, &width, &height));
        CHECK(width == static_cast<int>(size.width));
        CHECK(height == static_cast<int>(size.height));
        createAtWindowExtent(swapchain);
    }
    REQUIRE(SDL_MinimizeWindow(nativeWindow));
    REQUIRE(SDL_SyncWindow(nativeWindow));
    SDL_PumpEvents();
    const auto beforeDeferred = swapchain.Get();
    const auto viewBeforeDeferred = swapchain.ImageViews().front();
    // SDL may retain a nonzero pixel size while minimized; the caller explicitly suspends
    // rendering.
    CHECK(swapchain.CreateOrRecreate(*device, *surface, {0, 0}, error) ==
          SwapchainUpdateResult::Deferred);
    CHECK(swapchain.Get() == beforeDeferred);
    CHECK(swapchain.ImageViews().front() == viewBeforeDeferred);
    CHECK(error.empty());
    REQUIRE(SDL_RestoreWindow(nativeWindow));
    REQUIRE(SDL_SyncWindow(nativeWindow));
    SDL_PumpEvents();
    createAtWindowExtent(swapchain);

    swapchain = owl::vulkan::VulkanSwapchain{};
    CHECK_FALSE(swapchain.IsValid());
    CHECK(swapchain.Images().empty());
    CHECK(swapchain.ImageViews().empty());
    CHECK(device->IsValid());
    CHECK(surface->IsValid());
    createAtWindowExtent(swapchain);
}
