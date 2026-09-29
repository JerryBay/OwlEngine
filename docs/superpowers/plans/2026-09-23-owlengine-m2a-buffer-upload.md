# OwlEngine M2A Native Buffers and Staging Upload Implementation Plan

> **For agentic workers:** Use `superpowers:executing-plans` to execute the selected task.
> Work in the current checkout on `master`, as requested by the user. Keep each implementation
> slice small; no new task, branch, worktree, or parallel implementation is required.

**Goal:** Upload the existing triangle's vertex/index bytes through staging into a device-local
buffer, verify the GPU copy through readback, and preserve M1 rendering and cleanup behavior.

**Architecture:** Add private native buffer and startup-upload owners inside `OwlVulkan`.
Keep Vulkan memory requirements, stage/access masks, and fence completion explicit. The renderer
remains responsible for completed use before destroying its resource children.

**Tech Stack:** C++20, Vulkan 1.3, CMake Presets, existing pinned vcpkg dependencies, Catch2.

**Spec:** [M2 design](../specs/2026-09-23-owlengine-m2-design.md), sections 2, 4, and M2A acceptance.

**Status:** Tasks 1-3 complete for local VS2026 scope on 2026-09-24. Native code commits:
`1414ded`, `6f8441d`, `ef07f8e`. Final Debug/RelWithDebInfo full CTest passed 71/71 each with
synchronization validation, no skips, and no validation errors/warnings. Formal Sandbox checks
and fresh RenderDoc bytes/bindings/pixel comparison passed. See `PROJECT.md` and local evidence
`build/diagnostics/m2a-20260924/verification.md`. This plan covers M2A only.
M2B-M2D retain their own checkpoint outcomes in the design and need detailed plans when reached.

## Global Constraints

- C++20; Win64 first; Vulkan 1.3, Dynamic Rendering, and Synchronization2.
- Native implementation stays private to `OwlVulkan`; Platform and Foundation do not own GPU resources.
- Use pinned vcpkg dependencies and the existing VS2022/VS2026 presets.
- Verify VS2026 here and VS2022 on the separate workstation.
- Keep one `OwlUnitTests` executable, with test sources grouped by responsibility.
- Preserve Smoke, Clear, Triangle, optional presentation fences, and the documented WSI fallback limitation.
- No RHI, Render Graph, resource handle registry, custom general allocator, or new queue family.
- No VMA, ImGui, image resources, shader changes, or new command-line mode in M2A.
- Preserve unrelated user edits. Explicitly list owned headers and sources in their CMake target.

## Task 1: Extract Native Buffer Ownership and Memory Policy

**Files:**

- Create: `engine/vulkan/src/VulkanBuffer.h`, `engine/vulkan/src/VulkanBuffer.cpp`
- Modify: `engine/vulkan/src/VulkanTrianglePipeline.h`, `engine/vulkan/src/VulkanTrianglePipeline.cpp`
- Modify: `engine/vulkan/CMakeLists.txt`, `tests/CMakeLists.txt`
- Create: `tests/vulkan/BufferTests.cpp`
- Modify: `tests/vulkan/TrianglePipelineTests.cpp`

**Interfaces:** Private to the Vulkan module; declarations below specify the intended boundary.
Use ordinary noncopyable, movable RAII members and existing `std::optional` / `std::string& error`
conventions. Buffer creation never submits work or waits for the GPU.

```cpp
namespace owl::vulkan
{
    struct VulkanBufferDesc
    {
        VkDeviceSize size = 0;
        VkBufferUsageFlags usage = 0;
        VkMemoryPropertyFlags requiredMemory = 0;
        VkMemoryPropertyFlags preferredMemory = 0;
    };

    // VulkanBuffer owns VkBuffer + VkDeviceMemory; Device is borrowed.
    // Default construction is empty, copying is deleted, moves are noexcept.
    // Methods declared inside VulkanBuffer:
    // static std::optional<VulkanBuffer> Create(const VulkanDevice& device,
    //     const VulkanBufferDesc& desc, std::string& error);
    // bool IsValid() const noexcept;
    // VkBuffer Get() const noexcept;
    // VkDeviceSize Size() const noexcept;
    // VkDeviceSize AllocationSize() const noexcept;
    // VkMemoryPropertyFlags MemoryProperties() const noexcept;
    // bool Write(VkDeviceSize offset, std::span<const std::byte> data, std::string& error);
    // bool Read(VkDeviceSize offset, std::span<std::byte> data, std::string& error);

    namespace detail
    {
        std::optional<std::uint32_t> SelectBufferMemoryType(
            const VkPhysicalDeviceMemoryProperties& properties,
            std::uint32_t memoryTypeBits,
            VkMemoryPropertyFlags required,
            VkMemoryPropertyFlags preferred) noexcept;

        bool IsBufferRangeValid(VkDeviceSize size, VkDeviceSize offset,
                                VkDeviceSize bytes) noexcept;
    }
}
```

