/*
  Q Light Controller Plus
  spatialview_macos.mm

  Copyright (c) Marcus Williford
  Licensed under the Apache License, Version 2.0
*/

#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>

// Ensure the NSView backing the QWindow has a CAMetalLayer for bgfx Metal.
// We create the layer ourselves because bgfx's internal layer creation
// conflicts with Qt's QNSView on macOS, causing magenta screen.
void *setupMetalLayerForView(void *nativeHandle)
{
    NSView *view = (__bridge NSView *)nativeHandle;
    if (!view)
        return nativeHandle;

    [view setWantsLayer:YES];

    CAMetalLayer *metalLayer = [CAMetalLayer layer];
    metalLayer.contentsScale = view.window.backingScaleFactor;
    metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    [view setLayer:metalLayer];

    NSLog(@"[SpatialView] Configured CAMetalLayer on NSView %@, scale=%.1f",
          view, metalLayer.contentsScale);

    return nativeHandle;
}
