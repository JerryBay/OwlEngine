#include "VulkanBufferUpload.h"

#include "VulkanDevice.h"
#include "VulkanDeviceSelection.h"
#include "VulkanInstance.h"
#include "VulkanSurface.h"

#include <catch2/catch_test_macros.hpp>

#include <owl/platform/Platform.h>
#include <owl/foundation/Log.h>

#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    using owl::vulkan::detail::BufferUploadState;
    using owl::vulkan::detail::BufferUploadValidation;

    constexpr VkPipelineStageFlags2 CopyStage = VK_PIPELINE_STAGE_2_COPY_BIT;
    constexpr VkAccessFlags2 CopyRead = VK_ACCESS_2_TRANSFER_READ_BIT;

    struct GpuContext
    {
        std::optional<owl::platform::Platform> platform;
        std::optional<owl::platform::Window> window;
        std::optional<owl::vulkan::VulkanInstance> instance;
        std::optional<owl::vulkan::VulkanSurface> surface;
        std::optional<owl::vulkan::VulkanDeviceSelection> selection;
        std::optional<owl::vulkan::VulkanDevice> device;

        GpuContext() = default;
        GpuContext(const GpuContext&) = delete;
        GpuContext& operator=(const GpuContext&) = delete;
        GpuContext(GpuContext&&) noexcept = default;
        GpuContext& operator=(GpuContext&&) noexcept = default;

        ~GpuContext()
        {
            if (device && device->IsValid())
                static_cast<void>(vkDeviceWaitIdle(device->Get()));
        }
    };

    std::optional<GpuContext> CreateGpuContext(std::string& error)
    {
        GpuContext result;
        result.platform = owl::platform::Platform::Create(error);
        if (!result.platform)
            return std::nullopt;
        const owl::platform::WindowDesc windowDesc{
            .title = "OwlEngine - Vulkan Buffer Upload Test",
            .width = 320,
            .height = 180,
            .resizable = false,
            .surfaceApi = owl::platform::WindowSurfaceApi::Vulkan,
        };
        result.window = result.platform->CreateWindow(windowDesc, error);
        if (!result.window)
            return std::nullopt;
        result.instance = owl::vulkan::VulkanInstance::Create(error);
        if (!result.instance)
            return std::nullopt;
        result.surface = owl::vulkan::VulkanSurface::Create(*result.instance, *result.window, error);
        if (!result.surface)
            return std::nullopt;
        result.selection = owl::vulkan::VulkanDeviceSelection::Select(
            result.instance->Get(), result.surface->Get(), error);
        if (!result.selection)
            return std::nullopt;
        result.device = owl::vulkan::VulkanDevice::Create(
            *result.selection, error, result.instance->PresentationSupport());
        if (!result.device)
            return std::nullopt;
        error.clear();
        return std::optional<GpuContext>{std::move(result)};
    }

    bool IsGpuTestEnabled()
    {
        const char* enabled = SDL_getenv("OWL_RUN_VULKAN_BOOTSTRAP_TEST");
        return enabled != nullptr && std::string_view{enabled} == "1";
    }

    std::vector<std::byte> Pattern(const std::size_t size)
    {
        std::vector<std::byte> result(size);
        for (std::size_t index = 0; index < size; ++index)
            result[index] = static_cast<std::byte>((index * 37U + 11U) & 0xffU);
        return result;
    }

    std::vector<std::byte> TrianglePayload()
    {
        struct Vertex
        {
            float position[2];
            float color[3];
        };
        constexpr std::array<Vertex, 3> vertices{{
            {{0.0F, -0.6F}, {1.0F, 0.0F, 0.0F}},
            {{0.6F, 0.6F}, {0.0F, 1.0F, 0.0F}},
            {{-0.6F, 0.6F}, {0.0F, 0.0F, 1.0F}},
        }};
        constexpr std::array<std::uint16_t, 3> indices{0, 1, 2};
        std::vector<std::byte> result;
        const auto vertexBytes = std::as_bytes(std::span{vertices});
        const auto indexBytes = std::as_bytes(std::span{indices});
        result.insert(result.end(), vertexBytes.begin(), vertexBytes.end());
        result.insert(result.end(), indexBytes.begin(), indexBytes.end());
        return result;
    }

    bool WaitForFence(const VkDevice device, const VkFence fence, std::string& error)
    {
        const VkResult result = vkWaitForFences(
            device, 1, &fence, VK_TRUE, std::numeric_limits<std::uint64_t>::max());
        if (result == VK_SUCCESS)
            return true;
        error = "vkWaitForFences(readback) failed with VkResult " +
                std::to_string(static_cast<int>(result));
        return false;
    }

    bool Readback(const owl::vulkan::VulkanDevice& device,
                  const owl::vulkan::VulkanBuffer& source, std::span<std::byte> output,
                  VkMemoryPropertyFlags& memoryProperties, std::string& error)
    {
        const owl::vulkan::VulkanBufferDesc desc{
            .size = output.size(),
            .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .requiredMemory = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            .preferredMemory = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        };
        auto readback = owl::vulkan::VulkanBuffer::Create(device, desc, error);
        if (!readback)
            return false;
        memoryProperties = readback->MemoryProperties();
        owl::foundation::LogMessage(
            owl::foundation::LogLevel::Info, "Vulkan",
            "Buffer readback memory flags=" + std::to_string(memoryProperties));
        const auto families = device.QueueFamilies();
        if (!families.graphicsFamily)
        {
            error = "Readback requires a graphics queue family";
            return false;
        }
        const VkCommandPoolCreateInfo poolInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
            .queueFamilyIndex = *families.graphicsFamily,
        };
        VkCommandPool pool = VK_NULL_HANDLE;
        if (vkCreateCommandPool(device.Get(), &poolInfo, nullptr, &pool) != VK_SUCCESS)
        {
            error = "vkCreateCommandPool(readback) failed";
            return false;
        }
        const VkCommandBufferAllocateInfo allocateInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        VkCommandBuffer command = VK_NULL_HANDLE;
        if (vkAllocateCommandBuffers(device.Get(), &allocateInfo, &command) != VK_SUCCESS)
        {
            vkDestroyCommandPool(device.Get(), pool, nullptr);
            error = "vkAllocateCommandBuffers(readback) failed";
            return false;
        }
        const VkCommandBufferBeginInfo beginInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        if (vkBeginCommandBuffer(command, &beginInfo) != VK_SUCCESS)
        {
            vkDestroyCommandPool(device.Get(), pool, nullptr);
            error = "vkBeginCommandBuffer(readback) failed";
            return false;
        }
        const VkBufferCopy region{.srcOffset = 0, .dstOffset = 0, .size = source.Size()};
        vkCmdCopyBuffer(command, source.Get(), readback->Get(), 1, &region);
        const VkBufferMemoryBarrier2 barrier{
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
            .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = readback->Get(),
            .offset = 0,
            .size = readback->Size(),
        };
        const VkDependencyInfo dependency{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .bufferMemoryBarrierCount = 1,
            .pBufferMemoryBarriers = &barrier,
        };
        vkCmdPipelineBarrier2(command, &dependency);
        if (vkEndCommandBuffer(command) != VK_SUCCESS)
        {
            vkDestroyCommandPool(device.Get(), pool, nullptr);
            error = "vkEndCommandBuffer(readback) failed";
            return false;
        }
        const VkFenceCreateInfo fenceInfo{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VkFence fence = VK_NULL_HANDLE;
        if (vkCreateFence(device.Get(), &fenceInfo, nullptr, &fence) != VK_SUCCESS)
        {
            vkDestroyCommandPool(device.Get(), pool, nullptr);
            error = "vkCreateFence(readback) failed";
            return false;
        }
        const VkCommandBufferSubmitInfo commandInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .commandBuffer = command,
        };
        const VkSubmitInfo2 submit{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = &commandInfo,
        };
        const VkResult submitResult = vkQueueSubmit2(device.GraphicsQueue(), 1, &submit, fence);
        bool success = submitResult == VK_SUCCESS;
        if (!success)
            error = "vkQueueSubmit2(readback) failed with VkResult " +
                    std::to_string(static_cast<int>(submitResult));
        if (success)
            success = WaitForFence(device.Get(), fence, error);
        if (success)
            success = readback->Read(0, output, error);
        if (!success && submitResult == VK_SUCCESS)
            static_cast<void>(vkDeviceWaitIdle(device.Get()));
        vkDestroyFence(device.Get(), fence, nullptr);
        vkDestroyCommandPool(device.Get(), pool, nullptr);
        return success;
    }
} // namespace

