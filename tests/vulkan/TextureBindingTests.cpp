#include "GpuTestContext.h"
#include "VulkanResourceRetirement.h"
#include "VulkanTextureBinding.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <type_traits>
#include <utility>

namespace
{
    using namespace owl::vulkan;

    static_assert(!std::is_copy_constructible_v<VulkanTextureBinding>);
    static_assert(!std::is_move_constructible_v<VulkanTextureBinding>);
    static_assert(!std::is_copy_assignable_v<VulkanTextureBinding>);
    static_assert(!std::is_move_assignable_v<VulkanTextureBinding>);
    static_assert(std::is_nothrow_destructible_v<VulkanTextureBinding>);

    constexpr VulkanImageDesc ImageDesc{
        .extent = {16, 8},
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .mipLevels = 5,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
    };

    VmaStatistics AllocationStatistics(const VulkanAllocator& allocator)
    {
        VmaTotalStatistics stats{};
        vmaCalculateStatistics(allocator.Get(), &stats);
        return stats.total.statistics;
    }

    // Test-only command recording checks descriptor lifetime; nothing is submitted.
    struct BindingFixture
    {
        VkDevice device;
        VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;

        explicit BindingFixture(const VkDevice parent) noexcept : device(parent) {}
        BindingFixture(const BindingFixture&) = delete;
        BindingFixture& operator=(const BindingFixture&) = delete;
        ~BindingFixture()
        {
            vkDestroyCommandPool(device, commandPool, nullptr);
            vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
        }

        void Initialize(const std::uint32_t graphicsFamily)
        {
            const VkDescriptorSetLayoutBinding binding{
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            };
            const VkDescriptorSetLayoutCreateInfo layoutInfo{
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .bindingCount = 1,
                .pBindings = &binding,
            };
            REQUIRE(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorLayout) ==
                    VK_SUCCESS);
            const VkPipelineLayoutCreateInfo pipelineInfo{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .setLayoutCount = 1,
                .pSetLayouts = &descriptorLayout,
            };
            REQUIRE(vkCreatePipelineLayout(device, &pipelineInfo, nullptr, &pipelineLayout) ==
                    VK_SUCCESS);
            const VkCommandPoolCreateInfo poolInfo{
                .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                .queueFamilyIndex = graphicsFamily,
            };
            REQUIRE(vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) == VK_SUCCESS);
        }

        void RecordBinding(const VkDescriptorSet set) const
        {
            const VkCommandBufferAllocateInfo allocate{
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                .commandPool = commandPool,
                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                .commandBufferCount = 1,
            };
            VkCommandBuffer command = VK_NULL_HANDLE;
            REQUIRE(vkAllocateCommandBuffers(device, &allocate, &command) == VK_SUCCESS);
            const VkCommandBufferBeginInfo begin{
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            };
            REQUIRE(vkBeginCommandBuffer(command, &begin) == VK_SUCCESS);
            vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1,
                                    &set, 0, nullptr);
            REQUIRE(vkEndCommandBuffer(command) == VK_SUCCESS);
            vkFreeCommandBuffers(device, commandPool, 1, &command);
        }
    };
} // namespace

