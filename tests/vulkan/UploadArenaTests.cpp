#include "VulkanUploadArena.h"

#include "VulkanFrame.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace
{
    using owl::vulkan::detail::FindUploadRange;
    using owl::vulkan::detail::UploadArenaState;
    using owl::vulkan::detail::UploadRange;

    // Fixture setup only; expected offsets, serials and byte counts remain explicit in tests.
    void AppendAndSeal(UploadArenaState& state, const VkDeviceSize bytes)
    {
        const auto range = state.PreviewAppend(bytes, 1);
        REQUIRE(range);
        REQUIRE(state.CommitAppend(*range));
        REQUIRE(state.Seal());
    }
} // namespace

TEST_CASE("Upload arena ranges include alignment padding and exact fits", "[vulkan][upload-arena]")
{
    const auto first = FindUploadRange(32, 0, 3, 1);
    REQUIRE(first);
    CHECK(first->offset == 0);
    CHECK(first->size == 3);
    const auto second = FindUploadRange(32, 3, 5, 8);
    REQUIRE(second);
    CHECK(second->offset == 8);
    CHECK(second->size == 5);
    const auto third = FindUploadRange(32, 13, 16, 16);
    REQUIRE(third);
    CHECK(third->offset == 16);
    CHECK(third->size == 16);
    CHECK_FALSE(FindUploadRange(32, 32, 1, 1));

    // Odd-sized buffer copies have no universal multiple-of-four requirement.
    const auto odd = FindUploadRange(67, 1, 65, 1);
    REQUIRE(odd);
    CHECK(odd->offset == 1);
    CHECK(odd->size == 65);
    const auto last = FindUploadRange(67, 66, 1, 1);
    REQUIRE(last);
    CHECK(last->offset == 66);
    CHECK(last->size == 1);
}

TEST_CASE("Upload arena ranges reject invalid requests without wrapping", "[vulkan][upload-arena]")
{
    CHECK_FALSE(owl::vulkan::detail::IsUploadCapacityValid(0));
    CHECK(owl::vulkan::detail::IsUploadCapacityValid(1));
    CHECK_FALSE(FindUploadRange(0, 0, 1, 1));
    CHECK_FALSE(FindUploadRange(32, 33, 1, 1));
    CHECK_FALSE(FindUploadRange(32, 0, 0, 1));
    CHECK_FALSE(FindUploadRange(32, 32, 0, 1));
    for (const VkDeviceSize alignment : {0ULL, 3ULL, 6ULL})
    {
        CAPTURE(alignment);
        CHECK_FALSE(FindUploadRange(32, 0, 1, alignment));
    }
    CHECK_FALSE(FindUploadRange(8, 7, 1, 16));
    CHECK_FALSE(FindUploadRange(32, 25, 5, 4));
    CHECK_FALSE(FindUploadRange(32, 0, 33, 1));
    const auto aligned = FindUploadRange(32, 28, 4, 4);
    REQUIRE(aligned);
    CHECK(aligned->offset == 28);
    CHECK(aligned->size == 4);
}

TEST_CASE("Upload arena arithmetic handles device and host size limits", "[vulkan][upload-arena]")
{
    constexpr auto deviceMax = std::numeric_limits<VkDeviceSize>::max();
    constexpr VkDeviceSize hostMax = std::numeric_limits<std::size_t>::max();
    constexpr VkDeviceSize highAlignment = VkDeviceSize{1} << 63;
    CHECK(owl::vulkan::detail::IsUploadCapacityValid(hostMax));
    CHECK(owl::vulkan::detail::IsUploadCapacityValid(deviceMax) == (deviceMax <= hostMax));

    // These are arithmetic-only checks, not attempts to allocate buffers of these sizes.
    const auto exact = FindUploadRange(hostMax, hostMax - 3, 3, 1);
    REQUIRE(exact);
    CHECK(exact->offset == hostMax - 3);
    CHECK(exact->size == 3);
    CHECK_FALSE(FindUploadRange(hostMax, hostMax - 3, 4, 1));
    CHECK_FALSE(FindUploadRange(hostMax, hostMax, 1, 1));
    CHECK_FALSE(FindUploadRange(hostMax, hostMax - 2, 1, 8));
    CHECK_FALSE(FindUploadRange(hostMax, hostMax - 2, deviceMax, 1));
    CHECK_FALSE(FindUploadRange(hostMax, hostMax - 2, 1, highAlignment));
    CHECK_FALSE(FindUploadRange(hostMax, deviceMax, 1, 1));

    // Large alignment is allowed when offset zero already satisfies it.
    const auto atZero = FindUploadRange(1, 0, 1, highAlignment);
    REQUIRE(atZero);
    CHECK(atZero->offset == 0);
    CHECK(atZero->size == 1);
}