TEST_CASE("Buffer upload request policy rejects invalid local inputs", "[vulkan][buffer-upload]")
{
    using owl::vulkan::detail::ValidateBufferUploadArguments;
    constexpr auto usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    CHECK(ValidateBufferUploadArguments(false, false, 1, usage, CopyStage, CopyRead) ==
          BufferUploadValidation::InvalidDevice);
    CHECK(ValidateBufferUploadArguments(true, true, 0, usage, CopyStage, CopyRead) ==
          BufferUploadValidation::EmptyPayload);
    CHECK(ValidateBufferUploadArguments(true, true, 1, 0, CopyStage, CopyRead) ==
          BufferUploadValidation::InvalidUsage);
    CHECK(ValidateBufferUploadArguments(true, true, 1, usage, 0, CopyRead) ==
          BufferUploadValidation::MissingConsumerStage);
    CHECK(ValidateBufferUploadArguments(true, true, 1, usage, CopyStage, 0) ==
          BufferUploadValidation::MissingConsumerAccess);
    CHECK(ValidateBufferUploadArguments(true, false, 1, usage, CopyStage, CopyRead) ==
          BufferUploadValidation::MissingGraphicsQueue);
    CHECK(ValidateBufferUploadArguments(true, true, 1, usage, CopyStage, CopyRead) ==
          BufferUploadValidation::Valid);
}

