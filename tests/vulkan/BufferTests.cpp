#include "VulkanBuffer.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <initializer_list>

namespace
{
    VkPhysicalDeviceMemoryProperties Properties(std::initializer_list<VkMemoryPropertyFlags> flags)
    {
        VkPhysicalDeviceMemoryProperties properties{};
        properties.memoryTypeCount = static_cast<std::uint32_t>(flags.size());
        std::uint32_t index = 0;
        for (const auto value : flags)
            properties.memoryTypes[index++].propertyFlags = value;
        return properties;
    }
}

TEST_CASE("Buffer memory policy ranks preferred flags and supports combined local host memory",
          "[vulkan][buffer]")
{
    const auto properties = Properties({VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT});
    using owl::vulkan::detail::SelectBufferMemoryType;
    CHECK(SelectBufferMemoryType(properties, 0b111, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 1);
    CHECK(SelectBufferMemoryType(properties, 0b101, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == 2);
    CHECK_FALSE(SelectBufferMemoryType(properties, 0b001, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                       0));
    CHECK_FALSE(SelectBufferMemoryType(properties, 0, 0, 0));
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
