//
// Created by Stefan Balta on 2026-07-12.
//

#ifndef PIXELKILN_PIXELKILN_H
#define PIXELKILN_PIXELKILN_H

#include <cstdint>
#include <memory>

#include "computeProgram.h"
#include "config.h"
#include "imageDesc.h"
#include "programCall.h"
#include "rasterDrawProgram.h"

class PixelKilnImpl;

// Not thread-safe. Handles are uint64_t and 0 is never a valid handle.
// Resources created by the user are owned by the user; destroying one that the GPU is still using is safe,
// destruction is deferred until the GPU is done with it.
class PixelKiln
{
private:
    std::unique_ptr<PixelKilnImpl> m_impl;
public:
    uint64_t loadComputeProgram(const ComputeProgram &program);
    uint64_t loadRasterDrawProgram(const RasterDrawProgram &program);
    void unloadProgram(uint64_t program);

    // Buffers can be used as vertex, index and storage buffers.
    uint64_t createBuffer(uint64_t size);
    void destroyBuffer(uint64_t buffer);
    // Copies data into staging before returning, the GPU copy runs asynchronously on the transfer queue.
    void uploadBuffer(uint64_t buffer, const void* data, uint64_t size, uint64_t offset = 0);
    // Blocks until every earlier GPU use of the buffer is done and the data is in `data`.
    void downloadBuffer(uint64_t buffer, void* data, uint64_t size, uint64_t offset = 0);

    uint64_t createImage(const ImageDesc &desc);
    void destroyImage(uint64_t image);
    // Whole image, tightly packed rows. Depth images can't be uploaded or downloaded.
    void uploadImage(uint64_t image, const void* data, uint64_t size);
    void downloadImage(uint64_t image, void* data, uint64_t size);

    // Uploads the call's uniform data on the transfer queue, then runs the program on the all queue.
    // Returns without waiting, the returned ticket can be passed to isComplete/wait.
    uint64_t call(const ProgramCall &call);
    bool isComplete(uint64_t ticket);
    void wait(uint64_t ticket);
    void waitIdle();

    PixelKiln();
    PixelKiln(Config config);
    ~PixelKiln();
};


#endif //PIXELKILN_PIXELKILN_H
