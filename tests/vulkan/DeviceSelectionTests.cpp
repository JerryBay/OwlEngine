#include <catch2/catch_test_macros.hpp>

#include "VulkanDeviceSelection.h"

#include <algorithm>
#include <array>
#include <span>
#include <string>
#include <string_view>

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

owl::vulkan::detail::DeviceCandidateCapabilities
MakeSuitableCandidate(const std::span<const VkExtensionProperties> extensions,
                      const std::span<const owl::vulkan::detail::QueueFamilyInfo> queueFamilies,
                      const std::span<const VkSurfaceFormatKHR> surfaceFormats,
                      const std::span<const VkPresentModeKHR> presentModes)
{
    return {
        .apiVersion = VK_API_VERSION_1_3,
        .extensions = extensions,
        .queueFamilies = queueFamilies,
        .surfaceFormats = surfaceFormats,
        .presentModes = presentModes,
        .dynamicRendering = true,
        .synchronization2 = true,
    };
}
} // namespace

TEST_CASE("Queue selection prefers a unified family over earlier separate candidates",
          "[vulkan][device-selection]")
{
    const std::array families{
        owl::vulkan::detail::QueueFamilyInfo{
            .queueCount = 1, .flags = VK_QUEUE_GRAPHICS_BIT, .presentSupported = false},
        owl::vulkan::detail::QueueFamilyInfo{.queueCount = 1, .flags = 0, .presentSupported = true},
        owl::vulkan::detail::QueueFamilyInfo{
            .queueCount = 1, .flags = VK_QUEUE_GRAPHICS_BIT, .presentSupported = true},
    };

    const auto selection = owl::vulkan::detail::SelectQueueFamilies(families);

    REQUIRE(selection.IsComplete());
    REQUIRE(selection.graphicsFamily.has_value());
    REQUIRE(selection.presentFamily.has_value());
    CHECK(*selection.graphicsFamily == 2);
    CHECK(*selection.presentFamily == 2);
    CHECK(selection.UsesUnifiedFamily());
}

TEST_CASE("Queue selection retains valid separate graphics and present families",
          "[vulkan][device-selection]")
{
    const std::array families{
        owl::vulkan::detail::QueueFamilyInfo{
            .queueCount = 0, .flags = VK_QUEUE_GRAPHICS_BIT, .presentSupported = true},
        owl::vulkan::detail::QueueFamilyInfo{
            .queueCount = 1, .flags = VK_QUEUE_GRAPHICS_BIT, .presentSupported = false},
        owl::vulkan::detail::QueueFamilyInfo{.queueCount = 1, .flags = 0, .presentSupported = true},
    };

    const auto selection = owl::vulkan::detail::SelectQueueFamilies(families);

    REQUIRE(selection.IsComplete());
    REQUIRE(selection.graphicsFamily.has_value());
    REQUIRE(selection.presentFamily.has_value());
    CHECK(*selection.graphicsFamily == 1);
    CHECK(*selection.presentFamily == 2);
    CHECK_FALSE(selection.UsesUnifiedFamily());
}

TEST_CASE("Surface-detail queries depend on reported support rather than queue availability",
          "[vulkan][device-selection]")
{
    SECTION("no reported surface support")
    {
        const std::array families{owl::vulkan::detail::QueueFamilyInfo{
            .queueCount = 1, .flags = VK_QUEUE_GRAPHICS_BIT, .presentSupported = false}};

        CHECK_FALSE(owl::vulkan::detail::HasReportedSurfaceSupport(families));
    }

    SECTION("reported support with no available queues")
    {
        const std::array families{owl::vulkan::detail::QueueFamilyInfo{
            .queueCount = 0, .flags = 0, .presentSupported = true}};

        CHECK(owl::vulkan::detail::HasReportedSurfaceSupport(families));
    }
}