TEST_CASE("Upload arena previews consume space only after a successful commit",
          "[vulkan][upload-arena]")
{
    UploadArenaState state{32};
    CHECK(state.Capacity() == 32);
    REQUIRE(state.Begin(0));
    const auto first = state.PreviewAppend(3, 1);
    REQUIRE(first);
    CHECK(state.UsedBytes() == 0);
    REQUIRE(state.CommitAppend(*first));
    CHECK(state.UsedBytes() == 3);

    const auto second = state.PreviewAppend(5, 8);
    REQUIRE(second);
    CHECK(second->offset == 8);
    CHECK(second->size == 5);
    // A caller whose write/flush failed does not commit this preview.
    CHECK(state.UsedBytes() == 3);
    CHECK(state.LastUseSerial() == 0);
    const auto retry = state.PreviewAppend(5, 8);
    REQUIRE(retry);
    CHECK(retry->offset == 8);
    REQUIRE(state.CommitAppend(*retry));
    CHECK(state.UsedBytes() == 13);
    CHECK_FALSE(state.CommitAppend(*first));
    CHECK_FALSE(state.PreviewAppend(20, 16));
    CHECK(state.UsedBytes() == 13);
    const auto last = state.PreviewAppend(16, 16);
    REQUIRE(last);
    CHECK(last->offset == 16);
    REQUIRE(state.CommitAppend(*last));
    CHECK(state.UsedBytes() == 32);
    CHECK_FALSE(state.PreviewAppend(1, 1));
    CHECK(state.UsedBytes() == 32);
}

TEST_CASE("Upload arena invalid commits preserve the published cursor", "[vulkan][upload-arena]")
{
    UploadArenaState state{16};
    CHECK_FALSE(state.CommitAppend({0, 3}));
    REQUIRE(state.Begin(0));
    const auto first = state.PreviewAppend(3, 1);
    REQUIRE(first);
    REQUIRE(state.CommitAppend(*first));
    const std::array invalid{
        UploadRange{2, 1},  UploadRange{3, 0},
        UploadRange{16, 1}, UploadRange{17, 1},
        UploadRange{4, 13}, UploadRange{std::numeric_limits<VkDeviceSize>::max(), 2},
    };
    for (const auto& range : invalid)
    {
        CAPTURE(range.offset, range.size);
        CHECK_FALSE(state.CommitAppend(range));
        CHECK(state.UsedBytes() == 3);
        CHECK(state.LastUseSerial() == 0);
    }
    REQUIRE(state.Seal());
    CHECK_FALSE(state.CommitAppend({3, 1}));
    CHECK(state.UsedBytes() == 3);
}

TEST_CASE("Upload arena batches must begin and contain data before sealing",
          "[vulkan][upload-arena]")
{
    UploadArenaState empty;
    CHECK_FALSE(empty.CanBegin(0));
    CHECK_FALSE(empty.Begin(0));
    CHECK_FALSE(empty.PreviewAppend(1, 1));
    CHECK_FALSE(empty.Seal());
    CHECK_FALSE(empty.CanSubmit(1));

    UploadArenaState state{16};
    CHECK(state.CanBegin(0));
    CHECK_FALSE(state.PreviewAppend(1, 1));
    CHECK_FALSE(state.Seal());
    CHECK_FALSE(state.CanSubmit(1));
    REQUIRE(state.Begin(0));
    CHECK_FALSE(state.Seal());
    const auto range = state.PreviewAppend(3, 1);
    REQUIRE(range);
    REQUIRE(state.CommitAppend(*range));
    CHECK_FALSE(state.CanSubmit(1));
    REQUIRE(state.Seal());
    CHECK_FALSE(state.Seal());
    CHECK_FALSE(state.PreviewAppend(1, 1));
    CHECK_FALSE(state.CanSubmit(0));
    CHECK(state.CanSubmit(1));
}