TEST_CASE("Texture bindings reject empty parents before native creation",
          "[vulkan][texture-binding]")
{
    VulkanDevice device;
    VulkanImage image;
    std::string error;
    CHECK_FALSE(VulkanTextureBinding::Create(device, VK_NULL_HANDLE, image, error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(image.IsValid());
}

TEST_CASE("Texture bindings preserve input ownership across rejected creation",
          "[vulkan][texture-binding][integration]")
{
    if (!owl::tests::IsGpuTestEnabled())
        SKIP("Set OWL_RUN_VULKAN_BOOTSTRAP_TEST=1 to run texture binding ownership tests");
    std::string error;
    auto context = owl::tests::CreateGpuContext(error);
    INFO(error);
    REQUIRE(context);
    BindingFixture fixture{context->device->Get()};
    fixture.Initialize(*context->device->QueueFamilies().graphicsFamily);
    auto image = VulkanImage::Create(*context->allocator, ImageDesc, error);
    REQUIRE(image);
    const auto handle = image->Get();
    const auto allocated = AllocationStatistics(*context->allocator);

    CHECK_FALSE(VulkanTextureBinding::Create(*context->device, VK_NULL_HANDLE, *image, error));
    CHECK_FALSE(error.empty());
    CHECK(image->Get() == handle);
    VulkanDevice emptyDevice;
    CHECK_FALSE(VulkanTextureBinding::Create(emptyDevice, fixture.descriptorLayout, *image, error));
    CHECK_FALSE(error.empty());
    CHECK(image->Get() == handle);
    VulkanImage emptyImage;
    CHECK_FALSE(VulkanTextureBinding::Create(*context->device, fixture.descriptorLayout, emptyImage,
                                             error));
    CHECK_FALSE(error.empty());
    auto otherDevice = VulkanDevice::Create(*context->selection, error);
    REQUIRE(otherDevice);
    REQUIRE(otherDevice->Get() != context->device->Get());
    CHECK_FALSE(
        VulkanTextureBinding::Create(*otherDevice, fixture.descriptorLayout, *image, error));
    CHECK_FALSE(error.empty());
    CHECK(image->Get() == handle);
    CHECK(AllocationStatistics(*context->allocator).allocationCount == allocated.allocationCount);
    CHECK(AllocationStatistics(*context->allocator).allocationBytes == allocated.allocationBytes);

    auto binding =
        VulkanTextureBinding::Create(*context->device, fixture.descriptorLayout, *image, error);
    INFO(error);
    REQUIRE(binding);
    CHECK(error.empty());
    CHECK_FALSE(image->IsValid());
    CHECK(binding->DescriptorSet() != VK_NULL_HANDLE);
}

TEST_CASE("Texture bindings release independently through owned retirement",
          "[vulkan][texture-binding][integration]")
{
    if (!owl::tests::IsGpuTestEnabled())
        SKIP("Set OWL_RUN_VULKAN_BOOTSTRAP_TEST=1 to run texture binding lifetime tests");
    std::string error;
    auto context = owl::tests::CreateGpuContext(error);
    INFO(error);
    REQUIRE(context);
    BindingFixture fixture{context->device->Get()};
    fixture.Initialize(*context->device->QueueFamilies().graphicsFamily);
    const auto baseline = AllocationStatistics(*context->allocator);
    {
        auto firstImage = VulkanImage::Create(*context->allocator, ImageDesc, error);
        auto singleLevel = ImageDesc;
        singleLevel.mipLevels = 1;
        auto secondImage = VulkanImage::Create(*context->allocator, singleLevel, error);
        REQUIRE(firstImage);
        REQUIRE(secondImage);
        const auto imagesAllocated = AllocationStatistics(*context->allocator);
        REQUIRE(imagesAllocated.allocationCount == baseline.allocationCount + 2);
        auto first = VulkanTextureBinding::Create(*context->device, fixture.descriptorLayout,
                                                  *firstImage, error);
        REQUIRE(first);
        auto second = VulkanTextureBinding::Create(*context->device, fixture.descriptorLayout,
                                                   *secondImage, error);
        REQUIRE(second);
        CHECK_FALSE(firstImage->IsValid());
        CHECK_FALSE(secondImage->IsValid());
        REQUIRE(first->DescriptorSet() != VK_NULL_HANDLE);
        REQUIRE(second->DescriptorSet() != VK_NULL_HANDLE);
        CHECK(first->DescriptorSet() != second->DescriptorSet());
        CHECK(AllocationStatistics(*context->allocator).allocationCount ==
              imagesAllocated.allocationCount);
        CHECK(AllocationStatistics(*context->allocator).allocationBytes ==
              imagesAllocated.allocationBytes);

        const auto* identity = first.get();
        const auto originalSet = first->DescriptorSet();
        auto moved = std::move(first);
        CHECK_FALSE(first);
        CHECK(moved.get() == identity);
        CHECK(moved->DescriptorSet() == originalSet);
        detail::ResourceRetirementQueue<VulkanTextureBinding, 1> retired;
        REQUIRE(retired.TryRetire(0, moved)); // These images have never been submitted or sampled.
        CHECK_FALSE(moved);
        CHECK(AllocationStatistics(*context->allocator).allocationCount ==
              baseline.allocationCount + 2);
        REQUIRE(retired.Collect(0) == 1);
        CHECK(AllocationStatistics(*context->allocator).allocationCount ==
              baseline.allocationCount + 1);
        // Validation detects if destroying the first owner's pool invalidated this live set.
        fixture.RecordBinding(second->DescriptorSet());
    }
    CHECK(AllocationStatistics(*context->allocator).allocationCount == baseline.allocationCount);
    CHECK(AllocationStatistics(*context->allocator).allocationBytes == baseline.allocationBytes);
}
