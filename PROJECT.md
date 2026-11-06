---
project_memory: true
workflow_version: 1
---

# OwlEngine Project Memory

This file is the concise, rolling project-status entry. Current source, build definitions,
runtime evidence, and approved designs take precedence if they conflict with this summary.

## Goal and Current Phase

- Goal: Build a long-lived open-source rendering engine for learning modern graphics APIs, RHI
  design, and modern rendering architecture through production-quality code.
- Priority: Learning depth > architecture quality > engineering quality > feature count.
- Current phase: M2 GPU resources; M2C-3 local acceptance now combines automated verification,
  user-reported controls, drained shutdown statistics and GPU replacement LOD-0 capture. Evidence
  limits remain explicit below. M2D-1 allocator snapshots have local automated and sample R/exit
  log acceptance. M2D-2 Buffer/Image classification and retired-image byte accounting are
  implemented and pass focused Debug validation; the additional log fields were not manually replayed.
- Current implementation boundary: M1 Tasks 1-8 are complete and accepted on the local VS2026
  workstation. The maintained buffer/upload path uses VMA with explicit CPU access intent;
  exact GPU copy/readback and staged triangle geometry remain. Both configurations pass local
  synchronization validation. Private image/view/sampler resources and RGBA8 staging uploads with
  explicit layout transitions, generated mips and a sampled indexed quad are implemented.
  Renderer frame submissions now have stable serials and fence-observed completion. CPU policies
  for bounded upload ranges, sealing and completion-gated reuse, plus opt-in persistent Buffer
  mapping and owned upload arenas are implemented. The two mip samples now stream per-slot
  geometry. Runtime texture replacement now submits one bounded upload, publishes an immutable
  binding after completion, and retires old bindings by their last successful graphics-use serial.

## Implemented Baseline

