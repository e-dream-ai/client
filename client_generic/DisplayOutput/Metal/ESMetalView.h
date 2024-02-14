#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

@class ESMetalView;

@protocol ESMetalViewDelegate <NSObject>

/*!
 @method mtkView:drawableSizeWillChange:
 @abstract Called whenever the drawableSize of the view will change
 @discussion Delegate can recompute view and projection matricies or regenerate any buffers to be compatible with the new view size or resolution
 @param view MTKView which called this method
 @param size New drawable size in pixels
 */
- (void)mtkView:(nonnull MTKView*)view drawableSizeWillChange:(CGSize)size;

/*!
 @method drawInMTKView:
 @abstract Called on the delegate when it is asked to render into the view
 @discussion Called on the delegate when it is asked to render into the view
 */
- (void)drawInMetalView:(nonnull ESMetalView*)view;

@end

@interface ESMetalView : NSView <CALayerDelegate>
{
}

- (nonnull id)initWithFrame:(NSRect)frameRect;

- (BOOL)needsDisplay;

@property(nonatomic, readonly, nonnull) id<MTLDevice> device;
@property(nonatomic, nullable) id<ESMetalViewDelegate> delegate;
@property(nonatomic, readonly, nullable) id<CAMetalDrawable> currentDrawable;
@property(nonatomic) NSInteger preferredFramesPerSecond;

@end
