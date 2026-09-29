# OwlEngine M2 GPU Resources, Memory, and Uploads Design

- Status: M2A and M2B-1 accepted locally; M2B-2 resource ownership implemented (2026-09-29).
- Date: 2026-09-23
- Baseline: M1 accepted locally; native M2A buffer/upload/triangle implementation is in place.
- Roadmap: [M2](2026-08-17-owlengine-roadmap-design.md#m2-gpu-resources-memory-and-uploads)
- Completed native exercise: [M2A buffers and staging](../plans/2026-09-23-owlengine-m2a-buffer-upload.md)
- Completed allocator slice: [M2B-1 VMA migration](../plans/2026-09-29-owlengine-m2b-vma.md)
- Active texture slice: [M2B-2 image resource ownership](../plans/2026-09-29-owlengine-m2b-image-resources.md)

## 1. Outcome and Learning Goals

M2 separates the resource object, its backing memory, its interpretation through a view, and
the work that transfers data into it. The final demo is a textured indexed mesh with generated
mip levels, backed by a maintained allocator, with uploads and destruction safe across the
existing two frames in flight.

The first demo remains the current RGB triangle. Its appearance stays the same while the data
path changes from direct host writes to staging plus a GPU copy. Byte readback and validation
prove that mechanism independently of visual output.

Learn to explain:

- Why `VkBuffer` creation does not allocate backing storage, and why memory compatibility is
  determined by resource requirements as well as memory-property flags.
- Why CPU cache visibility, GPU memory dependencies, and completion/lifetime are separate duties.
- Why a finished upload permits staging reuse but does not permit destruction of geometry
  still used by later draws.
- Why an image, its allocation, its views, and a sampler have different ownership and use.
- Why shared-memory GPUs may not benefit from a desktop-style staging copy.

## 2. Existing Code and Scope

`VulkanTrianglePipeline` owns a combined 66-byte device-local vertex/index buffer. A startup
upload writes 60 vertex bytes plus 6 index bytes into staging, flushes when necessary, copies on
the graphics queue, records a consumer barrier, and waits for completion. The index offset is 60.

`VulkanTriangle::Impl` owns the Device, frame resources, and triangle pipeline. Its `WaitIdle`
tracks startup upload, frame, and presentation work. Its non-throwing destruction drain preserves
pending ownership even when frame initialization failed or allocating a diagnostic throws.

Retain these constraints throughout M2:

- C++20; Win64 first; Vulkan 1.3, Dynamic Rendering, and Synchronization2.
- Native implementation stays private to `OwlVulkan`; Platform and Foundation do not own GPU resources.
- Use pinned vcpkg dependencies and the existing VS2022/VS2026 presets.
- Verify VS2026 here and VS2022 on the separate workstation.
- Keep one `OwlUnitTests` executable, with test sources grouped by responsibility.
- Preserve Smoke, Clear, Triangle, optional presentation fences, and the documented WSI fallback limitation.
- No RHI, Render Graph, resource handle registry, custom general allocator, or new queue family.
- M3 owns general submission/queue experiments; M4 owns shader compilation, reflection, and
  reusable descriptor/pipeline systems. A texture's minimal binding in M2 does not preempt M4.

## 3. Sequence and Alternatives

| Approach | Benefit | Cost | Decision |
| --- | --- | --- | --- |
| Native buffer/upload exercise, then VMA | Exposes allocation and synchronization before delegating allocation mechanics | One deliberate internal migration | Recommended; matches the approved roadmap |
| VMA immediately | Reaches textures sooner | Hides the next learning objective behind library calls | Not the first slice |
| Maintain a custom allocator throughout M2 | Maximum control | Suballocation, fragmentation, and budget work dominate the renderer | Out of scope |

M2 is delivered through four checkpoints. M2A has the immediate executable task plan; subsequent
checkpoints receive their own detailed plans against the actual preceding code.

| Checkpoint | Implemented outcome | Demo / acceptance | Principal trade-off |
| --- | --- | --- | --- |
| M2A: native buffers and upload | Move-only buffer ownership, memory selection, startup staging copy, explicit barriers and fence completion | Existing triangle; exact buffer-copy readback; validation-clean cleanup | One native allocation per buffer and one startup wait keep the mechanism visible |
| M2B: maintained allocation and textures | VMA integration, image/view/sampler owners, texture upload, generated mip chain, minimal sample-local descriptor binding | Procedural checkerboard on an indexed quad, with scale changing to exercise minification | Use a procedural asset and existing precompiled shader workflow before adding an asset/shader system |
| M2C: uploads and retirement with frames in flight | Persistently mapped upload storage per frame slot, bounded allocation, resource retirement keyed to completed submissions | Update geometry and replace textures while rendering without per-frame idle waits | Existing graphics queue and fences provide sufficient evidence before M3 queue experiments |
| M2D: statistics and stress acceptance | Resource/allocation counters, optional heap-budget reporting, small ImGui diagnostics panel, bounded stress tests | Inspect allocated/used/pending bytes and return to baseline after resource churn | Debug/profiling UI only; no editor framework |

Completing M2A is not completing M2. The final textured demo, retirement, statistics, and stress
criteria remain required by the roadmap.

## 4. M2A Resource Ownership

Add private `VulkanBuffer` and `VulkanBufferUpload` types under `engine/vulkan/src`.
Keep resource creation and allocation choices explicit through native Vulkan descriptions and
required/preferred memory flags. These types are not cross-API contracts.

M2A supports ordinary non-sparse, non-protected, non-external buffers with transfer-source,
transfer-destination, vertex, and index usage only. Reject other usage bits rather than implying
support for device addresses, descriptors, external memory, or special allocation chains.
The simple one-allocation-per-buffer exercise is not Vulkan's explicitly declared dedicated
allocation mechanism; ordinary buffers in this restricted scope do not require that mechanism.
See [dedicated allocation requirements](https://docs.vulkan.org/refpages/latest/refpages/source/VkMemoryDedicatedRequirements.html).

| Owner | Owns | Borrows / lifetime contract |
| --- | --- | --- |
| `VulkanBuffer` | One `VkBuffer` and its native `VkDeviceMemory`; logical size and allocation metadata | Device outlives it; no pending GPU use or concurrent mapping at destruction/move replacement |
| `VulkanBufferUpload` | Staging buffer, destination until completion, transient command pool/buffer, fence, submission state | Device/graphics queue; single-threaded startup operation; resources remain owned across wait failure |
| `VulkanTrianglePipeline` | Completed geometry buffer, pipeline/layout, shader words; upload operation while initialization is pending | Device; caller finishes draws and drains any startup upload before cleanup |
| `VulkanTriangle::Impl` | Overall renderer lifetime and shutdown sequencing | Window; drains startup upload as well as frame/presentation work |

The buffer destructor destroys the resource before freeing its memory and does not implicitly
idle the Device. Creation failures unwind only objects that have not been submitted. Moves
transfer handles and metadata, leaving a valid empty source. Live move assignment has the same
completed-use precondition as destruction.

Keep the sample's immutable geometry composed inside `VulkanTrianglePipeline` for this slice.
Extract allocation and upload mechanics now; do not create a mesh manager or split every sample
object before a second consumer exists.

### Memory selection and host access

1. Create the buffer with the required size and usage, then query its memory requirements.
2. Filter by `memoryTypeBits` and all required property flags. Prefer the candidate satisfying
   the most preferred flags, using memory-type index as a deterministic tie breaker.
3. Allocate `requirements.size`, bind at offset 0, and retain logical size separately. Zero
   satisfies the binding alignment here; suballocation alignment is deferred to VMA.
4. Staging requires `HOST_VISIBLE`, prefers `HOST_COHERENT`, and uses `TRANSFER_SRC`.
5. Geometry requires `DEVICE_LOCAL`, with `TRANSFER_DST | VERTEX_BUFFER | INDEX_BUFFER` usage.
   Accept a type that is also `HOST_VISIBLE`; these properties are not mutually exclusive.
6. For the native exercise, map the whole allocation for each host access. Bounds-check against
   the logical buffer size using subtraction to avoid offset-plus-size overflow. A non-coherent
   write flushes offset 0 / `VK_WHOLE_SIZE` before unmapping. Readback invalidates the same range
   after completion and before CPU reads. Persistent mapping is a later checkpoint.

Whole-allocation cache operations are intentionally limited to separately allocated buffers with
no concurrent use. They are not a recipe for independently synchronizing suballocations that
share a non-coherent atom. Unmapping does not flush; mapping does not invalidate.
See [mapped writes](https://docs.vulkan.org/refpages/latest/refpages/source/vkFlushMappedMemoryRanges.html)
and [readback visibility](https://docs.vulkan.org/refpages/latest/refpages/source/vkInvalidateMappedMemoryRanges.html).

### Upload and synchronization

```text
CPU geometry bytes
  -> map staging -> memcpy -> flush if non-coherent -> unmap
  -> record CopyBuffer on the existing graphics queue
  -> COPY / TRANSFER_WRITE barrier to vertex and index reads
  -> submit with a fence -> successful CPU fence wait
  -> transfer destination ownership to the sample; release upload temporaries
  -> existing frame loop binds geometry and draws
```

The barrier uses `COPY` / `TRANSFER_WRITE` as source and
`VERTEX_ATTRIBUTE_INPUT | INDEX_INPUT` / `VERTEX_ATTRIBUTE_READ | INDEX_READ` as destination.
Both queue-family indices are `VK_QUEUE_FAMILY_IGNORED`; there is no ownership transfer. Write
and flush staging before submission. The submit provides the host-to-device domain operation;
the copy-to-consumer barrier still expresses the device memory dependency.

Use one copy region of 66 bytes. Buffer-copy sizes and offsets have no general multiple-of-four
requirement; do not confuse buffer copies with buffer/image copies or update/fill commands.
Copy offsets are relative to buffers, not to their memory binding offsets. The existing index
binding still obeys its own index-type alignment. See
[buffer copy validity](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdCopyBuffer.html),
[copy regions](https://docs.vulkan.org/refpages/latest/refpages/source/VkBufferCopy.html), and
[Khronos upload synchronization examples](https://docs.vulkan.org/guide/latest/synchronization_examples.html#upload-data-from-the-cpu-to-a-vertex-buffer).

Waiting once during initialization is acceptable. Uploading per draw, submitting per small
runtime resource, and calling `vkDeviceWaitIdle` every frame are not the maintained design.
This experiment is not a performance claim for a 66-byte triangle.

### Failure and shutdown

Track `not submitted`, `pending`, `completed`, and `device lost` explicitly. Set `pending` only
after successful submit. A wait timeout or non-device-lost error does not mean completion and
does not permit destroying staging, destination, command pool, or fence. Retain the operation
in its owner, prevent drawing from an incomplete destination, and let the renderer drain it.

Extend the renderer's shutdown path to drain startup uploads even if frame initialization never
succeeded. Before ordinary destruction, retry/drain pending work; queue-idle is allowed as a
terminal cleanup fallback. If neither completion nor device loss can be established, report a
terminal failure and stop the process without unwinding pending GPU owners. Do not free them
and claim a clean shutdown. Treat device loss separately; it is not a successful upload.
The implemented renderer destructor respects pending upload ownership even after diagnostic
exceptions: final cleanup uses a non-throwing drain/fatal path before child destructors run.

This exceptional path does not add a device-recovery system or a general submission scheduler.
Tests must distinguish safe pre-submit rejection, successful completion, and unresolved waits;
native driver-failure injection is reported separately from CPU state-policy tests.

## 5. Later M2 Boundaries

### VMA and image resources

Deliver allocation migration as M2B-1 before introducing image/descriptor behavior. The
[migration plan](../plans/2026-09-29-owlengine-m2b-vma.md) fixes host-access intent, native-handle
borrowing, allocation-relative mapping, and regression criteria. The M2A ownership/memory-selection
section above records the completed native exercise; VMA replaces that maintained allocation path.

After M2A acceptance, add VMA through the existing pinned registry. Use one allocator per Device,
created after it and destroyed after its resources but before the Device. Select the Vulkan 1.3
runtime contract explicitly. Compile `VMA_IMPLEMENTATION` in one translation unit. Replace the
maintained native allocation implementation instead of retaining two runtime allocator backends;
preserve the native lesson in Git history and the learning notes. VMA does not decide when GPU
resources are safe to release. See the [VMA setup and ownership guide](https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/quick_start.html).

Add an allocated image owner, separate image-view owner, and sampler owner. A view references an
image; the image must outlive its views and submitted uses. Do not treat swapchain images as
owned allocations. Use an optimal-tiling sampled color image, explicit subresource transitions,
and a bounded procedural checkerboard with an indexed quad. No file importer, camera, or depth
system is needed to prove texture ownership and sampling.

The first texture step now provides these three move-only owners, with RGBA8 UNORM/SRGB,
single-layer/single-sample 2D images, queried format limits, bounded mip views and basic normalized
samplers. The [resource plan](../plans/2026-09-29-owlengine-m2b-image-resources.md) defines ownership
and accepted inputs. Resource creation leaves contents undefined; upload/layout transitions,
descriptors and the checkerboard draw are the next steps. Allocated mip levels are not generated mips.

Generate mip levels with blits when the selected format supports the required blit and filtering
features. Query those capabilities. Provide a CPU-generated mip upload fallback with defined
color-space filtering; report which path was exercised. A missing GPU blit capability must not
silently pass the demo with an uninitialized chain. Keep layout tracking per mip during generation.

The texture sample needs one combined-image-sampler descriptor, its pool/layout, and sample-local
precompiled shaders with checked-in sources and regeneration instructions. Reusable descriptors,
reflection, HLSL/DXC build integration, and hot reload remain M4. Plan the second sample's shared
private frame plumbing when implementing it; do not duplicate the full WSI loop or create an RHI.

### Frame uploads and deferred destruction

Use bounded, persistently mapped upload storage owned by each existing frame slot. Reset it only
after that slot's submission is complete. Keep logical offsets, resource binding alignment,
non-coherent atom alignment, and buffer/image copy alignment distinct. Start with a per-slot
linear allocator, not a global lock-free ring; report exhaustion without overwriting pending data.

Retirement carries proof of last GPU use. On the single graphics queue, assign monotonically
increasing serials to successful submissions and associate each slot fence with its serial.
After a successful wait, advance completed progress before resetting/reusing that fence, then
release resources whose last-use serial is complete. A slot number or reused fence handle alone
is not a lifetime identity. Update retirement when a resource is used again; release views and
descriptor dependents before images/allocations. Do not mutate a descriptor set still in use.

Integrate these narrowly with the existing frame loop. Timeline semaphores, dedicated transfer
queues, queue-family handoff, and general command scheduling remain M3. Swapchain/present resource
retirement retains its existing separate WSI contract.

### Statistics and debug surface

Expose separate counts for live resources, requested resource bytes, allocator-used bytes,
reserved memory blocks, staging capacity, and pending-retirement bytes. Report per-heap budget
only when supported; label estimates and unavailable information. Heap size is not free memory,
and resource bytes are not the process's total VRAM usage.

Use the roadmap's ImGui debug surface for a small sample diagnostics panel. Its pinned dependency
and rendering integration belong to M2D, after meaningful resource counters exist. The panel has
no editor, docking, asset browser, or general tooling-framework scope.

## 6. Desktop, Mobile, and Future RHI Implications

`DEVICE_LOCAL` describes a memory property, not a guarantee of dedicated inaccessible VRAM.
UMA hardware can expose memory that is both device-local and host-visible. M2A deliberately
executes the copy for learning; later selection may avoid it when measured hardware behavior
justifies direct writes. Maintain correct cache management and in-flight lifetime in both paths.
See the [Khronos memory guide](https://docs.vulkan.org/guide/latest/memory_allocation.html).

Resource usage and CPU-access intent should remain separate inputs to placement decisions.
Keeping these distinctions now gives a future D3D12 implementation real requirements to compare;
it does not justify an API-independent allocator or universal resource-state enum in M2.
Android-specific tiling, bandwidth, and power conclusions require measurements on the later
mobile target, not extrapolation from the current desktop GPU.

## 7. Acceptance and Common Pitfalls

M2A acceptance:

- CPU memory-selection and host-range tests include incompatible masks, unmet required flags,
  coherent fallback, combined host-visible/device-local flags, zero sizes, and overflow boundaries.
- Opt-in GPU tests compare upload/readback bytes, including the actual 66-byte payload and sizes
  around common alignment boundaries. Use transfer-to-host synchronization and invalidate when needed.
- Triangle output and vertex/index decoding match M1; Clear and Smoke retain their behavior.
- Both local configurations pass relevant CPU/GPU tests with synchronization validation enabled.
  Missing layers or unavailable non-coherent types are reported as missing coverage, not passes.
- Resize/minimize/restore and shutdown do not leak or prematurely release upload resources.
- Record selected memory flags, logical/allocation sizes, bytes copied, and completion status.
  Ordinary RenderDoc frame captures may omit a startup upload; readback remains direct transfer evidence.

Full M2 acceptance additionally requires:

- The textured indexed quad samples a complete generated mip chain, confirmed by image/view,
  sampler, descriptor, and pixel inspection. Document GPU-generation versus fallback coverage.
- With two frames in flight, repeated resource replacement and per-frame uploads remain correct
  without steady-state queue/device-idle waits. No early reuse or mutation of in-use descriptors.
- Bounded stress cycles return live allocations and pending-retirement counts to their baseline
  after draining; reserved allocator blocks are accounted for separately. Use fixed iteration and
  memory limits rather than forcing the machine to exhaust memory.
- Validation reports no errors, and warnings are explained. Destruction order remains correct
  through resize and shutdown. Build/run and dependency reproduction stay documented.
- The debug panel reports measured resource/allocation statistics; learning notes explain the
  mechanisms and evidence without becoming a transcript of implementation work.

Avoid treating a visible triangle as proof of copied bytes, a fence as a cache flush, `COHERENT`
as permission for simultaneous CPU/GPU writes, a successful upload as proof of final draw
completion, or VMA as automatic synchronization. These are independent contracts.
