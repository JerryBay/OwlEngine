#include "VulkanResourceRetirement.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <type_traits>

namespace
{
    using DestructionCounts = std::array<unsigned int, 3>;

    // Observe real ownership transfers and destruction without a native GPU resource.
    struct TrackedResource
    {
        TrackedResource(DestructionCounts& counts, const std::size_t index) noexcept
            : destructions(counts), id(index)
        {
        }
        ~TrackedResource()
        {
            ++destructions[id];
        }
        TrackedResource(const TrackedResource&) = delete;
        TrackedResource& operator=(const TrackedResource&) = delete;
        TrackedResource(TrackedResource&&) = delete;
        TrackedResource& operator=(TrackedResource&&) = delete;

        DestructionCounts& destructions;
        std::size_t id;
    };

    template <std::size_t Capacity>
    using Queue = owl::vulkan::detail::ResourceRetirementQueue<TrackedResource, Capacity>;

    static_assert(!std::is_copy_constructible_v<Queue<2>>);
    static_assert(!std::is_move_constructible_v<Queue<2>>);
    static_assert(!std::is_copy_assignable_v<Queue<2>>);
    static_assert(!std::is_move_assignable_v<Queue<2>>);
} // namespace

TEST_CASE("Resource retirement retains ownership until exact completion", "[vulkan][retirement]")
{
    DestructionCounts destructions{};
    Queue<1> queue;
    auto resource = std::make_unique<TrackedResource>(destructions, 0);
    const auto* original = resource.get();

    REQUIRE(queue.TryRetire(7, resource));
    CHECK_FALSE(resource);
    CHECK(queue.PendingCount() == 1);
    CHECK(queue.Collect(0) == 0);
    CHECK(queue.Collect(6) == 0);
    REQUIRE(destructions[0] == 0);
    CHECK(original->id == 0);
    CHECK(queue.PendingCount() == 1);

    CHECK(queue.Collect(7) == 1);
    CHECK(destructions[0] == 1);
    CHECK(queue.PendingCount() == 0);
    CHECK(queue.Collect(7) == 0);
    CHECK(queue.Collect(8) == 0);
    CHECK(destructions[0] == 1);
}

TEST_CASE("Resource retirement collects eligible entries out of insertion order",
          "[vulkan][retirement]")
{
    DestructionCounts destructions{};
    Queue<3> queue;
    auto late = std::make_unique<TrackedResource>(destructions, 0);
    auto early = std::make_unique<TrackedResource>(destructions, 1);
    auto middle = std::make_unique<TrackedResource>(destructions, 2);
    REQUIRE(queue.TryRetire(8, late));
    REQUIRE(queue.TryRetire(3, early));
    REQUIRE(queue.TryRetire(6, middle));

    CHECK(queue.Collect(3) == 1);
    CHECK(destructions == DestructionCounts{0, 1, 0});
    CHECK(queue.PendingCount() == 2);
    CHECK(queue.Collect(7) == 1);
    CHECK(destructions == DestructionCounts{0, 1, 1});
    CHECK(queue.PendingCount() == 1);
    CHECK(queue.Collect(8) == 1);
    CHECK(destructions == DestructionCounts{1, 1, 1});
    CHECK(queue.PendingCount() == 0);
}

TEST_CASE("Resource retirement releases all owners sharing a completed submission",
          "[vulkan][retirement]")
{
    DestructionCounts destructions{};
    Queue<3> queue;
    for (std::size_t index = 0; index < destructions.size(); ++index)
    {
        auto resource = std::make_unique<TrackedResource>(destructions, index);
        REQUIRE(queue.TryRetire(5, resource));
        CHECK_FALSE(resource);
    }
    CHECK(queue.Collect(4) == 0);
    CHECK(destructions == DestructionCounts{0, 0, 0});
    CHECK(queue.Collect(5) == 3);
    CHECK(destructions == DestructionCounts{1, 1, 1});
    CHECK(queue.PendingCount() == 0);
    CHECK(queue.Collect(100) == 0);
    CHECK(destructions == DestructionCounts{1, 1, 1});
}

TEST_CASE("Resource retirement handles owners without submitted use", "[vulkan][retirement]")
{
    DestructionCounts destructions{};
    Queue<2> queue;
    auto unused = std::make_unique<TrackedResource>(destructions, 0);
    auto pending = std::make_unique<TrackedResource>(destructions, 1);
    REQUIRE(queue.TryRetire(0, unused));
    REQUIRE(queue.TryRetire(1, pending));
    CHECK(destructions == DestructionCounts{0, 0, 0});
    CHECK(queue.Collect(0) == 1);
    CHECK(destructions == DestructionCounts{1, 0, 0});
    CHECK(queue.PendingCount() == 1);
    CHECK(queue.Collect(1) == 1);
    CHECK(destructions == DestructionCounts{1, 1, 0});
}

