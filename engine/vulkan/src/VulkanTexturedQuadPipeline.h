#pragma once

#include "VulkanBuffer.h"
#include "VulkanBufferUpload.h"
#include "VulkanImage.h"
#include "VulkanImageUpload.h"
#include "VulkanImageView.h"
#include "VulkanSampler.h"
#include <owl/vulkan/VulkanTriangle.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace owl::vulkan::detail
{
    [[nodiscard]] std::array<std::byte, 64 * 64 * 4> MakeCheckerboardRgba8() noexcept;
    [[nodiscard]] bool IsTextureDrawParametersValid(const TextureDrawParameters& parameters,
                                                    std::uint32_t mipLevels) noexcept;
} // namespace owl::vulkan::detail

namespace owl::vulkan
{
    class VulkanDevice;
    class VulkanUploadArena;
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
                                      const TriangleShaderPaths& paths, std::string& error,
                                      TextureMipMode mipMode = TextureMipMode::SingleLevel,
                                      std::size_t streamingSlots = 0);
        static constexpr VkDeviceSize StreamedGeometryBytes = 76;
        // Caller proves this slot complete, discards old commands, then begins its arena.
        // Records copy dependencies outside rendering. Caller seals before submit and keeps
        // both arena and destination alive until completion; no waits occur here.
        [[nodiscard]] bool RecordGeometryUpload(VkCommandBuffer command, std::size_t slotIndex,
                                                VulkanUploadArena& arena,
                                                const TextureDrawParameters& parameters,
                                                std::string& error) const;
        [[nodiscard]] std::uint32_t MipLevels() const noexcept;
        [[nodiscard]] bool UsesGpuMipGeneration() const noexcept;
        [[nodiscard]] bool SetColorFormat(VkFormat format, std::string& error);
        [[nodiscard]] VkResult WaitForUpload(std::string& error);
        [[nodiscard]] VkResult DrainUploadForDestruction() noexcept;
        void MarkUploadCompleteAfterQueueIdleForDestruction() noexcept;
        void MarkUploadDeviceLostForDestruction() noexcept;
        void RecordDraw(VkCommandBuffer command, VkExtent2D extent,
                        const TextureDrawParameters& parameters) const noexcept;
        // Requires a successful RecordGeometryUpload for this slot before rendering begins.
        void RecordStreamedDraw(VkCommandBuffer command, VkExtent2D extent,
                                const TextureDrawParameters& parameters,
                                std::size_t slotIndex) const noexcept;

    private:
        void RecordDrawWithBuffer(VkCommandBuffer command, VkExtent2D extent,
                                  const TextureDrawParameters& parameters,
                                  VkBuffer geometry) const noexcept;
        VkDevice device_ = VK_NULL_HANDLE;
        VulkanBuffer geometry_;
        std::vector<VulkanBuffer> streamedGeometry_;
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
        std::uint32_t mipLevels_ = 1;
        bool gpuMipGeneration_ = false;
        std::vector<std::uint32_t> vertexShader_;
        std::vector<std::uint32_t> fragmentShader_;
    };
} // namespace owl::vulkan