TEST_CASE("Buffer upload state policy retains unresolved submissions", "[vulkan][buffer-upload]")
{
    using owl::vulkan::detail::StateAfterSubmit;
    using owl::vulkan::detail::StateAfterWait;
    CHECK(StateAfterSubmit(BufferUploadState::NotSubmitted, VK_SUCCESS) ==
          BufferUploadState::Pending);
    CHECK(StateAfterSubmit(BufferUploadState::NotSubmitted, VK_ERROR_DEVICE_LOST) ==
          BufferUploadState::DeviceLost);
    CHECK(StateAfterSubmit(BufferUploadState::NotSubmitted, VK_ERROR_OUT_OF_HOST_MEMORY) ==
          BufferUploadState::NotSubmitted);
    CHECK(StateAfterWait(BufferUploadState::Pending, VK_TIMEOUT) == BufferUploadState::Pending);
    CHECK(StateAfterWait(BufferUploadState::Pending, VK_NOT_READY) == BufferUploadState::Pending);
    CHECK(StateAfterWait(BufferUploadState::Pending, VK_ERROR_DEVICE_LOST) ==
          BufferUploadState::DeviceLost);
    CHECK(StateAfterWait(BufferUploadState::Pending, VK_SUCCESS) == BufferUploadState::Completed);
}

TEST_CASE("Buffer upload rejects early destination transfer", "[vulkan][buffer-upload]")
{
    owl::vulkan::VulkanBufferUpload emptyUpload;
    std::string emptyError;
    CHECK_FALSE(emptyUpload.TakeDestination(emptyError).has_value());
    CHECK_FALSE(emptyError.empty());

    owl::vulkan::VulkanDevice device;
    std::string error;
    const std::array<std::byte, 1> payload{std::byte{0x42}};
    auto upload = owl::vulkan::VulkanBufferUpload::Create(
        device, payload, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, CopyStage, CopyRead, error);
    CHECK_FALSE(upload.has_value());
    CHECK_FALSE(error.empty());
}

TEST_CASE("Vulkan buffer upload rejects destination transfer before completion",
          "[vulkan][buffer-upload][integration]")
{
    if (!IsGpuTestEnabled())
        SKIP("Set OWL_RUN_VULKAN_BOOTSTRAP_TEST=1 to run the local GPU upload test");

    std::string error;
    auto context = CreateGpuContext(error);
    INFO(error);
    REQUIRE(context.has_value());
    REQUIRE(context->device.has_value());
    const std::array<std::byte, 1> payload{std::byte{0x42}};
    auto upload = owl::vulkan::VulkanBufferUpload::Create(
        *context->device, payload, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, CopyStage, CopyRead, error);
    INFO(error);
    REQUIRE(upload.has_value());
    CHECK_FALSE(upload->IsPending());
    CHECK_FALSE(upload->TakeDestination(error).has_value());
    CHECK_FALSE(error.empty());
}

