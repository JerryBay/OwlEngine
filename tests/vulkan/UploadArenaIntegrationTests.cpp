#include "GpuTestContext.h"
#include "VulkanFrame.h"
#include "VulkanUploadArena.h"

#include <catch2/catch_test_macros.hpp>

#include <owl/foundation/Log.h>

#include <array>
#include <cstdint>
#include <exception>
#include <limits>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
    using namespace owl::vulkan;

    VmaStatistics AllocationStatistics(const VulkanAllocator& allocator)
    {
        VmaTotalStatistics statistics{};
        vmaCalculateStatistics(allocator.Get(), &statistics);
        return statistics.total.statistics;
    }

    // Declare after every submitted child owner, including move destinations. Catch2 failures
    // must drain here before stack unwinding destroys buffers, pools, fences or native parents.
    struct SubmissionDrainGuard
    {
        VkDevice device;
        VkQueue queue;
        VulkanFrameResources& frames;
        VulkanSubmissionProgress& progress;

        ~SubmissionDrainGuard()
        {
            for (auto& slot : frames.slots)
            {
                if (!slot.submissionPending)
                    continue;
                auto result = vkWaitForFences(device, 1, &slot.complete, VK_TRUE,
                                              std::numeric_limits<std::uint64_t>::max());
                if (result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST)
                    result = vkQueueWaitIdle(queue);
                if (result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST)
                    std::terminate();
                progress.RecordWaitResult(slot, result);
            }
        }
    };

    void CopyAndSubmit(const VkDevice device, const VkQueue queue, VulkanFrameSlot& slot,
                       VulkanSubmissionProgress& progress,
                       const std::span<const UploadSlice> slices, const VulkanBuffer& destination)
    {
        REQUIRE(slot.uploadArena);
        REQUIRE(progress.CanSubmit(slot));
        REQUIRE(slot.uploadArena->CanSubmit(progress.SubmittedSerial() + 1));
        const VkCommandBufferBeginInfo begin{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        REQUIRE(vkBeginCommandBuffer(slot.commandBuffer, &begin) == VK_SUCCESS);
        VkDeviceSize offset = 0;
        for (const auto& slice : slices)
        {
            const VkBufferCopy copy{
                .srcOffset = slice.offset, .dstOffset = offset, .size = slice.size};
            vkCmdCopyBuffer(slot.commandBuffer, slice.buffer, destination.Get(), 1, &copy);
            offset += slice.size;
        }
        const VkBufferMemoryBarrier2 barrier{
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
            .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = destination.Get(),
            .offset = 0,
            .size = offset,
        };
        const VkDependencyInfo dependency{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .bufferMemoryBarrierCount = 1,
            .pBufferMemoryBarriers = &barrier,
        };
        vkCmdPipelineBarrier2(slot.commandBuffer, &dependency);
        REQUIRE(vkEndCommandBuffer(slot.commandBuffer) == VK_SUCCESS);
        REQUIRE(vkResetFences(device, 1, &slot.complete) == VK_SUCCESS);
        const VkCommandBufferSubmitInfo command{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .commandBuffer = slot.commandBuffer,
        };
        const VkSubmitInfo2 submit{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = &command,
        };
        const auto result = vkQueueSubmit2(queue, 1, &submit, slot.complete);
        // Record success before any assertion can throw; the guard then knows what to drain.
        progress.RecordSubmitResult(slot, result);
        if (result == VK_SUCCESS)
            slot.uploadArena->MarkSubmitted(slot.submissionSerial);
        REQUIRE(result == VK_SUCCESS);
    }

    void WaitForSlot(const VkDevice device, VulkanFrameSlot& slot,
                     VulkanSubmissionProgress& progress)
    {
        const auto result = vkWaitForFences(device, 1, &slot.complete, VK_TRUE,
                                            std::numeric_limits<std::uint64_t>::max());
        progress.RecordWaitResult(slot, result);
        REQUIRE(result == VK_SUCCESS);
    }

    struct StressBatch
    {
        std::array<std::vector<std::byte>, 3> ranges;
        std::vector<std::byte> expected;
        std::array<VkDeviceSize, 3> alignments{1, 8, 16};
    };

    StressBatch MakeStressBatch(const std::size_t batch)
    {
        StressBatch result;
        const std::array<std::size_t, 3> sizes{3 + (batch % 5) * 2, 8, 17 + (batch % 7) * 2};
        for (std::size_t range = 0; range < result.ranges.size(); ++range)
        {
            auto& bytes = result.ranges[range];
            bytes.resize(sizes[range]);
            for (std::size_t index = 0; index < bytes.size(); ++index)
            {
                const auto value = (batch * 53U + range * 29U + index * 7U + 3U) & 0xffU;
                bytes[index] = static_cast<std::byte>(value);
            }
            result.expected.insert(result.expected.end(), bytes.begin(), bytes.end());
        }
        return result;
    }
} // namespace

