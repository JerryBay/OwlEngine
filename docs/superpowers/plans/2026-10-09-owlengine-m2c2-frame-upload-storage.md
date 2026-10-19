# M2C-2 Frame-Slot Upload Storage Implementation Plan

**Date:** 2026-10-09

**Status:** Tasks 1-5, including streamed quad geometry, independent dual-slot stress and streamed
mip output evidence, are implemented and verified locally on VS2026 Debug/RelWithDebInfo. Task 6
final manual/RenderDoc acceptance remains pending. Unchecked tasks below are planned work, not
evidence that the feature exists or tests have passed.

**Goal:** Reuse bounded, persistently mapped staging storage across the existing two frame slots,
then demonstrate changing geometry through GPU copies and indexed draws without a steady-state
queue/device-idle wait.

**Architecture:** Each `VulkanFrameSlot` owns one private `VulkanUploadArena`. `VulkanBuffer` retains
allocation and mapping ownership. `VulkanSubmissionProgress` remains the authority for successful
submission identities and host-observed completion. The textured-quad owner holds a separate
device-local geometry destination for each streaming slot; staging completion alone must not be
mistaken for permission to overwrite a destination still used by another frame.

**Spec:** [M2 design](../specs/2026-09-23-owlengine-m2-design.md), Frame uploads and deferred destruction.
**Prerequisite:** [M2C-1 submission progress](2026-10-08-owlengine-m2c-submission-progress.md).
**Stack:** C++20, Vulkan 1.3/Synchronization2, pinned VMA 3.4.0, Catch2, existing Windows presets.

## 1. Scope and Decisions

| Decision | Planned choice | Consequence |
| --- | --- | --- |
| Storage ownership | One arena per existing frame slot, borrowing the native allocator through its buffer | No global ring, cross-slot suballocation, or independent upload queue |
| Capacity | Fixed 64 KiB per streaming slot; smaller capacities in tests | Report exhaustion; never grow, wrap, overwrite, or idle the device to make room |
| Allocation | Append-only ranges with caller-specified positive power-of-two alignment | Bounds and padding count against capacity; no individual frees |
| Host access | Persistent mapping is opt-in on `VulkanBufferDesc`; arena uses `SequentialWrite` | Existing startup upload and readback defaults retain their current behavior |
| First consumer | Optional streamed geometry in the existing generated-mip quad samples | Reuse their scale/LOD input, texture, descriptor, shaders, and event loop |
| Destination lifetime | Textured-quad owner allocates one 76-byte geometry buffer per streaming slot | Waiting for that slot protects both staging reuse and its previous geometry draw |
| Public sample option | Add `VulkanTriangleOptions::streamTextureGeometry`, default `false` | Enable it in `texture-mips` and `texture-mips-cpu`; reject it outside generated-mip texture mode |
| Retirement | Separate later slice | No runtime texture replacement, descriptor mutation, or retirement queue here |

Keep single-threaded window ownership, the graphics queue, binary semaphores, and existing fences.
Preserve Smoke, Clear, static Triangle/Texture, static mip tests, serial progress, and both present
fence modes. No new shader binaries, CLI command, dependency, RHI, Timeline Semaphore, transfer
queue, general descriptor system, or ImGui panel is required. Startup image/mip uploads retain
their blocking contract. Swapchain/present release retains its separate WSI contract.

The 64 KiB choice is a bounded sample default, not a measured optimum or total GPU-memory figure.
Full M2 acceptance still requires resource retirement, statistics, and stress acceptance after this
slice. This plan does not mark M2C or M2 complete.

## 2. Ownership and API Contracts

### Buffer owns persistent mapping

Extend private `VulkanBufferDesc` with `bool persistentMapping = false`. Reject this option with
`BufferHostAccess::None`; retain the existing explicit `HOST_VISIBLE` and host-intent checks.
Combine `VMA_ALLOCATION_CREATE_MAPPED_BIT` with the corresponding host-access flag. Retain the
successful allocation's allocation-relative `pMappedData` inside `VulkanBuffer`; reject a requested
persistent mapping with no usable pointer before exposing the buffer.

