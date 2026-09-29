#pragma once

#include <vulkan/vulkan.h>

#include <optional>
#include <string>

namespace owl::vulkan
{
    class VulkanDevice;

    // Normalized color sampling without anisotropy, comparison, border color or LOD bias.
    struct VulkanSamplerDesc
    {
        VkFilter minFilter = VK_FILTER_LINEAR;
        VkFilter magFilter = VK_FILTER_LINEAR;
        VkSamplerMipmapMode mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        VkSamplerAddressMode addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        VkSamplerAddressMode addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        VkSamplerAddressMode addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        float minLod = 0.0F;
        float maxLod = 0.0F;
    };

    namespace detail
    {
        [[nodiscard]] bool IsSamplerDescValid(const VulkanSamplerDesc& desc) noexcept;
    } // namespace detail

    // Owns sampling rules, no image or view. The device must outlive it; finish GPU use before
    // destruction or move replacement. Image-format filtering support is checked at use time.
    class VulkanSampler
    {
    public:
        VulkanSampler() noexcept = default;
        ~VulkanSampler();
        VulkanSampler(const VulkanSampler&) = delete;
        VulkanSampler& operator=(const VulkanSampler&) = delete;
        VulkanSampler(VulkanSampler&& other) noexcept;
        VulkanSampler& operator=(VulkanSampler&& other) noexcept;

        [[nodiscard]] static std::optional<VulkanSampler>
        Create(const VulkanDevice& device, const VulkanSamplerDesc& desc, std::string& error);
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] VkSampler Get() const noexcept;

    private:
        void Reset() noexcept;

        VkDevice device_ = VK_NULL_HANDLE;
        VkSampler sampler_ = VK_NULL_HANDLE;
    };
} // namespace owl::vulkan
