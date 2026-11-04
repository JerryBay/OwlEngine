# M2C-3 Task 2 Immutable Texture Binding Ownership

**Date:** 2026-11-04
**Status:** Implemented and verified locally. Runtime replacement and last-use tracking are the following task.
**Goal:** Give an image and its descriptor dependents one independently destroyable owner while
preserving all current textured rendering and upload failure behavior.
**Spec:** [M2 deferred destruction](../specs/2026-09-23-owlengine-m2-design.md#frame-uploads-and-deferred-destruction).
**Prerequisite:** [Task 1 retirement queue](2026-11-04-owlengine-m2c3-retirement-queue.md).

## Ownership and Contracts

| Owner | Responsibility |
| --- | --- |
| New private `VulkanTextureBinding` | Image/allocation, full-range View, Sampler, one Pool and one Set |
| `VulkanTexturedQuadPipeline` | Shared Descriptor Set Layout, Pipeline Layout, Graphics Pipeline, geometry, shader words and upload operations |
| Existing image upload | Layout transitions, GPU copy/mips and fence-observed upload completion |
| Renderer | Completion of submitted draws/uploads before native child destruction |

- A binding is immutable after creation and addressed through `unique_ptr`. Its native owners
  are not copied or moved individually. `ResourceRetirementQueue` can later consume that pointer.
- Each binding has a dedicated one-set pool. This keeps ownership explicit and permits releasing
  one binding independently; it costs one pool per texture version. Pool batching belongs to a
  future descriptor allocator when measured scale justifies it.
- Shared layout stays in the Pipeline owner because future versions use the same shader binding
  contract. The factory borrows a valid layout from the same Device: binding 0, one fragment
  combined image sampler, no immutable sampler or update-after-bind flags. Native handles cannot
  be introspected to validate that layout contract; callers establish it.
- `Create(device, layout, image&, error)` returns `unique_ptr<VulkanTextureBinding>`. Reject empty
  Device, null layout, invalid image and mismatched image Device before native child creation.
  On failure the caller's image remains valid. Build view, sampler, pool and set in a temporary
  binding, write the descriptor once, then move the image into it only after all creation succeeds.
- Binding creation does not upload, transition or wait. Before sampling, the caller must establish
  `SHADER_READ_ONLY_OPTIMAL` and fragment sampled-read visibility for every exposed mip. The
  renderer continues to obtain the image from a completed `VulkanImageUpload`.
- Retain existing nearest min/mag filtering, clamp-to-edge addressing, nearest mip mode for a
  single level or linear mip interpolation for multiple levels, and maxLod = mipLevels - 1.
- Destroy Pool/Set before Sampler/View, and View before Image/allocation. Device and allocator
  outlive the whole binding. Before destruction, all submitted uses must be complete; no hidden
  wait or resource retirement is introduced in this refactor.
- The Pipeline explicitly resets its binding before destroying the shared descriptor layout.
  After a successful image-upload wait, Initialize takes the completed destination into a local
  owner and creates the binding. On failure, the local completed image is safe to release.
  Pending/failed waits retain `imageUpload_` for the renderer's existing destruction drain.
  `WaitForUpload` still handles geometry handoff, but image handoff now belongs to Initialize.
- Preserve the public renderer API, all samples/controls, shader files, per-slot geometry uploads,
  serials, presentation handling and platform/dependency boundaries. Work directly on master as
  authorized; no automatic commit/push and no unrelated cleanup.

The lifetime rule follows [vkDestroyDescriptorPool](https://docs.vulkan.org/refpages/latest/refpages/source/vkDestroyDescriptorPool.html):
pool destruction frees its sets and requires their submitted uses to have completed. Immutable
bindings also avoid modifying sets referenced by pending commands; see
[vkUpdateDescriptorSets](https://docs.vulkan.org/refpages/latest/refpages/source/vkUpdateDescriptorSets.html).

## Implementation and Verification

Files: new `engine/vulkan/src/VulkanTextureBinding.{h,cpp}` and
`tests/vulkan/TextureBindingTests.cpp`; update private `VulkanTexturedQuadPipeline.{h,cpp}`,
the existing two CMake source lists, `PROJECT.md`, `README.md`, `docs/building/windows.md` and this record.

- [x] Add three tests and buildable factory stubs. One CPU case rejects empty parents. GPU cases
  prove input ownership survives null layout/empty image/mismatched Device, then successful
  creation consumes it; two simultaneous bindings retain separate descriptors and allocations.
  Move their unique pointers, retire an unused binding at serial 0, and record a bind of the
  surviving set after the old pool is destroyed. Check VMA live count/bytes return to baseline.
  These lifecycle probes do not sample undefined image contents or submit GPU work; existing
  pixel/frame tests supply actual sampled-use coverage.
- [x] Build Debug and run `[texture-binding]` with GPU opt-in and actual validation. Require a
  behavior assertion failure against stubs rather than a compile error. Keep logs under ignored
  `build/diagnostics/m2c3-texture-binding-20261104/`.
- [x] Implement transactional native creation with independently owned pool/set, read-only
  `DescriptorSet()` access and ordered destruction. Keep the object noncopyable/nonmovable.
- [x] Replace the Pipeline's image/view/sampler/pool/set members with its binding pointer; keep
  the descriptor-layout creation there. Move completed-image handoff to Initialize and bind the
  set returned by the new owner. Review all initialization exits and native destruction drains.
- [x] Format touched C++, build both full VS2026 configurations, run focused lifecycle and
  existing texture/mip/streamed-pixel cases, then full opt-in CTest in both configurations.
  Require no skips, actual Khronos sync/submit-time validation, zero errors and inspect warnings.
  Check the cumulative diff and update documentation with actual counts and remaining limits.

```powershell
cmake --build --preset windows-vs2026-debug
cmake --build --preset windows-vs2026-relwithdebinfo
# Use the existing process-local validation environment and non-elevated launcher.
./build/windows-vs2026/tests/Debug/OwlUnitTests.exe '[texture-binding],[texture],[streaming]'
ctest --preset windows-vs2026-debug -V
ctest --preset windows-vs2026-relwithdebinfo -V
git diff --check
```

No new visual change is intended. Automated GPU/readback evidence does not constitute a new
manual or RenderDoc acceptance. Native allocation-failure injection, VS2022, non-coherent and
separate graphics/present hardware remain independently unverified. Runtime replacement, active
last-use updates and collecting retired bindings in the frame loop are not part of this task.

## Verification Record

- Design checked against current upload handoff, renderer drain and private descriptor ownership.
- 2026-11-04: the buildable stub passed the empty-parent CPU case and failed both new GPU cases
  at the expected missing-binding assertion (exit 42, 2 failed assertions out of 32). Validation
  was actually loaded, with zero errors/warnings and no skips.
- The implemented focused Debug run passed 14 cases / 37370 assertions across
  `[texture-binding],[texture],[streaming]`. Both full VS2026 Debug/RelWithDebInfo builds passed;
  full opt-in CTest passed 157/157 per configuration with no skips, actual Khronos synchronization
  and submit-time validation, and zero validation errors/warnings.
- Tests confirm rejected creation leaves the caller's image and allocation unchanged; successful
  creation consumes it. Two bindings own distinct sets; releasing the first through the retirement
  queue at serial 0 preserves the second set's validity and allocation, and final live allocation
  counts/bytes return to baseline. Those images were never submitted or sampled; this does not
  yet verify retirement of an in-flight texture. Existing sampled readback/frame tests cover the
  extracted binding in actual draws, including both mip-generation paths and streamed geometry.
- Main-session source review checked partial creation cleanup, same-device guards, nonthrowing
  final image handoff, pending upload retention and dependent-before-image destruction. Formatting
  and cumulative diff checks passed. No independent-agent review or native allocation-failure
  injection was performed; the manual, RenderDoc and portability limits above remain.
- Build logs: `build/m2c3-task2-{red-build,debug-build,relwithdebinfo-build}.log`. Red/focused/full
  test logs and JSON summaries: `build/diagnostics/m2c3-texture-binding-20261104/` (local, ignored).
  The existing restricted-process launcher lets the actual Vulkan loader read process-local
  validation settings; no system layer registration or tool installation was changed.
