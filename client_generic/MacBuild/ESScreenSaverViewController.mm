//
// ESScreenSaverViewController.mm
//
// Host creates and sizes the remote view after attach. Frame is unknown in
// loadView; isPreview is not conveyed to the extension — use zero frame and
// isPreview:NO like Apple's savers and AppexSaverMinimal.
//
// Do not override startAnimation/stopAnimation here: the framework forwards
// those to the ScreenSaverView.
//

#import "ESScreenSaverViewController.h"
#import "ESScreensaverView.h"
#include "Log.h"

@implementation ESScreenSaverViewController
{
    ESScreensaverView *_saverView;
}

- (void)loadView
{
    if (g_Log)
        g_Log->Info("ESScreenSaverViewController loadView");

    // Host sizes the remote view after attach; autoresizing follows resizes.
    ESScreensaverView *view =
        [[ESScreensaverView alloc] initWithFrame:NSZeroRect isPreview:NO];
    view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    _saverView = view;
    self.view = view ?: [[NSView alloc] initWithFrame:NSZeroRect];
}

@end
