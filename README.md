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

A program is run by passing a `ProgramCall` to `call()`. The call first uploads its uniform data on the transfer queue,
then runs the program on the all queue, and returns a ticket without waiting. Each queue has a timeline semaphore, so
the next upload runs while the current draw/compute executes. On devices with a single queue both queues refer to it.

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

Uploads return once the data is staged; downloads block until every earlier use of the resource is done. Resources and
programs can be destroyed while the GPU still uses them, destruction is deferred. PixelKiln is not thread-safe.

See `examples/` for compute, raster draw and pipelined upload examples.

## Tests

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Every `TEST(name)` in `tests/test*.cpp` and every example is its own ctest test (`ctest -L unit`, `ctest -L example`).
Tests run under the Khronos validation layer from the Vulkan SDK and fail on any validation message
(`-DPIXELKILN_TEST_VALIDATION=OFF` to disable). Optional cache variables add variants of every test:

- `PIXELKILN_TEST_DRIVER_FILES`: a list of Vulkan ICD manifests, each run as its own variant (via `VK_DRIVER_FILES`),
  to cover several drivers on one machine.
- `PIXELKILN_TEST_TRANSFER_ONLY_DRIVER_FILE`: an ICD manifest for a device with 4 queue families of 1 queue each
  (e.g. MoltenVK). Runs every test with the profiles layer reporting family 1 as transfer-only, like NVIDIA/AMD GPUs.

For example, on macOS with the Vulkan SDK:

```sh
cmake -S . -B build \
  -DPIXELKILN_TEST_DRIVER_FILES="/usr/local/share/vulkan/icd.d/MoltenVK_icd.json;/usr/local/share/vulkan/icd.d/libkosmickrisp_icd.json" \
  -DPIXELKILN_TEST_TRANSFER_ONLY_DRIVER_FILE=/usr/local/share/vulkan/icd.d/MoltenVK_icd.json
```
