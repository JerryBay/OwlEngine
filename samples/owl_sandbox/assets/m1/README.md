# M1 triangle shaders

These sample-local GLSL 450 sources and their precompiled SPIR-V binaries are kept together.
The normal configure/build/run path copies the `.spv` files and needs no shader compiler or
Vulkan SDK. Recompile both files after changing their source; shader build integration belongs
to the later Shader System milestone.

Both entry points are `main`. The vertex shader consumes location 0 `vec2` position and
location 1 `vec3` color, writes clip-space `(x, y, 0, 1)`, and passes color at location 0.
The fragment shader writes interpolated RGB with alpha 1 to color attachment location 0.
There are no descriptors or push constants.

## Regenerate

The checked-in binaries were generated with the official
[glslang 16.5.0 Windows x86-64 release](https://github.com/KhronosGroup/glslang/releases/tag/16.5.0),
whose `glslang.exe --version` reports `Glslang Version: 11:16.5.0`.
Download `glslang-16.5.0-windows-x86_64-release.zip` from that release and extract it into
the ignored `.tools/glslang-16.5.0` directory. Its SHA-256 is:

```text
06b71298b750268c127f2ee7ae0ef7525e2068120c6c8a3a08b2f58ca6f325ce
```

From the repository root, run these PowerShell commands:

```powershell
$triangleCompiler = '.tools/glslang-16.5.0/bin/glslang.exe'
& $triangleCompiler -V --target-env vulkan1.3 --spirv-val -e main -o samples/owl_sandbox/assets/m1/triangle.vert.spv samples/owl_sandbox/assets/m1/triangle.vert
if ($LASTEXITCODE -ne 0) { throw 'Vertex shader compilation or validation failed' }
& $triangleCompiler -V --target-env vulkan1.3 --spirv-val -e main -o samples/owl_sandbox/assets/m1/triangle.frag.spv samples/owl_sandbox/assets/m1/triangle.frag
if ($LASTEXITCODE -ne 0) { throw 'Fragment shader compilation or validation failed' }
& $triangleCompiler -l -q samples/owl_sandbox/assets/m1/triangle.vert samples/owl_sandbox/assets/m1/triangle.frag
if ($LASTEXITCODE -ne 0) { throw 'Shader stage interface validation failed' }
```

The target is Vulkan 1.3 / SPIR-V 1.6. `--spirv-val` runs the SPIRV-Tools validator
included in this compiler; the final link command checks the vertex/fragment interface.
The generation and validation commands completed successfully, and a second compilation
produced identical binary hashes:

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `triangle.vert.spv` | 1008 | `6a48a69f7dd2140f6642037dc25d872b8d436f558760829fcd073aed19975a05` |
| `triangle.frag.spv` | 500 | `cd7a3144e77a4f00676c8288a68bdffc03db07fbc7efffe628dbb4dfaf764da0` |

These checks verify shader compilation, SPIR-V validity, and the stage interface. Actual
rendering also needs GPU integration tests and a visual check in the Sandbox.