`Write` uses that pointer when present and otherwise retains its scoped map/unmap path. It still
calls `vmaFlushAllocation(allocator, allocation, offset, bytes)` after copying. `Read` retains its
Random-access requirement and invalidate-before-copy behavior on either mapping path. Expose
only a read-only `IsPersistentlyMapped()` query, not a writable pointer that can outlive a frame.

VMA rounds flush/invalidate ranges to non-coherent atoms; do not add the allocation's memory-block
offset or duplicate that rounding. Do not manually unmap the automatic mapping created by the
MAPPED flag. Moves transfer the mapping pointer with the allocation and clear the source; VMA
releases the automatic mapping when the allocation is destroyed. Existing short-lived mappings
still have balanced map/unmap calls.

### Arena owns ranges and their reuse guard

Add private `VulkanUploadArena.{h,cpp}`. The move-only owner contains one `VulkanBuffer`, capacity,
cursor, a small recording/submission state, and the last successful submission serial protecting
its storage. It borrows the allocator through the buffer; it never owns a queue, fence, command
pool, or destination resource.

Proposed private operations, with final spelling allowed to follow surrounding conventions:

- `Create(allocator, capacity, error)`: create a host-visible, persistently mapped `TRANSFER_SRC`
  buffer with SequentialWrite intent, preferably coherent. Reject zero or unaddressable capacity.
- `CanBegin(completedSerial) noexcept`: check reuse eligibility before the caller touches the old
  command pool or destination. This query makes no state changes and performs no native waits.
- `Begin(completedSerial, error)`: recheck eligibility and start a fresh writable batch. The caller
  must already have proved completion, then discarded old command recording referencing the ranges.
- `Append(bytes, alignment, error) -> optional<UploadSlice>`: allocate, copy and flush one range;
  publish `{buffer, offset, size}` and advance the cursor only after success.
- `Seal()`: close host writes for a nonempty writable batch before native submission. Empty or
  already sealed batches are rejected. No writes or new ranges are accepted until a later
  successful `Begin`.
- `MarkSubmitted(serial) noexcept`: associate the sealed batch with the actual successful frame
  serial, once per batch. Require a nonzero serial greater than this arena's previous use; never
  mint a separate identity. Preflight state/serial before native submit, then commit without
  allocating or throwing.
- Read-only validity, capacity, used-byte and memory-property queries for tests/diagnostics.

`UploadSlice` is a borrowed, batch-local copy-source description, not a resource owner or a raw CPU
pointer. It expires when the batch is reset/discarded. The current owner records all uses before
submitting and does not retain slices across frames. Do not add a resource handle registry.

Keep range arithmetic and reuse/seal decisions testable without a Vulkan device, and use those
same policies in the arena. Reject empty requests, zero/non-power-of-two alignment, overflow,
exhaustion and append-after-seal. Failure preserves the published cursor/ranges and submission
identity; an unsuccessful write may touch unpublished tail bytes, which must never be submitted.

`completedSerial` comes from this renderer's single graphics-queue progress owner. Slot indices,
fence handles, initial signaled fences, acquire/present results, elapsed time and device loss do
not supply completion proof. A new arena has no pending use. An abandoned unsubmitted batch may
be discarded after its command recording is discarded; it consumes no serial. A submitted batch
cannot begin again until `completedSerial >= lastUseSerial`. Moves preserve pending identity;
destruction or replacement requires completed GPU use or the established device-loss teardown.

All writes and flushes for a slot precede its one submission, then the whole arena is frozen.
Ranges inside that arena may share a non-coherent atom because they share this lifetime. Slots
use separate ordinary VMA allocations, retaining VMA's non-coherent atom isolation. Do not split
one allocation between independently writable/in-flight slots or add custom atom-rounding code.
Buffer binding alignment, append alignment, index alignment, and buffer/image-copy requirements
are different contracts. `vkCmdCopyBuffer` has no universal four-byte size/offset rule; this
slice's consumer chooses four-byte alignment for its float-based geometry. Image streaming is
not implemented by accepting a generic alignment argument.

### Frame and draw owners retain their responsibilities

