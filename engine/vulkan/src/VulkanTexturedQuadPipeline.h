#pragma once

#include "VulkanBuffer.h"
#include "VulkanBufferUpload.h"
#include "VulkanImageUpload.h"
#include "VulkanResourceRetirement.h"
#include "VulkanTextureBinding.h"
#include <owl/vulkan/VulkanTriangle.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
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

    // Borrows Device and Allocator. Caller finishes submitted draws and drains all uploads
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
        [[nodiscard]] TextureReplacementResult
        RequestTextureReplacement(const VulkanDevice& device, const VulkanAllocator& allocator,
                                  std::span<const std::byte> rgba8, std::string& error);
        // Call outside recording/submission, on the renderer thread. Completion belongs to
        // the same graphics submission domain as RecordTextureSubmit. Does not wait.
        [[nodiscard]] VkResult ProcessTextureReplacement(const VulkanDevice& device,
                                                         std::uint64_t completedSerial,
                                                         std::string& error);
        // Call immediately after successful submit of commands using the current binding.
        void RecordTextureSubmit(std::uint64_t serial) noexcept;
        std::size_t CollectRetiredTextures(std::uint64_t completedSerial) noexcept;
        [[nodiscard]] std::uint64_t TextureReplacements() const noexcept;
        [[nodiscard]] std::uint64_t TextureLastUseSerial() const noexcept;
        [[nodiscard]] std::size_t PendingTextureRetirements() const noexcept;
        [[nodiscard]] bool TextureReplacementPending() const noexcept;
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
        std::unique_ptr<VulkanTextureBinding> textureBinding_;
        static constexpr std::size_t MaxRetiredTextures = 4;
        detail::ResourceRetirementQueue<VulkanTextureBinding, MaxRetiredTextures> retiredTextures_;
        std::uint64_t textureLastUseSerial_ = 0;
        std::uint64_t textureReplacements_ = 0;
        TextureMipMode mipMode_ = TextureMipMode::SingleLevel;
        std::optional<VulkanBufferUpload> geometryUpload_;
        std::optional<VulkanImageUpload> imageUpload_;
        VkDescriptorSetLayout descriptorLayout_ = VK_NULL_HANDLE;
        VkPipelineLayout layout_ = VK_NULL_HANDLE;
        VkPipeline pipeline_ = VK_NULL_HANDLE;
        VkFormat format_ = VK_FORMAT_UNDEFINED;
        std::uint32_t mipLevels_ = 1;
        bool gpuMipGeneration_ = false;
        std::vector<std::uint32_t> vertexShader_;
        std::vector<std::uint32_t> fragmentShader_;
    };
} // namespace owl::vulkan
