#import <Cocoa/Cocoa.h>

#import "ESScreensaverView.h"

@interface ESWindow : NSWindow
#if defined(MAC_OS_X_VERSION_10_6) &&                                          \
    (MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_6)
                      <NSWindowDelegate, NSApplicationDelegate>
#endif
{
@public
    ESScreensaverView* mESView;

@protected
    ESWindow* mFullScreenWindow;

    ESWindow* mOriginalWindow;

    BOOL mIsFullScreen;

    BOOL mInSheet;

    BOOL mBlackouMonitors;

    NSMutableArray* mBlackingWindows;
}

- (void)awakeFromNib;

- (void)initWindowProperties;

//- (void)windowWillClose:(NSNotification *)notification;

- (BOOL)showPreferences:(id)sender;

- (void)didEndSheet:(NSWindow*)sheet
         returnCode:(NSModalResponse)returnCode
        contextInfo:(void*)contextInfo;

- (void)windowDidResize:(NSNotification*)notification;

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:
    (NSApplication*)theApplication;

- (BOOL)canBecomeKeyWindow;

- (ESWindow*)originalWindow;

- (void)setOriginalWindow:(ESWindow*)window;

- (BOOL)isFullScreen;

- (void)switchFullScreen:(id)sender;

- (void)blackScreensExcept:(NSScreen*)fullscreen;

- (void)unblackScreens;

- (void)fadeWindow:(NSWindow*)window withEffect:(NSString*)effect;

- (IBAction)newWindow:(id)sender;

@end
