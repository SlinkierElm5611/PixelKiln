# PixelKiln

A Vulkan-based, bare-bones gpu acceleration library.

In this library, there are two kinds of resources, buffers and images.

The programmer is responsible for allocating and freeing these resources from the PixelKiln API.

## GPU programs

Programs are created once with `loadComputeProgram` or `loadRasterDrawProgram`, which build the pipelines. A program
declares its `UniformBindings`: entry `i` is binding `i` of descriptor set 0 in the shader.

| Binding type | What the call provides |
|---|---|
| `UNIFORM_BINDING_TYPE_BUFFER` | the uniform bytes themselves (`data`, `size`), copied during the call |
| `UNIFORM_BINDING_TYPE_STORAGE_BUFFER` | a buffer handle |
| `UNIFORM_BINDING_TYPE_SAMPLER` | an image handle and a `SamplerDesc` |
| `UNIFORM_BINDING_TYPE_STORAGE_IMAGE` | an image handle |
| `UNIFORM_BINDING_TYPE_EMPTY` | nothing, the binding number is skipped |

Large data (vertex, index and storage buffers, textures) is uploaded by the programmer with `uploadBuffer` /
`uploadImage` and referenced by handle.

## Calls

A program is run by passing a `ProgramCall` to `call()`. The call copies its uniform data into a ring buffer the GPU
reads it from, records the program for the all queue, and returns a ticket without waiting. Uploads and downloads run on
the transfer queue. Each queue has a timeline semaphore, so the next upload runs while the current draw/compute
executes. On devices with a single queue both queues refer to it.

```cpp
PixelKiln kiln;
uint64_t program = kiln.loadComputeProgram({{spirv, sizeof(spirv)},
    {UNIFORM_BINDING_TYPE_BUFFER, UNIFORM_BINDING_TYPE_STORAGE_BUFFER}});
uint64_t data = kiln.createBuffer(size);
kiln.uploadBuffer(data, values, size);

ProgramCall call{};
call.type = PROGRAM_TYPE_COMPUTE;
call.program = program;
call.bindings = {{.data = &params, .size = sizeof(params)}, {.resource = data}};
call.groupCountX = groups;
uint64_t ticket = kiln.call(call);

kiln.downloadBuffer(data, results, size); // waits for the call
kiln.destroyBuffer(data);
```

Calls are submitted in batches: a call goes to the GPU right away when the GPU has nothing else to do, otherwise
together with the calls that follow it, at the latest when something needs its results (`wait`, `isComplete`, a
download, an upload into a resource it uses, `present`). `flush()` submits pending calls explicitly, e.g. before a long
stretch of CPU work that doesn't use PixelKiln.

Uploads return once the data is staged (uploads over 4 MiB are staged in pieces and may wait for the first ones to be
copied); downloads block until every earlier use of the resource is done. Resources and programs can be destroyed while
the GPU still uses them, destruction is deferred. PixelKiln is not thread-safe.

See `examples/` for compute and windowed examples.

## Multisampling

Raster draw programs can render with MSAA: create the targets with `ImageDesc::samples` and load the program with the
same `RasterDrawProgram::samples`. `getSupportedSampleCounts(format)` returns the counts a format supports as a bitmask;
Vulkan guarantees 4x for depth and non-integer color formats. A multisampled color target is resolved into a single
sample image of the same size and format, such as a swapchain image, at the end of the call. Targets whose samples
aren't needed afterwards are marked `store = false`, so the samples are never written to memory:

```cpp
uint64_t color = kiln.createImage({width, height, format, IMAGE_USAGE_COLOR_TARGET, 4});
uint64_t depth = kiln.createImage({width, height, IMAGE_FORMAT_D32_FLOAT, IMAGE_USAGE_DEPTH_TARGET, 4});
program.samples = 4;
// every frame:
call.colorTargets = {{.image = color, .clearColor = {0.0f, 0.0f, 0.0f, 1.0f}, .resolveImage = swapchainImage,
                      .store = false}};
call.depthTarget = {.image = depth, .store = false};
```

Keep `store = true` when a later call loads the target (`clear = false`) to draw more before resolving. Shaders can read
single samples of a multisampled image created with `IMAGE_USAGE_SAMPLED` (`texelFetch` on a `sampler2DMS`), but it
can't be a storage image, uploaded or downloaded. `RasterDrawProgram::alphaToCoverage` turns a fragment's alpha into
the fraction of the pixel's samples it covers. Integer formats resolve to one of their samples instead of an average.

## Windowing

PixelKiln presents into windows the application owns (GLFW, SDL, Qt, native). It only takes the window's native handles
as a `NativeWindow`; the application keeps the event loop. Swapchain images are regular image handles, valid between
`acquireSwapchainImage` and `present`:

