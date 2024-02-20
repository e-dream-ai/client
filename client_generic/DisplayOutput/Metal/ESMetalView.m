#import "ESMetalView.h"
#import <Cocoa/Cocoa.h>

@implementation ESMetalView
{
    id<MTLRenderPipelineState> _pipelineState;
    id<MTLCommandQueue> _commandQueue;
    id<MTLTexture> _texture;
    NSTimer* _redrawTimer;
}

- (instancetype)initWithFrame:(NSRect)frameRect
{
    self = [super initWithFrame:frameRect];
    if (self)
    {
        self.wantsLayer = YES;
        _device = MTLCreateSystemDefaultDevice();
        _commandQueue = [self.device newCommandQueue];
        CAMetalLayer* metalLayer = [CAMetalLayer layer];
        metalLayer.delegate = self;
        metalLayer.device = _device;
        metalLayer.contentsScale = [NSScreen mainScreen].backingScaleFactor;

        metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        metalLayer.framebufferOnly = NO;
        metalLayer.shouldRasterize = NO;
        [self.layer addSublayer:metalLayer];
        _redrawTimer = [NSTimer scheduledTimerWithTimeInterval:(1.0 / 60.0)
                                                        target:self
                                                      selector:@selector(redraw)
                                                      userInfo:nil
                                                       repeats:YES];
        [self viewDidEndLiveResize];
        [self redraw];
    }
    [self setNeedsDisplay:YES];

    return self;
}

- (void)setPreferredFramesPerSecond:(NSInteger)preferredFramesPerSecond
{
    [_redrawTimer invalidate];
    _redrawTimer =
        [NSTimer scheduledTimerWithTimeInterval:(1.0 / preferredFramesPerSecond)
                                         target:self
                                       selector:@selector(redraw)
                                       userInfo:nil
                                        repeats:YES];
    _preferredFramesPerSecond = preferredFramesPerSecond;
}

- (void)redraw
{
    [self setNeedsDisplay:YES];
}

- (void)viewDidEndLiveResize
{
    [super viewDidEndLiveResize];
    CAMetalLayer* metalLayer = (CAMetalLayer*)self.layer.sublayers[0];
    CGSize size = self.bounds.size;
    size.width *= [NSScreen mainScreen].backingScaleFactor;
    size.height *= [NSScreen mainScreen].backingScaleFactor;
    metalLayer.drawableSize = size;
}

- (void)drawRect:(NSRect)dirtyRect
{
    CAMetalLayer* metalLayer = (CAMetalLayer*)self.layer.sublayers[0];
    metalLayer.bounds = self.layer.bounds;
    metalLayer.frame = self.bounds;
    id<CAMetalDrawable> drawable = [metalLayer nextDrawable];
    if (drawable)
    {
        _currentDrawable = drawable;
    }
    [self.delegate drawInMetalView:self];
}

- (void)drawLayer:(CALayer*)layer inContext:(CGContextRef)ctx
{
    NSRect rect = self.bounds;
    rect.origin.x = 0;
    rect.origin.y = 0;
    [self drawRect:rect];
}

- (BOOL)needsDisplay
{
    return NO;
}

@end
