//
//  MetalDrawableAdapter.m
//

#import "MetalDrawableAdapter.h"

@implementation MetalDrawableAdapter
{
    CAMetalLayer *_layer;
    id<MTLDevice> _device;
    id<CAMetalDrawable> _currentDrawable;
    NSInteger _preferredFramesPerSecond;
}

- (instancetype)initWithMetalLayer:(CAMetalLayer *)layer
                            device:(id<MTLDevice>)device
{
    self = [super init];
    if (self) {
        _layer = layer;
        _device = device;
        _layer.device = device;
        _layer.framebufferOnly = NO;
        _preferredFramesPerSecond = 60;
        // Ensure metal layer is addressable as sublayers[0] for CTextMetal's
        // SCREEN_SAVER path: [[view.layer.sublayers objectAtIndex:0] addSublayer:].
        if (_layer.superlayer == nil) {
            CALayer *root = [CALayer layer];
            root.frame = _layer.frame;
            [root addSublayer:_layer];
        }
    }
    return self;
}

- (id<MTLDevice>)device
{
    return _device;
}

- (CAMetalLayer *)metalLayer
{
    return _layer;
}

- (CALayer *)layer
{
    return _layer.superlayer ?: _layer;
}

- (id<CAMetalDrawable>)currentDrawable
{
    return _currentDrawable;
}

- (NSInteger)preferredFramesPerSecond
{
    return _preferredFramesPerSecond > 0 ? _preferredFramesPerSecond : 60;
}

- (void)setPreferredFramesPerSecond:(NSInteger)preferredFramesPerSecond
{
    _preferredFramesPerSecond = preferredFramesPerSecond > 0 ? preferredFramesPerSecond : 60;
}

- (id<CAMetalDrawable>)nextDrawable
{
    _currentDrawable = [_layer nextDrawable];
    return _currentDrawable;
}

@end
