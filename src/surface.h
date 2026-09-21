//
// Created by Stefan Balta on 2026-09-21.
//

#ifndef PIXELKILN_SURFACE_H
#define PIXELKILN_SURFACE_H
#include <vector>

#include <vulkan/vulkan_core.h>

#include "nativeWindow.h"

// Window system integration. surface.cpp is the only file that sees platform headers (windows.h, X11, xcb, Wayland).

// Instance extensions for every window system compiled into this build, VK_KHR_surface first.
std::vector<const char*> surfaceInstanceExtensions();

// Creates a surface for the native window. Throws std::invalid_argument for bad handles and std::runtime_error when
// the window system isn't compiled in or its instance extension isn't enabled.
VkSurfaceKHR createSurface(VkInstance instance, const NativeWindow &window);

#ifdef __APPLE__
// metalLayer.mm: makes the NSView layer-backed with a CAMetalLayer and returns the layer. Main thread only.
void* pixelKilnAttachMetalLayer(void* view);
#endif

#endif //PIXELKILN_SURFACE_H
