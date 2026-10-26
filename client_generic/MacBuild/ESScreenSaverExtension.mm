//
// ESScreenSaverExtension.mm
//
// Minimal principal class — framework drives the lifecycle (same pattern as
// Apple's screensavers and AppexSaverMinimal).
//

#import "ESScreenSaverExtension.h"
#include "Log.h"
#include <unistd.h>

@implementation ESScreenSaverExtension

- (instancetype)init
{
    self = [super init];
    if (self) {
        if (g_Log) {
            g_Log->Info("ESScreenSaverExtension init PID=%d loaded from %s",
                        (int)getpid(),
                        [[[NSBundle bundleForClass:[self class]] bundlePath] UTF8String]);
        } else {
            NSLog(@"ESScreenSaverExtension init PID=%d path=%@",
                  @(getpid()),
                  [[NSBundle bundleForClass:[self class]] bundlePath]);
        }
    }
    return self;
}

@end
