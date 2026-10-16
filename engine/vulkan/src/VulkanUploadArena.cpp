#include "VulkanUploadArena.h"

#include <cstddef>
#include <exception>
#include <limits>
#include <utility>

namespace owl::vulkan::detail
{
    bool IsUploadCapacityValid(const VkDeviceSize capacity) noexcept
    {
        return capacity != 0 && capacity <= std::numeric_limits<std::size_t>::max();
    }

    std::optional<UploadRange> FindUploadRange(const VkDeviceSize capacity,
                                               const VkDeviceSize cursor, const VkDeviceSize bytes,
                                               const VkDeviceSize alignment) noexcept
    {
        if (!IsUploadCapacityValid(capacity) || cursor > capacity || bytes == 0 || alignment == 0 ||
            (alignment & (alignment - 1)) != 0)
            return std::nullopt;

        // Check remaining capacity before adding padding or bytes; align-up expressions that
        // first add alignment - 1 can wrap even when the requested payload itself is small.
        const auto remainder = cursor % alignment;
        const auto padding = remainder == 0 ? 0 : alignment - remainder;
        const auto available = capacity - cursor;
        if (padding > available || bytes > available - padding)
            return std::nullopt;
        return UploadRange{cursor + padding, bytes};
    }

    UploadArenaState::UploadArenaState(const VkDeviceSize capacity) noexcept : capacity_(capacity)
    {
    }

    bool UploadArenaState::CanBegin(const std::uint64_t completedSerial) const noexcept
    {
        return IsUploadCapacityValid(capacity_) && completedSerial >= lastUseSerial_;
    }

    bool UploadArenaState::Begin(const std::uint64_t completedSerial) noexcept
    {
        if (!CanBegin(completedSerial))
            return false;
        cursor_ = 0;
        phase_ = Phase::Writable;
        return true;
    }

    std::optional<UploadRange>
    UploadArenaState::PreviewAppend(const VkDeviceSize bytes,
                                    const VkDeviceSize alignment) const noexcept
    {
        if (phase_ != Phase::Writable)
            return std::nullopt;
        return FindUploadRange(capacity_, cursor_, bytes, alignment);
    }

    bool UploadArenaState::CommitAppend(const UploadRange& range) noexcept
    {
        if (phase_ != Phase::Writable || range.size == 0 || range.offset < cursor_ ||
            range.offset > capacity_ || range.size > capacity_ - range.offset)
            return false;
        cursor_ = range.offset + range.size;
        return true;
    }

    bool UploadArenaState::Seal() noexcept
    {
        if (phase_ != Phase::Writable || cursor_ == 0)
            return false;
        phase_ = Phase::Sealed;
        return true;
    }

    bool UploadArenaState::CanSubmit(const std::uint64_t serial) const noexcept
    {
        return phase_ == Phase::Sealed && serial != 0 && serial > lastUseSerial_;
    }

    void UploadArenaState::MarkSubmitted(const std::uint64_t serial) noexcept
    {
        // Native submission has already succeeded. Losing or changing its identity would
        // allow early reuse, so callers must preflight before the irreversible queue call.
        if (!CanSubmit(serial))
            std::terminate();
        lastUseSerial_ = serial;
        phase_ = Phase::Submitted;
    }

    VkDeviceSize UploadArenaState::Capacity() const noexcept
    {
        return capacity_;
    }

    VkDeviceSize UploadArenaState::UsedBytes() const noexcept
    {
        return cursor_;
    }

    std::uint64_t UploadArenaState::LastUseSerial() const noexcept
    {
        return lastUseSerial_;
    }
} // namespace owl::vulkan::detail

namespace owl::vulkan
{
    VulkanUploadArena::VulkanUploadArena(VulkanUploadArena&& other) noexcept
        : buffer_(std::move(other.buffer_)),
          state_(std::exchange(other.state_, detail::UploadArenaState{}))
    {
    }

    VulkanUploadArena& VulkanUploadArena::operator=(VulkanUploadArena&& other) noexcept
    {
        if (this != &other)
        {
            buffer_ = std::move(other.buffer_);
            state_ = std::exchange(other.state_, detail::UploadArenaState{});
        }
        return *this;
    }

    std::optional<VulkanUploadArena> VulkanUploadArena::Create(const VulkanAllocator& allocator,
                                                               const VkDeviceSize capacity,
                                                               std::string& error)
    {
        if (!detail::IsUploadCapacityValid(capacity))
        {
            error = "Upload arena capacity must be nonzero and host-addressable";
            return std::nullopt;
        }
        auto buffer = VulkanBuffer::Create(allocator,
                                           {.size = capacity,
                                            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                            .requiredMemory = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                            .preferredMemory = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                            .hostAccess = BufferHostAccess::SequentialWrite,
                                            .persistentMapping = true},
                                           error);
        if (!buffer)
            return std::nullopt;
        VulkanUploadArena arena;
        arena.buffer_ = std::move(*buffer);
        arena.state_ = detail::UploadArenaState{capacity};
        return arena;
    }

    bool VulkanUploadArena::IsValid() const noexcept
    {
        return buffer_.IsValid();
    }

    VkDeviceSize VulkanUploadArena::Capacity() const noexcept
    {
        return state_.Capacity();
    }

    VkDeviceSize VulkanUploadArena::UsedBytes() const noexcept
    {
        return state_.UsedBytes();
    }

    std::uint64_t VulkanUploadArena::LastUseSerial() const noexcept
    {
        return state_.LastUseSerial();
    }

    VkMemoryPropertyFlags VulkanUploadArena::MemoryProperties() const noexcept
    {
        return buffer_.MemoryProperties();
    }

    bool VulkanUploadArena::CanBegin(const std::uint64_t completedSerial) const noexcept
    {
        return IsValid() && state_.CanBegin(completedSerial);
    }

    bool VulkanUploadArena::Begin(const std::uint64_t completedSerial, std::string& error)
    {
        if (!IsValid() || !state_.Begin(completedSerial))
        {
            error = "Upload arena requires valid storage and completed prior GPU use";
            return false;
        }
        error.clear();
        return true;
    }

    std::optional<UploadSlice> VulkanUploadArena::Append(const std::span<const std::byte> bytes,
                                                         const VkDeviceSize alignment,
                                                         std::string& error)
    {
        const auto range = state_.PreviewAppend(static_cast<VkDeviceSize>(bytes.size()), alignment);
        if (!IsValid() || !range)
        {
            error = "Upload append requires a writable batch, nonempty data, power-of-two "
                    "alignment and enough remaining capacity";
            return std::nullopt;
        }
        // Write performs the allocation-relative flush. Publish only after it succeeds;
        // a failed write/flush may touch the unpublished tail but cannot advance the cursor.
        if (!buffer_.Write(range->offset, bytes, error))
            return std::nullopt;
        if (!state_.CommitAppend(*range))
            std::terminate(); // No state mutation may intervene between preview and commit.
        return UploadSlice{buffer_.Get(), range->offset, range->size};
    }

    bool VulkanUploadArena::Seal() noexcept
    {
        return IsValid() && state_.Seal();
    }

    bool VulkanUploadArena::CanSubmit(const std::uint64_t serial) const noexcept
    {
        return IsValid() && state_.CanSubmit(serial);
    }

    void VulkanUploadArena::MarkSubmitted(const std::uint64_t serial) noexcept
    {
        state_.MarkSubmitted(serial);
    }
} // namespace owl::vulkan