Add an optional arena to `VulkanFrameSlot`. Add an explicit upload-storage initialization step to
`VulkanFrameResources`, using its existing initialized device and the matching allocator. Make
partial creation safe before submission; a rejected request must not replace live slot storage.
Initialize arenas only for the streaming sample. Keep allocator lifetime outside and longer than
frames and draw owners, as the current `VulkanTriangle::Impl` declaration order already does.

`VulkanTexturedQuadPipeline` owns a fixed-at-initialization collection of geometry destinations,
with count supplied from `frames.slots.size()`. Do not duplicate an independently maintained frame
count constant. Each destination uses `TRANSFER_DST | VERTEX_BUFFER | INDEX_BUFFER` and requires
device-local memory. The non-streaming path retains its existing immutable geometry and startup
`VulkanBufferUpload`; the streaming path needs no unused startup geometry copy.

Create CPU-scaled positions from the existing four vertices, retain UVs, and append the six
`uint16_t` indices: 64 vertex bytes plus 12 index bytes, index offset 64. Copy all 76 bytes to the
selected slot's destination each submitted frame. Sandbox still supplies scale/LOD; no clock or
input moves into Vulkan. Push scale `1.0F` for streamed geometry to avoid applying scale twice;
retain the requested explicit/implicit LOD and the existing eight-byte shader interface.

Keep prepare/copy helpers outside `RecordDraw` and outside the Dynamic Rendering scope. Record
an explicit prior-vertex/index-read -> COPY/TRANSFER_WRITE dependency for destination reuse,
then the copy, then COPY/TRANSFER_WRITE -> VERTEX_ATTRIBUTE_INPUT/INDEX_INPUT reads. Queue-family
indices remain ignored. Draw binds this slot's destination; descriptors and texture contents
remain immutable. Do not silently redirect static draws to an uninitialized streaming buffer.

## 3. Frame Sequence and Failure Rules

Integrate with the existing `VulkanTriangle::Impl` order:

1. Validate draw parameters and process window/recreation deferral as today.
2. Wait for the selected slot, publish fence-observed progress, and preflight `CanSubmit` and
   upload-byte counter capacity. Reject counter overflow before acquiring another image.
3. Acquire a swapchain image and retain the existing acquire/present bookkeeping.
4. Only on the rendering path, require `CanBegin(CompletedSerial())` before resetting/discarding
   the old command pool. Then `Begin` the arena, append and flush geometry, seal the batch, and
   record copy/barriers/draw. A rejected reuse attempt must not reset a pending command pool.
5. Preflight both slot and arena submission state. Reset the submission fence immediately before
   `vkQueueSubmit2`, as today.
6. Feed the exact result to `RecordSubmitResult`. Only on success, associate the arena with
   `slot.submissionSerial`; count uploaded payload bytes at this point.
7. Present normally. An out-of-date Present after a successful submit retains both the serial
   and the arena's pending lifetime, even if `RenderFrame` returns `Deferred`.

| Event | Required behavior |
| --- | --- |
| Minimized/hidden/zero extent or acquire retry before submit | No new upload batch, copied-byte count, or submission identity |
| Wait timeout/error | No completion advancement and no arena/destination reuse |
| Append/flush/recording failure after acquisition | Preserve owned resources and acquire bookkeeping; enter the renderer's failed state and drain on teardown |
| Native submit failure | No new serial or upload-byte count; do not wait on a reset-but-unsubmitted fence as though it were pending |
| Successful submit, unsuccessful Present | Keep the successful graphics use pending until observed complete |
| Resize/recreation | Existing drain proves prior uses; arena/destination allocations survive extent-only recreation |
| Renderer move | Preserve all owners, progress, mappings and pending use through the existing `Impl` transfer |
| Device loss | Terminal cleanup path; never report normal completion or recycle for another frame |

The arena never waits implicitly. Ordinary frames wait only when reusing their existing slot;
startup, recreation and shutdown retain their existing waits. Capacity/flush failures must not
introduce a retry loop or a queue/device-idle fallback in steady state.

## 4. Implementation Tasks

### Task 1: CPU range and reuse policy

