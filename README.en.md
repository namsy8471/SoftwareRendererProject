<div align="center">

<h1>Software Renderer Project</h1>

<p><strong>A 3D graphics pipeline implemented directly on the CPU without a GPU rasterizer</strong></p>

<p>
  <a href="README.md">🇰🇷 한국어</a> ·
  <strong>🇺🇸 English</strong> ·
  <a href="README.ja.md">🇯🇵 日本語</a>
</p>

<p>
  <img alt="C++26 Preview" src="https://img.shields.io/badge/C%2B%2B-26%20Preview-00599C?logo=cplusplus">
  <img alt="CPU Renderer" src="https://img.shields.io/badge/renderer-CPU-7B61FF">
  <img alt="oneTBB" src="https://img.shields.io/badge/parallelism-oneTBB-FFC20A">
  <img alt="Windows" src="https://img.shields.io/badge/platform-Windows-0078D4?logo=windows">
</p>

<p>
  <a href="https://youtu.be/3StOC1_IjT4"><strong>▶ Watch the Korean presentation</strong></a>
  &nbsp;·&nbsp;
  <a href="https://youtu.be/AWwQuzhvvnM">Watch the Japanese presentation</a>
</p>

</div>

---

This CPU-based software renderer began as a way to verify the rendering-pipeline fundamentals that felt abstract while working with DirectX 11. It implements the full path—from model transforms, clipping, rasterization, depth testing, and lighting to post-processing and presentation—and then applies SIMD and tile-based multithreading on top of that foundation.

| Development | Period | Environment | Presentation |
|:---:|:---:|:---:|:---:|
| **Solo project** | 2025-07-07–2025-08-27 | C++26 Preview · MSVC v145 · Windows | WinAPI DIB · GDI |

## Preview

### Performance comparison

