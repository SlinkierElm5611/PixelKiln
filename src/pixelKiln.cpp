//
// Created by Stefan Balta on 2026-07-12.
//

#include "pixelKiln.h"
#include "pixelKilnImpl.h"

uint64_t PixelKiln::loadComputeProgram(const ComputeProgram &program) {
    return m_impl->loadComputeProgram(program);
}

uint64_t PixelKiln::loadRasterDrawProgram(const RasterDrawProgram &program) {
    return m_impl->loadRasterDrawProgram(program);
}

void PixelKiln::unloadProgram(uint64_t program) {
    m_impl->unloadProgram(program);
}

uint64_t PixelKiln::createBuffer(uint64_t size) {
    return m_impl->createBuffer(size);
}

void PixelKiln::destroyBuffer(uint64_t buffer) {
    m_impl->destroyBuffer(buffer);
}

void PixelKiln::uploadBuffer(uint64_t buffer, const void* data, uint64_t size, uint64_t offset) {
    m_impl->uploadBuffer(buffer, data, size, offset);
}

void PixelKiln::downloadBuffer(uint64_t buffer, void* data, uint64_t size, uint64_t offset) {
    m_impl->downloadBuffer(buffer, data, size, offset);
}

uint64_t PixelKiln::createImage(const ImageDesc &desc) {
    return m_impl->createImage(desc);
}

void PixelKiln::destroyImage(uint64_t image) {
    m_impl->destroyImage(image);
}

void PixelKiln::uploadImage(uint64_t image, const void* data, uint64_t size) {
    m_impl->uploadImage(image, data, size);
}

void PixelKiln::downloadImage(uint64_t image, void* data, uint64_t size) {
    m_impl->downloadImage(image, data, size);
}

uint64_t PixelKiln::call(const ProgramCall &call) {
    return m_impl->call(call);
}

bool PixelKiln::isComplete(uint64_t ticket) {
    return m_impl->isComplete(ticket);
}

void PixelKiln::wait(uint64_t ticket) {
    m_impl->wait(ticket);
}

void PixelKiln::waitIdle() {
    m_impl->waitIdle();
}

PixelKiln::PixelKiln() : PixelKiln(Config())
{
}

PixelKiln::PixelKiln(Config config)
{
    m_impl = std::make_unique<PixelKilnImpl>(config);
}

PixelKiln::~PixelKiln()
{
}
