//
// Created by Stefan Balta on 2026-09-21.
//

#ifndef PIXELKILN_NATIVEWINDOW_H
#define PIXELKILN_NATIVEWINDOW_H

// Platform window handles of an application-owned window. The window must outlive the swapchain created from it.
enum NativeWindowType {
    NATIVE_WINDOW_WIN32,       // display = HINSTANCE,         window = HWND
    NATIVE_WINDOW_XLIB,        // display = Display*,          window = Window (cast through uintptr_t)
    NATIVE_WINDOW_XCB,         // display = xcb_connection_t*, window = xcb_window_t (cast through uintptr_t)
    NATIVE_WINDOW_WAYLAND,     // display = wl_display*,       window = wl_surface*
    NATIVE_WINDOW_COCOA_VIEW,  // display unused,              window = NSView*, PixelKiln attaches a CAMetalLayer to it
                               // (createSwapchain must then be called on the main thread)
    NATIVE_WINDOW_METAL_LAYER, // display unused,              window = CAMetalLayer* owned by the application
    NATIVE_WINDOW_TYPE_COUNT
};

struct NativeWindow {
    NativeWindowType type = NATIVE_WINDOW_TYPE_COUNT;
    void* display = nullptr;
    void* window = nullptr;
};

#endif //PIXELKILN_NATIVEWINDOW_H
