//
// Created by Stefan Balta on 2026-09-21.
//

// Presentation into application-owned windows. Swapchain images are regular PixelKiln images owned by the swapchain.
// Presentation only works with binary semaphores, so each frame adds two of them to the timeline model:
//  - an acquire semaphore, signalled by vkAcquireNextImageKHR and waited by the first all queue submission touching the
//    image (a call rendering to it, or present() itself). It is reused once that submission has completed.
//  - a rendered semaphore per image index, signalled by present()'s submission and waited by vkQueuePresentKHR. The
//    swapchain only hands the image out again after that wait, so the semaphore is free when the image is reacquired.
// Everything else (ordering against calls and transfers, layouts, lifetime) goes through the existing timelines.

#include "pixelKilnImpl.h"

#include <algorithm>
#include <stdexcept>

#include "surface.h"
#include "vulkanTranslate.h"

PixelKilnImpl::Swapchain &PixelKilnImpl::getSwapchain(uint64_t swapchain) {
    auto it = m_swapchains.find(swapchain);
    if (it == m_swapchains.end()) {
        throw std::invalid_argument("PixelKiln: unknown swapchain handle");
    }
    return it->second;
}

void PixelKilnImpl::checkSwapchainImage(const Image &image) {
    if (!image.swapchain) {
        return;
    }
    const Swapchain &swapchain = m_swapchains.at(image.swapchain);
    if (swapchain.acquiredIndex < 0 ||
        m_images.at(swapchain.images[static_cast<size_t>(swapchain.acquiredIndex)]).image != image.image) {
        throw std::invalid_argument("PixelKiln: swapchain images can only be used between acquireSwapchainImage and "
                                    "present");
    }
}

uint64_t PixelKilnImpl::createSwapchain(const NativeWindow &window, const SwapchainDesc &desc) {
    collectGarbage();
    if (static_cast<unsigned>(window.type) >= NATIVE_WINDOW_TYPE_COUNT || !window.window) {
        throw std::invalid_argument("PixelKiln: NativeWindow needs a valid type and window handle");
    }
    if (static_cast<unsigned>(desc.presentMode) >= PRESENT_MODE_COUNT) {
        throw std::invalid_argument("PixelKiln: invalid PresentMode");
    }
    const ImageUsageFlags allowedUsage = IMAGE_USAGE_COLOR_TARGET | IMAGE_USAGE_STORAGE;
    if (desc.usage == 0 || (desc.usage & ~allowedUsage)) {
        throw std::invalid_argument("PixelKiln: swapchain usage must be IMAGE_USAGE_COLOR_TARGET and/or IMAGE_USAGE_STORAGE");
    }
    const vk::Format preferredFormat = toVkFormat(desc.format);
    if (!m_surfaceSupport || !m_swapchainSupport) {
        throw std::runtime_error("PixelKiln: presenting to windows is not supported by this Vulkan loader or device");
    }

    Swapchain swapchain;
    swapchain.desc = desc;
    swapchain.surface = createSurface(m_instance, window);
    const uint64_t handle = m_nextHandle++;
    try {
        if (!m_physicalDevice.getSurfaceSupportKHR(m_allFamily, swapchain.surface)) {
            throw std::runtime_error("PixelKiln: the device's graphics queue can't present to this window");
        }

        // Format: the requested one, else the common 8 bit formats, always in the standard sRGB color space.
        std::vector<vk::SurfaceFormatKHR> formats = m_physicalDevice.getSurfaceFormatsKHR(swapchain.surface);
        const vk::Format candidates[] = {preferredFormat, vk::Format::eB8G8R8A8Unorm, vk::Format::eR8G8B8A8Unorm,
                                         vk::Format::eB8G8R8A8Srgb, vk::Format::eR8G8B8A8Srgb};
        bool found = false;
        for (vk::Format candidate : candidates) {
            for (const vk::SurfaceFormatKHR &format : formats) {
                if (format.format == candidate && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear &&
                    fromVkSwapchainFormat(candidate) != IMAGE_FORMAT_UNDEFINED) {
                    swapchain.surfaceFormat = format;
                    found = true;
                    break;
                }
            }
            if (found) {
                break;
            }
        }
        if (!found) {
            throw std::runtime_error("PixelKiln: the window supports none of the 8 bit RGBA/BGRA sRGB color formats");
        }
        swapchain.info.format = fromVkSwapchainFormat(swapchain.surfaceFormat.format);

        // Present mode, FIFO is always supported.
        std::vector<vk::PresentModeKHR> modes = m_physicalDevice.getSurfacePresentModesKHR(swapchain.surface);
        auto supported = [&](vk::PresentModeKHR mode) { return std::find(modes.begin(), modes.end(), mode) != modes.end(); };
        swapchain.presentMode = vk::PresentModeKHR::eFifo;
        if (desc.presentMode == PRESENT_MODE_IMMEDIATE && supported(vk::PresentModeKHR::eImmediate)) {
            swapchain.presentMode = vk::PresentModeKHR::eImmediate;
        } else if (desc.presentMode != PRESENT_MODE_VSYNC && supported(vk::PresentModeKHR::eMailbox)) {
            swapchain.presentMode = vk::PresentModeKHR::eMailbox;
        }

        // Usage: what was asked for, plus transfer source when available so swapchain images can be downloaded.
        vk::SurfaceCapabilitiesKHR capabilities = m_physicalDevice.getSurfaceCapabilitiesKHR(swapchain.surface);
        if (desc.usage & IMAGE_USAGE_COLOR_TARGET) {
            swapchain.usage |= vk::ImageUsageFlagBits::eColorAttachment;
        }
        if (desc.usage & IMAGE_USAGE_STORAGE) {
            swapchain.usage |= vk::ImageUsageFlagBits::eStorage;
            if (!(formatFeatures(swapchain.info.format) & vk::FormatFeatureFlagBits::eStorageImage)) {
                throw std::invalid_argument("PixelKiln: the window's color format can't be used as a storage image");
            }
        }
        if ((capabilities.supportedUsageFlags & swapchain.usage) != swapchain.usage) {
            throw std::invalid_argument("PixelKiln: the window doesn't support the requested swapchain usage");
        }
        if (capabilities.supportedUsageFlags & vk::ImageUsageFlagBits::eTransferSrc) {
            swapchain.usage |= vk::ImageUsageFlagBits::eTransferSrc;
        }

        recreateSwapchain(handle, swapchain); // leaves it for the first acquire when the window has no area yet
    } catch (...) {
        teardownSwapchain(swapchain);
        throw;
    }
    m_swapchains[handle] = std::move(swapchain);
    return handle;
}