- Implemented:
  - M0 Foundation, Platform, Sandbox, CMake Presets, pinned vcpkg dependencies, tests, and Windows
    CI baseline.
  - Vulkan dependency and `OwlVulkan` target.
  - Explicit Vulkan window intent, framebuffer extent query, and private SDL surface bridge.
  - Vulkan 1.3 Instance, optional validation/debug messenger, and move-only Surface ownership.
  - Testable physical-device suitability policy covering required extensions/features, graphics
    and present queue families, surface format, present mode, and extent selection.
  - Move-only `VulkanDevice`, unique queue-family requests, M1 feature/extension enablement,
    borrowed graphics/present queue handles, and an opt-in real-GPU lifetime test in `OwlUnitTests`.
  - Move-only `VulkanSwapchain`, fresh Surface capability/format/mode queries, image views,
    local recreation, zero-extent deferral, and opt-in real-GPU resource lifecycle tests.
  - `VulkanTriangle` lifecycle: two frame slots, command pools/buffers, acquire, Dynamic Rendering
    clear and optional indexed draw, Synchronization2 submit, and present. Slot acquisition and
    submission fences track cleanup independently; present semaphores belong to swapchain images.
  - Optional KHR/EXT swapchain-maintenance feature/dependency negotiation and per-image present
    fences, with a diagnostic option to force the compatibility fallback. Window resize and
    out-of-date results request recreation; minimized/hidden/zero-size windows defer rendering.
  - `OwlSandbox --sample clear`: persistent resizable Vulkan window, event polling, paced frames,
    Escape/close handling, reported errors, and renderer-before-window teardown. Default command
    remains Smoke. MSVC delay-load and app-local deployment preserve non-Vulkan startup paths.
  - `--sample triangle`, sample-local GLSL/precompiled SPIR-V, staged device-local vertex/index geometry,
    and a private `VulkanTrianglePipeline`. Dynamic viewport/scissor preserve the pipeline across
    extent-only changes; color-format changes rebuild it after idle. No shader system or RHI.
  - `Platform::ExecutableDirectory()` supports executable-relative Sandbox asset lookup. Both
    Vulkan samples share the Sandbox event loop; Clear has no shader asset requirement.
  - M2B-1 private `VulkanAllocator`: VMA 3.4.0 from the existing pinned registry, Vulkan 1.3
    runtime contract, one implementation TU, and allocator-before-device destruction.
  - Private `VulkanBuffer`: buffer/VMA allocation ownership, explicit None/SequentialWrite/Random
    CPU intent, logical versus allocation slice size, and allocation-relative map/cache operations.
    Native memory-selection/allocation is retained as the M2A exercise in Git history, not a second backend.
  - M2A private `VulkanBufferUpload`: one graphics-queue copy, explicit consumer barrier and fence,
    retained pending ownership, and destination transfer after completion. Triangle uploads 66 bytes
    once; vertex/index bindings remain at offsets 0/60 and geometry survives extent-only recreation.
  - M2B-2 private move-only `VulkanImage`, `VulkanImageView`, `VulkanSampler`: VMA-backed
    optimal 2D RGBA8 UNORM/SRGB images, queried format/dimension/mip limits, bounded same-format
    color views, and basic normalized sampling rules. Only successful creation outputs become owned.
    Views borrow native image/device handles; callers finish GPU use and release views before images.
    Buffer and image tests share a test-only GpuContext; unsafe memberwise move assignment is disabled.
  - Private `VulkanImageUpload`: tightly packed RGBA8 UNORM/SRGB base input, checked byte size,
    staging copy on the graphics queue, and explicit UNDEFINED -> TRANSFER_DST -> SHADER_READ_ONLY
    transitions. Owns staging/image/commands/fence until observed completion; one-time destination
    handoff promises fragment sampled-read readiness on that queue. Pending waits retain ownership;
    an explicit non-throwing drain handles shutdown. Native parent handles survive wrapper moves.
    Independent test-only readback restores sampled layout and compares every source byte.
  - `--sample texture`: a procedural 64x64 checkerboard with an asymmetric origin marker is
    uploaded once and sampled by a six-index quad. A private draw owner holds geometry and the
    Graphics Pipeline, plus an immutable texture binding for its image and descriptor dependents.
    It shares the Clear/Triangle frame loop; extent-only recreation retains texture and bindings.
  - Private mip generation: capability-gated linear GPU blits keep each mip's layout explicit;
    missing blit support or transfer-source usage selects a CPU area-average chain. CPU sRGB RGB
    filtering decodes/averages/encodes in linear light, while alpha remains linear. The upload
    reports `SingleLevel`, `GpuBlit`, or `CpuUpload`.
  - `--sample texture-mips` / `texture-mips-cpu`: seven-level texture sampling with digits 0-6
    selecting explicit LOD, A starting implicit minification/zoom, and Space pausing/resuming.
    Sandbox owns input/time; the Vulkan owner records eight-byte scale/LOD push constants.
    Full-range view, sampler and descriptor remain immutable across LOD/zoom and extent changes;
    R now requests an independently owned replacement texture.
  - M2C-1 private `VulkanSubmissionProgress`: each successful renderer graphics submission
    receives a nonzero, nonwrapping serial retained by its frame slot. Successful fence waits
    advance host-observed completion monotonically; failed submit/wait leaves progress unchanged.
    Stats derive serials from the owner, and Sandbox reports both after WaitIdle. Initial signaled
    fences, startup uploads, acquire/present and scanout have distinct completion contracts.
  - M2C-2 Task 1 private `detail::FindUploadRange` / `UploadArenaState`: overflow-safe aligned
    ranges, separate preview/commit, nonempty sealing, successful-submission identity, and
    completion-gated reset. This is CPU metadata only; it does not allocate GPU storage or change
    the renderer. The Task 3 arena composes it with its owned buffer.
  - M2C-2 Task 2 `VulkanBufferDesc::persistentMapping`, default false: opt-in VMA automatic
    mapping retained with allocation ownership and transferred/cleared through Buffer moves.
    Read-only `IsPersistentlyMapped()` reports the retained mapping; CPU pointers stay private.
    Both mapping paths retain host-intent/range checks, write Flush and read Invalidate. Only
    explicit transient maps are manually unmapped. Existing startup uploads remain transient.
  - M2C-2 Task 3 private move-only `VulkanUploadArena`: one bounded persistent transfer-source
    Buffer plus the existing CPU policy. Append publishes a borrowed slice only after write/flush;
    sealing freezes the batch, and successful submission serials gate reuse. Moves retain pending
    identity; no allocation growth or implicit GPU waits. `VulkanFrameResources::CreateUploadArenas`
    checks complete initialization, allocator/device identity and idle empty slots, then publishes
    all slot allocations together. Task 4 enables this only for streamed texture geometry.
  - M2C-2 Task 4 `streamTextureGeometry`, default false, requires generated-mip texture shaders.
    Both mip Sandbox entries enable it. Each slot retains a 64 KiB arena and a 76-byte device-local
    destination. CPU-scaled vertex/index uploads and explicit dependencies precede Dynamic Rendering;
    streamed draws push scale 1 and retain LOD. Existing static paths remain separate. Reuse follows
    CanBegin -> command-pool reset -> Begin; successful submit associates the arena and counts bytes
    before Present. Capacity and submitted payload counters are reported at normal Sandbox shutdown.
  - M2C-2 Task 5 independently submits 64 deterministic batches through two upload slots, using
    aligned and odd-sized ranges, per-slot GPU readback, transfer-write to host-read barriers and
    post-fence invalidation. It rejects stale completion reuse without changing batch state, keeps
    source handles/capacity/allocation count stable, and returns live allocation counts/bytes to the
    baseline. Streamed pixel readback covers explicit LOD 0/6, implicit minification and scale 1/0.5
    for both GPU-generated and CPU-generated mip paths.
  - M2C-3 Task 1 private `detail::ResourceRetirementQueue<Resource, MaxPending>` owns unique
    resource pointers with final last-use serials. Fixed slots accept out-of-order retirement;
    collection releases only completed entries. Null/full rejection preserves ownership, with
    no queue growth or GPU wait. The outer owner still must drain before queue destruction.
    Initially tested with CPU owners; Task 3 now integrates it with native texture bindings.
  - M2C-3 Task 2 private `VulkanTextureBinding`: one immutable Image/View/Sampler and dedicated
    Descriptor Pool/Set owner. Creation transfers the caller's image only after all children are
    ready; rejection preserves it. Pool/Set destruction precedes sampler/view and image/allocation.
    `VulkanTexturedQuadPipeline` retains the shared descriptor/pipeline layouts and draw pipeline,
    takes only completed upload destinations, and binds the new owner's set. No upload, wait,
    transition or runtime retirement is hidden in the binding. Per-binding pools permit independent
    release at the cost of one pool per texture version; general descriptor allocation is deferred.
  - M2C-3 Task 3 runtime texture replacement: sample-local `RequestTextureReplacement` copies
    64x64 RGBA8 input and returns Accepted/Busy/Failed. One upload and four retirement slots bound
    ownership. Nonblocking `VulkanImageUpload::Poll` treats NOT_READY as progress. Publication
    creates a new immutable binding and retires the old one using its last successful draw serial;
    successful frame-fence waits collect completed versions. Upload completion does not advance
    frame progress. WaitIdle/recreation/shutdown drain accepted uploads and draws; partial/failure
    paths retain the existing destruction contract. Both mip samples expose R palette switching,
    replacement count, pending request and retirement counters. No steady-state GPU idle wait is
    introduced; CPU preparation and allocation costs remain, and the request is not a general API.
  - M2D-1 public value-only `VulkanMemoryStats` and explicit optional queries on Allocator/Renderer
    report allocation count/bytes and reserved block count/bytes across VMA heaps. Queries do not
    submit, wait or collect. Clear/empty/failed owners report unavailable. Sandbox logs startup,
    observed upload/replacement/retirement changes and post-WaitIdle values; active resources still
    exist at that exit snapshot. Native budgets and ImGui remain later.
  - M2D-2 `VulkanResourceStats` reports separate Buffer/Image counts, requested bytes and VMA
    allocation bytes through an explicit optional Renderer query. Existing resource owners sum
    staging, upload destinations, geometry, frame arenas, active and retired bindings once each.
    Retired image allocation bytes are a subset of Image bytes, not an additional allocation.
    Image requested bytes cover tightly packed RGBA8 pixels across all mips. The existing Sandbox
    event logs include these fields; query calls do not submit, wait, publish or collect resources.
