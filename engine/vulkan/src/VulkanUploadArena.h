#pragma once

#include "VulkanBuffer.h"

#include <cstdint>
#include <optional>

namespace owl::vulkan::detail
{
    struct UploadRange
    {
        VkDeviceSize offset = 0;
        VkDeviceSize size = 0;
    };

    [[nodiscard]] bool IsUploadCapacityValid(VkDeviceSize capacity) noexcept;
    [[nodiscard]] std::optional<UploadRange> FindUploadRange(VkDeviceSize capacity,
                                                             VkDeviceSize cursor,
                                                             VkDeviceSize bytes,
                                                             VkDeviceSize alignment) noexcept;

    // CPU metadata for one bounded upload allocation. Owns no native resources or completion
    // primitive. The arena composes this policy with its buffer, rather than duplicating
    // cursor/seal/serial bookkeeping. All calls belong to the submitting owner thread.
    class UploadArenaState
    {
    public:
        explicit UploadArenaState(VkDeviceSize capacity = 0) noexcept;

        // Check before resetting command storage. Completion must come from the same queue's
        // progress owner; the renderer separately enforces its terminal device-loss state.
        [[nodiscard]] bool CanBegin(std::uint64_t completedSerial) const noexcept;
        // After CanBegin succeeds, discard old command recording and borrowed ranges first.
        // Also permits abandoning a writable or sealed batch that was never submitted.
        [[nodiscard]] bool Begin(std::uint64_t completedSerial) noexcept;
        [[nodiscard]] std::optional<UploadRange>
        PreviewAppend(VkDeviceSize bytes, VkDeviceSize alignment) const noexcept;
        // Commit a preview from the current batch only after its write and flush succeed.
        // A rejected commit leaves the cursor unchanged; previews expire at Begin.
        [[nodiscard]] bool CommitAppend(const UploadRange& range) noexcept;
        [[nodiscard]] bool Seal() noexcept;
        [[nodiscard]] bool CanSubmit(std::uint64_t serial) const noexcept;
        // Preflight CanSubmit before the native submit, then record its actual successful
        // serial once. Violating this contract is terminal, matching submission progress.
        void MarkSubmitted(std::uint64_t serial) noexcept;

        [[nodiscard]] VkDeviceSize Capacity() const noexcept;
        [[nodiscard]] VkDeviceSize UsedBytes() const noexcept;
        [[nodiscard]] std::uint64_t LastUseSerial() const noexcept;

    private:
        enum class Phase
        {
            Idle,
            Writable,
            Sealed,
            Submitted,
        };

        VkDeviceSize capacity_ = 0;
        VkDeviceSize cursor_ = 0;
        std::uint64_t lastUseSerial_ = 0;
        Phase phase_ = Phase::Idle;
    };
} // namespace owl::vulkan::detail

namespace owl::vulkan
{
    // Borrowed source range. Discard with its batch/command recording before the next Begin.
    struct UploadSlice
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceSize offset = 0;
        VkDeviceSize size = 0;
    };

    // Bounded staging storage for one submission at a time. Borrows the native allocator.
    // Single owner thread; no waits or implicit growth. Finish GPU use and discard command
    // references before destruction or move replacement. Moving preserves pending identity.
    class VulkanUploadArena
    {
    public:
        VulkanUploadArena() noexcept = default;
        VulkanUploadArena(const VulkanUploadArena&) = delete;
        VulkanUploadArena& operator=(const VulkanUploadArena&) = delete;
        VulkanUploadArena(VulkanUploadArena&& other) noexcept;
        VulkanUploadArena& operator=(VulkanUploadArena&& other) noexcept;

        [[nodiscard]] static std::optional<VulkanUploadArena>
        Create(const VulkanAllocator& allocator, VkDeviceSize capacity, std::string& error);
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] VkDeviceSize Capacity() const noexcept;
        [[nodiscard]] VkDeviceSize UsedBytes() const noexcept;
        [[nodiscard]] std::uint64_t LastUseSerial() const noexcept;
        [[nodiscard]] VkMemoryPropertyFlags MemoryProperties() const noexcept;

        // Completion must come from the same graphics-queue progress owner. After CanBegin,
        // discard old command recording and slices before Begin resets the writable batch.
        [[nodiscard]] bool CanBegin(std::uint64_t completedSerial) const noexcept;
        [[nodiscard]] bool Begin(std::uint64_t completedSerial, std::string& error);
        [[nodiscard]] std::optional<UploadSlice> Append(std::span<const std::byte> bytes,
                                                        VkDeviceSize alignment, std::string& error);
        [[nodiscard]] bool Seal() noexcept;
        [[nodiscard]] bool CanSubmit(std::uint64_t serial) const noexcept;
        // Preflight before vkQueueSubmit2; commit its successful serial exactly once.
        void MarkSubmitted(std::uint64_t serial) noexcept;

    private:
        VulkanBuffer buffer_;
        detail::UploadArenaState state_;
    };
} // namespace owl::vulkan