- [x] Add CPU policy tests before replacing the triangle's existing allocation. Test compatible
  masks, all required flags, deterministic preferred-flag ranking, coherent fallback, and
  host-visible/device-local combinations. Include these concrete assertions:

  ```cpp
  VkPhysicalDeviceMemoryProperties properties{};
  properties.memoryTypeCount = 3;
  properties.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  properties.memoryTypes[1].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
  properties.memoryTypes[2].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  using owl::vulkan::detail::SelectBufferMemoryType;
  CHECK(SelectBufferMemoryType(properties, 0b111,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 2);
  CHECK(SelectBufferMemoryType(properties, 0b011,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 1);
  CHECK_FALSE(SelectBufferMemoryType(properties, 0b001,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 0));
  CHECK(SelectBufferMemoryType(properties, 0b100,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0) == 2);
  CHECK_FALSE(SelectBufferMemoryType(properties, 0,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0));
  using owl::vulkan::detail::IsBufferRangeValid;
  CHECK(IsBufferRangeValid(66, 60, 6));
  CHECK_FALSE(IsBufferRangeValid(66, 60, 7));
  CHECK_FALSE(IsBufferRangeValid(66, UINT64_MAX, 2));
  CHECK(IsBufferRangeValid(66, 66, 0));
  ```

- [x] Build the focused tests and implement selection:
  filter compatibility and required flags first, rank by preferred flags matched, then index.
  Implement range checking as `offset <= size && bytes <= size - offset`.
- [x] Implement `Create`: reject an invalid Device, zero size, zero usage, or bits outside
  `TRANSFER_SRC | TRANSFER_DST | VERTEX_BUFFER | INDEX_BUFFER` before Vulkan calls. Use an
  ordinary non-sparse/non-protected buffer with no external-memory or device-address chain;
  keep host/device memory preferences within `DEVICE_LOCAL`, `HOST_VISIBLE`, `HOST_COHERENT`, and
  `HOST_CACHED`. Create an exclusive-sharing buffer, query requirements, allocate `requirements.size`, and bind at
  offset 0. Preserve native operation names and `VkResult` in failure messages. Unwind creation
  in reverse order. Keep buffer size, allocation size, selected flags, and native handles distinct.
- [x] Implement `Write`/`Read`: require a valid host-visible buffer and an in-bounds logical range;
  zero-byte access is a successful no-op after validation. Map the whole allocation. Write copies
  then flushes non-coherent memory with offset 0 / `VK_WHOLE_SIZE`; Read invalidates when needed
  then copies. Always unmap after a successful map, including cache-operation failures.
  Document that callers must establish completion and exclude overlapping CPU/GPU access.
- [x] Replace the triangle's `VkBuffer`/`VkDeviceMemory` members with `VulkanBuffer`. At this task's
  checkpoint retain direct host-visible geometry writes. Remove `SelectTriangleMemoryType` and
  its old policy test after the new tests cover the contract; retain all SPIR-V tests.
- [x] Add opt-in GPU buffer tests using the existing SDL/Instance/Surface/Device fixture pattern
  (consolidated with the upload fixture in `BufferUploadTests.cpp`):
  create, host write/read, move construction, replacement after completed use, and destruction.
  Compare a known byte pattern and assert source handles become empty after moves. Do not force
  enormous allocations to simulate failure. Record the selected flags and whether non-coherent
  behavior was actually exercised.
- [x] Build Debug and RelWithDebInfo, run focused CPU/GPU buffer tests, then the existing triangle
  lifecycle test with validation. Inspect the diff; document any untested native failure paths.

**Acceptance:** Geometry behavior is unchanged, resource ownership is reusable within Vulkan,
and memory selection no longer belongs to triangle shader/pipeline code. No upload exists yet.

## Task 2: Add an Owned Startup Upload and Verify Copied Bytes

**Files:**

- Create: `engine/vulkan/src/VulkanBufferUpload.h`, `engine/vulkan/src/VulkanBufferUpload.cpp`
- Create: `tests/vulkan/BufferUploadTests.cpp`
- Modify: `engine/vulkan/CMakeLists.txt`, `tests/CMakeLists.txt`

**Consumes:** `VulkanBuffer::Create`, `Write`, `Read`, `Get`, `Size`, and memory metadata from Task 1;
`VulkanDevice::GraphicsQueue()` and its graphics-family index.

**Produces:** A private move-only operation owning staging, destination, command pool/buffer, fence,
and its submission state. `Create` prepares resources but does not submit. `SubmitAndWait` starts
the operation once; `Wait` drains it without resubmitting. Native results preserve device-loss
identity. Destination ownership is transferable only after successful completion.