- Verified:
  - 2026-11-06, M2D-2: VS2026 Debug build and 11 existing memory-statistics, frame-lifecycle and
    texture-replacement cases passed with actual synchronization/submit-time validation, no skips
    and zero validation errors/warnings. Independent VMA totals match Buffer+Image counts/bytes;
    checks cover non-square mip payloads, moves, pending uploads, full/partial retirement and
    drained baselines. No new test cases, full-suite repeat, second configuration or manual sample
    replay. Sandbox log formatting is source/build verified; new fields were not visually inspected.
  - 2026-11-05, test consolidation: removed five standalone GPU cases and combined command-line
    routing/trailing-argument cases; CTest discovery now reports 153 cases (previously 165).
    Frame-lifecycle combinations decreased from ten to six, retaining both presentation modes.
    Debug test-target build and the affected 14 cases passed with actual synchronization validation,
    no skips and zero validation errors/warnings. No full suite, second configuration or manual
    replay was repeated. Coverage tradeoffs and selective-test policy are in the Windows build guide.
  - 2026-11-05, M2D-1: focused red/green verified missing-query detection, then eight cases /
    30694 assertions passed. Final full VS2026 Debug/RelWithDebInfo builds and opt-in CTest passed
    165/165 each, no skips, real Khronos synchronization/submit-time validation and zero validation
    errors/warnings. Tests cover independent native totals, buffer/image release, wrapper/renderer
    moves, query side effects and stable live allocation baselines after repeated texture swaps.
    User-confirmed Debug GPU/CPU mip runs independently logged 21/14 replacements and exit 0;
    all drained observations returned to five allocations / 154784 allocation bytes, with zero
    validation errors/warnings. Detailed scope, transitions and logs are in the M2D-1 design.
  - 2026-11-05, M2C-3 acceptance on `c6083e5`: the user confirmed both mip samples, including a
    focused CPU retry. GPU/CPU retry exited 0 after 4/5 replacements, matching submitted/completed
    serials 4216/3372, zero pending retirements/uploads and exact 76-byte-per-frame payload totals.
    RenderDoc 1.43/MCP confirms the GPU draw samples the new seven-mip image and renders its blue
    checkerboard/magenta marker at LOD 0. CPU replacement/LOD-6 captures are absent. Manual logs
    retained swapchain generation 1, so recreation evidence remains the automated tests. Capture
    runs mute validation; separate 2026-11-04 CTest results remain authoritative for synchronization.
    No source/build/test rerun. Detailed scope is in the runtime replacement plan.
  - 2026-11-04, M2C-3 Task 3: buildable stubs failed 6 focused cases / 13 assertions at missing
    replacement, last-use and Poll behavior; 9 existing/negative cases passed. The green focused
    Debug run passed 15 cases / 29038 assertions. Both full VS2026 Debug/RelWithDebInfo builds and
    full opt-in CTest passed 163/163 per configuration, no skips, actual Khronos synchronization/
    submit-time validation, zero validation errors/warnings. Readback verifies new LOD 0/6 pixels,
    copied input, old output before publication, last use after upload submission, full-queue
    rejection, partial collection and allocation count/byte cleanup. Renderer cases exercise 32
    repeated replacements plus moves, resize/minimize/restore and pending-upload shutdown for
    single-level/GPU/CPU mip modes, with both presentation modes. Source/format/diff review passed.
    Manual R-key/RenderDoc, native failure injection and VS2022 remain unverified. Logs/results:
    `build/diagnostics/m2c3-replacement-20261104/`; full build logs: `build/m2c3-task3-*-build.log`.
  - 2026-11-04, M2C-3 Task 2: buildable stubs failed the two new GPU ownership cases; the empty-parent
    CPU case passed. The final focused Debug run passed 14 cases / 37370 assertions. Both full VS2026
    Debug/RelWithDebInfo builds and full opt-in CTest passed, 157/157 per configuration, no skips,
    actual Khronos synchronization/submit-time validation and zero validation errors/warnings.
    Tests cover rejected ownership transfer, independent binding destruction through serial-0
    retirement, surviving descriptor validity and allocation count/byte cleanup. Existing GPU
    texture/mip/streaming readback and frame lifecycle regressions pass. Source/format/diff review
    passed; no new manual/RenderDoc acceptance or native failure injection is claimed. See the
    texture-binding plan and `build/diagnostics/m2c3-texture-binding-20261104/` (ignored).
  - 2026-11-04, M2C-3 Task 1: buildable stubs failed all 9 new cases; the implementation passed
    9 cases / 93 assertions in Debug. OwlUnitTests target builds passed on VS2026 Debug and
    RelWithDebInfo; focused CPU CTest passed 28/28 per configuration without skips, including
    submission identity, upload-arena policies and byte accounting. Formatting/diff checks pass.
    No native resource integration, new full GPU/validation/Sandbox run or VS2022 verification
    is claimed. See the retirement-queue plan and `build/m2c3-task1-*.log` (ignored).
  - 2026-11-04, M2C-2 Task 6 on revision `336a30e`: the user confirmed both mip samples' fixed
    LOD, automatic zoom/pause, resize/minimize/restore and normal close. Separately, both captured
    VS2026 Debug instances exited with code 0 and `Shutdown complete`; completed/submitted
    serials match at 14661 and 14325, capacity is 131072 bytes each, and payload is exactly
    76 bytes per submission. RenderDoc 1.43/MCP replays both LOD-0 frames: copy before one
    six-index draw, the same 76-byte destination used for vertex/index input, exact exported
    geometry and matching checkerboard render targets. Source review found no blocking issues.
    These captures mute validation output; the separate 2026-10-19 full runs below remain the
    synchronization-validation evidence. No source changed and build/CTest were not rerun.
    Full capture details, evidence limits and diagnostics are in the frame-slot upload plan.
  - 2026-10-19, M2C-2 Task 5 on VS2026 Debug/RelWithDebInfo: both full builds and full opt-in CTest
    passed 145/145 per configuration, no skips, actual Khronos synchronization/submit-time validation,
    zero validation errors/warnings. The final focused Debug run passed 2419 assertions across the
    stress case and two streamed pixel cases. Readback, stale serial,
    dual-slot reuse, stable allocation behavior and LOD 6/implicit output are covered. Native failure
    injection, non-coherent hardware, manual controls/visual capture and VS2022 remain unverified.
    Logs/results: `task5-final-*` in `build/diagnostics/m2c2-stream-geometry-20261016/`.
  - 2026-10-16, M2C-2 Task 4 on VS2026 Debug/RelWithDebInfo: both full builds and full opt-in CTest
    passed 143/143 per configuration, no skips, actual Khronos synchronization/submit-time validation,
    zero validation errors/warnings. Renderer tests cover both mip/present modes, resize/minimize,
    invalid controls, moves and exact upload statistics. Independent streamed LOD-0 pixel readback
    checks scale 1/0.5, marker/light/dark and reusing a destination; static sampling checks remain.
    Both formal mip samples in both configurations exited normally with code 0, matching completed/
    submitted serials, capacity 131072 and payload bytes = 76 * submits, with validation active.
    Independent source review found no blockers. No new manual control/visual or RenderDoc capture
    acceptance is claimed. Broader streamed LOD checks, sustained data stress, native fault injection,
    non-coherent hardware and VS2022 remain unverified. See linked Task 4 diagnostics.
  - 2026-10-16, M2C-2 Task 3 on VS2026 Debug/RelWithDebInfo: both full builds and full opt-in CTest
    passed 138/138 per configuration without skips, with actual Khronos synchronization/submit-time
    validation and zero validation errors/warnings. GPU copies/readback cover odd and aligned
    payloads, exhaustion preserving prior data, two submissions before waiting, stale-completion
    rejection, pending arena/native-parent moves, completed replacement and reuse. Live allocation
    counts/bytes return to baseline. Invalid/mismatched/repeated creation is rejected. Independent
    source review found no blockers; second-allocation and native flush failures were not injected.
    Coherent upload memory was selected; native non-coherent behavior, new visual/RenderDoc capture
    and VS2022 remain unverified. Logs/results: the linked Task 3 diagnostics.
  - 2026-10-16, M2C-2 Task 2 on VS2026 Debug/RelWithDebInfo: both full builds succeeded, and
    full opt-in CTest passed 135/135 per configuration without skips. Khronos synchronization
    and submit-time validation were actually loaded with zero validation errors/warnings.
    Tests exercise transient/persistent mapping, repeated partial access, empty/end/overflow
    rejection, moved-from access, cross-mode replacement, allocator-wrapper moves, live-allocation
    release, SequentialWrite-to-GPU copy, and exact persistent readback for existing upload sizes.
    Eight independent allocations shared one VMA block and retained isolated bytes in both modes;
    individual allocation offsets were not queried. Mapped Random/readback flags were 14
    (host-visible/coherent/cached); native non-coherent behavior remains unverified. No new manual
    visual or RenderDoc acceptance was performed. Logs/results: the linked Task 2 diagnostics.
  - 2026-10-09, M2C-2 Task 1: `OwlUnitTests` target builds succeed in VS2026 Debug/RelWithDebInfo.
    New CPU tests first failed against placeholders, then passed 12 cases / 285 assertions in
    the focused Debug run. CTest covering these cases plus existing submission and buffer policies
    passes 20/20 in each configuration, without skips. Formatting/diff checks pass. This task
    adds no native Vulkan calls; full GPU/validation/Sandbox regression was not rerun. Logs are
    under `build/m2c2-task1-*.log` (ignored); the previous 122/122 full run remains historical.
  - 2026-10-08, M2C-1 on VS2026 Debug/RelWithDebInfo: both builds and full opt-in CTest passed
    122/122 per configuration without skips, with Khronos synchronization/submit-time validation
    active and zero validation errors/warnings. Five CPU cases cover exact result handling,
    timeout/device-loss noncompletion, initial fence state, reused identities, completion not
    regressing and serial exhaustion. Real-GPU Clear/Triangle/Texture/mip frame tests check
    bounded monotonic progress, successful submit counts, minimize deferral, moves, recreation,
    both presentation modes and WaitIdle equality. CPU policy tests model error results;
    native driver failure injection and this revision on VS2022 remain unverified.
    Formal Clear Sandbox normal-close runs exited with code 0 and reported matching serials:
    Debug 59/59, RelWithDebInfo 56/56. Those elevated-process runs did not load validation;
    validation coverage comes from the separate full opt-in CTest runs above.
  - 2026-10-08, VS2026 Debug/RelWithDebInfo: both builds and full opt-in CTest passed 117/117
    without skips, with Khronos synchronization/submit-time validation active and zero errors/warnings.
    Mip frame cases cover explicit LOD 0-6, implicit minification, both present-fence paths and
    resize/minimize/restore. Independent 64x64 offscreen UNORM draws verify marker/light/dark at
    LOD 0, analytical average at LOD 6, and implicit sampling matching explicit LOD 4 while
    differing from LOD 0. Both GPU blits and forced CPU generation were exercised.
    RenderDoc 1.43/MCP replay confirms one six-index draw, seven image levels, an eight-byte
    Shader block, expected checkerboard versus uniform average, and a shrinking automatic frame.
    Synthetic digit/A/Space events changed the title and captured output; this does not replace
    human acceptance of the keyboard/animation feel. Initial elevated diagnostic runs lacked
    Layer-path overrides; final verification used process-local medium-integrity child processes.
    No system Layer registration changed. MCP automatic launch failed; target-control capture
    and MCP file replay/query/export succeeded. New sample acceptance on VS2022 remains unverified.
  - 2026-09-30, M2B-3 generated mips on VS2026 Debug/RelWithDebInfo: CPU tests cover odd extents, malformed input,
    sRGB linear-light filtering and alpha. GPU tests read back every level of a forced CPU 7x3 chain,
    exercise automatic fallback without transfer-source usage, and read back lower levels from a
    capability-gated RGBA8 GPU blit. Full opt-in CTest passed 106/106 in both configurations,
    with Khronos synchronization validation active and no validation errors. Both upload paths
    were exercised on NVIDIA GeForce RTX 5060; visible mip sampling was unverified at that checkpoint.
  - 2026-09-30, M2B-2 textured quad on VS2026 Debug/RelWithDebInfo: both builds and full opt-in
    CTest presets passed 99/99 each, without skips. The texture frame test covered resize,
    minimize/restore, move, idle, and both present-fence modes; Khronos synchronization validation
    was active with zero errors/warnings in focused runs of both configurations. RenderDoc 1.43 captured a
    replayable Vulkan frame with one six-index draw, a 64x64 sampled RGBA8 image, and no messages.
    The exported 1280x720 output and pixel reads show the sharp checkerboard, red top-left marker,
    and intact green clear background. At that checkpoint, VS2022 and generated mips were unverified.
  - 2026-09-29, M2B-2 image upload on VS2026 Debug/RelWithDebInfo: both builds and full opt-in
    CTest presets passed (92/92 each, no skips); Khronos synchronization/submit-time validation
    was enabled with zero validation errors/warnings. New tests cover both RGBA8 formats, 1x1,
    1x7, 7x1, 7x3 and 17x9, checkerboard/asymmetric patterns and repeated exact readback.
    Pending owner moves, parent wrapper moves, rejection/handoff rules, staging release and final
    live allocation counts/bytes passed, alongside existing image/view/sampler and renderer tests.
    Default focused testing passes four CPU cases and skips four GPU cases as intended.
    Staging/readback flags were 6/14 (coherent); non-coherent hardware, sampling output and native
    failure injection remain unverified. Earlier resource-only evidence is in its linked plan.
  - 2026-09-29, M2B-1 on VS2026 Debug/RelWithDebInfo: both builds passed; full opt-in CTest
    passed 73/73 per configuration without skips, with Khronos synchronization/submit-time
    validation enabled and zero validation errors/warnings. Eight live host allocations shared
    one VMA block; nonzero-offset read/write isolation, allocator moves, live allocation release,
    wrong-device upload rejection, host-intent enforcement and exact upload roundtrips passed.
    Local host/readback flags were 14 (visible/coherent/cached), staging 6, geometry 1; native
    non-coherent cache behavior remains unexercised. No new RenderDoc/pixel comparison this slice.
    Formal Debug Triangle/Clear/Smoke passed resize/maximize/minimize/restore and close/Escape,
    each exiting with code 0. Triangle uploaded 66 bytes once (80-byte VMA slice), submitted 305
    indexed draws and reached swapchain generation 5; Vulkan logs had no validation errors/warnings.
    Smoke's inspected module list contained no Vulkan Loader.
  - 2026-09-29: the user reported M2A tests complete and authorized the next step. The message
    did not specify additional configurations or coverage beyond the existing recorded evidence.
  - 2026-09-24, M2A on VS2026 Debug/RelWithDebInfo: builds passed. With the existing local
    Khronos Validation Layer 1.4.357 and synchronization/submit-time validation enabled, full
    CTest passed 71/71 in each configuration with no skips and zero validation errors/warnings.
    Coverage includes host mapping/moves/replacement, exact 1/3/4/65/66/67/4097-byte roundtrips
    plus triangle bytes, single-transfer destination ownership, and Clear/Triangle lifecycle.
    The staged triangle reported 66 logical bytes, 80 allocation bytes, staging flags 6
    (host-visible/coherent), and destination flags 1 (device-local) on the local RTX 5060.
    Terminal cleanup branches were reviewed, not validated by native failure injection.
  - Formal Debug Triangle/Clear/Smoke each passed resize, maximize, minimize/restore, and normal
    close/Escape with exit code 0. Triangle presented 310 indexed frames; Clear presented 309
    frames with no indexed draws. Both Vulkan samples reached swapchain generation 4 and logged
    zero validation errors/warnings. The triangle uploaded geometry once across these changes.
  - Fresh RenderDoc 1.43 capture/MCP replay of the M2A Debug triangle: one indexed draw at EID 16,
    three indices/one instance; exported 66-byte Buffer 98 matches the CPU payload exactly.
    Native replay confirms vertex offset/stride 0/20 and index offset/stride 60/2 on that buffer.
    The 1280x720 target matches the earlier M1 capture pixel-for-pixel (921,600 pixels, zero diffs).
    Captured process loaded only the intended 1.43 capture DLL and closed with exit code 0.
  - 2026-09-14, VS2026 Debug and RelWithDebInfo: both build presets succeeded; each default CTest
    preset reported 58 passed, 5 opt-in GPU tests skipped, and 0 failed.
  - Both configurations passed all five GPU tests with `OWL_RUN_VULKAN_BOOTSTRAP_TEST=1` on
    NVIDIA GeForce RTX 5060 (reported API 1.4.341, unified family 0; loader 1.4.350).
    Swapchain checks cover 320x180, 640x360, and 480x270, moves including live-owner replacement,
    preflight failure preservation, explicit zero-size deferral on minimize, and recreation on
    restore. Each created swapchain had 3 images/views; device-level objects remained unchanged.
  - Clear and triangle tests each presented 300 frames per mode with automatic present fences enabled
    and explicitly disabled: 640x360, resize to 800x450 and 480x270, minimize without submission,
    restore, move, idle, and cleanup. Both modes passed in both configurations.
  - Catch2 discovery now uses `PRE_TEST`: after building both configurations, CTest JSON listings
    and verbose GPU runs confirm each preset selects its own executable. The previous shared
    post-build list could select the last-built configuration instead of the requested one.
  - Validation Layer was unavailable during the 2026-09-14 runs; those GPU passes do not establish
    validation coverage. See the newer validation-enabled result below.
  - The formal Debug Clear entry presented 4,980 frames and exited with code 0 via Escape.
    Its loaded module was the app-local Vulkan Loader; both deployed configurations' DLL hashes
    matched their vcpkg package DLLs. A running Debug Smoke process had no Vulkan Loader module
    and exited with code 0 through the window-close path.
  - The user previously confirmed the temporary 640x360 green clear probe visually.
  - 2026-09-14: the user reported normal results for the formal Clear sample's manual checks.
    This records user-reported acceptance, not an independent capture or validation-layer run.
  - Formal Debug Triangle launched from a different working directory, presented 10,820 frames,
    and exited with code 0 via Escape without Validation Layer. Missing deployed fragment SPIR-V
    reported its path and returned code 4; rebuilding restored the generated asset. Both runtime
    configurations' deployed shader hashes matched the checked-in assets.
  - GLSL compiled and validated using pinned glslang 16.5.0 for Vulkan 1.3/SPIR-V 1.6. Stage linking
    and repeated binary hashes passed; source, binary, and regeneration instructions are checked in.
  - 2026-09-14: the user confirmed the triangle is visible in the formal sample. This establishes
    user-reported visual output, not independent pixel inspection or validation-layer coverage.
  - 2026-09-23, VS2026 Debug: RenderDoc 1.46 captured and replayed the formal Clear sample with
    the expected green output. The existing MCP's 1.43 replay runtime rejected that newer format.
  - RenderDoc 1.43 then captured the formal Triangle sample on NVIDIA GeForce RTX 5060. The target
    loaded only the intended 1.43 capture DLL and closed normally with exit code 0. Existing MCP
    opened the capture, reported one indexed draw (3 indices, 1 instance), queried VS/PS reflection
    and the 1280x720 sRGB target, and exported the expected colored triangle. Pixel reads passed.
    Capture used RenderDoc target control; MCP file replay/query/export passed, not its automatic
    launch tool. No captured debug messages were reported; this does not establish validation-layer
    coverage. Capture-layer selection was process-local; system registration was not changed.
  - 2026-09-23, VS2026: configure and Debug/RelWithDebInfo builds succeeded. Each default CTest
    preset passed 58 tests and skipped the five opt-in GPU tests. With Khronos Validation Layer
    1.4.357 and synchronization/submit-time validation enabled, each configuration passed all five
    GPU test cases with zero validation errors and warnings after correcting the acquire-to-layout
    transition dependency. Tests cover automatic presentation fences and forced compatibility mode.
  - CTest now rejects `[error] [VulkanValidation]` output. The rule rejected the original
    RelWithDebInfo binary's Clear/Triangle tests with exit code 8, then passed with exit code 0
    after rebuilding the correction. Direct test/Sandbox execution still requires log inspection.
  - The corrected formal Debug Triangle presented 12,124 frames/indexed draws and exited with
    code 0; Clear presented 707 frames and Smoke also exited with code 0. Programmatic resize,
    maximize, minimize, restore, and close/Escape completed; both Vulkan samples reached swapchain
    generation 5. Complete logs contained zero validation errors or warnings. Recording overlays
    were disabled for these diagnostic processes; SDK layer registration was not changed.
  - 2026-09-23: the user confirmed M1 has no remaining issue, closing the pending manual visual
    acceptance. Together with the recorded validation and RenderDoc results, this completes M1
    for the local VS2026 scope. This is user-reported visual acceptance, not a new automated run.