**Files:** new `engine/vulkan/src/VulkanUploadArena.{h,cpp}`,
`tests/vulkan/UploadArenaTests.cpp`; `engine/vulkan/CMakeLists.txt`, `tests/CMakeLists.txt`.

- [x] Add focused `[upload-arena]` CPU cases and observe the relevant failing expectations.
- [x] Use hand-derived allocation examples: capacity 32, requests `(3,1)`, `(5,8)`, `(16,16)`
  yield offsets 0, 8, 16 and final cursor 32. Test exact fit, padding exhaustion, odd sizes,
  invalid alignment, empty input and arithmetic near `VkDeviceSize`/host-size limits.
- [x] Test writable/sealed/submitted states: no append after seal; serial 7 cannot reset at
  completion 6, can reset at 7; abandonment without submission consumes no serial. Failure
  preserves cursor/identity. Zero, repeated, or regressing submission identity is invalid. Reused
  slots receive fresh serials from the existing progress owner. `CanBegin` is a read-only preflight;
  native command-pool ordering remains part of Task 4.
- [x] Cover unsuccessful submit/wait results by composing with `VulkanSubmissionProgress`,
  including timeout, device loss, initial signaled state and nonwrapping serial exhaustion.
- [x] Implement CPU policies for later arena composition; no Vulkan wait or allocation is
  performed. Buffer ownership and frame integration remain Tasks 2-4.

**Delivered:** `detail::FindUploadRange` and `detail::UploadArenaState` in the private arena files.
`PreviewAppend` computes a range without consuming it; `CommitAppend` advances only after the
caller confirms successful writing/flushing. `Seal` rejects empty or repeated sealing, and
`MarkSubmitted` requires a preflighted, nonzero, increasing serial. Invalid post-submit identity
is terminal, consistent with `VulkanSubmissionProgress`. The GPU-owning arena will compose these
policies; no incomplete runtime owner is exposed by this task.

### Task 2: Persistent mapping in the existing buffer owner

**Files:** `engine/vulkan/src/VulkanBuffer.{h,cpp}`, `tests/vulkan/BufferTests.cpp`,
`tests/vulkan/BufferUploadTests.cpp` as needed for the existing mapping fixtures.

- [x] Add description validation, mapping ownership, moves and the read-only mapping query.
- [x] Retain transient defaults and both host-intent rules; apply flush/invalidate on both paths.
- [x] Opt-in GPU tests exercise repeated partial writes/reads, empty operations, boundary rejection,
  wrong host intent, move construction, safe replacement and allocator-wrapper moves.
- [x] Verify independent live allocations, including a nonzero VMA allocation offset when observed,
  remain isolated. Return live allocation counts/bytes to baseline after releasing owners.
- [x] Record selected memory flags. Missing non-coherent hardware is a coverage limit; coherent
  success must not be reported as native non-coherent validation.

### Task 3: Arena storage and frame ownership

**Files:** `VulkanUploadArena.{h,cpp}`, `engine/vulkan/src/VulkanFrame.{h,cpp}`,
`tests/vulkan/UploadArenaTests.cpp`, new `tests/vulkan/UploadArenaIntegrationTests.cpp`.

- [x] Implement bounded persistent storage, append/flush, seal and post-submit serial association.
  A write/flush failure returns no slice; no growing allocation or implicit idle is allowed.
- [x] Add conditional per-slot creation, allocator/device identity checks, and safe partial cleanup.
- [x] Test exhausted-arena rejection without overwriting prior ranges, independent slots, pending
  owner moves, safe completed replacement, and native-parent wrapper moves where supported.
- [x] Add a test-owned submission guard declared so it drains before GPU child owners unwind on
  Catch2 failure. Do not rely on `GpuContext`'s later destructor to protect earlier local children.

**Delivered:** `VulkanUploadArena` composes `VulkanBuffer` and `UploadArenaState`; no duplicate
cursor/serial bookkeeping. `CreateUploadArenas` requires completely initialized, idle slots and
a matching allocator, creates temporary owners, then publishes all slots without replacing storage.
Default renderer paths allocate no arenas; Task 4 opts in for both mip samples. Native second-allocation/flush
failure cleanup was reviewed but not fault-injected; tests cover deterministic rejection paths.

