#include "VulkanTextureBinding.h"

#include "VulkanDevice.h"
#include <owl/foundation/Log.h>

#include <utility>

namespace owl::vulkan
{
    namespace
    {
        bool Check(const VkResult result, const char* operation, std::string& error)
        {
            if (result == VK_SUCCESS)
                return true;
            error = std::string{operation} + " failed with VkResult " +
                    std::to_string(static_cast<int>(result));
            owl::foundation::LogMessage(owl::foundation::LogLevel::Error, "Vulkan", error);
            return false;
        }
    } // namespace

    VulkanTextureBinding::~VulkanTextureBinding() noexcept
    {
        // Pool destruction releases the set before member destruction releases its referents.
        // Reverse member order then releases sampler/view before image/allocation.
        if (device_ != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
    }

    std::unique_ptr<VulkanTextureBinding>
    VulkanTextureBinding::Create(const VulkanDevice& device, const VkDescriptorSetLayout layout,
                                 VulkanImage& image, std::string& error)
    {
        if (!device.IsValid() || layout == VK_NULL_HANDLE || !image.IsValid() ||
            image.Device() != device.Get())
        {
            error =
                "Texture binding requires a valid device, descriptor layout and same-device image";
            return nullptr;
        }
        auto binding = std::unique_ptr<VulkanTextureBinding>{new VulkanTextureBinding};
        binding->device_ = device.Get();
        const auto mipLevels = image.Desc().mipLevels;
        auto view = VulkanImageView::Create(image, {.levelCount = mipLevels}, error);
        if (!view)
            return nullptr;
        binding->view_ = std::move(*view);
        const VulkanSamplerDesc samplerDesc{
            .minFilter = VK_FILTER_NEAREST,
            .magFilter = VK_FILTER_NEAREST,
            .mipmapMode =
                mipLevels > 1 ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .maxLod = static_cast<float>(mipLevels - 1),
        };
        auto sampler = VulkanSampler::Create(device, samplerDesc, error);
        if (!sampler)
            return nullptr;
        binding->sampler_ = std::move(*sampler);

        const VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
        const VkDescriptorPoolCreateInfo poolInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = 1,
            .poolSizeCount = 1,
            .pPoolSizes = &poolSize,
        };
        VkDescriptorPool pool = VK_NULL_HANDLE;
        if (!Check(vkCreateDescriptorPool(device.Get(), &poolInfo, nullptr, &pool),
                   "vkCreateDescriptorPool(texture binding)", error))
            return nullptr;
        binding->descriptorPool_ = pool;
        const VkDescriptorSetAllocateInfo allocate{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = pool,
            .descriptorSetCount = 1,
            .pSetLayouts = &layout,
        };
        VkDescriptorSet set = VK_NULL_HANDLE;
        if (!Check(vkAllocateDescriptorSets(device.Get(), &allocate, &set),
                   "vkAllocateDescriptorSets(texture binding)", error))
            return nullptr;
        binding->descriptorSet_ = set;
        const VkDescriptorImageInfo imageInfo{
            .sampler = binding->sampler_.Get(),
            .imageView = binding->view_.Get(),
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        const VkWriteDescriptorSet write{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = set,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &imageInfo,
        };
        vkUpdateDescriptorSets(device.Get(), 1, &write, 0, nullptr);
        // This nonthrowing handoff is last: failures above release only new child objects.
        binding->image_ = std::move(image);
        error.clear();
        return binding;
    }

    VkDescriptorSet VulkanTextureBinding::DescriptorSet() const noexcept
    {
        return descriptorSet_;
    }
} // namespace owl::vulkan