- Unverified:
  - The current revision on VS2022; validate it on the separate VS2022 computer.
  - GPU creation on hardware with separate graphics/present families (CPU policy tests cover it).
  - Native resource/submit/fence failure injection and driver-returned out-of-date recovery;
    policy branches and synchronization/lifetime code are tested/reviewed, not fault-injected.
  - Native non-coherent flush and live color-format replacement paths: policy/static review covers
    them, but this GPU selected coherent memory and resizing retained the color format.

## Milestones

| Milestone | Delivery | Validation | Completion condition |
| --- | --- | --- | --- |
| M0 Reproducible Engineering Baseline | Complete | VS2026 Debug and RelWithDebInfo build/tests pass; cross-toolchain support is defined by presets and CI | Preserve clean-clone configure, build, test, and smoke workflows |
| M1 Vulkan Bootstrap and Frame Lifecycle | Complete for local VS2026 scope | Both configurations' CPU/GPU tests, Debug 12,124-frame synchronization-validation run, earlier RenderDoc/MCP inspection, and user visual acceptance | Met locally; retain the unverified portability and fault-injection limits above |
| M2 GPU Resources, Memory, and Uploads | M2D-2 classification implemented; current suite has 153 cases | M2D-2 affected 11 Debug cases pass with validation; earlier M2D-1 full CTest and manual log acceptance retained | Safe uploads/retirement with frames in flight, resource statistics, and validation-clean stress checks |
| M3-M15 | Planned | Unverified | Follow the approved milestone roadmap and per-milestone design gates |

