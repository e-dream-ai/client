#ifndef USE_METAL
#import <Cocoa/Cocoa.h>

@interface ESOpenGLView : NSOpenGLView
{

}

+ (NSOpenGLPixelFormat*) esPixelFormat;

- (id) initWithFrame: (NSRect) frameRect;

- (BOOL)needsDisplay;

@end
#endif //!USE_METAL
