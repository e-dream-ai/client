//
//  MetalDrawableAdapter.h
//  Thin CAMetalLayer wrapper duck-typed like ESMetalView for RendererMetal
//  (expects -device / -currentDrawable / -preferredFramesPerSecond / -layer).
//

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

NS_ASSUME_NONNULL_BEGIN

@interface MetalDrawableAdapter : NSObject

- (instancetype)initWithMetalLayer:(CAMetalLayer *)layer
                            device:(id<MTLDevice>)device NS_DESIGNATED_INITIALIZER;
- (instancetype)init NS_UNAVAILABLE;

@property (nonatomic, readonly) id<MTLDevice> device;
@property (nonatomic, readonly, nullable) id<CAMetalDrawable> currentDrawable;
@property (nonatomic, readonly) CAMetalLayer *metalLayer;
/// Same selector surface as ESMetalView / MTKView — RendererMetal reads this.
@property (nonatomic) NSInteger preferredFramesPerSecond;
/// Root (or metal) layer so CTextMetal SCREEN_SAVER path can add HUD sublayers.
@property (nonatomic, readonly) CALayer *layer;

/// Call before DoFrame so currentDrawable is fresh for this tick.
- (nullable id<CAMetalDrawable>)nextDrawable;

@end

NS_ASSUME_NONNULL_END
