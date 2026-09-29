# M2B-1: VMA Allocation Migration

- Status: implemented and locally verified on VS2026 (2026-09-29); M2A accepted by the user.
- Goal: move the existing buffer/upload/triangle path to maintained allocation without changing its synchronization or geometry.
- Architecture: one private move-only `VulkanAllocator` per renderer device; buffers own a `VkBuffer` and `VmaAllocation` and borrow the native allocator handle.
- Tech stack: C++20, Vulkan 1.3, VMA 3.4.0 from the existing pinned vcpkg baseline, CMake, Catch2.
- Spec: [M2 design](../specs/2026-09-23-owlengine-m2-design.md).

## Scope and contracts

The current request authorizes the next planned M2 checkpoint. Deliver its allocation slice first.
Images, views, samplers, descriptors and mip generation need the next M2B slice; no RHI or new queue.
Develop on the user-approved master branch and preserve existing documentation edits.

Creation order is Instance -> Device -> Allocator -> Buffers; destruction reverses it after GPU
completion. The allocator stores native parent handles, not addresses of movable wrappers.
Moving an allocator transfers its native handle; replacing a live allocator still requires all
its old allocations to have been released. Buffer replacement also requires completed GPU use.
VMA never supplies this completion proof.

Use `VMA_MEMORY_USAGE_AUTO`, required/preferred memory flags and explicit host access:

| Host access | CPU methods | Allocation intent |
| --- | --- | --- |
| None | Neither Read nor Write | GPU-only use, even if the chosen type happens to be host-visible |
| SequentialWrite | Write | Host-visible staging; sequential writes |
| Random | Read and Write | Host-visible readback / arbitrary CPU access |

Host-access intent requires `HOST_VISIBLE` in required flags. Conversely a required
`HOST_VISIBLE` flag requires explicit host access. Reject contradictory descriptions before VMA.
Keep existing restricted buffer usage/memory flags. Logical bounds are independent of allocation
size. Map pointers and flush/invalidate ranges are allocation-relative; VMA handles underlying
block offsets and non-coherent atom alignment. `AllocationSize` means this allocation's slice,
not the complete reserved block. No persistent mapping, BDA or optional memory-budget extensions.

Compile VMA implementation exactly once. Use Vulkan 1.3 at compile and allocator creation time,
with explicit dynamic procedure resolvers. The dependency stays private to OwlVulkan and its
private-header tests. Smoke retains delayed loader behavior; Clear needs no resource allocator.

## Tasks

- [x] 1. Replace obsolete native memory-ranking tests with buffer description/host-intent policy
  tests in `tests/vulkan/BufferTests.cpp`; confirm the missing contract fails the build.
- [x] 2. Add `vulkan-memory-allocator` to `vcpkg.json`; add private allocator/header/implementation
  files to `engine/vulkan/src` and CMake. Configure with `scripts/configure.ps1 -Preset windows-vs2026`.
- [x] 3. Migrate `VulkanBuffer`; pass allocator explicitly through `VulkanBufferUpload`,
  `VulkanTrianglePipeline`, `VulkanTriangle::Impl`, and the existing GPU test fixture. Reject
  mismatched upload device/allocator. Keep existing command/barrier/fence behavior unchanged.
- [x] 4. Extend GPU tests for host-intent rejection, partial mapping of multiple allocations,
  allocator moves, and live allocation counts returning to baseline. Reuse exact-byte upload
  readback (including 66 bytes) and buffer move-replacement coverage.
- [x] 5. Build Debug and RelWithDebInfo, run all tests with local GPU opt-in plus synchronization
  validation, review the final patch, and run the formal Triangle/Clear/Smoke routes. Record
  observed evidence separately from missing non-coherent or VS2022 coverage.
- [x] 6. Update project/build/learning documentation concisely with the maintained path and next slice.

## Definition of done

Both configurations compile; no new validation errors/warnings; exact upload bytes remain correct;
separate live allocations retain their independent bytes through nonzero-offset writes and moves;
allocation counts return to baseline after release (reserved blocks may remain cached). Existing
triangle, resize/recreate and shutdown tests remain valid. Report runtime and visual checks honestly.

## References and trade-offs

[VMA setup](https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/quick_start.html)
and [mapping](https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/memory_mapping.html).
Native allocation remains a documented M2A exercise in Git history, not a second maintained backend.
This delegates allocator mechanics while leaving API synchronization and resource ownership visible.

## Verification

Both builds and full opt-in CTest presets pass: 73/73 each, zero skips and zero Khronos validation
errors/warnings with synchronization and submit-time checks enabled. The allocator test observes
8 allocations in 1 block, independent partial reads/writes and allocation counts/bytes returning
to baseline. Existing exact-byte copies, moves, and renderer lifecycle cases also pass.
Formal Debug Triangle/Clear/Smoke resize/minimize/restore/exit checks pass; Smoke loads no Vulkan DLL.
An independent source review found no actionable defects. Evidence: `build/diagnostics/m2b-vma-20260929/`.
VS2022, native non-coherent behavior, driver-failure injection, and a new pixel capture remain unverified.
