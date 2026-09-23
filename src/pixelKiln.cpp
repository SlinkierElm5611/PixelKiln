//
// Created by Stefan Balta on 2026-07-12.
//

#include "pixelKiln.h"
#include "pixelKilnImpl.h"

uint64_t PixelKiln::loadComputeProgram(const ComputeProgram &program, const char* debugName) {
    return m_impl->loadComputeProgram(program, debugName);
}

uint64_t PixelKiln::loadRasterDrawProgram(const RasterDrawProgram &program, const char* debugName) {
    return m_impl->loadRasterDrawProgram(program, debugName);
}

void PixelKiln::unloadProgram(uint64_t program) {
    m_impl->unloadProgram(program);
}

uint64_t PixelKiln::createBuffer(uint64_t size, const char* debugName) {
    return m_impl->createBuffer(size, debugName);
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

uint64_t PixelKiln::createImage(const ImageDesc &desc, const char* debugName) {
    return m_impl->createImage(desc, debugName);
}

void PixelKiln::destroyImage(uint64_t image) {
    m_impl->destroyImage(image);
}

uint32_t PixelKiln::getSupportedSampleCounts(ImageFormat format) {
    return m_impl->getSupportedSampleCounts(format);
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

void PixelKiln::flush() {
    m_impl->flush();
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

uint64_t PixelKiln::createSwapchain(const NativeWindow &window, const SwapchainDesc &desc) {
    return m_impl->createSwapchain(window, desc);
}

void PixelKiln::destroySwapchain(uint64_t swapchain) {
    m_impl->destroySwapchain(swapchain);
}

void PixelKiln::resizeSwapchain(uint64_t swapchain, uint32_t width, uint32_t height) {
    m_impl->resizeSwapchain(swapchain, width, height);
}

SwapchainInfo PixelKiln::getSwapchainInfo(uint64_t swapchain) {
    return m_impl->getSwapchainInfo(swapchain);
}

uint64_t PixelKiln::acquireSwapchainImage(uint64_t swapchain) {
    return m_impl->acquireSwapchainImage(swapchain);
}

void PixelKiln::present(uint64_t swapchain) {
    m_impl->present(swapchain);
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
