# M2D-3 Memory Heap Budgets

**Status:** Confirmed scope; implemented, focused Debug and bounded sample logs verified.
**Baseline:** `0c2b270`, resource classification and retired-image accounting.

## Scope and ownership

The user approved per-heap capacity, properties, usage/budget and VMA allocation/block statistics,
using native Vulkan properties and `vmaGetHeapBudgets`, with read-only queries, existing event logs
and affected Debug tests. No ImGui, dependency upgrade, eviction policy or RHI abstraction.

- Device enumerates extensions once and enables optional `VK_EXT_memory_budget` when supported.
  Vulkan 1.3 supplies its properties2 dependency. Missing support does not reject the device.
  A private creation switch allows the same machine to exercise the disabled-extension path.
- Allocator sets the corresponding VMA creation flag only when Device enabled the extension.
  Its capability flag follows moves and clears with reset. Queries return value snapshots in a
  fixed 16-entry array (checked against Vulkan's limit); no native headers leak to sample consumers.
- Each entry contains heap index, `VkMemoryHeapFlags`, device-local classification, heap size,
  estimated usage/budget, and this allocator's allocation/block counts and bytes. Host-visible,
  coherent and cached flags belong to memory types and are not merged into heap properties.
- Renderer delegates the optional query. Empty/moved-from/failed/Clear renderers and devices
  without the extension return unavailable. Existing allocation/resource statistics remain usable
  without the extension. No emulated driver budget is presented on that path.

## Freshness and meaning

Pinned VMA 3.4.0 caches driver budgets. Renderer calls `vmaSetCurrentFrameIndex` through Allocator
at RenderFrame entry and after successful explicit WaitIdle, refreshing host budget data without
GPU submission, waiting or resource collection. Startup uses VMA's creation-time fetch. Query
reads that cache (VMA can refresh it after enough allocation operations); frame `Stats()` remains
cheap. The existing owner-thread contract applies. This adds a host property query per frame,
as recommended by VMA; no performance improvement is claimed.

Usage is a process estimate, potentially including allocations outside VMA. Budget is a changing
guideline for total usage, not remaining space or a guarantee that an allocation succeeds.
Heap size is physical capacity, not free capacity. Usage may exceed budget. VMA adjusts cached
usage for its block changes and sanitizes invalid driver budget values, so logs label these as
estimates from the extension via VMA, not exact raw driver measurements.

Enabling budgets also lets VMA use them in its existing block-sizing heuristics. This slice adds
no `WITHIN_BUDGET` allocation restriction, automatic unloading or rejection threshold.

## Validation

Extend existing memory tests for per-heap native properties and independent detailed VMA totals,
enabled/disabled extension paths, moves and release. Extend existing device move and renderer
lifecycle checks. Dynamic usage/budget values must not be asserted constant across queries.
Run the affected Debug selection with actual synchronization validation; inspect one bounded
Sandbox log run. No new independent cases, full suite, second configuration or manual replay.

References: [VMA budget integration](https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/staying_within_budget.html)
and [Vulkan budget semantics](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceMemoryBudgetPropertiesEXT.html).
The pinned local VMA header is the implementation authority.

## Verification — 2026-11-06

- VS2026 Debug build passed. `[memory-stats],[device],[frame-lifecycle]` passed 15 existing cases /
  57654 assertions, no skips, actual Khronos synchronization and submit-time validation loaded,
  zero errors/warnings. Discovery remains 153 cases. Memory tests run the same checks with the
  optional extension enabled and actively disabled; real extension absence on other GPUs remains
  unverified. Device bootstrap independently checks reported extension availability.
- Native physical-device heap sizes/flags and detailed VMA per-heap allocation/block statistics
  match the snapshots. Moves preserve capabilities and ownership; release restores live totals.
  Query calls remain inside existing renderer side-effect checks during pending replacement.
- The final bounded GPU mip sample rendered 105 frames, closed normally and exited 0. Its two
  heap entries were logged at startup and after WaitIdle, with zero validation errors/warnings.
  Device-local heap: size 8278507520 bytes, budget 7473201152 bytes; estimated usage changed from
  36290560 to 58179584 bytes while its VMA allocation bytes stayed 23712. These are observations
  on this workstation, not portable expected constants or a claim that usage equals live resources.
- No full suite, RelWithDebInfo, VS2022, manual controls or RenderDoc replay was repeated. No
  performance claim; the new host-side budget refresh cost has not been benchmarked.
- Local ignored evidence: `build/m2d3-debug-build.log` and
  `build/diagnostics/m2d3-budget-20261106/` (`focused-Debug`, `sample-result`, sample stdout/stderr).
