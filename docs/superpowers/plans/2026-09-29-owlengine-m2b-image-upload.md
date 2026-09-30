# M2B-2 Step 2: Image Upload and Readback Implementation Plan

**Goal:** Upload tightly packed RGBA8 pixels into a single-mip image and prove exact GPU
roundtrips before adding descriptor binding or a textured draw.

**Architecture:** A private move-only `VulkanImageUpload` owns staging, a newly created image,
one transient command pool/buffer and a fence. It records explicit native Vulkan transitions on
the existing graphics queue. Readback and procedural test patterns stay under `tests/vulkan`.

**Tech Stack:** C++20, Vulkan 1.3 / Synchronization2, existing pinned VMA and Catch2.

**Spec:** [M2 design](../specs/2026-09-23-owlengine-m2-design.md).
Status: Implemented and verified on local VS2026 (2026-09-29). Independent review covered
ownership and synchronization. No new dependency or backend abstraction.

## Contracts and boundaries

- Preserve Platform/Foundation, the single OwlUnitTests target, and Clear/Smoke/Triangle.
- RGBA8 UNORM/SRGB, optimal 2D image, one layer/sample/mip, graphics queue only.
- Input is `VulkanImageDesc` plus exactly `width * height * 4` bytes; checked arithmetic must
  reject overflow before allocation. Require sampled and transfer-destination usage; optional
  transfer-source usage enables readback. Reuse Image's queried device/format limits.
- `Create(device, allocator, desc, bytes, error)` copies CPU bytes into owned staging and records
  commands without submitting. Native allocator/device/queue must outlive the upload and image.
  Device/allocator wrappers may move without changing native identity; exclude concurrent queue use.
- `Submit(error)` submits once. `Wait(error, timeoutNanoseconds = UINT64_MAX)` observes completion;
  timeout or recoverable wait error retains every pending resource. `IsPending()` describes
  outstanding completion proof, not an instantaneous hardware-busy query.
- `TakeDestination(error)` succeeds once, only after observed completion. The transferred image
  is in SHADER_READ_ONLY_OPTIMAL, with a dependency for fragment sampled reads on the same queue.
  Its later uses/destruction belong to the caller. Image itself does not track arbitrary layouts.
- `DrainForDestruction() noexcept` waits without allocating diagnostics, falling back to queue idle
  on non-device-loss fence errors. An unresolved operation stays pending. Destruction/replacement
  of a pending owner terminates rather than releasing resources still in use; callers drain first.
- Only successful native creation outputs become owned. Successful wait releases staging and
  command/fence resources; device loss is terminal and does not permit destination transfer.
- Startup blocking is deliberate. No general scheduler, image updates, mip generation, descriptors,
  sampled draw, transfer queue, or RHI in this step.

## Explicit data path

```text
CPU tightly packed RGBA8 -> staging.Write (flush if needed)
new Image: UNDEFINED -> TRANSFER_DST_OPTIMAL (NONE -> COPY / TRANSFER_WRITE)
vkCmdCopyBufferToImage: offset 0, rowLength 0, imageHeight 0, mip/layer 0
Image: TRANSFER_DST_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL
       COPY / TRANSFER_WRITE -> FRAGMENT_SHADER / SHADER_SAMPLED_READ
Submit -> fence completion -> release temporaries -> hand off Image

Test-only roundtrip on same queue, after upload completion:
SHADER_READ_ONLY_OPTIMAL -> TRANSFER_SRC_OPTIMAL
vkCmdCopyImageToBuffer -> transfer-write-to-host-read barrier
restore Image to SHADER_READ_ONLY_OPTIMAL -> submit -> fence -> Read (invalidate if needed)
compare every byte against original CPU data
```

The readback entry barrier includes prior writes/layout transitions, even though no shader has
sampled the image. It must not infer the last producer from the current layout. RGBA8 transfer
does not perform sRGB decoding. Buffer row pitches describe linear transfer bytes, not the
implementation's optimal image storage layout.