TEST_CASE("Upload arena reuse requires observed completion of its last submission",
          "[vulkan][upload-arena]")
{
    UploadArenaState state{32};
    REQUIRE(state.Begin(0));
    AppendAndSeal(state, 5);
    REQUIRE(state.CanSubmit(7));
    state.MarkSubmitted(7);
    CHECK(state.LastUseSerial() == 7);
    CHECK_FALSE(state.CanSubmit(7));
    CHECK_FALSE(state.CanSubmit(8));
    CHECK_FALSE(state.PreviewAppend(1, 1));
    CHECK_FALSE(state.CommitAppend({5, 1}));
    CHECK_FALSE(state.Seal());
    CHECK_FALSE(state.CanBegin(6));
    CHECK_FALSE(state.Begin(6));
    CHECK(state.UsedBytes() == 5);
    CHECK(state.LastUseSerial() == 7);
    CHECK(state.CanBegin(7));
    CHECK(state.UsedBytes() == 5); // Preflight does not itself reset storage.
    REQUIRE(state.Begin(7));
    CHECK(state.UsedBytes() == 0);
    CHECK(state.LastUseSerial() == 7);
    AppendAndSeal(state, 3);
    CHECK_FALSE(state.CanSubmit(0));
    CHECK_FALSE(state.CanSubmit(6));
    CHECK_FALSE(state.CanSubmit(7));
    REQUIRE(state.CanSubmit(8));
    state.MarkSubmitted(8);
    CHECK_FALSE(state.Begin(7));
    CHECK(state.UsedBytes() == 3);
    CHECK(state.LastUseSerial() == 8);
    REQUIRE(state.Begin(8));
    CHECK(state.UsedBytes() == 0);
}

TEST_CASE("Upload arena abandoned batches retain their last successful identity",
          "[vulkan][upload-arena]")
{
    UploadArenaState state{16};
    REQUIRE(state.Begin(0));
    const auto first = state.PreviewAppend(3, 1);
    REQUIRE(first);
    REQUIRE(state.CommitAppend(*first));
    REQUIRE(state.Begin(0)); // Caller discards the old unsubmitted recording first.
    CHECK(state.UsedBytes() == 0);
    CHECK(state.LastUseSerial() == 0);
    AppendAndSeal(state, 5);
    REQUIRE(state.Begin(0)); // A sealed but unsubmitted batch can also be discarded.
    CHECK(state.UsedBytes() == 0);
    AppendAndSeal(state, 3);
    REQUIRE(state.CanSubmit(7));
    state.MarkSubmitted(7);
    REQUIRE(state.Begin(7));
    AppendAndSeal(state, 5);
    REQUIRE(state.Begin(7));
    CHECK(state.LastUseSerial() == 7);
    CHECK(state.UsedBytes() == 0);
    AppendAndSeal(state, 3);
    CHECK_FALSE(state.CanSubmit(7));
    CHECK(state.CanSubmit(8));
}

TEST_CASE("Upload arena failed submissions cannot supply a new identity",
          "[vulkan][upload-arena][submission]")
{
    owl::vulkan::VulkanSubmissionProgress progress;
    owl::vulkan::VulkanFrameSlot slot;
    UploadArenaState state{32};
    REQUIRE(state.Begin(0));
    AppendAndSeal(state, 3);
    REQUIRE(progress.CanSubmit(slot));
    REQUIRE(state.CanSubmit(1));
    progress.RecordSubmitResult(slot, VK_SUCCESS);
    state.MarkSubmitted(slot.submissionSerial);
    progress.RecordWaitResult(slot, VK_SUCCESS);
    REQUIRE(state.Begin(progress.CompletedSerial()));
    AppendAndSeal(state, 5);

    // Result-policy checks only; these do not simulate recovery of a lost native device.
    for (const auto result :
         {VK_ERROR_OUT_OF_HOST_MEMORY, VK_ERROR_OUT_OF_DEVICE_MEMORY, VK_ERROR_DEVICE_LOST})
    {
        CAPTURE(result);
        progress.RecordSubmitResult(slot, result);
        CHECK_FALSE(slot.submissionPending);
        CHECK(progress.SubmittedSerial() == 1);
        CHECK(progress.CompletedSerial() == 1);
        CHECK(slot.submissionSerial == 1);
        CHECK_FALSE(state.CanSubmit(slot.submissionSerial));
        CHECK(state.LastUseSerial() == 1);
        CHECK(state.UsedBytes() == 5);
        CHECK_FALSE(state.PreviewAppend(1, 1));
    }
}

