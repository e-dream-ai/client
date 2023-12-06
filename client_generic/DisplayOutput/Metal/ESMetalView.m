#import "ESMetalView.h"
#import "base.h"
#import <Cocoa/Cocoa.h>

@implementation ESMetalView

- (id)initWithFrame:(NSRect)frameRect
{
  NSArray<id<MTLDevice>> *devices = MTLCopyAllDevices();
  id<MTLDevice> selectedDevice = nil;
  for (id<MTLDevice> device in devices)
  {
    if (!device.isLowPower)
    {
      selectedDevice = device;
      break; // Found the discrete GPU, exit the loop
    }
  }
  if (selectedDevice == nil)
    selectedDevice = devices[0];

  self = [super initWithFrame:frameRect device:selectedDevice];
  //[viewController.view addSubview:metalView];

  return self;
}

- (BOOL)needsDisplay
{
  return NO;
}

@end