References: [copy regions](https://docs.vulkan.org/refpages/latest/refpages/source/VkBufferImageCopy.html),
[copy requirements](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdCopyBufferToImage.html),
[synchronization examples](https://docs.vulkan.org/guide/latest/synchronization_examples.html).

## Task 1: Input and completion policies

Files: `engine/vulkan/src/VulkanImageUpload.h/.cpp`, `tests/vulkan/ImageUploadTests.cpp`,
and existing module/test CMakeLists.

- [x] Add failing tests for 1x1 = 4 bytes, 7x3 = 84 bytes, zero extent, unsupported format/usage,
  multi-mip rejection, extreme extent overflow and exact payload sizes.
- [x] Add failing state tests: successful submit -> pending; timeout/error -> still pending;
  success -> complete; device lost -> terminal. Invalid/repeated transitions do not resurrect work.
- [x] Implement checked sizing and state policy; run focused CPU tests.

```cpp
CHECK(detail::ImageUploadByteSize(desc7x3) == 84);
CHECK(detail::StateAfterImageUploadWait(ImageUploadState::Pending, VK_TIMEOUT) ==
      ImageUploadState::Pending);
```

## Task 2: Owned upload and independent readback

Files: same production owner; `tests/vulkan/ImageUploadIntegrationTests.cpp` (including local
readback helpers), existing `GpuTestContext`; list test source in existing OwlUnitTests.

- [x] Write real-GPU tests against creation/submission stubs and observe expected failures.
- [x] Implement creation, command recording, Submit/Wait, explicit drain, moves and one-time handoff.
- [x] Verify both formats with checkerboard and asymmetric/channel-varying bytes at 1x1, 1x7,
  7x1, 7x3 and 17x9, including a repeated readback to validate restored layout.
- [x] Verify rejected requests do not allocate, wrong-device allocator rejection, pre-submit and
  pending handoff rejection, no resubmit, moving a pending owner, replacing an unsubmitted owner,
  completion release and final VMA live allocation counts/bytes returning to baseline.

```cpp
auto upload = VulkanImageUpload::Create(device, allocator, desc, pixels, error);
REQUIRE(upload);
CHECK_FALSE(upload->TakeDestination(error));
REQUIRE(upload->Submit(error) == VK_SUCCESS);
CHECK_FALSE(upload->TakeDestination(error));
REQUIRE(upload->Wait(error) == VK_SUCCESS);
auto image = upload->TakeDestination(error);
REQUIRE(image);
// Read back independently through VkBufferImageCopy and compare with pixels.
```

## Task 3: Review, regression and durable documentation

- [x] Inspect ownership and synchronization with an independent reviewer; address actionable issues.
- [x] Build VS2026 Debug and RelWithDebInfo; run both full CTest presets with GPU opt-in,
  Khronos synchronization and submit-time validation. Inspect full logs and skips.
- [x] Update PROJECT.md, README, Windows testing instructions and concise graphics notes.
  Record measured results and limits: other workstation, native failure injection, non-coherent
  hardware, sampled pixels and generated mips are separate coverage.
- [x] Inspect final diff and `git diff --check`.

Commands: `cmake --build --preset windows-vs2026-debug` (also relwithdebinfo), then
`ctest --preset windows-vs2026-debug -R '^[Ii]mage upload|^Vulkan image upload' -V` for focused
cases. Full acceptance removes `-R`, with `OWL_RUN_VULKAN_BOOTSTRAP_TEST=1` and the existing
local validation-layer setup from `docs/building/windows.md`. Default GPU tests must still skip.

## Verification result

2026-09-29: VS2026 Debug and RelWithDebInfo builds succeeded. Both full CTest runs passed 92/92
without skips, with the existing Khronos layer and synchronization/submit-time validation enabled.
No Vulkan validation errors or warnings; existing intentional Platform-failure tests and forced
WSI compatibility-mode warning retain their established behavior.

Focused upload checks passed 8/8 with GPU opt-in; without opt-in, four CPU cases passed and four
GPU cases skipped. The roundtrip case compared 20 format/extent/pattern combinations twice each,
plus independent move/drain roundtrips. Staging/readback memory flags were 6/14 (coherent).
Live allocation count/bytes returned to baseline; retained VMA blocks are not counted as live allocations.

Test-first evidence: rejection stubs failed the two CPU positive sizing/state cases, then passed
all four CPU cases; creation stubs failed all four GPU cases before the native upload was implemented.
Independent source review found no remaining actionable defect. Logs and diagnostic scripts:
`build/diagnostics/m2b-image-upload-20260929/` (local, ignored).

VS2022/other GPUs, native OOM/submit/device-loss injection and non-coherent hardware are unverified.
The zero-timeout test accepts either successful completion or timeout; it does not claim a forced
native timeout. No descriptor binding, sampled output, mip generation or new RenderDoc capture
is part of this step. The next slice integrates the uploaded texture into an indexed quad.