## Decisions and Constraints

- Confirmed:
  - Keep native Vulkan calls visible through the reference renderer; extract RHI only after real
    Vulkan call sites exist, then use D3D12 to challenge Vulkan-shaped assumptions.
  - Vulkan 1.3, Dynamic Rendering, and Synchronization2 are the M1 desktop baseline.
  - `VulkanDevice` owns only its logical device and borrows its physical device and queues. Its Instance must outlive
    it; before destruction or replacement the caller must finish GPU work, destroy child objects,
    and exclude concurrent host access. Destruction performs no implicit idle wait.
  - `VulkanSwapchain` owns its swapchain and views, not the images or parents. Recreate/destroy
    requires completed use, released external dependents, and no concurrent access; no implicit wait.
    `Deferred` preserves resources but suspends acquisition. Preflight failures preserve resources;
    once `vkCreateSwapchainKHR` is called, the old chain is retired and destroyed even on failure.
  - `VulkanTriangle` owns renderer objects and waits before replacement/destruction; its borrowed
    Window must outlive it and remain unmoved. All renderer calls belong to the window thread.
    The caller pumps events and throttles deferred frames; no per-frame device-idle wait is used.
  - Submission/completion serials describe this renderer's frame submissions on one graphics
    queue. They record CPU-observed fence completion, not live GPU polling or WSI release.
    Successful submit followed by out-of-date Present still consumes a serial. Device loss
    never advances normal completion. Startup uploads retain their separate blocking fences.
  - Keep Vulkan 1.3 as the minimum. Present fences are optional, with KHR preferred over EXT.
    On unsupported devices, use the approved device-idle recreation/shutdown fallback and report
    its presentation-resource completion guarantee limitation rather than claiming strict WSI safety.
  - Support both VS2022 and VS2026 through separate CMake Presets; validate each on its available
    computer rather than requiring duplicate third-party source versions.
  - Use pinned vcpkg manifest dependencies. SDL3 remains behind `OwlPlatform`.
  - Update this file only for durable phase, capability, contract, workflow, validation, blocker,
    or next-task changes; do not use it as a chronological discussion log.
  - Maintain `docs/learning/graphics-api-notes.md` for transferable low-level graphics API concepts.
    Consolidate mechanisms and API differences, not individual questions, project details, or
    inferred learner assessments. Consult relevant topics before explanations; update only for
    durable knowledge and respect explicit read-only requests.
