#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

@interface ESMetalView : MTKView
{
}

- (id)initWithFrame:(NSRect)frameRect;

- (BOOL)needsDisplay;

@end
