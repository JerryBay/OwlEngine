# M2C-3 Task 3 Runtime Texture Replacement

**Status:** Implemented and automatically verified locally; manual/RenderDoc acceptance pending.
**Goal:** Replace the sample texture while frames are in flight, retaining every old binding
until its last successful graphics use is observed complete. Continue directly on master;
preserve the preceding uncommitted Task 2 extraction. No automatic commit/push.

## Design

- Keep ownership in `VulkanTexturedQuadPipeline`: active binding plus last-use serial, at most
  one image upload, and a four-entry retirement queue. The renderer owns submission/completion
  evidence and passes it to the pipeline. No RHI, new queue, general texture manager or shader change.
- A sample-local public `RequestTextureReplacement(span<byte>, error)` accepts exactly 64x64
  tightly packed RGBA8 UNORM, matching the existing sample. Copy input during the call; retain the
  existing mip count and generation policy. Return Accepted, Busy or Failed. Busy preserves both
  the requester's data and current resources; invalid input never invalidates the renderer.
- Request creates and submits a new `VulkanImageUpload` on the graphics queue without waiting.
  Reuse the existing upload owner/drain paths. Add nonblocking fence Poll: NOT_READY is ordinary
  progress and emits no error; native failures preserve ownership for explicit destruction drain.
- Before recording a frame, collect with observed graphics completion and poll the new upload.
  Only after upload completion create an immutable binding, retire the old one with its final
  last-use serial, and publish the new one. Capacity checks precede ownership changes. Upload
  completion does not advance renderer frame progress: old draws submitted after the upload can
  still be outstanding. Never update the old descriptor in place.
- After successful graphics submission only, record the active binding's last use. Present failure
  does not undo that use. Collection follows successful slot waits; failed submits/waits never
  invent completion. Replacement occurs outside the record/submit interval on the window thread.
- WaitIdle drains the accepted upload, waits frame fences, publishes its completed replacement
  and collects old bindings. Resize uses that existing path. Minimize may defer publication and
  retain one bounded request until restore or WaitIdle. Teardown covers partial creation, pending
  upload, pending draws, failure and device-loss paths before native children are released.
- Expose replacement count, active last-use serial, pending upload and retirement count through
  existing sample stats. Add R to the existing mip samples for visible palette changes; keep
  input/time/pixel generation in Sandbox. No automatic replacement in existing sample defaults.
- Tradeoff: one dedicated upload and descriptor pool per version is intentionally simple and
  bounded. CPU preparation/allocation can still cost time; avoiding GPU idle waits is not a claim
  of hitch-free streaming. Transfer scheduling and descriptor suballocation remain later work.

The native contracts are documented in
[vkGetFenceStatus](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetFenceStatus.html)
and [vkDestroyDescriptorPool](https://docs.vulkan.org/refpages/latest/refpages/source/vkDestroyDescriptorPool.html).

## Implementation and Acceptance

- [x] Write failing tests against buildable stubs: public empty rejection; actual replacement
  pixel readback across GPU/CPU mip policies; busy/invalid input preserving active output;
  bounded retirements with conservative completion, partial collection and allocation cleanup.
  Update the independent offscreen test submit path to report actual successful-use serials.
- [x] Implement request/poll/publish/retire and frame-loop successful-use/completion integration.
  Test repeated renderer replacement with two slots, resize/minimize/restore, moves, immediate
  shutdown with accepted work, both presentation modes and WaitIdle cleanup. No fast-GPU timing
  assumptions; verify ownership against observed completion, not wall-clock execution guesses.
- [x] Add R and replacement statistics to the mip demo and usage docs. Build full VS2026 Debug
  and RelWithDebInfo, run focused tests then full opt-in CTest with actual synchronization and
  submit-time validation; require no skips and zero errors/warnings. Inspect cumulative diff.
- [x] Update PROJECT/README and this verification record. Keep manual/RenderDoc acceptance,
  native failure injection, non-coherent/separate-queue hardware and VS2022 explicitly separate.

## Verification

- Red: Debug target built successfully; `[texture-replacement],[image-upload]` failed 6 cases /
  13 assertions at the missing request, last-use and Poll behaviors. Nine existing/negative cases
  passed. Actual validation was loaded with no errors/warnings or skips, exit 42.
- Green: the same focused filter passed 15 cases / 29038 assertions. Both full VS2026 Debug and
  RelWithDebInfo builds passed. Final full opt-in CTest passed 163/163 per configuration, no skips,
  actual Khronos synchronization/submit-time validation and zero validation errors/warnings.
- Pixel tests verify copied input and old output before publication, replacement LOD 0/6 pixels
  for GPU/CPU mip generation, last use recorded after upload submission, full-capacity backpressure,
  partial collection and VMA live allocation count/bytes returning to baseline. Completion is
  deliberately withheld from the owner to exercise its conservative lifetime policy; these pixel
  tests do not claim the GPU was still physically executing when the host checked retention.
- Frame tests exercise 32 repeated replacements plus accepted work across moves, resize/minimize/
  restore, WaitIdle publication/collection and immediate shutdown. Single-level/GPU/CPU mip modes
  run both presentation-fence configurations. Successful submitted draws update last use before
  Present; pending uploads survive deferred frames. Source review inspected native failure and
  destruction paths, but no driver failure injection or independent-agent review was performed.
- R demo/manual and new RenderDoc acceptance have not been performed. VS2022, non-coherent-memory
  hardware and separate graphics/present families remain unverified. Existing captured frames
  predate runtime replacement. CPU preparation/native allocation performance was not benchmarked.
- Logs and JSON summaries: `build/diagnostics/m2c3-replacement-20261104/`; build logs:
  `build/m2c3-task3-{red-build,green-build,debug-build,relwithdebinfo-build}.log` (local, ignored).
  Validation used the existing restricted-process launcher and process-local layer settings;
  system registration and tool installations were unchanged.
