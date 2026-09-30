#pragma once

#include "VulkanImage.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace owl::vulkan::detail
{
    struct CpuMipLevel
    {
        VkExtent2D extent{};
        std::size_t offset = 0;
    };

    struct CpuMipChain
    {
        std::vector<std::byte> bytes;
        std::vector<CpuMipLevel> levels;
    };

    // Area-average RGB in linear light for sRGB; alpha is always averaged directly.
    [[nodiscard]] std::optional<CpuMipChain>
    BuildCpuMipChain(const VulkanImageDesc& desc, std::span<const std::byte> base,
                     std::string& error);
} // namespace owl::vulkan::detail