TEST_CASE("Upload arena empty owners reject storage operations", "[vulkan][upload-arena]")
{
    static_assert(!std::is_copy_constructible_v<VulkanUploadArena>);
    static_assert(std::is_nothrow_move_constructible_v<VulkanUploadArena>);
    static_assert(std::is_nothrow_move_assignable_v<VulkanUploadArena>);
    VulkanUploadArena arena;
    VulkanAllocator allocator;
    VulkanFrameResources frames;
    std::string error;
    const std::array bytes{std::byte{1}};
    CHECK_FALSE(arena.IsValid());
    CHECK(arena.Capacity() == 0);
    CHECK(arena.UsedBytes() == 0);
    CHECK(arena.LastUseSerial() == 0);
    CHECK(arena.MemoryProperties() == 0);
    CHECK_FALSE(arena.CanBegin(0));
    CHECK_FALSE(arena.Begin(0, error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(arena.Append(bytes, 1, error));
    CHECK_FALSE(arena.Seal());
    CHECK_FALSE(arena.CanSubmit(1));
    CHECK_FALSE(VulkanUploadArena::Create(allocator, 32, error));
    CHECK_FALSE(frames.CreateUploadArenas(allocator, 32, error));
}

TEST_CASE("Frame upload storage is opt in and rejects incompatible or repeated creation",
          "[vulkan][upload-arena][gpu]")
{
    if (!owl::tests::IsGpuTestEnabled())
        SKIP("Set OWL_RUN_VULKAN_BOOTSTRAP_TEST=1 to enable GPU tests");
    std::string error;
    auto context = owl::tests::CreateGpuContext(error);
    INFO(error);
    REQUIRE(context);
    const auto baseline = AllocationStatistics(*context->allocator);
    {
        VulkanFrameResources frames;
        CHECK_FALSE(frames.CreateUploadArenas(*context->allocator, 32, error));
        REQUIRE(frames.Initialize(context->device->Get(),
                                  *context->device->QueueFamilies().graphicsFamily, error));
        for (const auto& slot : frames.slots)
            CHECK_FALSE(slot.uploadArena);
        CHECK(AllocationStatistics(*context->allocator).allocationCount ==
              baseline.allocationCount);
        CHECK_FALSE(VulkanUploadArena::Create(*context->allocator, 0, error));
        CHECK_FALSE(frames.CreateUploadArenas(*context->allocator, 0, error));
        VulkanAllocator empty;
        CHECK_FALSE(frames.CreateUploadArenas(empty, 32, error));

        auto otherDevice = VulkanDevice::Create(*context->selection, error);
        REQUIRE(otherDevice);
        auto otherAllocator =
            VulkanAllocator::Create(context->instance->Get(), *otherDevice, error);
        REQUIRE(otherAllocator);
        CHECK_FALSE(frames.CreateUploadArenas(*otherAllocator, 32, error));
        CHECK(AllocationStatistics(*otherAllocator).allocationCount == 0);
        for (const auto& slot : frames.slots)
            CHECK_FALSE(slot.uploadArena);

        REQUIRE(frames.CreateUploadArenas(*context->allocator, 32, error));
        CHECK(error.empty());
        CHECK(AllocationStatistics(*context->allocator).allocationCount ==
              baseline.allocationCount + frames.slots.size());
        CHECK_FALSE(frames.CreateUploadArenas(*context->allocator, 64, error));
        for (const auto& slot : frames.slots)
        {
            REQUIRE(slot.uploadArena);
            CHECK(slot.uploadArena->IsValid());
            CHECK(slot.uploadArena->Capacity() == 32);
            CHECK(slot.uploadArena->UsedBytes() == 0);
        }
    }
    CHECK(AllocationStatistics(*context->allocator).allocationCount == baseline.allocationCount);
    CHECK(AllocationStatistics(*context->allocator).allocationBytes == baseline.allocationBytes);
}

TEST_CASE("Upload arenas preserve GPU payloads across exhaustion pending moves and completed reuse",
          "[vulkan][upload-arena][gpu]")
{
    if (!owl::tests::IsGpuTestEnabled())
        SKIP("Set OWL_RUN_VULKAN_BOOTSTRAP_TEST=1 to enable GPU tests");
    std::string error;
    auto context = owl::tests::CreateGpuContext(error);
    INFO(error);
    REQUIRE(context);
    // Empty destinations precede every child so native wrapper moves preserve teardown order.
    VulkanDevice movedDevice;
    VulkanAllocator movedAllocator;
    const auto device = context->device->Get();
    const auto queue = context->device->GraphicsQueue();
    const auto baseline = AllocationStatistics(*context->allocator);
    {
        VulkanFrameResources frames;
        REQUIRE(frames.Initialize(device, *context->device->QueueFamilies().graphicsFamily, error));
        REQUIRE(frames.CreateUploadArenas(*context->allocator, 32, error));
        std::array<std::optional<VulkanBuffer>, 2> readbacks;
        for (auto& readback : readbacks)
        {
            readback = VulkanBuffer::Create(*context->allocator,
                                            {.size = 32,
                                             .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                             .requiredMemory = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                             .hostAccess = BufferHostAccess::Random,
                                             .persistentMapping = true},
                                            error);
            REQUIRE(readback);
        }
        std::optional<VulkanUploadArena> movedArena;
        VulkanSubmissionProgress progress;
        SubmissionDrainGuard guard{device, queue, frames, progress};
        auto& first = *frames.slots[0].uploadArena;
        auto& second = *frames.slots[1].uploadArena;
        owl::foundation::LogMessage(owl::foundation::LogLevel::Info, "Vulkan",
                                    "Upload arena memory flags=" +
                                        std::to_string(first.MemoryProperties()));
        CHECK((first.MemoryProperties() & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0);
        const std::array a{std::byte{1}, std::byte{2}, std::byte{3}};
        const std::array b{std::byte{4}, std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8}};
        const std::array<std::byte, 16> c{
            std::byte{9},  std::byte{10}, std::byte{11}, std::byte{12},
            std::byte{13}, std::byte{14}, std::byte{15}, std::byte{16},
            std::byte{17}, std::byte{18}, std::byte{19}, std::byte{20},
            std::byte{21}, std::byte{22}, std::byte{23}, std::byte{24}};
        std::array<std::byte, 32> other;
        other.fill(std::byte{0xa5});
        CHECK_FALSE(first.Append(a, 1, error));
        REQUIRE(first.Begin(0, error));
        CHECK(error.empty());
        CHECK_FALSE(first.Seal());
        CHECK_FALSE(first.Append({}, 1, error));
        CHECK_FALSE(first.Append(a, 0, error));
        CHECK_FALSE(first.Append(a, 3, error));
        const auto sa = first.Append(a, 1, error);
        const auto sb = first.Append(b, 8, error);
        const auto sc = first.Append(c, 16, error);
        REQUIRE(sa);
        REQUIRE(sb);
        REQUIRE(sc);
        CHECK(sa->offset == 0);
        CHECK(sb->offset == 8);
        CHECK(sc->offset == 16);
        CHECK(sa->size == 3);
        CHECK(sb->size == 5);
        CHECK(sc->size == 16);
        CHECK(first.UsedBytes() == 32);
        CHECK_FALSE(first.Append(a, 1, error));
        CHECK(first.UsedBytes() == 32);
        REQUIRE(first.Seal());
        CHECK_FALSE(first.Seal());
        CHECK_FALSE(first.Append(a, 1, error));
        CHECK_FALSE(first.CanSubmit(0));
        REQUIRE(second.Begin(0, error));
        const auto so = second.Append(other, 1, error);
        REQUIRE(so);
        CHECK(so->buffer != sa->buffer);
        const auto transferredBuffer = so->buffer;
        REQUIRE(second.Seal());
        CopyAndSubmit(device, queue, frames.slots[0], progress, std::array{*sa, *sb, *sc},
                      *readbacks[0]);
        CopyAndSubmit(device, queue, frames.slots[1], progress, std::array{*so}, *readbacks[1]);
        CHECK(progress.CompletedSerial() == 0);
        CHECK_FALSE(first.Begin(0, error));
        CHECK_FALSE(second.Begin(0, error));
        CHECK(first.UsedBytes() == 32);
        CHECK(first.LastUseSerial() == 1);
        CHECK(second.LastUseSerial() == 2);

        movedArena.emplace(std::move(first));
        CHECK_FALSE(first.IsValid());
        CHECK(first.Capacity() == 0);
        CHECK(first.UsedBytes() == 0);
        CHECK(first.LastUseSerial() == 0);
        CHECK_FALSE(first.CanBegin(0));
        CHECK(movedArena->LastUseSerial() == 1);
        CHECK(movedArena->UsedBytes() == 32);
        CHECK_FALSE(movedArena->CanBegin(0));
        first =
            std::move(*movedArena); // The destination is empty; pending source identity survives.
        CHECK_FALSE(movedArena->IsValid());
        CHECK(first.LastUseSerial() == 1);
        movedDevice = std::move(*context->device);
        movedAllocator = std::move(*context->allocator);
        CHECK_FALSE(context->device->IsValid());
        CHECK_FALSE(context->allocator->IsValid());
        CHECK(movedAllocator.Device() == movedDevice.Get());

        WaitForSlot(device, frames.slots[0], progress);
        CHECK(first.CanBegin(progress.CompletedSerial()));
        CHECK_FALSE(second.CanBegin(progress.CompletedSerial()));
        WaitForSlot(device, frames.slots[1], progress);
        std::array<std::byte, 24> actualFirst{};
        std::array<std::byte, 32> actualSecond{};
        REQUIRE(readbacks[0]->Read(0, actualFirst, error));
        REQUIRE(readbacks[1]->Read(0, actualSecond, error));
        for (std::size_t i = 0; i < actualFirst.size(); ++i)
            CHECK(actualFirst[i] == static_cast<std::byte>(i + 1));
        CHECK(actualSecond == other);

        // Completion alone does not discard executable command references. Reset both pools
        // before replacing the first arena or reusing the second arena's storage.
        for (auto& slot : frames.slots)
            REQUIRE(vkResetCommandPool(device, slot.commandPool, 0) == VK_SUCCESS);
        const auto beforeReplacement = AllocationStatistics(movedAllocator).allocationCount;
        first = std::move(second);
        CHECK_FALSE(second.IsValid());
        CHECK(first.LastUseSerial() == 2);
        CHECK(AllocationStatistics(movedAllocator).allocationCount == beforeReplacement - 1);
        REQUIRE(first.Begin(progress.CompletedSerial(), error));
        CHECK(first.UsedBytes() == 0);
        const auto reused = first.Append(a, 1, error);
        REQUIRE(reused);
        CHECK(reused->buffer == transferredBuffer);
        CHECK(reused->offset == 0);
        REQUIRE(first.Seal());
        CopyAndSubmit(device, queue, frames.slots[0], progress, std::array{*reused}, *readbacks[0]);
        CHECK(first.LastUseSerial() == 3);
        CHECK_FALSE(first.CanBegin(2));
        WaitForSlot(device, frames.slots[0], progress);
        std::array<std::byte, 3> actualReused{};
        REQUIRE(readbacks[0]->Read(0, actualReused, error));
        CHECK(actualReused == a);
        CHECK(first.CanBegin(3));
        REQUIRE(vkResetCommandPool(device, frames.slots[0].commandPool, 0) == VK_SUCCESS);
    }
    CHECK(AllocationStatistics(movedAllocator).allocationCount == baseline.allocationCount);
    CHECK(AllocationStatistics(movedAllocator).allocationBytes == baseline.allocationBytes);
}

TEST_CASE("Upload arenas preserve deterministic payloads across 64 dual-slot submissions",
          "[vulkan][upload-arena][gpu][stress]")
{
    if (!owl::tests::IsGpuTestEnabled())
        SKIP("Set OWL_RUN_VULKAN_BOOTSTRAP_TEST=1 to enable GPU tests");
    std::string error;
    auto context = owl::tests::CreateGpuContext(error);
    INFO(error);
    REQUIRE(context);
    const auto device = context->device->Get();
    const auto queue = context->device->GraphicsQueue();
    const auto baseline = AllocationStatistics(*context->allocator);
    constexpr VkDeviceSize arenaCapacity = 256;
    constexpr std::size_t batchCount = 64;

    {
        VulkanFrameResources frames;
        REQUIRE(frames.Initialize(device, *context->device->QueueFamilies().graphicsFamily, error));
        REQUIRE(frames.CreateUploadArenas(*context->allocator, arenaCapacity, error));
        std::array<std::optional<VulkanBuffer>, 2> readbacks;
        for (auto& readback : readbacks)
        {
            readback = VulkanBuffer::Create(*context->allocator,
                                            {.size = arenaCapacity,
                                             .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                             .requiredMemory = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                             .hostAccess = BufferHostAccess::Random,
                                             .persistentMapping = true},
                                            error);
            REQUIRE(readback);
        }

        VulkanSubmissionProgress progress;
        SubmissionDrainGuard guard{device, queue, frames, progress};
        const auto steadyAllocations = AllocationStatistics(*context->allocator);
        std::array<std::vector<std::byte>, 2> expected;
        std::array<VkBuffer, 2> sourceIdentity{};
        std::array<std::uint64_t, 2> staleCompletion{};
        std::array<bool, 2> hasIdentity{};

        for (std::size_t batch = 0; batch < batchCount; ++batch)
        {
            const auto slotIndex = batch % frames.slots.size();
            auto& slot = frames.slots[slotIndex];
            REQUIRE(slot.uploadArena);

            if (slot.submissionPending)
            {
                // This is deliberately the completion observed before the current batch was
                // submitted. It must not release the still-owned source range.
                const auto fenceStatus = vkGetFenceStatus(device, slot.complete);
                CHECK((fenceStatus == VK_SUCCESS || fenceStatus == VK_NOT_READY));
                const auto usedBeforeStaleBegin = slot.uploadArena->UsedBytes();
                const auto serialBeforeStaleBegin = slot.uploadArena->LastUseSerial();
                std::string staleError;
                CHECK_FALSE(slot.uploadArena->Begin(staleCompletion[slotIndex], staleError));
                CHECK_FALSE(staleError.empty());
                CHECK(slot.uploadArena->UsedBytes() == usedBeforeStaleBegin);
                CHECK(slot.uploadArena->LastUseSerial() == serialBeforeStaleBegin);
                CHECK_FALSE(slot.uploadArena->CanBegin(staleCompletion[slotIndex]));
                WaitForSlot(device, slot, progress);
                // GPU completion is now certain. An obsolete caller-supplied serial must
                // still reject reuse; readiness comes from the supplied progress evidence.
                staleError.clear();
                CHECK_FALSE(slot.uploadArena->Begin(staleCompletion[slotIndex], staleError));
                CHECK_FALSE(staleError.empty());
                CHECK(slot.uploadArena->UsedBytes() == usedBeforeStaleBegin);
                CHECK(slot.uploadArena->LastUseSerial() == serialBeforeStaleBegin);
                CHECK(slot.uploadArena->CanBegin(progress.CompletedSerial()));
                REQUIRE_FALSE(expected[slotIndex].empty());
                std::vector<std::byte> actual(expected[slotIndex].size());
                REQUIRE(readbacks[slotIndex]->Read(0, actual, error));
                CHECK(actual == expected[slotIndex]);
                REQUIRE(vkResetCommandPool(device, slot.commandPool, 0) == VK_SUCCESS);
            }

            staleCompletion[slotIndex] = progress.CompletedSerial();
            REQUIRE(slot.uploadArena->Begin(progress.CompletedSerial(), error));
            const auto payload = MakeStressBatch(batch);
            std::array<UploadSlice, 3> slices{};
            for (std::size_t range = 0; range < payload.ranges.size(); ++range)
            {
                const auto slice = slot.uploadArena->Append(payload.ranges[range],
                                                            payload.alignments[range], error);
                REQUIRE(slice);
                slices[range] = *slice;
            }
            REQUIRE(slot.uploadArena->Seal());
            REQUIRE(slot.uploadArena->UsedBytes() <= arenaCapacity);
            if (!hasIdentity[slotIndex])
            {
                sourceIdentity[slotIndex] = slices.front().buffer;
                hasIdentity[slotIndex] = true;
            }
            else
            {
                CHECK(slices.front().buffer == sourceIdentity[slotIndex]);
            }

            // The first two iterations are submitted before either fence is waited. Every
            // later iteration waits only when reusing its own slot; no steady-state idle wait.
            CopyAndSubmit(device, queue, slot, progress, slices, *readbacks[slotIndex]);
            CHECK_FALSE(slot.uploadArena->CanBegin(staleCompletion[slotIndex]));
            expected[slotIndex] = payload.expected;
            CHECK(slot.uploadArena->Capacity() == arenaCapacity);
            // Check live allocations separately from VMA's reserved/cached blocks.
            const auto activeAllocations = AllocationStatistics(*context->allocator);
            CHECK(activeAllocations.allocationCount == steadyAllocations.allocationCount);
            CHECK(activeAllocations.allocationBytes == steadyAllocations.allocationBytes);
        }

        for (std::size_t slotIndex = 0; slotIndex < frames.slots.size(); ++slotIndex)
        {
            auto& slot = frames.slots[slotIndex];
            REQUIRE(slot.submissionPending);
            WaitForSlot(device, slot, progress);
            std::vector<std::byte> actual(expected[slotIndex].size());
            REQUIRE(readbacks[slotIndex]->Read(0, actual, error));
            CHECK(actual == expected[slotIndex]);
            CHECK(slot.uploadArena->CanBegin(progress.CompletedSerial()));
        }
        CHECK(progress.SubmittedSerial() == batchCount);
        CHECK(progress.CompletedSerial() == batchCount);
        for (const auto& slot : frames.slots)
        {
            REQUIRE(slot.uploadArena);
            CHECK(slot.uploadArena->Capacity() == arenaCapacity);
            CHECK_FALSE(slot.submissionPending);
        }
    }
    CHECK(AllocationStatistics(*context->allocator).allocationCount == baseline.allocationCount);
    CHECK(AllocationStatistics(*context->allocator).allocationBytes == baseline.allocationBytes);
}
