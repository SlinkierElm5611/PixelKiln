//
// Created by Stefan Balta on 2026-09-21.
//

#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>

#include <stdexcept>

#include "surface.h"

void* pixelKilnAttachMetalLayer(void* view)
{
    // AppKit views may only be modified on the main thread.
    if (![NSThread isMainThread]) {
        throw std::runtime_error("PixelKiln: createSwapchain with NATIVE_WINDOW_COCOA_VIEW must be called on the main "
                                 "thread (or pass a CAMetalLayer with NATIVE_WINDOW_METAL_LAYER)");
    }
    NSView* nsView = (__bridge NSView*)view;
    if ([nsView.layer isKindOfClass:[CAMetalLayer class]]) {
        return (__bridge void*)nsView.layer;
    }
    CAMetalLayer* layer = [CAMetalLayer layer];
    // Layer-hosting view: set the layer before enabling wantsLayer.
    [nsView setLayer:layer];
    [nsView setWantsLayer:YES];
    NSScreen* screen = nsView.window ? nsView.window.screen : [NSScreen mainScreen];
    layer.contentsScale = nsView.window ? nsView.window.backingScaleFactor : (screen ? screen.backingScaleFactor : 1.0);
    return (__bridge void*)layer;
}
