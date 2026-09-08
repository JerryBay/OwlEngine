#include <catch2/catch_test_macros.hpp>

#include "VulkanDevice.h"

#include <string>
#include <type_traits>
#include <utility>

static_assert(!std::is_copy_constructible_v<owl::vulkan::VulkanDevice>);
static_assert(!std::is_copy_assignable_v<owl::vulkan::VulkanDevice>);
static_assert(std::is_nothrow_move_constructible_v<owl::vulkan::VulkanDevice>);
static_assert(std::is_nothrow_move_assignable_v<owl::vulkan::VulkanDevice>);

TEST_CASE("Device queue requests deduplicate a unified family including family zero",
          "[vulkan][device]")
{
    const owl::vulkan::detail::QueueFamilySelection selection{
        .graphicsFamily = 0,
        .presentFamily = 0,
    };
    std::string error = "stale";

    const auto families = owl::vulkan::detail::BuildDeviceQueueFamilyIndices(selection, error);

    REQUIRE(families.has_value());
    REQUIRE(families->size() == 1);
    CHECK(families->front() == 0);
    CHECK(error.empty());
}

TEST_CASE("Device queue requests preserve separate graphics and present families",
          "[vulkan][device]")
{
    const owl::vulkan::detail::QueueFamilySelection selection{
        .graphicsFamily = 7,
        .presentFamily = 2,
    };
    std::string error = "stale";

    const auto families = owl::vulkan::detail::BuildDeviceQueueFamilyIndices(selection, error);

    REQUIRE(families.has_value());
    REQUIRE(families->size() == 2);
    CHECK((*families)[0] == 7);
    CHECK((*families)[1] == 2);
    CHECK(error.empty());
}

TEST_CASE("Device queue requests reject incomplete selections", "[vulkan][device]")
{
    owl::vulkan::detail::QueueFamilySelection selection;
    std::string missingRole;
    SECTION("missing graphics")
    {
        selection.presentFamily = 0;
        missingRole = "graphics";
    }
    SECTION("missing present")
    {
        selection.graphicsFamily = 0;
        missingRole = "present";
    }
    std::string error;

    CHECK_FALSE(owl::vulkan::detail::BuildDeviceQueueFamilyIndices(selection, error));
    CHECK(error.find(missingRole) != std::string::npos);
}

TEST_CASE("Empty Vulkan devices have no queues after moves", "[vulkan][device]")
{
    owl::vulkan::VulkanDevice source;
    owl::vulkan::VulkanDevice moved{std::move(source)};
    owl::vulkan::VulkanDevice assigned;
    assigned = std::move(moved);

    for (const auto* device : {&source, &moved, &assigned})
    {
        CHECK_FALSE(device->IsValid());
        CHECK(device->Get() == VK_NULL_HANDLE);
        CHECK(device->GraphicsQueue() == VK_NULL_HANDLE);
        CHECK(device->PresentQueue() == VK_NULL_HANDLE);
        CHECK_FALSE(device->QueueFamilies().graphicsFamily.has_value());
        CHECK_FALSE(device->QueueFamilies().presentFamily.has_value());
    }
}