bool PixelKilnImpl::recreateSwapchain(uint64_t handle, Swapchain &swapchain) {
    vk::SurfaceCapabilitiesKHR capabilities = m_physicalDevice.getSurfaceCapabilitiesKHR(swapchain.surface);
    vk::Extent2D extent = capabilities.currentExtent;
    if (extent.width == UINT32_MAX) {
        // The window system lets the swapchain decide (Wayland): use the size the application gave.
        extent.width = std::clamp(swapchain.desc.width, capabilities.minImageExtent.width,
                                  capabilities.maxImageExtent.width);
        extent.height = std::clamp(swapchain.desc.height, capabilities.minImageExtent.height,
                                   capabilities.maxImageExtent.height);
        if (swapchain.desc.width == 0 || swapchain.desc.height == 0) {
            extent = vk::Extent2D(0, 0);
        }
    }
    if (extent.width == 0 || extent.height == 0) {
        swapchain.needsRecreate = true; // minimized, try again at the next acquire
        return false;
    }

    // The old swapchain's images may still be in use by calls, transfers or the presentation engine. Resizes are
    // rare, so wait for all of it instead of tracking each image.
    if (swapchain.swapchain) {
        waitValue(QUEUE_ALL, m_allValue);
        waitValue(QUEUE_TRANSFER, m_transferValue);
        m_allQueue.waitIdle();
    }

    uint32_t imageCount = std::max(capabilities.minImageCount + 1, 3u);
    if (capabilities.maxImageCount > 0) {
        imageCount = std::min(imageCount, capabilities.maxImageCount);
    }
    vk::CompositeAlphaFlagBitsKHR compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    for (vk::CompositeAlphaFlagBitsKHR alpha : {vk::CompositeAlphaFlagBitsKHR::eOpaque,
                                                vk::CompositeAlphaFlagBitsKHR::eInherit,
                                                vk::CompositeAlphaFlagBitsKHR::ePreMultiplied,
                                                vk::CompositeAlphaFlagBitsKHR::ePostMultiplied}) {
        if (capabilities.supportedCompositeAlpha & alpha) {
            compositeAlpha = alpha;
            break;
        }
    }
    vk::SwapchainCreateInfoKHR createInfo{};
    createInfo.surface = swapchain.surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = swapchain.surfaceFormat.format;
    createInfo.imageColorSpace = swapchain.surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = swapchain.usage;
    uint32_t families[2] = {m_allFamily, m_transferFamily};
    if (m_allFamily != m_transferFamily) {
        createInfo.imageSharingMode = vk::SharingMode::eConcurrent;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = families;
    } else {
        createInfo.imageSharingMode = vk::SharingMode::eExclusive;
    }
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = compositeAlpha;
    createInfo.presentMode = swapchain.presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = swapchain.swapchain;
    vk::SwapchainKHR created = m_device.createSwapchainKHR(createInfo);

    destroySwapchainImages(swapchain);
    m_device.destroySwapchainKHR(swapchain.swapchain);
    swapchain.swapchain = created;

    std::vector<vk::Image> images = m_device.getSwapchainImagesKHR(created);
    for (vk::Image vkImage : images) {
        Image image;
        image.image = vkImage;
        image.desc = {extent.width, extent.height, swapchain.info.format, swapchain.desc.usage};
        image.aspect = vk::ImageAspectFlagBits::eColor;
        image.swapchain = handle;
        vk::ImageViewCreateInfo viewInfo{};
        viewInfo.image = vkImage;
        viewInfo.viewType = vk::ImageViewType::e2D;
        viewInfo.format = swapchain.surfaceFormat.format;
        viewInfo.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
        image.view = m_device.createImageView(viewInfo);
        uint64_t imageHandle = m_nextHandle++;
        m_images[imageHandle] = image;
        swapchain.images.push_back(imageHandle);
        swapchain.renderedSemaphores.push_back(m_device.createSemaphore({}));
    }
    for (size_t i = 0; i <= images.size(); i++) {
        swapchain.acquireSemaphores.push_back({m_device.createSemaphore({}), 0});
    }
    swapchain.info.width = extent.width;
    swapchain.info.height = extent.height;
    swapchain.info.imageCount = static_cast<uint32_t>(images.size());
    swapchain.acquiredIndex = -1;
    swapchain.pendingAcquire = -1;
    swapchain.needsRecreate = false;
    return true;
}

