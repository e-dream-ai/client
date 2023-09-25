#import <Cocoa/Cocoa.h>
#import "ESMetalView.h"
#import "base.h"

@implementation ESMetalView

-(id) initWithFrame: (NSRect) frameRect
{

	//self = [super initWithFrame: frameRect pixelFormat: pf];
    self = [super initWithFrame:frameRect device:MTLCreateSystemDefaultDevice()];
    //[viewController.view addSubview:metalView];

    return self;
}

- (BOOL)needsDisplay
{
	return NO;
}

@end