### Task 4: Streamed textured-quad geometry and frame-loop integration

**Files:** `engine/vulkan/src/VulkanTriangle.cpp`,
`engine/vulkan/include/owl/vulkan/VulkanTriangle.h`,
`engine/vulkan/src/VulkanTexturedQuadPipeline.{h,cpp}`,
`samples/owl_sandbox/src/TextureSample.cpp`, `samples/owl_sandbox/src/VulkanSample.cpp`.

- [x] Add the opt-in sample option, reject incompatible options before creating Vulkan objects,
  and allocate arenas/destinations only for the streaming mode.
- [x] Build the scaled 76-byte payload, record dependencies and copy before rendering, and bind
  the corresponding destination. Preserve the static pipeline path and scale/LOD validation.
- [x] Follow Section 3, including command-recording discard before arena reuse and failure after
  acquire. Check invariants before submit; post-success ownership bookkeeping is non-throwing.
- [x] Enable streaming for the two mip Sandbox entries. Preserve 0-6/A/Space behavior and visual
  meaning; no new controls or shader assets are needed.
- [x] Add only two diagnostic Stats fields: `frameUploadCapacityBytes` (sum of logical slot
  capacities) and `frameUploadedBytes` (payload bytes from successful submits). Report them at
  shutdown. Exclude alignment padding, startup uploads, VMA reserved blocks and failed submits;
  zero capacity/bytes identifies a non-streaming renderer. Handle counter overflow explicitly.

**Delivered:** `RecordGeometryUpload` appends CPU-scaled geometry and records copy dependencies;
the frame owner begins/seals the batch and associates its successful serial. Explicit
`RecordStreamedDraw` binds that slot's destination and pushes scale 1; static `RecordDraw` stays
separate. Destination count comes from `frames.slots.size()`. No per-frame allocation or new idle
wait is introduced. CPU counter-boundary tests, renderer integration and narrow pixel evidence
ship with this task; final control/RenderDoc acceptance and the remaining Task 5 matrix follow.

### Task 5: Independent GPU data and rendered-output evidence

**Files:** `tests/vulkan/UploadArenaIntegrationTests.cpp`, `tests/vulkan/FrameTests.cpp`,
`tests/vulkan/TextureSamplingTests.cpp`, `tests/vulkan/TexturePipelineTests.cpp`,
`tests/CMakeLists.txt`.

- [x] Submit 64 deterministic batches through two slots. Submit both before waiting to reuse the
  first; use distinct payloads per iteration and several aligned/odd-sized ranges per batch.
  Do not require both fences to remain unsignaled on a fast GPU.
- [x] Copy into test-owned per-slot readback storage, insert transfer-write -> host-read barriers,
  and compare every byte after that slot's fence succeeds and readback invalidates. Compare
  before overwriting the slot's prior result; drain the final two batches too. No steady-state
  queue/device-idle waits or submit-and-immediately-wait helper may serialize the test.
- [x] Try reuse with stale observed completion and verify local rejection without modifying the
  still-owned batch, even if the hardware has already finished. Validate success after observation.
- [x] Extend independent offscreen sampled-pixel checks to consume production streamed geometry.
  Use fixed scale 1 and 0.5 with an interior pixel that changes from textured output to clear,
  plus marker/light/dark checks, explicit LOD 0/6 and implicit minification. Compute expected
  coverage independently of the production payload builder. Retain existing static sampling cases.
- [x] Exercise streaming GPU-generated and forced-CPU-mip renderers in both present modes,
  covering more than two frames, resize/minimize/restore, moves, successful WaitIdle and cleanup.
  Preserve all existing serial assertions. For bounded runs, uploaded bytes equal 76 times
  successful streaming submissions; pre-submit deferral adds none, post-submit Present deferral
  still counts. Capacity remains fixed across extent-only recreation.
- [x] Confirm stable buffer identity/capacity and allocation counts during reuse, then return live
  allocation counts/bytes to baseline after drain/destruction. VMA reserved blocks are separate.

### Task 6: Local acceptance and documentation