void PixelKilnImpl::destroySwapchainImages(Swapchain &swapchain) {
    for (uint64_t imageHandle : swapchain.images) {
        m_device.destroyImageView(m_images.at(imageHandle).view);
        m_images.erase(imageHandle);
    }
    swapchain.images.clear();
    for (vk::Semaphore semaphore : swapchain.renderedSemaphores) {
        m_device.destroySemaphore(semaphore);
    }
    swapchain.renderedSemaphores.clear();
    for (const AcquireSemaphore &acquire : swapchain.acquireSemaphores) {
        m_device.destroySemaphore(acquire.semaphore);
    }
    swapchain.acquireSemaphores.clear();
}

void PixelKilnImpl::teardownSwapchain(Swapchain &swapchain) {
    if (swapchain.pendingAcquire >= 0) {
        // An acquired image nothing waited on yet: consume the semaphore so it isn't destroyed while pending.
        vk::SemaphoreSubmitInfo waitInfo{};
        waitInfo.semaphore = swapchain.acquireSemaphores[static_cast<size_t>(swapchain.pendingAcquire)].semaphore;
        waitInfo.stageMask = vk::PipelineStageFlagBits2::eAllCommands;
        vk::SubmitInfo2 submitInfo{};
        submitInfo.waitSemaphoreInfoCount = 1;
        submitInfo.pWaitSemaphoreInfos = &waitInfo;
        m_allQueue.submit2(submitInfo);
        swapchain.pendingAcquire = -1;
    }
    if (swapchain.swapchain || !swapchain.images.empty()) {
        waitValue(QUEUE_ALL, m_allValue);
        waitValue(QUEUE_TRANSFER, m_transferValue);
        m_allQueue.waitIdle();
    }
    destroySwapchainImages(swapchain);
    m_device.destroySwapchainKHR(swapchain.swapchain);
    swapchain.swapchain = nullptr;
    m_instance.destroySurfaceKHR(swapchain.surface);
    swapchain.surface = nullptr;
}

void PixelKilnImpl::destroySwapchain(uint64_t swapchain) {
    collectGarbage();
    Swapchain &destroyed = getSwapchain(swapchain);
    teardownSwapchain(destroyed);
    m_swapchains.erase(swapchain);
}

void PixelKilnImpl::resizeSwapchain(uint64_t swapchain, uint32_t width, uint32_t height) {
    Swapchain &resized = getSwapchain(swapchain);
    resized.desc.width = width;
    resized.desc.height = height;
    resized.needsRecreate = true;
}

SwapchainInfo PixelKilnImpl::getSwapchainInfo(uint64_t swapchain) {
    return getSwapchain(swapchain).info;
}

