#pragma once

#include "VulkanBuffer.h"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <optional>
#include <span>
#include <string>

namespace owl::vulkan
{
    namespace detail
    {
        enum class BufferUploadState
        {
            NotSubmitted,
            Pending,
            Completed,
            DeviceLost,
        };

        enum class BufferUploadValidation
        {
            Valid,
            InvalidDevice,
            EmptyPayload,
            InvalidUsage,
            MissingConsumerStage,
            MissingConsumerAccess,
            MissingGraphicsQueue,
        };

        [[nodiscard]] BufferUploadValidation ValidateBufferUploadArguments(
            bool deviceValid, bool graphicsQueueValid, std::size_t payloadSize,
            VkBufferUsageFlags finalUsage, VkPipelineStageFlags2 consumerStages,
            VkAccessFlags2 consumerAccess) noexcept;

        [[nodiscard]] BufferUploadState
        StateAfterSubmit(BufferUploadState state, VkResult result) noexcept;

        [[nodiscard]] BufferUploadState
        StateAfterWait(BufferUploadState state, VkResult result) noexcept;

        [[nodiscard]] bool IsBufferUploadPending(BufferUploadState state) noexcept;
    } // namespace detail

    // Owns one startup copy operation. The device and graphics queue are borrowed and must
    // outlive this owner, as must the allocator. Destruction never waits; callers must drain
    // a submitted operation.
    class VulkanBufferUpload
    {
    public:
        VulkanBufferUpload() noexcept = default;
        ~VulkanBufferUpload();
        VulkanBufferUpload(const VulkanBufferUpload&) = delete;
        VulkanBufferUpload& operator=(const VulkanBufferUpload&) = delete;
        VulkanBufferUpload(VulkanBufferUpload&& other) noexcept;
        VulkanBufferUpload& operator=(VulkanBufferUpload&& other) noexcept;

        [[nodiscard]] static std::optional<VulkanBufferUpload>
        Create(const VulkanDevice& device, const VulkanAllocator& allocator,
               std::span<const std::byte> bytes,
               VkBufferUsageFlags finalUsage, VkPipelineStageFlags2 consumerStages,
               VkAccessFlags2 consumerAccess, std::string& error);

        [[nodiscard]] VkResult SubmitAndWait(std::string& error);
        [[nodiscard]] VkResult Wait(std::string& error);
        [[nodiscard]] bool IsPending() const noexcept;
        // Destructor support: waits and updates ownership without allocating diagnostics.
        [[nodiscard]] VkResult DrainForDestruction() noexcept;
        void MarkCompleteAfterQueueIdleForDestruction() noexcept;
        void MarkDeviceLostForDestruction() noexcept;
        [[nodiscard]] VkMemoryPropertyFlags StagingMemoryProperties() const noexcept;
        [[nodiscard]] std::optional<VulkanBuffer> TakeDestination(std::string& error);

    private:
        void Reset() noexcept;
        void ReleaseTransient() noexcept;

        const VulkanDevice* device_ = nullptr;
        VkCommandPool commandPool_ = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
        VkFence fence_ = VK_NULL_HANDLE;
        std::optional<VulkanBuffer> staging_;
        std::optional<VulkanBuffer> destination_;
        VkMemoryPropertyFlags stagingMemoryProperties_ = 0;
        detail::BufferUploadState state_ = detail::BufferUploadState::NotSubmitted;
    };
} // namespace owl::vulkan
