#include "VulkanImageUpload.h"
#include "VulkanAllocator.h"
#include "VulkanDevice.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <limits>

namespace
{
    using namespace owl::vulkan;
    constexpr VulkanImageDesc Desc{
        .extent = {7, 3},
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .mipLevels = 1,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
    };
} // namespace

TEST_CASE("Image upload sizing accepts tightly packed RGBA8 rows", "[vulkan][image-upload]")
{
    auto desc = Desc;
    CHECK(detail::ImageUploadByteSize(desc) == 84);
    desc.format = VK_FORMAT_R8G8B8A8_SRGB;
    desc.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    CHECK(detail::ImageUploadByteSize(desc) == 84);
    desc.extent = {1, 1};
    CHECK(detail::ImageUploadByteSize(desc) == 4);
    desc.extent = {17, 9};
    CHECK(detail::ImageUploadByteSize(desc) == 612);
}

TEST_CASE("Image upload sizing rejects unsupported descriptions and overflow", "[vulkan][image-upload]")
{
    auto desc = Desc;
    SECTION("zero dimension") { desc.extent.height = 0; }
    SECTION("unsupported format") { desc.format = VK_FORMAT_R8_UNORM; }
    SECTION("multiple mips") { desc.mipLevels = 2; }
    SECTION("no mips") { desc.mipLevels = 0; }
    SECTION("no transfer destination") { desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT; }
    SECTION("no sampling") { desc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT; }
    SECTION("unsupported usage") { desc.usage |= VK_IMAGE_USAGE_STORAGE_BIT; }
    SECTION("RGBA multiplication overflows")
    {
        desc.extent = {std::numeric_limits<std::uint32_t>::max(),
                       std::numeric_limits<std::uint32_t>::max()};
    }
    CHECK_FALSE(detail::ImageUploadByteSize(desc));
}

TEST_CASE("Image upload completion policy retains unresolved work", "[vulkan][image-upload]")
{
    using State = detail::ImageUploadState;
    using detail::StateAfterImageUploadSubmit;
    using detail::StateAfterImageUploadWait;
    CHECK(StateAfterImageUploadSubmit(State::NotSubmitted, VK_SUCCESS) == State::Pending);
    CHECK(StateAfterImageUploadSubmit(State::NotSubmitted, VK_ERROR_OUT_OF_HOST_MEMORY) ==
          State::NotSubmitted);
    CHECK(StateAfterImageUploadSubmit(State::NotSubmitted, VK_ERROR_DEVICE_LOST) == State::DeviceLost);
    CHECK(StateAfterImageUploadWait(State::Pending, VK_TIMEOUT) == State::Pending);
    CHECK(StateAfterImageUploadWait(State::Pending, VK_ERROR_OUT_OF_HOST_MEMORY) == State::Pending);
    CHECK(StateAfterImageUploadWait(State::Pending, VK_SUCCESS) == State::Completed);
    CHECK(StateAfterImageUploadWait(State::Pending, VK_ERROR_DEVICE_LOST) == State::DeviceLost);
    CHECK(StateAfterImageUploadWait(State::NotSubmitted, VK_SUCCESS) == State::NotSubmitted);
    CHECK(StateAfterImageUploadSubmit(State::Completed, VK_SUCCESS) == State::Completed);
    CHECK(StateAfterImageUploadSubmit(State::Pending, VK_SUCCESS) == State::Pending);
    CHECK(StateAfterImageUploadWait(State::DeviceLost, VK_SUCCESS) == State::DeviceLost);
}

TEST_CASE("Image upload empty owners reject submission and handoff", "[vulkan][image-upload]")
{
    VulkanDevice device;
    VulkanAllocator allocator;
    std::array<std::byte, 84> bytes{};
    std::string error;
    CHECK_FALSE(VulkanImageUpload::Create(device, allocator, Desc, bytes, error));
    CHECK_FALSE(error.empty());
    VulkanImageUpload upload;
    CHECK_FALSE(upload.IsPending());
    CHECK(upload.Submit(error) == VK_NOT_READY);
    CHECK_FALSE(error.empty());
    CHECK(upload.Wait(error) == VK_NOT_READY);
    CHECK_FALSE(error.empty());
    CHECK_FALSE(upload.TakeDestination(error));
    CHECK_FALSE(error.empty());
    CHECK(upload.DrainForDestruction() == VK_SUCCESS);
}
