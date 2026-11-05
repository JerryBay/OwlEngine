# M2D-1 Allocator Memory Statistics

**Status: Confirmed and implemented — local automated and sample log acceptance passed.**
**Date:** 2026-11-05
**Baseline:** `c6083e5`, runtime texture replacement and deferred binding retirement.
**Parent design:** [M2 GPU resources](2026-09-23-owlengine-m2-design.md#statistics-and-debug-surface).

## Goal and Current Evidence

Make the difference between live allocation storage and reserved Vulkan memory visible while
replacing textures. At the baseline, the renderer already reported submission progress, staging
capacity, uploaded payload, replacement count and retirement count. VMA is pinned at 3.4.0;
tests used `vmaCalculateStatistics`, while production `VulkanAllocator` exposed only native handles.
The allocator does not currently enable the memory-budget extension. No ImGui dependency exists.

This first slice supplies measured allocator totals and diagnostic logs. The broader M2D resource
classification, logical resource sizes, retired allocation bytes, heap budget and ImGui panel
remain subsequent slices; completing this proposal does not complete M2D.

## Proposed Ownership, Interface and Flow

- `VulkanAllocator` owns the query. Read basic `VmaBudget::statistics` from `vmaGetHeapBudgets`
  over the allocator's actual heap count, and sum allocationCount/allocationBytes/blockCount/
  blockBytes. Ignore `usage` and `budget`; they have different meanings and may be estimates.
  Use stack/value data only. No resource registry, shared telemetry owner or global counter.
- Expose a Vulkan-specific value snapshot through a new, explicit
  `VulkanTriangle::QueryMemoryStats()` returning an optional snapshot. Keep native VMA structs
  private. Existing per-frame `Stats()` remains a cheap counter read and does not trigger a
  memory query. All calls retain the existing window-thread serialization contract.
- Fields mean **VMA allocation count**, **bytes occupied by those allocations**, **VkDeviceMemory
  block count**, and **bytes reserved in those blocks**, respectively. They cover this renderer's
  VMA allocator across all its heaps, not the whole process, all device memory or only VRAM.
  They do not count descriptors, pipelines or swapchain-owned allocations. Allocation count is
  not a universal resource count; allocation bytes are not the application's logical payload.
- Sandbox logs a snapshot at renderer startup, whenever replacement/pending-upload/retirement
  counters change, and after explicit WaitIdle on normal exit. Detect changes using existing
  cheap stats; query once per observed change rather than every frame. Use labels distinguishing
  VMA totals from existing upload/pending counters. No new key, timer or rendering pass is needed.

## State, Failure and Compatibility

- Query does not submit, wait, collect, map, allocate GPU storage or change resource ownership.
  A query during an upload includes whatever live VMA allocations exist at that host observation.
  It does not prove GPU completion or detect a leak by itself.
- Empty/moved-from/failed renderer returns no snapshot, displayed as unavailable rather than
  a misleading all-zero result. A valid empty allocator may report valid zero counts.
  Clear also reports unavailable because its existing path never creates a VMA allocator.
- Moving an initialized renderer preserves its underlying allocator and query values. Native
  parents retain their existing lifetime contract; the snapshot contains values, not borrowed handles.
- Released allocations must reduce live counts/bytes. Reserved blocks may remain cached, so
  tests must not require block bytes to return to zero or claim cached storage is a leak.
  The exit snapshot follows WaitIdle but precedes renderer destruction: active geometry, upload
  arenas and the current texture still exist, so live allocation values should match startup.
- Preserve Vulkan 1.3, VS2022/VS2026 presets, all rendering paths, shaders, upload/retirement
  behavior and dependency versions. No native memory-budget feature negotiation in this slice.

## Confirmed Decisions

1. **Scope:** recommend the four VMA totals plus existing upload/retirement counters first.
   This is an intentional incremental design, not an API limitation. Adding resource categories,
   requested bytes and retired bytes now would broaden wrapper/queue metadata and its move tests.
2. **Collection:** recommend basic per-heap statistics and an explicit value snapshot. Alternative:
   `vmaCalculateStatistics` gives detailed fragmentation data but traverses allocator structures.
   Per-frame detailed queries would add unnecessary debug overhead. No performance gain is claimed
   until measured; basic queries also have a nonzero cost.
3. **Presentation:** recommend event-triggered logs first. Alternative: integrate ImGui immediately,
   adding dependency/input/rendering lifetime work to this change. The approved long-term panel
   remains planned after the data contracts are useful and verified.

The user approved these three defaults, the explicit query interface and logging triggers on
2026-11-05. Resource lifetimes and the overall M2 direction remain unchanged.

## Acceptance After Approval

- Tests first: empty/unavailable query; known buffer/image allocation count deltas; live bytes
  growing with allocations; blockBytes >= allocationBytes; stable observations without mutation;
  wrapper/renderer moves preserving values; release returning live values to their baseline.
- Runtime replacements must temporarily retain old/new/upload allocations according to ownership,
  then return live allocation count/bytes to the stable renderer baseline after drain. Keep cached
  reserved blocks separate and verify existing retirement/upload counters simultaneously.
- Build VS2026 Debug/RelWithDebInfo, run the affected native tests and appropriate full regression
  with actual synchronization validation. Exercise the sample log changes with R and normal exit.
  VS2022 remains the other workstation's acceptance; no new visual rendering parity claim is needed
  for the data-only slice. ImGui/heap-budget tests belong to their later proposals.

Reference: [VMA statistics documentation](https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/statistics.html).
The pinned local VMA header is the implementation authority if online docs change.

## Implementation and Verification on 2026-11-05

- `VulkanMemoryStats.h` is a public value-only four-counter snapshot; no native VMA/Vulkan types
  are exposed. `VulkanAllocator::QueryMemoryStats()` sums basic statistics over the actual heaps.
  `VulkanTriangle::QueryMemoryStats()` delegates only for a valid renderer. No GPU command,
  wait, allocation or retirement is performed by either query; normal Stats remains unchanged.
- Sandbox logs startup, observed texture-state changes and post-WaitIdle values. It checks
  counters both after input and after rendering, so an accepted upload is observable even if
  the GPU finishes before that frame's Poll. There is no per-frame allocator query when idle.
- Buildable empty query stubs failed four of eight focused cases (seven assertions); the same
  `[memory-stats],[texture-replacement]` filter passed after implementation: eight cases,
  30694 assertions, no skips, zero validation errors/warnings.
- Final VS2026 Debug and RelWithDebInfo full builds passed. Each full opt-in CTest passed
  **165/165**, no skips, with the real Khronos layer, process-local synchronization/submit-time
  validation settings and zero validation errors/warnings.
  This is the pre-consolidation checkpoint. The later user-requested test cleanup leaves 153
  discovered cases and verifies the affected 14 Debug cases only; see the
  [retained coverage](../../building/windows.md#current-coverage-after-consolidation).
- Allocator tests compare basic totals against independent detailed VMA traversal, then check
  buffer/image deltas, repeated observation, moves and release. Existing renderer lifecycle
  tests now verify Clear unavailable, stable allocation totals through moves, retained upload
  storage without completion/publication side effects, and live totals returning to startup
  after repeated single-level/GPU/CPU replacement and drain in both presentation modes.
- The user confirmed both Debug mip windows switched textures normally and exited. GPU/CPU
  logs independently record 21/14 replacements and exit 0, 5581/6032 submitted and completed
  frames, no pending upload/retirements, active synchronization validation and zero errors/warnings.
  Every drained observation returned to the same startup baseline: five allocations, 154784
  allocation bytes. Upload increased counts to seven, publication reduced them to six, and
  retirement collection restored five. Peak allocation bytes were 194720 (GPU) / 200192 (CPU);
  both retained two blocks totaling 67108864 reserved bytes. These are local observations,
  not portable expected constants or a claim of whole-process memory usage.
- The GPU/CPU logs contained 65/44 memory snapshots across thousands of frames, exactly startup,
  three state transitions per replacement and post-WaitIdle. The local acceptance script checks
  every drained baseline, avoids unchanged-state duplicate logs, and checks frame completion and
  76-byte-per-submission upload totals. See `sample-acceptance.json` in the diagnostics directory.
- Source/diff review is single-agent. Device-failure injection, VS2022, other memory architectures
  and performance measurements remain unverified. No new RenderDoc capture was needed for this
  data-only slice; visual acceptance is the user's report. M2D resource categories, budget and
  ImGui remain later.
- Local evidence (ignored): `build/diagnostics/m2d1-statistics-20261105/`; full build logs:
  `build/m2d1-{debug,relwithdebinfo}-build.log`; red build: `build/m2d1-red-build.log`.