| Before optimization | After optimization |
|:---:|:---:|
| ![Before optimization: 8 FPS](https://github.com/user-attachments/assets/a8bf1116-44d3-42cb-b58f-d5229d874cd4) | ![After optimization: 58 FPS](https://github.com/user-attachments/assets/8cb37072-23fa-4ebc-9cae-0bb7e25ededb) |
| **8 FPS** | **58 FPS** |

### Debug visualizations

| Wireframe | AABB | Normal vectors |
|:---:|:---:|:---:|
| <img src="https://github.com/user-attachments/assets/c22bb7bd-24b1-4339-894c-616426d648c9" alt="Wireframe debug view" width="100%"> | <img src="https://github.com/user-attachments/assets/ee8e5c99-7a6e-42c6-b56e-84313f51a172" alt="AABB debug view" width="100%"> | <img src="https://github.com/user-attachments/assets/c3c06817-227d-4ac8-b756-f737de7136cb" alt="Normal-vector debug view" width="100%"> |

### FXAA

| Before | After |
|:---:|:---:|
| ![Before FXAA](https://github.com/user-attachments/assets/1afcc928-aa3e-4001-b0e2-845a360d3c66) | ![After FXAA](https://github.com/user-attachments/assets/0c303273-3a2b-40f6-a8f7-178497ab5726) |

## Key outcomes

| Area | Result |
|---|---|
| Rendering path | Implemented directly on the **CPU from vertex processing to pixel output** |
| Captured performance comparison | **8 FPS → 58 FPS** |
| Parallel work unit | **16×16 tiles** + oneTBB task scheduling |
| SIMD paths | Baseline **128-bit SSE** + runtime-selected **256-bit AVX** |
| Spatial acceleration | Object-AABB frustum culling + octree-node culling |
| Debug tools | Wireframe · Normal · AABB · Light Direction |
| Build matrix | Debug/Release × Win32/x64 |

The performance images were captured on 2025-08-27. Results vary with the CPU, resolution, scene, and build configuration, so these figures should not be interpreted as a standardized benchmark.

## What this project demonstrates

### Graphics pipeline

- Model-view-projection transforms and viewport mapping
- Sutherland–Hodgman frustum-plane clipping
- Back-face culling and a reciprocal-depth buffer
- Triangle rasterization using fixed-point edge functions
- Perspective-correct attribute interpolation
- Directional-light Phong illumination
- OBJ/MTL and diffuse-texture loading
- Bresenham/DDA line drawing and FXAA

### Systems and optimization

- A render queue and 16×16 tile binning
- oneTBB-based command processing and tile rasterization
- Thread-local vertex caches, stamps, clip buffers, and triangle pools
- SSE operations in the custom `SRMath` library
- Runtime dispatch between SSE and AVX after CPUID/XCR0 validation
- Coarse/fine frustum culling using AABBs and an octree
- Move-only RAII for the GDI back buffer and structured asset errors
- Modern C++ design using C++20/23/26 features as API contracts

## Rendering architecture

```text
SoftrendererProject.exe
├─ Framework
│  ├─ WinAPI window / input / frame loop
│  └─ Camera / GameObject update
├─ Asset Pipeline
│  ├─ OBJ / MTL parser
│  ├─ Texture loader
│  └─ Missing-normal generation
├─ Scene Submission
│  ├─ AABB / Frustum / Octree culling
│  └─ RenderQueue
└─ Renderer
   ├─ Vertex transform / back-face culling / clipping
   ├─ 16×16 tile binning
   ├─ Parallel rasterization / depth / Phong shading
   ├─ Optional FXAA
   └─ GDI DIB Present
```

```text
OBJ · MTL · Texture
→ Scene update
→ Frustum / Octree culling
→ RenderQueue
→ Vertex transform / clipping
→ Tile binning
→ Rasterization / depth test / shading
→ Optional FXAA
→ GDI Present
```

Render commands are collected from scene objects. Triangles that affect the screen are distributed into tile bins, and separate tiles are processed in parallel by oneTBB workers. The final pixels are written to a 32-bit DIB back buffer and presented with `BitBlt`.

## Problem-solving cases

This section records not only the final changes, but also the problems encountered, their causes, the solutions, and the resulting behavior.

| Case | Problem encountered | Core solution |
|---|---|---|
| C++ standards and resource lifetime | Manual initialization/release and raw handles were fragile on failure paths | RAII, explicit conversions, `/W4 /WX` |
| SIMD math | Repeated vector/matrix work was a CPU-renderer bottleneck | Custom `SRMath`, SSE, selective AVX |
| Clipping | New intersection vertices made ordering and winding difficult to preserve | Ping-pong clip buffers + triangle fan |
| Near-camera geometry | Projected coordinates exceeded screen and Z-buffer bounds | Call-stack analysis, clipping, and clamping |
| Missing normals | Some teapot faces had no normal data, breaking shading | Accumulate face normals and generate smooth normals |
| MTL names | Material lookup failed for keys containing a colon | Fallback lookup using the suffix after the last colon |
| Multithreading | Single-threaded rasterization and repeated allocation dominated frame time | Tile rendering, oneTBB, TLS caches/pools |

### 1. C++ standards compliance and resource lifetime

**Problem:** The initial structure relied on the call order of `Init`/`Shutdown` and on raw WinAPI/GDI handles. A failure during initialization or a window resize required a cleanup sequence different from the normal shutdown path, while implicit conversions and third-party warnings could obscure issues in project-owned code.

**Root-cause analysis:** Resource ownership and release order were expressed only through calling conventions and comments rather than through types. A DIB bitmap, in particular, cannot be represented safely by one raw pointer: the original object selected into the DC must be restored before that bitmap is deleted.

**Solution:** Constructors and destructors now manage lifetime, while the screen DC, memory DC, DIB bitmap, and previously selected object are grouped into the move-only `Renderer::GdiBackBuffer`. Conversions use `static_cast` and boundary-specific `reinterpret_cast` explicitly. Project code is compiled with `/W4 /WX`, while vendor headers are isolated behind the external-warning boundary.

**Result:** Intermediate failures and resize paths follow the same RAII cleanup rules as normal shutdown. Project-owned source remains buildable with warnings treated as errors after third-party diagnostics are excluded.

### 2. SIMD optimization for mathematical operations

**Problem:** A software renderer repeats matrix multiplication, dot products, normalization, and related math for every vertex, so their CPU cost accumulated directly in the total frame time.

**Root-cause analysis:** 3D vectors and 4×4 matrices repeatedly perform the same floating-point patterns, and scalar-only code does not use the processor's SIMD execution width. However, widening every single `vec4` operation to AVX would leave half of a 256-bit register unused and would not guarantee a benefit.

**Solution:** A custom math library, `SRMath`, uses SSE intrinsics for the baseline 128-bit implementations of matrix multiplication, dot products, normalization, and other core operations. Only a batch that transforms two `vec4` values by the same matrix is implemented in a separate AVX translation unit. At runtime, the program checks AVX and OSXSAVE through CPUID and verifies XMM/YMM state preservation through XCR0 before selecting the SSE or AVX function.

**Result:** Single-vector operations retain the naturally matched SSE width, while AVX is used only where eight floats can fill the register. This reduced the math bottleneck as part of the overall optimization work, but the published FPS improvement is not attributed to SIMD alone.

### 3. Vertex ordering and triangle reconstruction during clipping

**Problem:** Clipping a triangle successively against six frustum planes with the Sutherland–Hodgman algorithm creates intersection vertices and changes the polygon's vertex count. If their order is disturbed, winding and back-face decisions become invalid and malformed triangles can reach the rasterizer.

**Root-cause analysis:** The output polygon from each plane becomes the input to the next. Reading from and writing to one buffer can overwrite vertices that have not yet been processed. The final polygon is also not guaranteed to contain exactly three vertices, so it must be triangulated again.

**Solution:** Input and output alternate between ping-pong clip buffers. An intersection vertex is inserted whenever the inside/outside state differs between the previous and current vertices. Once all planes have been processed, the ordered polygon is converted into a triangle fan around its first vertex. Because the maximum clip size is bounded, the buffers were also changed to fixed-capacity storage.

**Result:** Polygons crossing frustum boundaries retain their winding and are passed to the rasterizer consistently. Repeated heap allocation was also removed from this small clipping hot path.

### 4. Out-of-range access for near-camera geometry

**Problem:** When an object was extremely close to the camera, its projected coordinates could extend far beyond the viewport and produce an out-of-range memory access in the Z-buffer.

**Root-cause analysis:** The invalid index was traced with the debugger's call stack and memory watch. Large projected coordinates near the near plane were reaching the rasterization bounding box without being constrained to the viewport, and the resulting indices were used to access the depth and pixel buffers.

**Solution:** Clip-space frustum tests and polygon clipping were applied before rasterization. NDC and tile ranges are constrained to the screen with `std::clamp`, and the final pixel write also performs a boundary check to prevent any out-of-buffer write.

**Result:** Near-camera objects and triangles crossing the viewport now iterate only over valid pixel coordinates, eliminating the observed out-of-range access in the test scenes.

### 5. Reconstructing missing normal vectors

**Problem:** Some polygons in the Utah Teapot OBJ had no normal-vector data, causing inconsistent Phong shading or visibly incorrect brightness in those regions.

**Root-cause analysis:** Positions and indices were valid, but some vertices did not reference a valid `vn`. A zero normal cannot provide meaningful diffuse or specular directions when it reaches the lighting calculation.

**Solution:** The loader checks whether every mesh vertex has a valid normal. If any are missing, it computes a face normal from the cross product of two triangle edges, accumulates that normal into all three vertices, and normalizes the accumulated values to generate smooth normals. Degenerate faces use a safe fallback direction.

**Result:** OBJ files with incomplete normal data can be rendered with consistent shading without editing the source asset manually.

### 6. MTL material names containing colons

**Problem:** When an OBJ `usemtl` name contained a colon (`:`), it sometimes failed to match the key parsed from the MTL file, so the expected color and texture were not loaded.

**Root-cause analysis:** The OBJ could use a name such as `object-part:material`, while the MTL map contained only the actual material name after the colon. Looking up only the full string left the mesh on the default material with no fallback.

**Solution:** The loader first attempts an exact lookup. If that fails, it finds the final colon and performs a second lookup using the substring that follows it.

**Result:** Models whose material names include a colon are remapped to the intended MTL entry, restoring their color and texture data.

### 7. Tile-based multithreading optimization

**Problem:** Processing every triangle and pixel on one thread kept only one CPU core busy. Temporary vectors used by vertex shading and clipping also expanded and reallocated during each frame, adding allocation overhead to the frame time.

**Root-cause analysis:** Rasterization can process independent screen regions in parallel, but the original design had no safe work-partitioning unit. At the same time, short-lived containers repeatedly grew their capacity instead of reusing memory already allocated in previous frames.

**Solution:** The viewport is split into 16×16 tiles. A triangle's screen-space AABB determines which tile bins receive a reference to it, and oneTBB processes those tiles as parallel tasks. Vertex results, stamps, clip buffers, and triangle pools are reused through thread-local storage. `reserve()`, high-watermark caches, and ping-pong buffers reduce reallocation.

**Result:** One development measurement improved from roughly 5 FPS on the single-threaded path to roughly 60 FPS after multithreading. The comparison images captured on 2025-08-27 show 8 FPS increasing to 58 FPS. These figures came from different development stages and conditions and are not standardized benchmarks.

<details>
<summary><strong>View the C++26 modernization details</strong></summary>

### Modern C++ usage

| Previous approach | Current approach | Reason for the change |
|---|---|---|
| Raw pointer + separate length | `std::span`, fixed-extent span | Express buffer length and non-owning contracts in the type |
| C arrays and `#define` | `std::array`, `inline constexpr`, scoped enum | Preserve size/type information and avoid macro pollution |
| `stringstream`, `stoi` | `std::string_view`, `std::from_chars` | Report error positions without temporary strings, locale work, or exception cost |
| Loader-owned `MessageBox`/exceptions | `std::expected<T, AssetLoadError>` | Separate parsing from WinAPI presentation |
| `static_assert` inside templates | C++20 concepts and the `sr.math` module | Diagnose invalid types at the call site |
| `std::vector` for clipping | `FixedCapacityVector` / `std::inplace_vector` compatibility path | Remove heap allocation from a fixed-size hot path |
| Manual cleanup of raw GDI handles | Move-only `Renderer::GdiBackBuffer` | Guarantee restore-selected-object → delete bitmap → delete DC order |
| One SSE path | CPUID/XCR0 runtime dispatch + separate AVX translation unit | Retain old-CPU compatibility and use AVX only for suitable batches |
| Overlapping parallel FXAA writes | `span` API + separated luminance/filter passes | Remove the data race even when workers would write equal values |

The project uses `/std:c++latest`. C++26 library facilities that are not yet available are detected through feature-test macros and replaced with fallbacks that preserve the same contract. Features are not adopted merely because they are newer; operations such as a single `vec4`, whose width naturally fits SSE, deliberately retain that implementation.

</details>

## Build and run

### Requirements

- Windows 10 or Windows 11
- Visual Studio 2026 with MSVC v145
- The `Desktop development with C++` workload and a Windows SDK
- Support for the latest C++ standard and standard-library modules

oneTBB 2023.1.0 and stb_image are included under `src/ThirdParty`, so no separate package installation is required.

### Visual Studio

1. Open `SoftrendererProject.sln` from the repository root.
2. Select `Release | x64`, or another required configuration.
3. Run **Build > Build Solution**.
4. Run **Debug > Start Without Debugging**.

The supported configurations are `Debug|x64`, `Release|x64`, `Debug|Win32`, and `Release|Win32`. After a build, `assets` and the architecture-specific `tbb12.dll` are copied automatically to the output directory.

### Command line

Run this from Visual Studio's **Developer PowerShell**:

```powershell
msbuild .\SoftrendererProject.sln /m /p:Configuration=Release /p:Platform=x64
```

### Controls

| Input | Action |
|---|---|
| `W` / `A` / `S` / `D` | Move the camera |
| Right mouse button + drag | Rotate the camera |
| `Space` | Toggle automatic model rotation |
| `F1` | Disable anti-aliasing |
| `F2` | Enable FXAA |

The window menu also provides Bresenham/DDA line drawing, None/FXAA selection, and Normal Vector/AABB/Wire Frame/Light Direction visualizations.

<details>
<summary><strong>View the directory structure</strong></summary>

### Directory structure

```text
SoftwareRendererProject/
├─ assets/                 # OBJ, MTL, and texture assets
├─ src/
│  ├─ Core/               # WinAPI application and main loop
│  ├─ Graphics/           # Model, Mesh, Material, Texture, Octree
│  ├─ Math/               # SRMath, AABB, Frustum, SSE/AVX dispatch
│  ├─ Modules/            # sr.math C++ module interface
│  ├─ Platform/           # Minimal Win32 boundary header
│  ├─ Renderer/           # RenderQueue, clipping, tile rasterizer, FXAA
│  ├─ Scene/              # Camera and GameObject
│  ├─ ThirdParty/         # oneTBB and stb_image
│  └─ Utils/              # Error types, fixed-capacity storage, timing
├─ SoftrendererProject.sln
└─ SoftrendererProject.vcxproj
```

</details>

## Scope and limitations

- The project targets Windows and the MSVC project system.
- OBJ faces support triangles and quads; an n-gon with five or more vertices returns an explicit error.
- Texture sampling currently uses nearest-neighbor filtering and clamp addressing.
- Lighting is centered on a directional-light Phong model.
- Published FPS figures come from different development stages and are not controlled, same-condition benchmarks.
- The scope is CPU rendering-pipeline study and optimization; a GPU API backend and a general-purpose scene editor are intentionally outside it.

## Third-party libraries

- [oneTBB 2023.1.0](https://github.com/uxlfoundation/oneTBB): task-based parallelism
- [stb_image](https://github.com/nothings/stb): image decoding