TEST_CASE("Vulkan buffer host mapping and moves locally", "[vulkan][buffer][integration]")
{
    if (!IsGpuTestEnabled())
        SKIP("Set OWL_RUN_VULKAN_BOOTSTRAP_TEST=1 to run the local GPU buffer test");

    std::string error;
    auto context = CreateGpuContext(error);
    INFO(error);
    REQUIRE(context.has_value());
    REQUIRE(context->device.has_value());
    const auto payload = Pattern(37);
    const owl::vulkan::VulkanBufferDesc desc{
        .size = payload.size(),
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .requiredMemory = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        .preferredMemory = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
    };
    auto source = owl::vulkan::VulkanBuffer::Create(*context->device, desc, error);
    INFO(error);
    REQUIRE(source.has_value());
    REQUIRE(source->Write(0, payload, error));
    std::vector<std::byte> actual(payload.size());
    REQUIRE(source->Read(0, actual, error));
    CHECK(actual == payload);

    owl::vulkan::VulkanBuffer moved{std::move(*source)};
    CHECK_FALSE(source->IsValid());
    REQUIRE(moved.IsValid());
    std::fill(actual.begin(), actual.end(), std::byte{});
    REQUIRE(moved.Read(0, actual, error));
    CHECK(actual == payload);

    auto replacement = owl::vulkan::VulkanBuffer::Create(*context->device, desc, error);
    INFO(error);
    REQUIRE(replacement.has_value());
    *replacement = std::move(moved);
    CHECK_FALSE(moved.IsValid());
    REQUIRE(replacement->IsValid());
    std::fill(actual.begin(), actual.end(), std::byte{});
    REQUIRE(replacement->Read(0, actual, error));
    CHECK(actual == payload);
}

TEST_CASE("Vulkan buffer upload roundtrips exact byte sizes", "[vulkan][buffer-upload][integration]")
{
    if (!IsGpuTestEnabled())
        SKIP("Set OWL_RUN_VULKAN_BOOTSTRAP_TEST=1 to run the local GPU upload test");

    std::string error;
    auto context = CreateGpuContext(error);
    INFO(error);
    REQUIRE(context.has_value());
    REQUIRE(context->device.has_value());

    const std::array<std::size_t, 8> sizes{1, 3, 4, 65, 66, 67, 4097, 66};
    for (std::size_t caseIndex = 0; caseIndex < sizes.size(); ++caseIndex)
    {
        const auto size = sizes[caseIndex];
        const auto payload = caseIndex == sizes.size() - 1 ? TrianglePayload() : Pattern(size);
        REQUIRE(payload.size() == size);
        auto upload = owl::vulkan::VulkanBufferUpload::Create(
            *context->device, payload, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, CopyStage, CopyRead,
            error);
        INFO(error);
        REQUIRE(upload.has_value());
        CHECK_FALSE(upload->TakeDestination(error).has_value());
        CHECK_FALSE(error.empty());
        owl::foundation::LogMessage(
            owl::foundation::LogLevel::Info, "Vulkan",
            "Buffer upload memory flags: staging=" +
                std::to_string(upload->StagingMemoryProperties()));
        const VkResult uploadResult = upload->SubmitAndWait(error);
        if (uploadResult != VK_SUCCESS)
        {
            if (upload->IsPending())
            {
                static_cast<void>(vkDeviceWaitIdle(context->device->Get()));
                static_cast<void>(upload->Wait(error));
            }
            if (upload->IsPending())
                std::terminate();
            CHECK(uploadResult == VK_SUCCESS);
            return;
        }
        auto destination = upload->TakeDestination(error);
        INFO(error);
        REQUIRE(destination.has_value());
        CHECK_FALSE(upload->TakeDestination(error).has_value());
        CHECK_FALSE(error.empty());
        std::vector<std::byte> actual(size);
        VkMemoryPropertyFlags readbackMemoryProperties = 0;
        CHECK(Readback(*context->device, *destination, actual, readbackMemoryProperties, error));
        INFO(error);
        CHECK(actual == payload);
        if (destination)
        {
            owl::foundation::LogMessage(
                owl::foundation::LogLevel::Info, "Vulkan",
                "Buffer upload memory flags: destination=" +
                    std::to_string(destination->MemoryProperties()));
            INFO("destination memory flags: " +
                 std::to_string(destination->MemoryProperties()));
            INFO("readback memory flags: " + std::to_string(readbackMemoryProperties));
            INFO("non-coherent readback: " +
                 std::string{(readbackMemoryProperties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
                                 ? "false"
                                 : "true"});
        }
    }
}
