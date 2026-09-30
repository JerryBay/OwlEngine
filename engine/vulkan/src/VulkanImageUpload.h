#pragma once

#include "VulkanBuffer.h"
#include "VulkanImage.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>

namespace owl::vulkan
{
    class VulkanAllocator;
    class VulkanDevice;

    enum class MipUploadPath
    {
        SingleLevel,
        GpuBlit,
        CpuUpload,
    };

    enum class MipGenerationMode
    {
        Auto,
        ForceCpu,
    };

    namespace detail
    {
        enum class ImageUploadState
        {
            NotSubmitted,
            Pending,
            Completed,
            DeviceLost,
        };

        // Tight-packed CPU transfer size, not the optimal image allocation size.
        [[nodiscard]] std::optional<std::size_t>
        ImageUploadByteSize(const VulkanImageDesc& desc) noexcept;
        [[nodiscard]] MipUploadPath ChooseMipUploadPath(
            const VulkanImageDesc& desc, VkFormatFeatureFlags optimalFeatures,
            MipGenerationMode mode) noexcept;
        [[nodiscard]] ImageUploadState
        StateAfterImageUploadSubmit(ImageUploadState state, VkResult result) noexcept;
        [[nodiscard]] ImageUploadState
        StateAfterImageUploadWait(ImageUploadState state, VkResult result) noexcept;
    } // namespace detail

    // Owns one startup upload into a new RGBA8 image. Borrows native device/queue
    // and allocator lifetimes; wrappers may move, native parents must remain alive. Serialize
    // host access to this object and its graphics queue. CPU input may be released after Create.
    // Destruction/move replacement never waits: drain pending work explicitly first.
    class VulkanImageUpload
    {
    public:
        VulkanImageUpload() noexcept = default;
        ~VulkanImageUpload();
        VulkanImageUpload(const VulkanImageUpload&) = delete;
        VulkanImageUpload& operator=(const VulkanImageUpload&) = delete;
        VulkanImageUpload(VulkanImageUpload&& other) noexcept;
        VulkanImageUpload& operator=(VulkanImageUpload&& other) noexcept;

        [[nodiscard]] static std::optional<VulkanImageUpload>
        Create(const VulkanDevice& device, const VulkanAllocator& allocator,
               const VulkanImageDesc& desc, std::span<const std::byte> bytes, std::string& error,
               MipGenerationMode mode = MipGenerationMode::Auto);
        [[nodiscard]] MipUploadPath Path() const noexcept;
        [[nodiscard]] VkResult Submit(std::string& error);
        [[nodiscard]] VkResult Wait(
            std::string& error,
            std::uint64_t timeoutNanoseconds = std::numeric_limits<std::uint64_t>::max());
        [[nodiscard]] bool IsPending() const noexcept;
        // Non-throwing fence wait with queue-idle fallback. Unresolved errors retain ownership.
        [[nodiscard]] VkResult DrainForDestruction() noexcept;
        // Only call after an external queue/device idle proves submitted work completed.
        void MarkCompleteAfterQueueIdleForDestruction() noexcept;
        void MarkDeviceLostForDestruction() noexcept;
        [[nodiscard]] VkMemoryPropertyFlags StagingMemoryProperties() const noexcept;
        // One-time handoff after completion: SHADER_READ_ONLY_OPTIMAL, fragment sampled read
        // dependency on the same graphics queue. Subsequent layout/lifetime tracking is the caller's.
        [[nodiscard]] std::optional<VulkanImage> TakeDestination(std::string& error);

    private:
        void Reset() noexcept;
        void ReleaseTransient() noexcept;
        void ObserveWaitResult(VkResult result) noexcept;

        VkDevice device_ = VK_NULL_HANDLE;
        VkQueue queue_ = VK_NULL_HANDLE;
        VkCommandPool commandPool_ = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
        VkFence fence_ = VK_NULL_HANDLE;
        std::optional<VulkanBuffer> staging_;
        std::optional<VulkanImage> destination_;
        VkMemoryPropertyFlags stagingMemoryProperties_ = 0;
        MipUploadPath path_ = MipUploadPath::SingleLevel;
        detail::ImageUploadState state_ = detail::ImageUploadState::NotSubmitted;
    };
} // namespace owl::vulkan
