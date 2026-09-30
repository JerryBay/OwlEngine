#include "VulkanMipChain.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace owl::vulkan::detail
{
    namespace
    {
        double Decode(const std::byte value, const bool srgb) noexcept
        {
            const double normalized =
                static_cast<double>(std::to_integer<unsigned int>(value)) / 255.0;
            if (!srgb)
                return normalized;
            return normalized <= 0.04045 ? normalized / 12.92
                                         : std::pow((normalized + 0.055) / 1.055, 2.4);
        }

        std::byte Encode(const double value, const bool srgb) noexcept
        {
            const double encoded = !srgb ? value
                                         : value <= 0.0031308
                                               ? value * 12.92
                                               : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
            return static_cast<std::byte>(static_cast<unsigned int>(
                std::lround(std::clamp(encoded, 0.0, 1.0) * 255.0)));
        }

        VkExtent2D NextExtent(const VkExtent2D extent) noexcept
        {
            return {std::max(1U, extent.width / 2), std::max(1U, extent.height / 2)};
        }
    } // namespace

    std::optional<CpuMipChain> BuildCpuMipChain(const VulkanImageDesc& desc,
                                               const std::span<const std::byte> base,
                                               std::string& error)
    {
        if (!IsImageDescValid(desc) || !(desc.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        {
            error = "Mip generation requires a sampled RGBA8 transfer destination";
            return std::nullopt;
        }
        CpuMipChain chain;
        chain.levels.reserve(desc.mipLevels);
        VkExtent2D extent = desc.extent;
        std::size_t total = 0;
        for (std::uint32_t level = 0; level < desc.mipLevels; ++level)
        {
            constexpr auto max = std::numeric_limits<std::size_t>::max();
            if (extent.width > max / 4 ||
                extent.height > max / (static_cast<std::size_t>(extent.width) * 4) ||
                static_cast<std::size_t>(extent.width) * extent.height * 4 > max - total)
            {
                error = "Mip chain byte size overflows";
                return std::nullopt;
            }
            chain.levels.push_back({extent, total});
            total += static_cast<std::size_t>(extent.width) * extent.height * 4;
            extent = NextExtent(extent);
        }
        if (base.size() != (chain.levels.size() == 1 ? total : chain.levels[1].offset))
        {
            error = "Mip base level must contain exactly width * height * 4 bytes";
            return std::nullopt;
        }
        chain.bytes.resize(total);
        std::copy(base.begin(), base.end(), chain.bytes.begin());
        const bool srgb = desc.format == VK_FORMAT_R8G8B8A8_SRGB;
        for (std::size_t level = 1; level < chain.levels.size(); ++level)
        {
            const auto& source = chain.levels[level - 1];
            const auto& target = chain.levels[level];
            for (std::uint32_t y = 0; y < target.extent.height; ++y)
            {
                const double y0 =
                    static_cast<double>(y) * source.extent.height / target.extent.height;
                const double y1 =
                    static_cast<double>(y + 1) * source.extent.height / target.extent.height;
                for (std::uint32_t x = 0; x < target.extent.width; ++x)
                {
                    const double x0 =
                        static_cast<double>(x) * source.extent.width / target.extent.width;
                    const double x1 =
                        static_cast<double>(x + 1) * source.extent.width / target.extent.width;
                    for (std::size_t channel = 0; channel < 4; ++channel)
                    {
                        double sum = 0.0;
                        for (auto sy = static_cast<std::uint32_t>(y0);
                             sy < static_cast<std::uint32_t>(std::ceil(y1)); ++sy)
                        {
                            const double row = std::min(y1, static_cast<double>(sy + 1)) -
                                               std::max(y0, static_cast<double>(sy));
                            for (auto sx = static_cast<std::uint32_t>(x0);
                                 sx < static_cast<std::uint32_t>(std::ceil(x1)); ++sx)
                            {
                                const double column = std::min(x1, static_cast<double>(sx + 1)) -
                                                      std::max(x0, static_cast<double>(sx));
                                const auto offset =
                                    source.offset +
                                    (static_cast<std::size_t>(sy) * source.extent.width + sx) * 4 +
                                    channel;
                                sum += Decode(chain.bytes[offset], srgb && channel < 3) * row *
                                       column;
                            }
                        }
                        const auto offset =
                            target.offset +
                            (static_cast<std::size_t>(y) * target.extent.width + x) * 4 + channel;
                        chain.bytes[offset] =
                            Encode(sum / ((x1 - x0) * (y1 - y0)), srgb && channel < 3);
                    }
                }
            }
        }
        error.clear();
        return chain;
    }
} // namespace owl::vulkan::detail