- Open:
  - Select an open-source license before the first public release.
- Known limitations:
  - Device selection currently returns the first suitable physical device; there is no adapter
    scoring, discrete-GPU preference, or user override.
  - Device-selection surface preferences are snapshots; Swapchain creation/recreation re-queries
    current Surface support instead of trusting those snapshots.
  - Minimized windows can retain a nonzero framebuffer size in SDL; the renderer checks window
    flags as well as pixel size. Surface/device loss is terminal; automatic recovery is deferred.
  - Presentation fences establish resource release, not display scanout completion. Compatibility
    fallback runs successfully locally but lacks the same specification-level release guarantee.
  - Buffer allocation uses VMA. Static geometry and textures retain blocking startup uploads;
    streamed geometry uses the graphics queue and waits only when reusing its frame slot.
    Runtime textures now use a separate bounded upload and serial-based retirement. General
    transfer scheduling, a Shader System and RHI remain later work.

## Entry Points and Evidence

- Authoritative roadmap: `docs/superpowers/specs/2026-08-17-owlengine-roadmap-design.md`
- Active milestone design: `docs/superpowers/specs/2026-09-23-owlengine-m2-design.md`
- First M2 implementation plan: `docs/superpowers/plans/2026-09-23-owlengine-m2a-buffer-upload.md`
- Current allocation slice: `docs/superpowers/plans/2026-09-29-owlengine-m2b-vma.md`
- Current texture resource slice: `docs/superpowers/plans/2026-09-29-owlengine-m2b-image-resources.md`
- Current texture upload slice: `docs/superpowers/plans/2026-09-29-owlengine-m2b-image-upload.md`
- Textured draw slice: `docs/superpowers/plans/2026-09-30-owlengine-m2b-textured-quad.md`
- Mip sampling slice: `docs/superpowers/plans/2026-10-08-owlengine-m2b-mip-sampling.md`;
  independent pixel tests in `tests/vulkan/TextureSamplingTests.cpp`.