TEST_CASE("Device suitability reports each missing requirement", "[vulkan][device-selection]")
{
    const std::array extensions{MakeExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME)};
    const std::array queueFamilies{owl::vulkan::detail::QueueFamilyInfo{
        .queueCount = 1, .flags = VK_QUEUE_GRAPHICS_BIT, .presentSupported = true}};
    const std::array surfaceFormats{VkSurfaceFormatKHR{
        .format = VK_FORMAT_B8G8R8A8_SRGB,
        .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
    }};
    const std::array presentModes{VK_PRESENT_MODE_FIFO_KHR};
    const std::array<owl::vulkan::detail::QueueFamilyInfo, 0> noQueueFamilies{};
    const std::array<VkExtensionProperties, 0> noExtensions{};
    const std::array<VkSurfaceFormatKHR, 0> noSurfaceFormats{};
    const std::array<VkPresentModeKHR, 0> noPresentModes{};
    const auto suitable =
        MakeSuitableCandidate(extensions, queueFamilies, surfaceFormats, presentModes);
    std::string rejectionReason;

    SECTION("Vulkan 1.3")
    {
        auto candidate = suitable;
        candidate.apiVersion = VK_API_VERSION_1_2;
        CHECK_FALSE(owl::vulkan::detail::EvaluateDeviceSuitability(candidate, rejectionReason));
        CHECK(rejectionReason.find("Vulkan 1.3") != std::string::npos);
    }

    SECTION("swapchain extension")
    {
        auto candidate = suitable;
        candidate.extensions = noExtensions;
        CHECK_FALSE(owl::vulkan::detail::EvaluateDeviceSuitability(candidate, rejectionReason));
        CHECK(rejectionReason.find(VK_KHR_SWAPCHAIN_EXTENSION_NAME) != std::string::npos);
    }

    SECTION("graphics queue")
    {
        auto candidate = suitable;
        candidate.queueFamilies = noQueueFamilies;
        CHECK_FALSE(owl::vulkan::detail::EvaluateDeviceSuitability(candidate, rejectionReason));
        CHECK(rejectionReason.find("graphics") != std::string::npos);
    }

    SECTION("present queue")
    {
        const std::array graphicsOnly{owl::vulkan::detail::QueueFamilyInfo{
            .queueCount = 1, .flags = VK_QUEUE_GRAPHICS_BIT, .presentSupported = false}};
        auto candidate = suitable;
        candidate.queueFamilies = graphicsOnly;
        CHECK_FALSE(owl::vulkan::detail::EvaluateDeviceSuitability(candidate, rejectionReason));
        CHECK(rejectionReason.find("present") != std::string::npos);
    }

    SECTION("surface formats")
    {
        auto candidate = suitable;
        candidate.surfaceFormats = noSurfaceFormats;
        CHECK_FALSE(owl::vulkan::detail::EvaluateDeviceSuitability(candidate, rejectionReason));
        CHECK(rejectionReason.find("surface format") != std::string::npos);
    }

    SECTION("present modes")
    {
        auto candidate = suitable;
        candidate.presentModes = noPresentModes;
        CHECK_FALSE(owl::vulkan::detail::EvaluateDeviceSuitability(candidate, rejectionReason));
        CHECK(rejectionReason.find("present mode") != std::string::npos);
    }

    SECTION("dynamic rendering")
    {
        auto candidate = suitable;
        candidate.dynamicRendering = false;
        CHECK_FALSE(owl::vulkan::detail::EvaluateDeviceSuitability(candidate, rejectionReason));
        CHECK(rejectionReason.find("dynamicRendering") != std::string::npos);
    }

    SECTION("synchronization2")
    {
        auto candidate = suitable;
        candidate.synchronization2 = false;
        CHECK_FALSE(owl::vulkan::detail::EvaluateDeviceSuitability(candidate, rejectionReason));
        CHECK(rejectionReason.find("synchronization2") != std::string::npos);
    }
}

TEST_CASE("Suitable devices retain the selected queues and swapchain preferences",
          "[vulkan][device-selection]")
{
    const std::array extensions{MakeExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME)};
    const std::array queueFamilies{owl::vulkan::detail::QueueFamilyInfo{
        .queueCount = 1, .flags = VK_QUEUE_GRAPHICS_BIT, .presentSupported = true}};
    const std::array surfaceFormats{VkSurfaceFormatKHR{
        .format = VK_FORMAT_B8G8R8A8_SRGB,
        .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
    }};
    const std::array presentModes{VK_PRESENT_MODE_FIFO_KHR};
    const auto candidate =
        MakeSuitableCandidate(extensions, queueFamilies, surfaceFormats, presentModes);
    std::string rejectionReason = "stale";

    const auto evaluation =
        owl::vulkan::detail::EvaluateDeviceSuitability(candidate, rejectionReason);

    REQUIRE(evaluation.has_value());
    CHECK(evaluation->queueFamilies.UsesUnifiedFamily());
    CHECK(evaluation->surfaceFormat.format == VK_FORMAT_B8G8R8A8_SRGB);
    CHECK(evaluation->presentMode == VK_PRESENT_MODE_FIFO_KHR);
    CHECK(rejectionReason.empty());
}