TEST_CASE("Resource retirement preserves every owner when capacity is exhausted",
          "[vulkan][retirement]")
{
    DestructionCounts destructions{};
    Queue<2> queue;
    auto first = std::make_unique<TrackedResource>(destructions, 0);
    auto second = std::make_unique<TrackedResource>(destructions, 1);
    auto third = std::make_unique<TrackedResource>(destructions, 2);
    const auto* thirdIdentity = third.get();
    REQUIRE(queue.TryRetire(4, first));
    REQUIRE(queue.TryRetire(9, second));
    CHECK_FALSE(queue.TryRetire(10, third));
    CHECK(third.get() == thirdIdentity);
    CHECK(queue.PendingCount() == 2);
    CHECK(destructions == DestructionCounts{0, 0, 0});

    REQUIRE(queue.Collect(4) == 1);
    CHECK(destructions == DestructionCounts{1, 0, 0});
    REQUIRE(queue.TryRetire(10, third));
    CHECK_FALSE(third);
    CHECK(queue.PendingCount() == 2);
    CHECK(queue.Collect(9) == 1);
    CHECK(destructions == DestructionCounts{1, 1, 0});
    CHECK(queue.Collect(10) == 1);
    CHECK(destructions == DestructionCounts{1, 1, 1});
    CHECK(queue.PendingCount() == 0);
}

TEST_CASE("Resource retirement rejects empty ownership without consuming capacity",
          "[vulkan][retirement]")
{
    DestructionCounts destructions{};
    Queue<1> queue;
    std::unique_ptr<TrackedResource> empty;
    CHECK_FALSE(queue.TryRetire(3, empty));
    CHECK(queue.PendingCount() == 0);
    auto resource = std::make_unique<TrackedResource>(destructions, 0);
    REQUIRE(queue.TryRetire(3, resource));
    CHECK_FALSE(queue.TryRetire(3, empty));
    CHECK(queue.PendingCount() == 1);
    CHECK(destructions[0] == 0);
    CHECK(queue.Collect(3) == 1);
    CHECK(destructions[0] == 1);
}

TEST_CASE("Resource retirement compares the largest serial without wrapping",
          "[vulkan][retirement]")
{
    DestructionCounts destructions{};
    Queue<1> queue;
    auto resource = std::make_unique<TrackedResource>(destructions, 0);
    constexpr auto lastSerial = std::numeric_limits<std::uint64_t>::max();
    REQUIRE(queue.TryRetire(lastSerial, resource));
    CHECK(queue.Collect(lastSerial - 1) == 0);
    CHECK(queue.PendingCount() == 1);
    CHECK(destructions[0] == 0);
    CHECK(queue.Collect(lastSerial) == 1);
    CHECK(queue.PendingCount() == 0);
    CHECK(destructions[0] == 1);
}

TEST_CASE("Resource retirement reused storage retains the replacement use serial",
          "[vulkan][retirement]")
{
    DestructionCounts destructions{};
    Queue<1> queue;
    auto first = std::make_unique<TrackedResource>(destructions, 0);
    REQUIRE(queue.TryRetire(2, first));
    REQUIRE(queue.Collect(2) == 1);
    auto replacement = std::make_unique<TrackedResource>(destructions, 1);
    REQUIRE(queue.TryRetire(7, replacement));
    CHECK(queue.Collect(2) == 0);
    CHECK(queue.Collect(1) == 0);
    CHECK(queue.Collect(6) == 0);
    CHECK(queue.PendingCount() == 1);
    CHECK(destructions == DestructionCounts{1, 0, 0});
    CHECK(queue.Collect(7) == 1);
    CHECK(destructions == DestructionCounts{1, 1, 0});
}

TEST_CASE("Resource retirement releases remaining ownership at externally drained teardown",
          "[vulkan][retirement]")
{
    DestructionCounts destructions{};
    {
        Queue<2> queue;
        auto first = std::make_unique<TrackedResource>(destructions, 0);
        auto second = std::make_unique<TrackedResource>(destructions, 1);
        REQUIRE(queue.TryRetire(1, first));
        REQUIRE(queue.TryRetire(2, second));
        CHECK(queue.Collect(1) == 1);
        CHECK(destructions == DestructionCounts{1, 0, 0});
        // Native users must drain before this point. The queue performs no GPU wait itself.
    }
    CHECK(destructions == DestructionCounts{1, 1, 0});
}
