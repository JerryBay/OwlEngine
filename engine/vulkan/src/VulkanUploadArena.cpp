#include "VulkanUploadArena.h"

#include <cstddef>
#include <exception>
#include <limits>

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