uint64_t PixelKilnImpl::acquireSwapchainImage(uint64_t handle) {
    collectGarbage();
    Swapchain &swapchain = getSwapchain(handle);
    if (swapchain.acquiredIndex >= 0) {
        throw std::invalid_argument("PixelKiln: present the acquired swapchain image before acquiring another");
    }
    for (int attempt = 0; attempt < 2; attempt++) {
        if (swapchain.needsRecreate && !recreateSwapchain(handle, swapchain)) {
            return 0;
        }
        // An acquire semaphore whose last wait has completed; with imageCount + 1 of them one always frees up.
        uint64_t completed = completedValue(QUEUE_ALL);
        size_t chosen = swapchain.acquireSemaphores.size();
        size_t oldest = 0;
        for (size_t i = 0; i < swapchain.acquireSemaphores.size(); i++) {
            if (swapchain.acquireSemaphores[i].allValue <= completed) {
                chosen = i;
                break;
            }
            if (swapchain.acquireSemaphores[i].allValue < swapchain.acquireSemaphores[oldest].allValue) {
                oldest = i;
            }
        }
        if (chosen == swapchain.acquireSemaphores.size()) {
            waitValue(QUEUE_ALL, swapchain.acquireSemaphores[oldest].allValue);
            chosen = oldest;
        }

        uint32_t index = 0;
        VkResult result = vkAcquireNextImageKHR(static_cast<VkDevice>(m_device),
                                                static_cast<VkSwapchainKHR>(swapchain.swapchain), UINT64_MAX,
                                                static_cast<VkSemaphore>(swapchain.acquireSemaphores[chosen].semaphore),
                                                VK_NULL_HANDLE, &index);
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            swapchain.needsRecreate = true;
            continue;
        }
        if (result == VK_SUBOPTIMAL_KHR) {
            swapchain.needsRecreate = true; // still usable, recreated at the next acquire
        } else if (result != VK_SUCCESS) {
            throw std::runtime_error("PixelKiln: acquiring a swapchain image failed (VkResult " +
                                     std::to_string(result) + ")");
        }
        swapchain.acquiredIndex = index;
        swapchain.pendingAcquire = static_cast<int64_t>(chosen);
        return swapchain.images[index];
    }
    return 0;
}

void PixelKilnImpl::present(uint64_t handle) {
    collectGarbage();
    Swapchain &swapchain = getSwapchain(handle);
    if (swapchain.acquiredIndex < 0) {
        throw std::invalid_argument("PixelKiln: present needs an image from acquireSwapchainImage");
    }
    const uint32_t index = static_cast<uint32_t>(swapchain.acquiredIndex);
    Image &image = m_images.at(swapchain.images[index]);

    // Nothing rendered to it this frame: its contents are undefined and the acquire semaphore is still pending.
    std::vector<vk::Semaphore> acquireWaits;
    vk::ImageLayout oldLayout = image.layout;
    if (swapchain.pendingAcquire >= 0) {
        acquireWaits.push_back(swapchain.acquireSemaphores[static_cast<size_t>(swapchain.pendingAcquire)].semaphore);
        oldLayout = vk::ImageLayout::eUndefined;
    }

    vk::CommandBuffer commandBuffer = beginCommands(QUEUE_ALL);
    vk::ImageMemoryBarrier2 barrier{};
    barrier.srcStageMask = vk::PipelineStageFlagBits2::eAllCommands;
    barrier.srcAccessMask = vk::AccessFlagBits2::eMemoryWrite;
    barrier.dstStageMask = vk::PipelineStageFlagBits2::eAllCommands;
    barrier.dstAccessMask = {};
    barrier.oldLayout = oldLayout;
    barrier.newLayout = vk::ImageLayout::ePresentSrcKHR;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image.image;
    barrier.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
    vk::DependencyInfo dependency{};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    commandBuffer.pipelineBarrier2(dependency);
    uint64_t ticket = submitCommands(QUEUE_ALL, commandBuffer, image.lastTransferUse, acquireWaits,
                                     swapchain.renderedSemaphores[index]);

    image.layout = vk::ImageLayout::ePresentSrcKHR;
    image.lastAllUse = ticket;
    if (swapchain.pendingAcquire >= 0) {
        swapchain.acquireSemaphores[static_cast<size_t>(swapchain.pendingAcquire)].allValue = ticket;
        swapchain.pendingAcquire = -1;
    }
    swapchain.acquiredIndex = -1;

    VkSemaphore rendered = static_cast<VkSemaphore>(swapchain.renderedSemaphores[index]);
    VkSwapchainKHR vkSwapchain = static_cast<VkSwapchainKHR>(swapchain.swapchain);
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &rendered;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &vkSwapchain;
    presentInfo.pImageIndices = &index;
    VkResult result = vkQueuePresentKHR(static_cast<VkQueue>(m_allQueue), &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        swapchain.needsRecreate = true;
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("PixelKiln: presenting failed (VkResult " + std::to_string(result) + ")");
    }
}