```cpp
// Methods declared inside VulkanBufferUpload:
static std::optional<VulkanBufferUpload> Create(
    const VulkanDevice& device, std::span<const std::byte> bytes,
    VkBufferUsageFlags finalUsage, VkPipelineStageFlags2 consumerStages,
    VkAccessFlags2 consumerAccess, std::string& error);
VkResult SubmitAndWait(std::string& error);
VkResult Wait(std::string& error);
bool IsPending() const noexcept;
std::optional<VulkanBuffer> TakeDestination(std::string& error);
```

- [x] Add tests for empty payload, zero final usage, missing consumer stage/access, invalid Device,
  and taking a destination before completion. These reject locally before native submission.
  Consumer masks are native caller contracts: use a compatible graphics-queue stage/access pair;
  the two consumers in this slice are copy reads and vertex/index reads, not arbitrary queue work.
  Add an explicit completion-policy test covering success, timeout, device loss, and wait errors;
  timeout/error must leave a successfully submitted operation pending. A failed submit must not
  create a pending fence wait. Keep this small policy in the uploader's private `detail` namespace.
- [x] Implement preparation using one staging buffer (`TRANSFER_SRC`, required `HOST_VISIBLE`,
  preferred `HOST_COHERENT`) and one destination (`finalUsage | TRANSFER_DST`, required
  `DEVICE_LOCAL`). Use independent allocations. Copy the bytes into staging via `Write`.
- [x] Create a transient graphics-family command pool, primary command buffer, and unsignaled
  fence. Record the native copy and dependency outside Dynamic Rendering:

  ```cpp
  const VkBufferCopy region{.srcOffset = 0, .dstOffset = 0, .size = destination.Size()};
  vkCmdCopyBuffer(command, staging.Get(), destination.Get(), 1, &region);
  const VkBufferMemoryBarrier2 barrier{
      .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
      .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
      .dstStageMask = consumerStages,
      .dstAccessMask = consumerAccess,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .buffer = destination.Get(),
      .offset = 0,
      .size = destination.Size(),
  };
  const VkDependencyInfo dependency{
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .bufferMemoryBarrierCount = 1,
      .pBufferMemoryBarriers = &barrier,
  };
  vkCmdPipelineBarrier2(command, &dependency);
  ```

- [x] Submit with `vkQueueSubmit2` and the fence; use one command-buffer submit record and no
  semaphores. Set pending only on successful submission. Wait for the fence. Keep all submitted
  resources on unresolved waits; do not reset the pool, release staging, or return the destination.
  Make destruction's completed-use precondition explicit. A diagnostic device-loss state is
  terminal, not a successful transfer. Check every fallible create/map/flush/record/submit/wait.
- [x] Add an opt-in GPU roundtrip test. Upload deterministic patterns of 1, 3, 4, 65, 66, 67, and
  4097 bytes with destination `TRANSFER_SRC` use and consumer `COPY / TRANSFER_READ`. Include the
  triangle's three position/color vertices plus three `uint16_t` indices as a 66-byte fixture.
  Take the completed destination; copy it into a host-visible `TRANSFER_DST` readback buffer.
  Record a `COPY / TRANSFER_WRITE -> HOST / HOST_READ` barrier, submit, wait successfully, then
  call `Read` and compare every byte. Report actual memory flags, including whether invalidate
  was required. This proves transfer content, not just command recording.
- [x] Ensure GPU test fixtures drain their own pending work before assertions can unwind owners.
  Do not use a fatal Catch2 assertion between successful submission and establishing safe cleanup.
  Run CPU policy tests and the GPU roundtrip under synchronization validation in both configurations.

**Acceptance:** Byte-identical GPU roundtrips, no validation errors, explicit submission ownership,
and no copy-size padding assumption. The public Sandbox behavior is still unchanged.

## Task 3: Use Staging for Triangle Geometry and Close M2A

**Files:**

- Modify: `engine/vulkan/src/VulkanTrianglePipeline.h`, `engine/vulkan/src/VulkanTrianglePipeline.cpp`
- Modify: `engine/vulkan/src/VulkanTriangle.cpp`
- Modify: `tests/vulkan/FrameTests.cpp` only for upload-related acceptance assertions
- Modify: `docs/learning/m1-triangle.md`, `docs/learning/graphics-api-notes.md`
- Modify: `docs/building/windows.md`, `PROJECT.md`, `README.md`

**Consumes:** Task 2 uploader and Task 1 buffer owner. Existing shader files, vertex format,
index type, frame loop, and public `VulkanTriangleOptions` remain sufficient for this slice.

**Produces:** Staged triangle geometry and a private pipeline method
`VkResult WaitForUpload(std::string& error)` called by `VulkanTriangle::Impl::WaitIdle`.
When no upload exists, it returns `VK_SUCCESS`; otherwise it forwards the uploader wait result.

