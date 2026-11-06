# M2D-2 Resource Classification and Retired Image Bytes

**Status:** Confirmed scope; implemented and focused Debug verification passed.
**Baseline:** `f7a9ff6`, M2D-1 allocator totals and selective testing.

The user approved continuing with Buffer/Image counts and bytes, requested versus allocated
storage, and retired texture allocation bytes, aggregated through existing owners and displayed
in the existing event logs. GPU lifetime behavior remains unchanged.

## Ownership and Accounting

- New value-only `VulkanResourceStats` contains separate Buffer/Image usage (count, requested
  bytes, allocation bytes) plus `retiredImageAllocationBytes`. `QueryResourceStats()` exposes an
  optional renderer snapshot alongside the existing independent VMA `QueryMemoryStats()`.
- Buffer reports its requested size and existing allocation size. Image reports tightly packed
  RGBA8 UNORM/SRGB pixels summed across every mip, using the actual dimensions at each level;
  this is logical payload, not a VkImage native layout. VMA supplies the actual allocation slice.
  No extra resource metadata, global registry or allocator-wrapper pointer is retained.
- Upload owners count staging and destinations only while still owned. Geometry, frame arenas,
  the active texture binding and queued retired bindings each contribute once. A const queue
  visitor observes its at-most-four bindings without collecting or exposing mutable entries.
- Retired bytes are a subset of Image allocation bytes. They stay visible until observed GPU
  completion permits collection; physical GPU completion alone does not update host ownership.
  Counts/bytes include queued uploads before first draw and completed but untransferred images.
- Empty leaf owners contribute zero. Empty, moved-from, failed and Clear renderers report no
  snapshot, consistent with M2D-1. Moves and handoffs are reflected by current ownership. Views,
  samplers, descriptors, pipelines and swapchain images are excluded from these VMA statistics.
- Queries perform no GPU submit/wait, mapping, allocation, publication or collection. Existing
  window-thread serialization applies. The small owner/mip traversal occurs only on explicit
  queries; the cheap per-frame counters remain separate.

## Tradeoffs and Validation

Direct aggregation adds small read methods to resource owners but avoids incremental counters
that must be repaired on failure, move and handoff. This is a Vulkan learning slice, not an RHI
or resource manager. It reports requested bytes, not fragmentation or total process VRAM.
Heap budgets, ImGui and additional formats remain later work.

Extend existing tests instead of adding cases: compare Buffer+Image allocation counts/bytes to
independent VMA totals; verify requested mip bytes, ownership moves, startup/pending uploads,
full and partial retirement collection, and drained baselines. Run affected Debug cases with
actual synchronization validation, keeping GPU/CPU pixel tests in that selection. Do not repeat
the full suite or a second configuration for this slice. Sample log inspection and actual manual
interaction are recorded separately; no RenderDoc capture is required for these numeric fields.

## Verification Record — 2026-11-06

- VS2026 Debug build passed. The existing `[memory-stats],[frame-lifecycle],[texture-replacement]`
  selection passed 11 cases / 54795 assertions, with no skips and actual Khronos synchronization
  and submit-time validation loaded; zero validation errors and warnings.
- Native VMA totals independently match owner classification. Checks cover a 7x3 three-level
  RGBA8 chain (100 logical bytes), wrapper/renderer moves, pending upload resources, GPU/CPU mip
  replacement and sampled pixels, full/partial retirement collection, and drained baselines.
- No independent cases were added. The full suite, RelWithDebInfo and manual sample interaction
  were not repeated. The Sandbox log fields are source/build verified, not manually inspected.
- Local ignored evidence: `build/m2d2-debug-build.log` and
  `build/diagnostics/m2d2-statistics-20261106/focused-Debug.{log,json}`.
