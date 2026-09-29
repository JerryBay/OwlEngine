#include "VulkanBuffer.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
TEST_CASE("Buffer descriptions require explicit and consistent host access", "[vulkan][buffer]")
{
    using owl::vulkan::BufferHostAccess;
    using owl::vulkan::detail::IsBufferDescValid;
    owl::vulkan::VulkanBufferDesc desc{
        .size = 66,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .requiredMemory = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    };
    CHECK(IsBufferDescValid(desc));
    desc.requiredMemory |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    CHECK_FALSE(IsBufferDescValid(desc));
    desc.hostAccess = BufferHostAccess::SequentialWrite;
    CHECK(IsBufferDescValid(desc));
    desc.hostAccess = BufferHostAccess::Random;
    CHECK(IsBufferDescValid(desc));
    desc.requiredMemory = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    CHECK_FALSE(IsBufferDescValid(desc));
    desc.hostAccess = static_cast<BufferHostAccess>(99);
    CHECK_FALSE(IsBufferDescValid(desc));
}

TEST_CASE("Buffer descriptions reject unsupported allocation contracts", "[vulkan][buffer]")
{
    using owl::vulkan::detail::IsBufferDescValid;
    owl::vulkan::VulkanBufferDesc desc{.size = 66, .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT};
    CHECK(IsBufferDescValid(desc));
    SECTION("zero size") { desc.size = 0; }
    SECTION("zero usage") { desc.usage = 0; }
    SECTION("device address") { desc.usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT; }
    SECTION("protected memory") { desc.requiredMemory = VK_MEMORY_PROPERTY_PROTECTED_BIT; }
    SECTION("unsupported preferred memory")
    {
        desc.preferredMemory = VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
    }
    CHECK_FALSE(IsBufferDescValid(desc));
}

TEST_CASE("Buffer ranges reject overflow and accept an empty end range", "[vulkan][buffer]")
{
    using owl::vulkan::detail::IsBufferRangeValid;
    CHECK(IsBufferRangeValid(10, 0, 10));
    CHECK(IsBufferRangeValid(10, 10, 0));
    CHECK(IsBufferRangeValid(10, 9, 1));
    CHECK_FALSE(IsBufferRangeValid(10, 11, 0));
    CHECK_FALSE(IsBufferRangeValid(10, 9, 2));
    CHECK_FALSE(IsBufferRangeValid(10, UINT64_MAX, 1));
}
