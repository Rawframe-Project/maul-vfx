# Kernel containers

The GPU part's kernels are WGSL sources under `rhi/kernels/` (and the test-only ones under `test/kernels/`), compiled offline into Maul RHI shader containers that are committed as C headers (`*_container.h`). A build of the library needs no shader tools; changing a kernel does. This page says how to rebuild them and what CI checks. [gpu.md](../internals/gpu.md) explains why the kernels are built this way.

## What CI checks

CI has none of the shader tools, so it cannot rebuild a container. `python3 tools/gen_kernels.py --check` needs none of them: each committed container must carry, byte for byte, a fresh splice of its kernel's WGSL sources (the container keeps the WGSL beside the compiled forms). A kernel part edited without a rebuild fails the check. The check also refuses `atomicStore`, which the pinned SPIRV-Cross crosses into HLSL that DXC refuses; kernels write `_ = atomicExchange(&a, v);` instead.

## Rebuilding

Needs, on the path:

- `naga` (the WGSL compiler of the wgpu project; the committed containers were built with naga 30.0.1),
- `spirv-val` and `spirv-cross` (the Vulkan SDK's, or Ubuntu's `spirv-tools` and `spirv-cross` packages; SPIRV-Cross 2021.01.15 as Ubuntu 24.04 ships it),
- `dxc` (DirectX Shader Compiler); with its validator `libdxil` beside it the DXIL is signed,

and a source tree of Maul RHI at the version the build uses (`MAUL_VFX_RHI_VERSION` in `cmake/Rhi.cmake`), for its container tools (`tools/mrhi_container.py`, `mrhi_dxil.py`, `mrhi_msl.py`).

```sh
git clone --depth 1 --branch v<version> https://github.com/Rawframe-Project/maul-rhi.git ../maul-rhi
python3 tools/gen_kernels.py ../maul-rhi            # every kernel
python3 tools/gen_kernels.py ../maul-rhi sim gas    # some, by name
```

For each kernel the script splices its sources, compiles them to SPIR-V with naga, validates them for Vulkan 1.3, crosses them to DXIL and to Metal with Maul RHI's tools, and writes the container with the WGSL beside the compiled forms into `<name>_container.h`. Each kernel's bindings are described by `<name>.json` beside it.

Commit the sources and the containers together, then run the GPU part's suites on lavapipe (`ctest --preset linux-rhi-lavapipe`, see [development.md](development.md#the-gpu-parts-tests)): CI runs them only at night.

## Splicing

A line `// include: PATH.wgsl` in a kernel is replaced by `rhi/kernels/PATH.wgsl` before naga reads it, each file once. `shared/` holds what several kernels splice (functions with no bindings); a kernel's own directory (`sim/`, `gas/`) holds the parts only it splices, cut at their top-level declarations so no file passes the family's length limit. `rhi/kernels/shared/marching_words.wgsl` is written by `tools/gen_marching_cubes.py`, not by hand.

## Size

The containers are large (`sim_container.h` alone is tens of megabytes) because each carries SPIR-V, DXIL, Metal and WGSL for every entry point. Git stores them compressed; a change to a kernel rewrites its container whole. They are kept in the repository rather than fetched, because a build must not need the shader tools or the network.