```cpp
uint64_t swapchain = kiln.createSwapchain(nativeWindow, {framebufferWidth, framebufferHeight});
program.colorFormats = {kiln.getSwapchainInfo(swapchain).format};
// every frame:
if (resized) kiln.resizeSwapchain(swapchain, framebufferWidth, framebufferHeight);
uint64_t image = kiln.acquireSwapchainImage(swapchain);   // 0 while minimized
call.colorTargets = {{image, true, {0.0f, 0.0f, 0.0f, 1.0f}}};
kiln.call(call);
kiln.present(swapchain);
// before destroying the window:
kiln.destroySwapchain(swapchain);
```

With GLFW (create the window with `GLFW_CLIENT_API` set to `GLFW_NO_API`), the native handles are:

```cpp
#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#else
#define GLFW_EXPOSE_NATIVE_X11
#endif
#include <GLFW/glfw3native.h>

NativeWindow toNativeWindow(GLFWwindow* window)
{
#if defined(_WIN32)
    return {NATIVE_WINDOW_WIN32, GetModuleHandleW(nullptr), glfwGetWin32Window(window)};
#elif defined(__APPLE__)
    return {NATIVE_WINDOW_COCOA_VIEW, nullptr, glfwGetCocoaView(window)};
#else
    return {NATIVE_WINDOW_XLIB, glfwGetX11Display(), (void*)(uintptr_t)glfwGetX11Window(window)};
#endif
}
```

On macOS, `createSwapchain` with `NATIVE_WINDOW_COCOA_VIEW` must be called on the main thread (it attaches a
`CAMetalLayer` to the view); pass your own layer with `NATIVE_WINDOW_METAL_LAYER` to avoid that. On Linux, X11 (Xlib,
xcb) and Wayland support is compiled in when their development headers are found.

`examples/common/exampleWindow.h` shows the full GLFW setup used by the windowed examples. GLFW is a git submodule used
only by those examples and the window tests:

```sh
git submodule update --init
```

## Examples

| Example | What it shows |
|---|---|
| `ComputeBasic` | Compute on storage buffers and a storage image |
| `Particles` | 262144 particles simulated in compute and drawn straight from the same buffer as points, with fading trails. Hold the left mouse button to pull them to the cursor |
| `ReactionDiffusion` | Gray-Scott reaction-diffusion, 16 compute steps per frame ping-ponging two buffers, colored by a second compute pass. Paint with the mouse, `1`-`4` presets, `R` reset, `Space` pause |
| `Mandelbrot` | Fractal explorer computed at window resolution. Drag to pan, scroll to zoom; left alone it dives into Seahorse Valley |
| `TorusKnot` | A lit, spinning knot drawn with MSAA into multisampled color and depth targets that are resolved straight into the window. `M` toggles MSAA, `Space` pauses |

The windowed examples accept `--frames N` (deterministic run of N frames) and `--screenshot file.bmp`.

## Tests

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Every `TEST(name)` in `tests/test*.cpp` and every example is its own ctest test (`ctest -L unit`, `ctest -L example`).
Tests run under the Khronos validation layer from the Vulkan SDK with synchronization validation
(`-DPIXELKILN_TEST_VALIDATION=OFF` to disable). Validation messages are printed, but a test's result comes from its own
checks, because older layers report false positives (see `tests/CMakeLists.txt`). Optional cache variables add variants
of every test:

- `PIXELKILN_TEST_DRIVER_FILES`: a list of Vulkan ICD manifests, each run as its own variant (via `VK_DRIVER_FILES`),
  to cover several drivers on one machine.
- `PIXELKILN_TEST_TRANSFER_ONLY_DRIVER_FILE`: an ICD manifest for a device with 4 queue families of 1 queue each
  (e.g. MoltenVK). Runs every test with the profiles layer reporting family 1 as transfer-only, like NVIDIA/AMD GPUs.
- `PIXELKILN_TEST_WINDOW=ON`: also builds and registers the window/presentation tests (label `window`). They open real
  windows, so they need a display and the GLFW submodule.

For example, on macOS with the Vulkan SDK:

```sh
cmake -S . -B build \
  -DPIXELKILN_TEST_DRIVER_FILES="/usr/local/share/vulkan/icd.d/MoltenVK_icd.json;/usr/local/share/vulkan/icd.d/libkosmickrisp_icd.json" \
  -DPIXELKILN_TEST_TRANSFER_ONLY_DRIVER_FILE=/usr/local/share/vulkan/icd.d/MoltenVK_icd.json
```