TEST_CASE("Surface format selection prefers conventional SRGB and handles undefined",
          "[vulkan][device-selection]")
{
    SECTION("conventional BGRA SRGB wins over other usable SRGB formats")
    {
        const std::array formats{
            VkSurfaceFormatKHR{
                .format = VK_FORMAT_R8G8B8A8_UNORM,
                .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
            },
            VkSurfaceFormatKHR{
                .format = VK_FORMAT_R8G8B8A8_SRGB,
                .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
            },
            VkSurfaceFormatKHR{
                .format = VK_FORMAT_B8G8R8A8_SRGB,
                .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
            },
        };

        const auto selected = owl::vulkan::detail::SelectSurfaceFormat(formats);

        REQUIRE(selected.has_value());
        CHECK(selected->format == VK_FORMAT_B8G8R8A8_SRGB);
        CHECK(selected->colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);
    }

    SECTION("another usable SRGB format wins when conventional BGRA is unavailable")
    {
        const std::array formats{
            VkSurfaceFormatKHR{
                .format = VK_FORMAT_R8G8B8A8_UNORM,
                .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
            },
            VkSurfaceFormatKHR{
                .format = VK_FORMAT_R8G8B8A8_SRGB,
                .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
            },
        };

        const auto selected = owl::vulkan::detail::SelectSurfaceFormat(formats);

        REQUIRE(selected.has_value());
        CHECK(selected->format == VK_FORMAT_R8G8B8A8_SRGB);
        CHECK(selected->colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);
    }

    SECTION("a single undefined format selects the conventional preference")
    {
        const std::array formats{VkSurfaceFormatKHR{
            .format = VK_FORMAT_UNDEFINED,
            .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        }};

        const auto selected = owl::vulkan::detail::SelectSurfaceFormat(formats);

        REQUIRE(selected.has_value());
        CHECK(selected->format == VK_FORMAT_B8G8R8A8_SRGB);
        CHECK(selected->colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);
    }

    SECTION("the first advertised format is the deterministic fallback")
    {
        const std::array formats{
            VkSurfaceFormatKHR{
                .format = VK_FORMAT_A2B10G10R10_UNORM_PACK32,
                .colorSpace = VK_COLOR_SPACE_HDR10_ST2084_EXT,
            },
            VkSurfaceFormatKHR{
                .format = VK_FORMAT_R8G8B8A8_UNORM,
                .colorSpace = VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT,
            },
        };

        const auto selected = owl::vulkan::detail::SelectSurfaceFormat(formats);

        REQUIRE(selected.has_value());
        CHECK(selected->format == formats.front().format);
        CHECK(selected->colorSpace == formats.front().colorSpace);
    }
}

TEST_CASE("Present mode selection prefers mailbox then FIFO then the first advertised mode",
          "[vulkan][device-selection]")
{
    SECTION("mailbox")
    {
        const std::array modes{VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_FIFO_KHR,
                               VK_PRESENT_MODE_MAILBOX_KHR};
        CHECK(owl::vulkan::detail::SelectPresentMode(modes) == VK_PRESENT_MODE_MAILBOX_KHR);
    }

    SECTION("FIFO")
    {
        const std::array modes{VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_FIFO_KHR};
        CHECK(owl::vulkan::detail::SelectPresentMode(modes) == VK_PRESENT_MODE_FIFO_KHR);
    }

    SECTION("defensive fallback")
    {
        const std::array modes{VK_PRESENT_MODE_IMMEDIATE_KHR};
        CHECK(owl::vulkan::detail::SelectPresentMode(modes) == modes.front());
    }
}

TEST_CASE("Surface extent uses fixed values or clamps requested dimensions independently",
          "[vulkan][device-selection]")
{
    VkSurfaceCapabilitiesKHR capabilities{
        .minImageExtent = {320, 240},
        .maxImageExtent = {1920, 1080},
    };

    SECTION("fixed extent")
    {
        capabilities.currentExtent = {1280, 720};
        const VkExtent2D selected =
            owl::vulkan::detail::SelectSurfaceExtent(capabilities, {4000, 1});
        CHECK(selected.width == 1280);
        CHECK(selected.height == 720);
    }

    SECTION("independent clamping")
    {
        capabilities.currentExtent = {UINT32_MAX, UINT32_MAX};
        const VkExtent2D selected =
            owl::vulkan::detail::SelectSurfaceExtent(capabilities, {4000, 1});
        CHECK(selected.width == 1920);
        CHECK(selected.height == 240);
    }
}
