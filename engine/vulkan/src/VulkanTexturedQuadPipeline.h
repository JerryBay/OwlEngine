#pragma once

#include "VulkanBuffer.h"
#include "VulkanBufferUpload.h"
#include "VulkanImage.h"
#include "VulkanImageUpload.h"
#include "VulkanImageView.h"
#include "VulkanSampler.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace owl::vulkan::detail
{
    [[nodiscard]] std::array<std::byte, 64 * 64 * 4> MakeCheckerboardRgba8() noexcept;
} // namespace owl::vulkan::detail

namespace owl::vulkan
{
    class VulkanDevice;
    struct TriangleShaderPaths;

    // Borrows Device and Allocator. Caller finishes submitted draws and drains startup uploads
    // before destruction or format changes. Image, descriptor and geometry survive resize.
    class VulkanTexturedQuadPipeline
    {
    public:
        VulkanTexturedQuadPipeline() = default;
        ~VulkanTexturedQuadPipeline();
        VulkanTexturedQuadPipeline(const VulkanTexturedQuadPipeline&) = delete;
        VulkanTexturedQuadPipeline& operator=(const VulkanTexturedQuadPipeline&) = delete;

        [[nodiscard]] bool Initialize(const VulkanDevice& device, const VulkanAllocator& allocator,
                                      const TriangleShaderPaths& paths, std::string& error);
        [[nodiscard]] bool SetColorFormat(VkFormat format, std::string& error);
        [[nodiscard]] VkResult WaitForUpload(std::string& error);
        [[nodiscard]] VkResult DrainUploadForDestruction() noexcept;
        void MarkUploadCompleteAfterQueueIdleForDestruction() noexcept;
        void MarkUploadDeviceLostForDestruction() noexcept;
        void RecordDraw(VkCommandBuffer command, VkExtent2D extent) const noexcept;

    private:
        VkDevice device_ = VK_NULL_HANDLE;
        VulkanBuffer geometry_;
        std::optional<VulkanImage> image_;
        std::optional<VulkanImageView> view_;
        std::optional<VulkanSampler> sampler_;
        std::optional<VulkanBufferUpload> geometryUpload_;
        std::optional<VulkanImageUpload> imageUpload_;
        VkDescriptorSetLayout descriptorLayout_ = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;
        VkPipelineLayout layout_ = VK_NULL_HANDLE;
        VkPipeline pipeline_ = VK_NULL_HANDLE;
        VkFormat format_ = VK_FORMAT_UNDEFINED;
        std::vector<std::uint32_t> vertexShader_;
        std::vector<std::uint32_t> fragmentShader_;
    };
} // namespace owl::vulkan