TEST_CASE("Upload arena unsuccessful waits and presentation do not allow reuse",
          "[vulkan][upload-arena][submission]")
{
    owl::vulkan::VulkanSubmissionProgress progress;
    owl::vulkan::VulkanFrameSlot slot;
    UploadArenaState state{32};
    progress.RecordWaitResult(slot, VK_SUCCESS); // Initial signaled fence has no submission.
    CHECK(progress.CompletedSerial() == 0);
    REQUIRE(state.Begin(progress.CompletedSerial()));
    AppendAndSeal(state, 3);
    REQUIRE(state.CanSubmit(1));
    progress.RecordSubmitResult(slot, VK_SUCCESS);
    state.MarkSubmitted(slot.submissionSerial);
    const auto present = owl::vulkan::detail::ClassifyPresentResult(VK_ERROR_OUT_OF_DATE_KHR);
    CHECK(present.recreate);
    CHECK_FALSE(state.CanBegin(progress.CompletedSerial()));

    for (const auto result :
         {VK_TIMEOUT, VK_NOT_READY, VK_ERROR_OUT_OF_HOST_MEMORY, VK_ERROR_DEVICE_LOST})
    {
        CAPTURE(result);
        progress.RecordWaitResult(slot, result);
        CHECK(slot.submissionPending);
        CHECK(progress.CompletedSerial() == 0);
        CHECK_FALSE(state.CanBegin(progress.CompletedSerial()));
        CHECK_FALSE(state.Begin(progress.CompletedSerial()));
        CHECK(state.UsedBytes() == 3);
        CHECK(state.LastUseSerial() == 1);
    }
}

TEST_CASE("Upload arena slots retain distinct identities across fence reuse",
          "[vulkan][upload-arena][submission]")
{
    owl::vulkan::VulkanSubmissionProgress progress;
    owl::vulkan::VulkanFrameSlot firstSlot;
    owl::vulkan::VulkanFrameSlot secondSlot;
    UploadArenaState first{32};
    UploadArenaState second{32};
    REQUIRE(first.Begin(0));
    REQUIRE(second.Begin(0));
    AppendAndSeal(first, 3);
    AppendAndSeal(second, 5);
    REQUIRE(first.CanSubmit(1));
    progress.RecordSubmitResult(firstSlot, VK_SUCCESS);
    first.MarkSubmitted(firstSlot.submissionSerial);
    REQUIRE(second.CanSubmit(2));
    progress.RecordSubmitResult(secondSlot, VK_SUCCESS);
    second.MarkSubmitted(secondSlot.submissionSerial);
    CHECK_FALSE(first.CanBegin(progress.CompletedSerial()));
    CHECK_FALSE(second.CanBegin(progress.CompletedSerial()));

    progress.RecordWaitResult(firstSlot, VK_SUCCESS);
    CHECK(progress.CompletedSerial() == 1);
    REQUIRE(first.Begin(progress.CompletedSerial()));
    CHECK_FALSE(second.Begin(progress.CompletedSerial()));
    CHECK(second.UsedBytes() == 5);
    AppendAndSeal(first, 7);
    REQUIRE(first.CanSubmit(3));
    progress.RecordSubmitResult(firstSlot, VK_SUCCESS);
    first.MarkSubmitted(firstSlot.submissionSerial);
    CHECK(first.LastUseSerial() == 3);

    progress.RecordWaitResult(secondSlot, VK_SUCCESS);
    CHECK(progress.CompletedSerial() == 2);
    CHECK_FALSE(first.Begin(progress.CompletedSerial()));
    REQUIRE(second.Begin(progress.CompletedSerial()));
    CHECK(first.UsedBytes() == 7);
    CHECK(second.UsedBytes() == 0);
    progress.RecordWaitResult(firstSlot, VK_SUCCESS);
    CHECK(progress.CompletedSerial() == 3);
    REQUIRE(first.Begin(progress.CompletedSerial()));
    CHECK(first.UsedBytes() == 0);
}

TEST_CASE("Upload arena submission identity cannot wrap after serial exhaustion",
          "[vulkan][upload-arena][submission]")
{
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    UploadArenaState state{16};
    REQUIRE(state.Begin(0));
    AppendAndSeal(state, 3);
    const auto lastSerial = owl::vulkan::detail::NextSubmissionSerial(maximum - 1);
    REQUIRE(lastSerial);
    CHECK(*lastSerial == maximum);
    REQUIRE(state.CanSubmit(*lastSerial));
    state.MarkSubmitted(*lastSerial);
    CHECK_FALSE(state.Begin(maximum - 1));
    REQUIRE(state.Begin(maximum));
    AppendAndSeal(state, 5);
    CHECK_FALSE(owl::vulkan::detail::NextSubmissionSerial(maximum));
    CHECK_FALSE(state.CanSubmit(0));
    CHECK_FALSE(state.CanSubmit(1));
    CHECK_FALSE(state.CanSubmit(maximum));
    CHECK(state.LastUseSerial() == maximum);
    CHECK(state.UsedBytes() == 5);
}