**Files:** `PROJECT.md`, `README.md`, `docs/building/windows.md`,
`docs/learning/graphics-api-notes.md`, and this plan's verification section.

- [x] Format touched C++, build VS2026 Debug and RelWithDebInfo, and run the focused CPU/GPU cases.
- [x] Run each full CTest preset with GPU opt-in and actual Khronos synchronization/submit-time
  validation. Require no skips for this acceptance run, zero validation errors and inspect every
  warning. Record actual case counts; do not copy M2C-1's historical 122/122 into new results.
- [ ] Run both formal mip samples; exercise fixed LOD, zoom/pause, resize/minimize/restore and
  normal close. Record exit codes, serial equality after WaitIdle, upload counts and capacity.
  Capture a streamed frame if RenderDoc is available to inspect copy-before-draw and the bound
  destination; a capture is separate evidence from exact readback and validation.
- [ ] Review steady-state waits, failure cleanup, mapping moves, serial association, dependencies,
  and descriptor immutability. Run `git diff --check` and document implemented/verified/unverified
  scope separately. Do not stage or commit automatically.

Use the existing process-local validation setup in [Windows build instructions](../../building/windows.md).
Do not install tools or change system layer registration as part of this slice. In the configured,
non-elevated validation shell, the final full-run commands are:

```powershell
cmake --build --preset windows-vs2026-debug
cmake --build --preset windows-vs2026-relwithdebinfo
$env:OWL_RUN_VULKAN_BOOTSTRAP_TEST = '1'
$env:VK_KHRONOS_VALIDATION_VALIDATE_SYNC = 'true'
$env:VK_KHRONOS_VALIDATION_SYNCVAL_SUBMIT_TIME_VALIDATION = 'true'
ctest --preset windows-vs2026-debug --output-on-failure
ctest --preset windows-vs2026-relwithdebinfo --output-on-failure
# Restore the validation shell's prior environment values after collecting evidence.
```

Keep complete logs under an ignored diagnostics directory and verify actual layer loading, not
just requested settings. Build/test failures stop acceptance. `ctest -N` only discovers tests and
can overwrite `LastTest.log`; it is not execution evidence. The current revision on VS2022,
native driver-failure injection, separate graphics/present hardware, non-coherent hardware when
unavailable, and changed live color formats remain separately reported limits.

## 5. Verification Record

- Planning: based on current Buffer/VMA mapping, frame/progress, textured-quad and test ownership.
- Main-session design review: completed; clarified completion preflight before command-pool reset,
  one-time arena serial association, and upload-byte overflow rejection before acquisition.
- Independent design review: not performed; the review request was rejected by the model gateway's
  concurrent-request limit before producing any output. It is not counted as completed review.
- Task 1 implementation: complete on 2026-10-09; CPU metadata/range policies only.
- Red: buildable placeholder policy failed all 12 new cases (16 failed assertions, exit 42).
- Green: the Debug `[upload-arena]` run passes 12 cases / 285 assertions. `OwlUnitTests` builds
  succeed in VS2026 Debug and RelWithDebInfo. Focused CTest including the 12 new cases, five
  submission-progress cases and three buffer-policy cases passes 20/20 in each configuration,
  without skips. Formatting and diff checks pass. Logs: `build/m2c2-task1-*.log` (ignored).
- Task 1 adds no native Vulkan calls. Task 2 adds opt-in automatic VMA mapping in the Buffer
  owner; Task 3 composes it with arena storage. Renderer streaming acceptance remains pending.
- Task 2 completed on 2026-10-16: the new API first failed compilation; buildable stubs then
  failed the CPU host-intent policy and real-GPU mapping expectations (two cases, three failed
  assertions, exit 42). Both full VS2026 builds succeed; full opt-in CTest passes 135/135 per
  configuration with no skips, actual Khronos synchronization/submit-time validation and zero
  validation errors/warnings. Existing mapping/isolation/move/readback fixtures exercise both
  lifetimes, including SequentialWrite CPU-to-GPU copy and GPU-written persistent readback.