- Submission progress slice: `docs/superpowers/plans/2026-10-08-owlengine-m2c-submission-progress.md`;
  state-policy and real-GPU frame expectations in `tests/vulkan/FrameTests.cpp`.
- Frame-slot upload storage plan (Tasks 1-6 accepted locally):
  `docs/superpowers/plans/2026-10-09-owlengine-m2c2-frame-upload-storage.md`;
  `engine/vulkan/src/VulkanUploadArena.{h,cpp}`, `tests/vulkan/UploadArenaTests.cpp`, and
  `tests/vulkan/UploadArenaIntegrationTests.cpp`.
- Resource retirement queue (Task 1 CPU ownership verified; Task 3 runtime integration verified):
  `docs/superpowers/plans/2026-11-04-owlengine-m2c3-retirement-queue.md`;
  `engine/vulkan/src/VulkanResourceRetirement.h` and `tests/vulkan/ResourceRetirementTests.cpp`.
- Immutable texture binding (Task 2 native ownership extraction verified):
  `docs/superpowers/plans/2026-11-04-owlengine-m2c3-texture-binding.md`;
  `engine/vulkan/src/VulkanTextureBinding.{h,cpp}` and `tests/vulkan/TextureBindingTests.cpp`.
- Runtime replacement (Task 3 local acceptance recorded with explicit evidence limits):
  `docs/superpowers/plans/2026-11-04-owlengine-m2c3-runtime-replacement.md`;
  `VulkanTexturedQuadPipeline`, `VulkanTriangle`, `TextureSamplingTests.cpp` and `FrameTests.cpp`.
- Allocator statistics (M2D-1, confirmed, implemented and locally accepted):
  `docs/superpowers/specs/2026-11-05-owlengine-m2d-memory-statistics-design.md`;
  `VulkanMemoryStats.h`, `VulkanAllocator`, `VulkanTriangle`, `VulkanSample.cpp`,
  `tests/vulkan/MemoryStatsTests.cpp` and the existing renderer lifecycle tests.
- Resource classification (M2D-2, implemented and focused Debug verified):
  `docs/superpowers/specs/2026-11-06-owlengine-m2d-resource-statistics-design.md`;
  `VulkanResourceStats.h`, resource-owner `ResourceStats()` methods, `VulkanTriangle`,
  `VulkanSample.cpp` and existing memory/lifecycle/replacement tests.
- Completed M1 design/plan: `docs/superpowers/specs/2026-08-19-owlengine-m1-design.md` and
  `docs/superpowers/plans/2026-08-19-owlengine-m1.md`
- Build documentation: `README.md` and `docs/building/windows.md`
- Test selection/coverage: `docs/building/windows.md#choose-tests-for-the-change`. Routine work
  uses affected Debug tests; full/multi-configuration regression is reserved for milestone or
  materially broader changes. Do not repeat tests for unchanged code or documentation-only edits.
- Build/test definitions: `CMakePresets.json`, root/module `CMakeLists.txt`, and
  `tests/CMakeLists.txt`
- Device contract and tests: `engine/vulkan/src/VulkanDevice.h`, `tests/vulkan/DeviceTests.cpp`,
  and `tests/vulkan/VulkanBootstrapTests.cpp`
- Swapchain contract and tests: `engine/vulkan/src/VulkanSwapchain.h` and
  `tests/vulkan/SwapchainTests.cpp`
- Allocator/buffer/upload ownership: `engine/vulkan/src/VulkanAllocator.h`, `engine/vulkan/src/VulkanBuffer.h`,
  `engine/vulkan/src/VulkanBufferUpload.h`, and `tests/vulkan/BufferUploadTests.cpp`.
