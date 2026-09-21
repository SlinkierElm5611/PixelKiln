//
// Created by Stefan Balta on 2026-09-21.
//

// Platform defines must come before any Vulkan header in this file, and nowhere else in the library.
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined(__APPLE__)
#define VK_USE_PLATFORM_METAL_EXT
#else
// Set by CMake when the window system's headers were found.
#ifdef PIXELKILN_SURFACE_XLIB
#define VK_USE_PLATFORM_XLIB_KHR
#endif
#ifdef PIXELKILN_SURFACE_XCB
#define VK_USE_PLATFORM_XCB_KHR
#endif
#ifdef PIXELKILN_SURFACE_WAYLAND
#define VK_USE_PLATFORM_WAYLAND_KHR
#endif
#endif

#include "surface.h"

#include <cstdint>
#include <stdexcept>
#include <string>

#include <vulkan/vulkan.h>

std::vector<const char*> surfaceInstanceExtensions()
{
    std::vector<const char*> extensions = {VK_KHR_SURFACE_EXTENSION_NAME};
#ifdef VK_USE_PLATFORM_WIN32_KHR
    extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#endif
#ifdef VK_USE_PLATFORM_METAL_EXT
    extensions.push_back(VK_EXT_METAL_SURFACE_EXTENSION_NAME);
#endif
#ifdef VK_USE_PLATFORM_XLIB_KHR
    extensions.push_back(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
#endif
#ifdef VK_USE_PLATFORM_XCB_KHR
    extensions.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
#endif
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    extensions.push_back(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME);
#endif
    return extensions;
}

// Surface functions are looked up at runtime: they only exist when their extension was enabled on the instance.
template <typename Function>
static Function instanceFunction(VkInstance instance, const char* name)
{
    auto function = reinterpret_cast<Function>(vkGetInstanceProcAddr(instance, name));
    if (!function) {
        throw std::runtime_error(std::string("PixelKiln: ") + name + " is not available (window system not supported "
                                 "by the Vulkan loader or driver)");
    }
    return function;
}

static void check(VkResult result)
{
    if (result != VK_SUCCESS) {
        throw std::runtime_error("PixelKiln: creating the window surface failed (VkResult " + std::to_string(result) + ")");
    }
}

VkSurfaceKHR createSurface(VkInstance instance, const NativeWindow &window)
{
    if (!window.window) {
        throw std::invalid_argument("PixelKiln: NativeWindow::window must be set");
    }
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    switch (window.type) {
        case NATIVE_WINDOW_WIN32: {
#ifdef VK_USE_PLATFORM_WIN32_KHR
            VkWin32SurfaceCreateInfoKHR info{};
            info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
            info.hinstance = window.display ? static_cast<HINSTANCE>(window.display) : GetModuleHandleW(nullptr);
            info.hwnd = static_cast<HWND>(window.window);
            check(instanceFunction<PFN_vkCreateWin32SurfaceKHR>(instance, "vkCreateWin32SurfaceKHR")(
                instance, &info, nullptr, &surface));
            return surface;
#else
            break;
#endif
        }
        case NATIVE_WINDOW_XLIB: {
#ifdef VK_USE_PLATFORM_XLIB_KHR
            if (!window.display) {
                throw std::invalid_argument("PixelKiln: NATIVE_WINDOW_XLIB needs the Display* in NativeWindow::display");
            }
            VkXlibSurfaceCreateInfoKHR info{};
            info.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
            info.dpy = static_cast<Display*>(window.display);
            info.window = static_cast<Window>(reinterpret_cast<uintptr_t>(window.window));
            check(instanceFunction<PFN_vkCreateXlibSurfaceKHR>(instance, "vkCreateXlibSurfaceKHR")(
                instance, &info, nullptr, &surface));
            return surface;
#else
            break;
#endif
        }
        case NATIVE_WINDOW_XCB: {
#ifdef VK_USE_PLATFORM_XCB_KHR
            if (!window.display) {
                throw std::invalid_argument("PixelKiln: NATIVE_WINDOW_XCB needs the xcb_connection_t* in NativeWindow::display");
            }
            VkXcbSurfaceCreateInfoKHR info{};
            info.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
            info.connection = static_cast<xcb_connection_t*>(window.display);
            info.window = static_cast<xcb_window_t>(reinterpret_cast<uintptr_t>(window.window));
            check(instanceFunction<PFN_vkCreateXcbSurfaceKHR>(instance, "vkCreateXcbSurfaceKHR")(
                instance, &info, nullptr, &surface));
            return surface;
#else
            break;
#endif
        }
        case NATIVE_WINDOW_WAYLAND: {
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
            if (!window.display) {
                throw std::invalid_argument("PixelKiln: NATIVE_WINDOW_WAYLAND needs the wl_display* in NativeWindow::display");
            }
            VkWaylandSurfaceCreateInfoKHR info{};
            info.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
            info.display = static_cast<wl_display*>(window.display);
            info.surface = static_cast<wl_surface*>(window.window);
            check(instanceFunction<PFN_vkCreateWaylandSurfaceKHR>(instance, "vkCreateWaylandSurfaceKHR")(
                instance, &info, nullptr, &surface));
            return surface;
#else
            break;
#endif
        }
        case NATIVE_WINDOW_COCOA_VIEW:
        case NATIVE_WINDOW_METAL_LAYER: {
#ifdef VK_USE_PLATFORM_METAL_EXT
            VkMetalSurfaceCreateInfoEXT info{};
            info.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
            info.pLayer = static_cast<const CAMetalLayer*>(window.type == NATIVE_WINDOW_COCOA_VIEW
                                                               ? pixelKilnAttachMetalLayer(window.window)
                                                               : window.window);
            check(instanceFunction<PFN_vkCreateMetalSurfaceEXT>(instance, "vkCreateMetalSurfaceEXT")(
                instance, &info, nullptr, &surface));
            return surface;
#else
            break;
#endif
        }
        default:
            throw std::invalid_argument("PixelKiln: invalid NativeWindowType");
    }
    throw std::runtime_error("PixelKiln: this window system is not compiled into this build of PixelKiln");
}
