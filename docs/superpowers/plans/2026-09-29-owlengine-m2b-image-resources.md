# M2B-2 Step 1: Image Resource Ownership Implementation Plan

> Execute this scoped plan inline, with independent ownership review and fresh verification.

**Goal:** Create and release texture Image, ImageView and Sampler resources correctly before adding uploads or sampling draws.

**Architecture:** Three private move-only owners in OwlVulkan. Image owns a VMA allocation;
ImageView borrows the native image/device; Sampler borrows only the device. The caller supplies
GPU completion and destroys dependent views before images, then allocator, then device.

**Tech Stack:** C++20, Vulkan 1.3, VMA 3.4.0, CMake, one Catch2 OwlUnitTests target.

**Spec:** [M2 texture boundaries](../specs/2026-09-23-owlengine-m2-design.md#vma-and-image-resources).
The user authorized the previously proposed first step. Status: implemented and verified locally.

## Global Constraints

- Keep native implementation private to OwlVulkan; no RHI, new backend, queue or dependency.
- Keep the current Vulkan 1.3, Dynamic Rendering and Synchronization2 baseline.
- Use existing VS2022/VS2026 presets; verify VS2026 here, VS2022 on the other workstation.
- Preserve Clear, Triangle and Smoke. No new visual sample in this resource-only step.
- Work on the explicitly authorized master branch, preserving unrelated changes.
- Upload commands, layout transitions, descriptor binding, mip generation and visual acceptance
  remain separate subsequent texture steps. Allocating mip storage does not initialize its pixels.

## Contracts and trade-offs

`VulkanImageDesc` contains `VkExtent2D extent`, `VkFormat format`, `uint32_t mipLevels`, and
`VkImageUsageFlags usage`. Support only RGBA8 UNORM/SRGB, 2D optimal tiling, one array layer,
one sample, exclusive sharing and initial layout UNDEFINED. Require SAMPLED usage and permit
TRANSFER_SRC/DST. Storage/depth/attachments/cube/arrays/external/sparse/mutable images are outside
this sample's contract. Query exact format/type/tiling/usage support and dimensions/mips before
creation; do not estimate opaque optimal image size from texel bytes or promise allocation success.
Require DEVICE_LOCAL via VMA AUTO; incidental host visibility does not add CPU mapping methods.

`VulkanImageViewDesc` contains `baseMipLevel` and explicit `levelCount` (default 0/1). Only a
same-format 2D color view with identity swizzle and array layer 0 is supported. Reject zero,
overflow and out-of-range levels; no VK_REMAINING_MIP_LEVELS sentinel. Multiple views may refer
to one image. Wrapper moves preserve native identity; destroying/replacing an image requires all
views released first. There is no shared ownership or implicit device wait.

`VulkanSamplerDesc` contains min/mag filter, mipmap mode, address U/V/W, minLod and maxLod.
Allow nearest/linear, repeat/mirrored-repeat/clamp-to-edge, and finite 0 <= minLod <= maxLod.
Defaults are linear/repeat with LOD 0..0. Anisotropy, comparison, LOD bias and unnormalized
coordinates remain disabled. Creation does not associate a sampler with an image; format filtering
capability must be checked by the later sampling/upload path. Do not enable device features now.

## Task 1: Resource descriptions and local rejection policy

**Files:** `engine/vulkan/src/VulkanImage.{h,cpp}`, `VulkanImageView.{h,cpp}`,
`VulkanSampler.{h,cpp}`, `engine/vulkan/CMakeLists.txt`, `tests/vulkan/ImageTests.cpp`,
`tests/CMakeLists.txt`.

**Interfaces:** `detail::IsImageDescValid(desc)`, `IsImageSupported(desc, VkImageFormatProperties)`,
`IsImageViewRangeValid(totalMipLevels, viewDesc)`, `IsSamplerDescValid(samplerDesc)` return bool.

- [x] Write tests with literal dimensions/ranges, including 7x3 -> maximum 3 mip levels,
  zero extents, unsupported usage/format, simulated device limits, overflowing view ranges,
  invalid sampler enums and NaN/infinite/reversed LOD values.
- [x] Build the test target and run the focused CPU cases against initial rejection stubs;
  confirm valid requests fail assertions, rather than failing due to missing headers/symbols.
- [x] Implement the policy used by creation; rerun the focused cases until green.

Example expected behavior:

```cpp
VulkanImageDesc desc{.extent = {7, 3}, .format = VK_FORMAT_R8G8B8A8_SRGB,
                     .mipLevels = 3, .usage = VK_IMAGE_USAGE_SAMPLED_BIT};
CHECK(detail::IsImageDescValid(desc));
desc.mipLevels = 4;
CHECK_FALSE(detail::IsImageDescValid(desc));
CHECK_FALSE(detail::IsImageViewRangeValid(3, {2, 2}));
```

## Task 2: Native owners and real GPU lifecycle

**Interfaces:** all owners have empty construction, noexcept moves, `IsValid()` and native `Get()`.
Creation returns `std::optional<Owner>` and populates `std::string& error`:

```cpp
VulkanImage::Create(const VulkanAllocator&, const VulkanImageDesc&, std::string&);
VulkanImageView::Create(const VulkanImage&, const VulkanImageViewDesc&, std::string&);
VulkanSampler::Create(const VulkanDevice&, const VulkanSamplerDesc&, std::string&);
```

Image exposes `Device()` and `Desc()` for view creation and subsequent uploads. It stores native
allocator/image/allocation/device handles, so parent wrapper moves do not change its references.
Use `vmaGetAllocatorInfo` for physical-device support queries; no new allocator public contract.
View/Sampler store VkDevice values and their owned native handles, not parent wrapper addresses.

**Files:** the three owners above; `tests/vulkan/ImageResourceTests.cpp`; shared test-only
`tests/vulkan/GpuTestContext.{h,cpp}` extracted from `BufferUploadTests.cpp` because both now use it.

- [x] Write opt-in real GPU tests for empty-parent rejection; multiple views; single/multiple mip
  images; moves and live-owner replacement; invalid range/description preservation; and image
  allocation counts/bytes returning to baseline. Do not require cached memory blocks to disappear.
- [x] Observe valid creation assertions fail against empty-owner stubs, then implement creation
  and cleanup. Failed creation unwinds only unsubmitted resources; every native result is checked.
- [x] Build Debug and RelWithDebInfo, then run both full CTest presets with
  OWL_RUN_VULKAN_BOOTSTRAP_TEST=1 and existing process-local synchronization-validation settings.
  Require no skipped GPU cases and no validation errors; inspect warnings and layer activation.
- [x] Independently review ownership, failure paths and native validity. Check the final diff.

## Task 3: Documentation and acceptance

- [x] Update PROJECT.md, the M2 design and Windows testing guide with the implemented boundary
  and observed test results. Extend conceptual notes by topic, not as a conversation diary.
- [x] Record uncovered environments/paths. This slice proves resource creation/lifetime and API
  validity, not pixel upload, sampling, mip contents, new visual output or driver failure recovery.

References: [Image creation](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageCreateInfo.html),
[Image views](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageViewCreateInfo.html),
[Samplers](https://docs.vulkan.org/refpages/latest/refpages/source/VkSamplerCreateInfo.html).

## Verification result

2026-09-29: both VS2026 builds pass. Final Debug/RelWithDebInfo CTest runs pass 84/84 each with
GPU opt-in, no skips and no Khronos validation errors/warnings (synchronization and submit-time
checks enabled). Policy rejection stubs first failed 5 CPU cases; empty creation stubs failed 3
GPU cases. After implementation all 11 new cases pass, including 4 real GPU cases, plus all existing
regressions. Image allocation counts/bytes return to baseline after move replacement and teardown.

Independent review identified failure outputs being adopted too early; all three Create functions
now publish native handles to owners only after success. This branch is source-reviewed, not
driver-failure injected. Evidence is in `build/diagnostics/m2b-images-20260929/` (local/ignored).
VS2022, other GPUs, image uploads/layout transitions, sampled pixels and generated mip contents
remain unverified or unimplemented as appropriate. The next step is bounded checkerboard upload
and exact readback through explicit image layout transitions.
