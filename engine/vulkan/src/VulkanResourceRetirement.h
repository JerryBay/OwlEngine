#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>

namespace owl::vulkan::detail
{
    // Bounded ownership on the renderer thread; serials belong to one graphics queue.
    // The caller stops all future resource use before retiring, supplies observed completion,
    // and drains GPU work (or establishes terminal device loss) before destroying this queue.
    // Destruction performs no wait. Dependencies must be grouped inside Resource, whose
    // destructor must not throw or reenter the queue.
    template <typename Resource, std::size_t MaxPending> class ResourceRetirementQueue
    {
        static_assert(MaxPending > 0);
        static_assert(std::is_nothrow_destructible_v<Resource>);

    public:
        ResourceRetirementQueue() noexcept = default;
        ResourceRetirementQueue(const ResourceRetirementQueue&) = delete;
        ResourceRetirementQueue& operator=(const ResourceRetirementQueue&) = delete;
        ResourceRetirementQueue(ResourceRetirementQueue&&) = delete;
        ResourceRetirementQueue& operator=(ResourceRetirementQueue&&) = delete;

        // Zero means no outstanding submitted use, including separate startup uploads.
        // A rejected enqueue leaves the caller's owner and all queued resources intact.
        [[nodiscard]] bool TryRetire(const std::uint64_t lastUseSerial,
                                     std::unique_ptr<Resource>& resource) noexcept
        {
            if (!resource)
                return false;
            for (auto& entry : entries_)
            {
                if (!entry.resource)
                {
                    entry.lastUseSerial = lastUseSerial;
                    entry.resource = std::move(resource);
                    ++pendingCount_;
                    return true;
                }
            }
            return false;
        }

        std::size_t Collect(const std::uint64_t completedSerial) noexcept
        {
            std::size_t released = 0;
            // Retirement order can differ from last-use order; an unfinished entry must not
            // block eligible entries behind it. No ordering is promised between resources.
            for (auto& entry : entries_)
            {
                if (entry.resource && entry.lastUseSerial <= completedSerial)
                {
                    entry.resource.reset();
                    entry.lastUseSerial = 0;
                    --pendingCount_;
                    ++released;
                }
            }
            return released;
        }

        [[nodiscard]] std::size_t PendingCount() const noexcept
        {
            return pendingCount_;
        }

    private:
        struct Entry
        {
            std::uint64_t lastUseSerial = 0;
            std::unique_ptr<Resource> resource;
        };

        std::array<Entry, MaxPending> entries_{};
        std::size_t pendingCount_ = 0;
    };
} // namespace owl::vulkan::detail
