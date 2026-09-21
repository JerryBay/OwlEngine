# M1: from clear to an indexed triangle

The existing acquire, frame fences, image transitions, Dynamic Rendering scope, submit, and present
sequence stays in `VulkanTriangle`. Providing `VulkanTriangleOptions::triangleShaders` enables a
draw inside that rendering scope. Leaving it absent preserves the original clear-only sample.
`OwlSandbox` selects the sample and asset paths; Vulkan objects remain private to `OwlVulkan`.

## Geometry and memory

`VulkanTrianglePipeline` owns one immutable buffer containing three vertices followed by three
16-bit indices. A vertex is two position floats and three color floats. Buffer usage enables both
vertex and index reads; the two binding commands point to different byte offsets in the same buffer.
This is sample geometry ownership, not a general GPU buffer or allocator abstraction.

Buffer creation describes size and use; allocation provides the backing memory. The chosen memory
type must appear in the buffer's `memoryTypeBits` and be host-visible. Coherent memory is preferred;
otherwise the code flushes the whole mapped allocation before unmapping. `offset=0` and
`VK_WHOLE_SIZE` avoid a partial non-coherent atom range. Queue submission makes preceding flushed
host writes available for GPU reads. No CPU writes occur while frames are in flight.

This direct upload keeps the first memory contract visible. It does not promise optimal discrete-GPU
placement: device-local buffers plus staging and a reusable upload path belong to the next resource
milestone. A D3D12 implementation will express upload/default heap placement and resource states
through different API contracts; this sample does not try to unify them yet.

References: [host-to-device synchronization](https://docs.vulkan.org/spec/latest/chapters/synchronization.html)
and [mapped memory ranges](https://docs.vulkan.org/refpages/latest/refpages/source/VkMappedMemoryRange.html).

## Shader modules and graphics state

The checked-in GLSL/SPIR-V pair has explicit location-based interfaces and no descriptors or push
constants. `ReadTriangleSpirv` reads aligned `uint32_t` storage and rejects missing/truncated files
and invalid binary headers. It is not a semantic validator; generation uses SPIRV-Tools validation.
Temporary Shader Modules are released after graphics pipeline creation. Small CPU copies of the
SPIR-V remain available if the surface color format later changes.

The graphics pipeline connects the vertex layout, vertex/fragment entry points, triangle-list
assembly, rasterization, multisampling, and color output. Pipeline Layout is empty because this draw
has no shader resources. Depth, blending, culling, and MSAA are disabled for the first triangle.
The viewport has positive height: negative clip-space Y is toward the top of this sample's window.

Dynamic Rendering still requires pipeline color-format compatibility, expressed with
`VkPipelineRenderingCreateInfo`. Viewport and scissor are dynamic, so changing only window extent
reuses the pipeline. Changing the color format rebuilds it after the renderer has finished submitted
work. Geometry and device objects survive swapchain recreation. Pipeline, layout, buffer, and memory
are released before the device; the borrowed window remains alive through all renderer cleanup.

Reference: [Khronos Dynamic Rendering sample](https://docs.vulkan.org/samples/latest/samples/extensions/dynamic_rendering/README.html).

## Verification boundary

CPU tests cover the memory-type filter and shader-file envelope. Opt-in GPU tests exercise indexed
draw submission and rendered window lifecycle in both presentation-fence and compatibility modes.
Counters establish submitted work, not visual correctness. A visual check and RenderDoc draw/output
inspection are separate acceptance steps; a run without Validation Layer does not establish a
validation-clean result. See `PROJECT.md` for the current evidence and open M1 checks.