- Image/view/sampler ownership: `engine/vulkan/src/VulkanImage.h`, `VulkanImageView.h`,
  `VulkanSampler.h`; policies and lifecycle cases in `tests/vulkan/ImageTests.cpp` and
  `tests/vulkan/ImageResourceTests.cpp`.
- Image upload ownership: `engine/vulkan/src/VulkanImageUpload.h`; policies in
  `tests/vulkan/ImageUploadTests.cpp`, real GPU readback in `tests/vulkan/ImageUploadIntegrationTests.cpp`.
- Frame lifecycle and tests: `engine/vulkan/include/owl/vulkan/VulkanTriangle.h`,
  `engine/vulkan/src/VulkanTriangle.cpp`, `engine/vulkan/src/VulkanFrame.h`, and
  `tests/vulkan/FrameTests.cpp`
- Interactive clear entry: `samples/owl_sandbox/src/ClearSample.cpp`; launch and manual checks
  are documented in `docs/building/windows.md`.
- Triangle entry/resources: `samples/owl_sandbox/src/TriangleSample.cpp`,
  `engine/vulkan/src/VulkanTrianglePipeline.cpp`, `samples/owl_sandbox/assets/m1/README.md`, and
  `docs/learning/m1-triangle.md`.
- Texture entry/resources: `samples/owl_sandbox/src/TextureSample.cpp`,
  `engine/vulkan/src/VulkanTexturedQuadPipeline.cpp`, and `samples/owl_sandbox/assets/m2/README.md`.
- Low-level graphics API concepts: [graphics API notes](docs/learning/graphics-api-notes.md).
  Keep engineering details and project validation status in their existing documents.
- Current validation evidence: the VS2026 build/CTest and both-configuration GPU results above.
  Reproduce the GPU checks with the opt-in commands in `docs/building/windows.md`.
  Local RenderDoc 1.43 capture, MCP-exported PNG, and inspection results are under `build/diagnostics/`.
- Validation setup and acceptance findings: `docs/learning/m1-vulkan-bootstrap.md`; detailed
  2026-09-23 logs are in `build/diagnostics/m1-validation-20260923/` (local, ignored). Use
  `fixed-gpu/`, `fixed-sandbox/`, and `fixed-verification.md` for the corrected implementation.
- M2A acceptance logs, scripts, captured frame, exported pixels/buffer, and verification summary:
  `build/diagnostics/m2a-20260924/` (local, ignored). Validation uses the existing copy-only SDK;
  RenderDoc uses process-local `VK_IMPLICIT_LAYER_PATH`, with system registration unchanged.
- M2B-1 local validation logs and scripts: `build/diagnostics/m2b-vma-20260929/` (ignored).
- M2B-2 resource validation logs and scripts: `build/diagnostics/m2b-images-20260929/` (ignored).
- M2B-2 image upload logs, red/green checks and verification:
  `build/diagnostics/m2b-image-upload-20260929/` (local, ignored).
- Mip sampling validation logs/results, capture scripts, four RenderDoc frames and exported
  fixed/automatic output: `build/diagnostics/m2b-mip-sampling-20261008/` (local, ignored).
- Submission progress red/green checks and both-configuration validation logs/results:
  `build/diagnostics/m2c-submission-progress-20261008/` (local, ignored).
- Persistent Buffer mapping red/green checks and both-configuration validation logs/results:
  `build/diagnostics/m2c2-persistent-mapping-20261016/` (local, ignored);
  build logs: `build/m2c2-task2-*-build*.log`.
- Upload arena red/green checks and both-configuration validation logs/results:
  `build/diagnostics/m2c2-upload-arena-20261016/` (local, ignored);
  build logs: `build/m2c2-task3-*-build.log`.
- Streamed geometry red/green checks, full validation and formal Sandbox normal-close results:
  `build/diagnostics/m2c2-stream-geometry-20261016/` (local, ignored);
  build logs: `build/m2c2-task4-*-build.log`.
- Task 5 stress/pixel and full validation: `task5-final-*` in the same diagnostics directory;
  build logs: `build/m2c2-task5-*-build.log` (local, ignored).
- Task 6 captures, exported geometry/images, MCP evidence and shutdown statistics:
  `build/diagnostics/m2c2-acceptance-20261104/` (local, ignored).
- M2C-3 Task 1 red/green tests, target builds and focused CPU CTest:
  `build/m2c3-task1-*.log` (local, ignored).
- M2C-3 Task 2 red/green lifecycle tests, sampled regressions and full synchronization validation:
  `build/diagnostics/m2c3-texture-binding-20261104/`; full build logs:
  `build/m2c3-task2-*-build.log` (local, ignored).
- M2C-3 Task 3 replacement red/green, readback and full synchronization validation:
  `build/diagnostics/m2c3-replacement-20261104/`; full build logs:
  `build/m2c3-task3-*-build.log` (local, ignored).
- M2C-3 local manual reports, capture replay/exports and shutdown counters:
  `build/diagnostics/m2c3-acceptance-20261105/`, including `cpu-recheck/` (local, ignored).
- M2D-1 red/green, full synchronization validation and sample memory/exit records:
  `build/diagnostics/m2d1-statistics-20261105/`; full build logs:
  `build/m2d1-{debug,relwithdebinfo}-build.log` (local, ignored).
- Test consolidation: `build/diagnostics/test-pruning-20261105/` (discovery and focused validation);
  `build/test-pruning-debug-build.log` (local, ignored).
- M2D-2 focused synchronization validation: `build/diagnostics/m2d2-statistics-20261106/`;
  Debug build: `build/m2d2-debug-build.log` (local, ignored).

## Next Work

- Next: propose the next M2D slice using the verified allocator/resource snapshots. Heap-budget
  negotiation and ImGui integration remain unimplemented and require technical proposals.
  Preserve completed-use collection, bounded replacement ownership, per-slot upload reuse and
  single-mip/generated-mip sampled-pixel regressions.
- Preserve Clear/Smoke and staged Triangle regressions. Keep the graphics queue initially;
  heap budgets and the debug panel remain M2D. RHI and additional
  queue families remain out of scope. VS2022 validation belongs to the separate workstation.