- [x] Assemble the existing vertices/indices in a 66-byte CPU payload and prepare an upload with
  `VERTEX_BUFFER | INDEX_BUFFER` final usage and these consumer masks:

  ```cpp
  constexpr VkPipelineStageFlags2 geometryStages =
      VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT | VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
  constexpr VkAccessFlags2 geometryAccess =
      VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT;
  ```

- [x] Install the operation in the pipeline's owned optional member before calling `SubmitAndWait`.
  On success, take the destination into its geometry member and release upload temporaries. On
  failure, preserve unresolved ownership; return the operation/error and prevent drawing.
  Bind the resulting buffer at the same vertex offset 0 and index offset 60. Log copied byte count,
  logical/allocation sizes, selected staging/destination flags, and successful completion once.
- [x] Drain `WaitForUpload` from renderer `WaitIdle`, including the partial-initialization path
  before frame resources exist. Propagate device loss distinctly. Normal teardown must establish
  completion before any child resource destructor. For unresolved terminal waits, attempt a
  checked idle cleanup (device-idle in the final implementation, covering all device queues);
  acquire/present fences still require explicit completion checks. If this cannot establish
  completion or device loss, issue a fatal
  diagnostic and terminate without unwinding pending GPU owners. Never label that path clean
  shutdown. Do not introduce implicit Device waits into `VulkanBuffer` or steady-state rendering.
  Replace the current destructor's catch-and-continue behavior for pending uploads: even if
  diagnostics or string allocation throws, a non-throwing final drain must establish safe cleanup
  or take the fatal path before child destructors run.
- [x] Verify geometry survives extent-only swapchain recreation. Clear creates no geometry upload;
  Smoke keeps its platform-only startup. Keep existing presentation-fence/fallback tests.
- [x] Run both default presets and all opt-in GPU tests with validation enabled. Run the formal
  Triangle sample, check its unchanged RGB output, resize/maximize/minimize/restore, and close/Escape.
  Inspect exit code and full validation logs. Record real coverage instead of reusing M1 evidence
  as proof of the changed resource path.
- [x] Inspect a draw with the working RenderDoc capture/replay combination and check vertex/index
  bytes, binding offsets, draw arguments, and pixels. A normal steady-state capture may omit the
  initialization copy; do not mistake that for an absent upload. Task 2's readback proves the copy.
- [x] Update documentation with the actual memory path and acceptance results. Keep M1 historical
  evidence distinguishable from new M2A results. Add only transferable cache/synchronization/
  lifetime lessons to graphics API notes. Inspect the final diff and mark only completed steps.

**Acceptance:** The same triangle uses staged device-local geometry, upload temporaries are safe
through initialization/cleanup, readback is exact, and validation is clean. M2A is complete;
M2B's VMA/texture design is the next checkpoint, not an implicit part of this task.

## Verification Commands and Evidence

Use the installed CMake executable if `cmake`/`ctest` are not on PATH. Preserve repository-local
dependency setup; no global Vulkan SDK or loader change is required by this implementation.

```powershell
./scripts/configure.ps1 -Preset windows-vs2026
cmake --build --preset windows-vs2026-debug
cmake --build --preset windows-vs2026-relwithdebinfo
ctest --preset windows-vs2026-debug
ctest --preset windows-vs2026-relwithdebinfo

# First enable the process-local layer and synchronization settings documented in windows.md.
$env:OWL_RUN_VULKAN_BOOTSTRAP_TEST = '1'
try {
    ctest --preset windows-vs2026-debug -R '^Vulkan (buffer|clear|triangle)'
    if ($LASTEXITCODE -ne 0) { throw 'Debug Vulkan acceptance failed' }
    ctest --preset windows-vs2026-relwithdebinfo -R '^Vulkan (buffer|clear|triangle)'
    if ($LASTEXITCODE -ne 0) { throw 'RelWithDebInfo Vulkan acceptance failed' }
} finally {
    Remove-Item Env:OWL_RUN_VULKAN_BOOTSTRAP_TEST
}
./build/windows-vs2026/bin/Debug/OwlSandbox.exe --sample triangle
```

Name new GPU cases `Vulkan buffer ...` so the focused expression above includes them; verify the
CTest listing before relying on a filtered run. Task 3 also runs the full opt-in presets to cover
existing Instance/Device/Swapchain regressions. Follow the existing
[validation setup](../../building/windows.md#validation-enabled-acceptance); explicitly confirm
layer activation and zero validation errors. CPU CI skips GPU cases. VS2022 remains a separate
workstation result. Native wait/submit failure injection and a non-coherent GPU path are not
established by policy tests or by a coherent-memory GPU pass.
