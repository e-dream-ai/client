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

- (void)stopRedrawTimer
{
    [_redrawTimer invalidate];
    _redrawTimer = nil;
}

- (void)redraw
{
    [self setNeedsDisplay:YES];
}

- (void)_updateMetalDrawableSize
{
    if (self.layer.sublayers.count == 0)
        return;
    CAMetalLayer* metalLayer = (CAMetalLayer*)self.layer.sublayers[0];
    CGFloat scale = self.window.backingScaleFactor;
    if (scale <= 0)
        scale = NSScreen.mainScreen.backingScaleFactor;
    if (scale <= 0)
        scale = 1.0;
    CGSize size = self.bounds.size;
    // WallpaperAgent/System Settings attach at 0×0 before remoteViewSizeChanged;
    // never ask Metal for a zero-sized drawable.
    if (size.width < 1.0 || size.height < 1.0)
        return;
    size.width *= scale;
    size.height *= scale;
    if (!CGSizeEqualToSize(metalLayer.drawableSize, size))
        metalLayer.drawableSize = size;
}

- (void)layout
{
    [super layout];
    [self _updateMetalDrawableSize];
}

- (void)viewDidEndLiveResize
{
    [super viewDidEndLiveResize];
    [self _updateMetalDrawableSize];
}

- (void)drawRect:(NSRect)dirtyRect
{
    if (self.bounds.size.width < 1.0 || self.bounds.size.height < 1.0)
        return;
    if (self.layer.sublayers.count == 0)
        return;
    CAMetalLayer* metalLayer = (CAMetalLayer*)self.layer.sublayers[0];
    [self _updateMetalDrawableSize];
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
