#include "VulkanSampler.h"

#include "VulkanDevice.h"

#include <cmath>
#include <utility>

namespace owl::vulkan::detail
{
    bool IsSamplerDescValid(const VulkanSamplerDesc& desc) noexcept
    {
        const auto validFilter = [](const VkFilter filter)
        { return filter == VK_FILTER_NEAREST || filter == VK_FILTER_LINEAR; };
        const auto validAddress = [](const VkSamplerAddressMode mode)
        {
            return mode == VK_SAMPLER_ADDRESS_MODE_REPEAT ||
                   mode == VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT ||
                   mode == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        };
        return validFilter(desc.minFilter) && validFilter(desc.magFilter) &&
               (desc.mipmapMode == VK_SAMPLER_MIPMAP_MODE_NEAREST ||
                desc.mipmapMode == VK_SAMPLER_MIPMAP_MODE_LINEAR) &&
               validAddress(desc.addressModeU) && validAddress(desc.addressModeV) &&
               validAddress(desc.addressModeW) && std::isfinite(desc.minLod) &&
               std::isfinite(desc.maxLod) && desc.minLod >= 0.0F && desc.maxLod >= desc.minLod;
    }
} // namespace owl::vulkan::detail

namespace owl::vulkan
{
    VulkanSampler::~VulkanSampler()
    {
        Reset();
    }

    VulkanSampler::VulkanSampler(VulkanSampler&& other) noexcept
        : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
          sampler_(std::exchange(other.sampler_, VK_NULL_HANDLE))
    {
    }

    VulkanSampler& VulkanSampler::operator=(VulkanSampler&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            device_ = std::exchange(other.device_, VK_NULL_HANDLE);
            sampler_ = std::exchange(other.sampler_, VK_NULL_HANDLE);
        }
        return *this;
    }

    std::optional<VulkanSampler> VulkanSampler::Create(const VulkanDevice& device,
                                                      const VulkanSamplerDesc& desc,
                                                      std::string& error)
    {
        if (!device.IsValid() || !detail::IsSamplerDescValid(desc))
        {
            error = "Sampler creation requires a valid device and supported sampling description";
            return std::nullopt;
        }
        const VkSamplerCreateInfo info{
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = desc.magFilter,
            .minFilter = desc.minFilter,
            .mipmapMode = desc.mipmapMode,
            .addressModeU = desc.addressModeU,
            .addressModeV = desc.addressModeV,
            .addressModeW = desc.addressModeW,
            .mipLodBias = 0.0F,
            .anisotropyEnable = VK_FALSE,
            .maxAnisotropy = 1.0F,
            .compareEnable = VK_FALSE,
            .compareOp = VK_COMPARE_OP_NEVER,
            .minLod = desc.minLod,
            .maxLod = desc.maxLod,
            .borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
            .unnormalizedCoordinates = VK_FALSE,
        };
        VkSampler sampler = VK_NULL_HANDLE;
        const VkResult created = vkCreateSampler(device.Get(), &info, nullptr, &sampler);
        if (created != VK_SUCCESS)
        {
            error = "vkCreateSampler failed with VkResult " +
                    std::to_string(static_cast<int>(created));
            return std::nullopt;
        }
        VulkanSampler result;
        result.device_ = device.Get();
        result.sampler_ = sampler;
        error.clear();
        return result;
    }

    bool VulkanSampler::IsValid() const noexcept { return sampler_ != VK_NULL_HANDLE; }
    VkSampler VulkanSampler::Get() const noexcept { return sampler_; }

    void VulkanSampler::Reset() noexcept
    {
        if (sampler_ != VK_NULL_HANDLE)
            vkDestroySampler(device_, sampler_, nullptr);
        sampler_ = VK_NULL_HANDLE;
        device_ = VK_NULL_HANDLE;
    }
} // namespace owl::vulkan
