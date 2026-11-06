#pragma once

#include "VulkanImage.h"
#include "VulkanImageView.h"
#include "VulkanSampler.h"

#include <memory>
#include <string>

namespace owl::vulkan
{
    class VulkanDevice;

    // Immutable image binding. Device/allocator outlive it; finish GPU uses before destruction.
    // Caller provides a same-device binding-0 combined-image-sampler layout and establishes
    // shader-read layout/visibility before sampling. Creation does not upload, transition or wait.
    class VulkanTextureBinding
    {
    public:
        ~VulkanTextureBinding() noexcept;
        VulkanTextureBinding(const VulkanTextureBinding&) = delete;
        VulkanTextureBinding& operator=(const VulkanTextureBinding&) = delete;
        VulkanTextureBinding(VulkanTextureBinding&&) = delete;
        VulkanTextureBinding& operator=(VulkanTextureBinding&&) = delete;

        // Image ownership transfers only after every child is ready. Failure preserves image.
        [[nodiscard]] static std::unique_ptr<VulkanTextureBinding>
        Create(const VulkanDevice& device, VkDescriptorSetLayout layout, VulkanImage& image,
               std::string& error);
        [[nodiscard]] VkDescriptorSet DescriptorSet() const noexcept;
        [[nodiscard]] VulkanResourceStats ResourceStats() const noexcept;

    private:
        VulkanTextureBinding() noexcept = default;

        VkDevice device_ = VK_NULL_HANDLE;
        VulkanImage image_;
        VulkanImageView view_;
        VulkanSampler sampler_;
        VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;
    };
} // namespace owl::vulkan