- Eight independent allocations shared one block and returned live counts/bytes to baseline in
  both modes. Exact allocation offsets were not queried. Random/readback memory flags were 14
  (visible/coherent/cached); native non-coherent cache behavior and native failure injection
  remain unverified. No new visual/RenderDoc or VS2022 acceptance is claimed.
- Task 2 read-only independent source review found no blocking issues; the recommended direct
  SequentialWrite-to-GPU readback check was added. Runtime evidence is separate from that review.
  Logs/results: `build/diagnostics/m2c2-persistent-mapping-20261016/`; build logs:
  `build/m2c2-task2-*-build*.log` (all ignored).
- Task 3 completed on 2026-10-16: buildable storage placeholders failed the two new GPU cases
  (two assertions, exit 42). Implementation passed the focused Debug run (15 cases / 450
  assertions); a final command-pool reset assertion was then added. Both full builds and full
  opt-in CTest pass 138/138 per configuration without skips, with actual Khronos synchronization/
  submit-time validation and zero validation errors/warnings. Readback proves odd/aligned ranges,
  exhaustion safety, independent slots, pending moves, completed replacement and actual reuse.
  Both native-parent wrappers move during pending use; live allocation counts/bytes return to baseline.
- Task 3 independent source review found no blockers. Partial native allocation/flush failures
  were not injected; coherent upload memory was selected, so non-coherent hardware remains
  unverified. No new visual/RenderDoc or VS2022 acceptance is claimed.
  Logs/results: `build/diagnostics/m2c2-upload-arena-20261016/`; build logs:
  `build/m2c2-task3-*-build.log` (all ignored).
- Task 4 completed on 2026-10-16: new frame/counter/options tests first failed against scaffolding
  (four cases, 11 assertions); the independent pixel test failed against the upload placeholder.
  Implementation passes focused Debug checks (five cases / 14831 assertions). Both full builds and
  opt-in CTest pass 143/143 per configuration without skips, with Khronos synchronization/submit-time
  validation and zero validation errors/warnings. Full/half-size LOD-0 output confirms a single
  scale application and updating both slot destinations; renderer tests cover both mip/present
  modes, resize/minimize/restore, invalid controls, moves and successful-submit-only statistics.
- Both formal mip samples in both configurations exited normally with code 0 and active validation.
  All four runs report capacity 131072, completed serial = submitted serial and uploaded payload
  = 76 * submissions. This is launch/render/normal-close evidence, not manual control/visual or
  RenderDoc acceptance. Independent source review found no blockers. Native driver failures,
  non-coherent hardware and VS2022 remain unverified.
  The first RelWithDebInfo CPU-mip close probe selected an IME helper window instead of SDL.
  Closing the identified SDL window exited with code 0 after 22818 valid submissions; a fresh
  probe targeting the SDL window also passed. This required only a local probe correction.
  Logs/results: `build/diagnostics/m2c2-stream-geometry-20261016/`; build logs:
  `build/m2c2-task4-*-build.log` (all ignored).
- Task 5 completed on 2026-10-19: the two-slot test submits 64 deterministic batches with aligned
  and odd-sized ranges, compares each readback before reuse, drains the last two batches, rejects
  stale completion before and after a successful fence wait without changing batch state, and
  checks stable source handles/capacity/live allocation counts/bytes plus return to baseline.
  Streamed pixel checks cover LOD 0/6, implicit LOD 4
  minification and scale 1/0.5 for both generated and forced-CPU mips. No production API changed.
  Both full VS2026 builds and opt-in CTest pass 145/145 per configuration with no skips; the final
  focused Debug stress/pixel check passes 2419 assertions in three cases. Khronos synchronization/submit-time
  validation is loaded, with zero errors/warnings. Logs/results use the same diagnostics directory
  above with `task5-final-*` names; build logs are `build/m2c2-task5-*-build.log`. Final manual/RenderDoc acceptance, native
  failure injection, non-coherent hardware and VS2022 remain unverified.
- Next implementation task: Task 6 final manual/RenderDoc acceptance and documentation review.
- Next slice after acceptance: resource retirement keyed to last successful graphics use, with
  runtime texture/descriptor replacement and explicit dependent-before-image destruction.
