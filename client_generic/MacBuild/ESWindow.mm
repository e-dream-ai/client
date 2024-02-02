#import <IOKit/IOMapTypes.h>
#import <IOKit/pwr_mgt/IOPMLib.h>
#import "ESWindow.h"
#import "ESScreensaver.h"

#include "client.h"

@implementation ESWindow
{
    IOPMAssertionID noSleepAssertionIDUser;
    IOPMAssertionID noSleepAssertionIDSystem;
};

static __weak ESWindow* s_pWindow = nil;
static void ShowPreferencesCallback()
{
    dispatch_async(dispatch_get_main_queue(), ^{
        [s_pWindow showPreferences:nil];
    });
}

- (IBAction)showHelpFile:(NSMenuItem*)sender
{
    [[NSWorkspace sharedWorkspace]
        openFile:[[NSBundle mainBundle] pathForResource:@"Instructions"
                                                 ofType:@"rtf"]];
}

- (void)awakeFromNib // was - (NSWindow *)window
{
    self.delegate = self;

    [NSApplication sharedApplication].delegate = self;

    NSRect frame = [self contentRectForFrameRect:self.frame];

    mInSheet = NO;

    mFullScreenWindow = nil;

    mIsFullScreen = NO;

    mOriginalWindow = nil;

    mBlackingWindows = nil;

    //ESScreensaver_InitClientStorage();

    frame.size.width = 1280;
    frame.size.height = 720;
    self.contentAspectRatio = CGSizeMake(16.f, 9.f);

    mBlackouMonitors =
        ESScreensaver_GetBoolSetting("settings.player.blackout_monitors", true);

    ESScreensaver_DeinitClientStorage();

    [self setFrame:[self frameRectForContentRect:frame] display:NO];

    [self makeKeyWindow];

    self.collectionBehavior = NSWindowCollectionBehaviorFullScreenPrimary;

    mESView = [[ESScreensaverView alloc] initWithFrame:frame isPreview:NO];

    if (mESView != nil)
    {
        self.contentView = mESView;

        mESView.autoresizingMask = (NSViewWidthSizable | NSViewHeightSizable);

        [mESView setAutoresizesSubviews:YES];

        [self makeFirstResponder:mESView];

        [mESView startAnimation];
    }
    s_pWindow = self;
    ESSetShowPreferencesCallback(ShowPreferencesCallback);
}

- (void)switchFullScreen:(id)sender
{
    [self toggleFullScreen:sender];
}

- (void)toggleFullScreen:(id)sender
{
    //NSWorkspace *workspace = [NSWorkspace sharedWorkspace];
    //[workspace setIdleTimerDisabled:self.isFullScreen];
    if ([self isFullScreen])
    {
        IOPMAssertionRelease(noSleepAssertionIDUser);
        IOPMAssertionRelease(noSleepAssertionIDSystem);
    }
    else
    {
        IOPMAssertionCreateWithName(kIOPMAssertPreventUserIdleDisplaySleep,
                                    IOPMAssertionLevel(kIOPMAssertionLevelOn),
                                    CFSTR("Full Screen Video (e-dream)"),
                                    &noSleepAssertionIDUser);
        IOPMAssertionCreateWithName(
            kIOPMAssertPreventUserIdleSystemSleep, kIOPMAssertionLevelOn,
            CFSTR("Full Screen Video (e-dream)"), &noSleepAssertionIDSystem);
    }
    mIsFullScreen = !mIsFullScreen;
    ESScreensaver_SetIsFullScreen(mIsFullScreen);
    [super toggleFullScreen:sender];
}

/*
        Black out all screens except fullscreen screen
 */
- (void)blackScreensExcept:(NSScreen*)fullscreen
{
    if (mBlackingWindows != nil)
    {
        mBlackingWindows = nil;
    }

    mBlackingWindows =
        [[NSMutableArray alloc] initWithCapacity:[NSScreen screens].count];

    unsigned int i;
    NSWindow* win;
    NSRect fs_rect;
    for (i = 0; i < [NSScreen screens].count; i++)
    {

        NSScreen* actScreen = NSScreen.screens[i];

        if (actScreen == nil || fullscreen == actScreen)
            continue;

        // when blacking the main screen, hide the menu bar and dock
        if (actScreen == [NSScreen mainScreen])
            [NSApplication sharedApplication].presentationOptions =
                NSApplicationPresentationFullScreen |
                NSApplicationPresentationAutoHideDock |
                NSApplicationPresentationAutoHideMenuBar;

        fs_rect = actScreen.frame;
        fs_rect.origin = NSZeroPoint;
        win = [[NSWindow alloc] initWithContentRect:fs_rect
                                          styleMask:NSBorderlessWindowMask
                                            backing:NSBackingStoreBuffered
                                              defer:NO
                                             screen:NSScreen.screens[i]];
        win.backgroundColor = [NSColor blackColor];
        win.level = NSScreenSaverWindowLevel;
        [win orderFront:nil];

        // if ([[AppController sharedController] animateInterface])
        [self fadeWindow:win withEffect:NSViewAnimationFadeInEffect];

        [mBlackingWindows addObject:win];
    }
}

/*
        Remove black out windows
 */
- (void)unblackScreens
{
    if (mBlackingWindows == nil)
        return;

    unsigned int i;
    for (i = 0; i < mBlackingWindows.count; i++)
    {
        /*if (![[AppController sharedController] animateInterface])
                [[mBlackingWindows objectAtIndex:i] close];
        else*/
        [self fadeWindow:mBlackingWindows[i]
              withEffect:NSViewAnimationFadeOutEffect];
    }

    mBlackingWindows = nil;
}

/*
        Animate window fading in/out
*/
- (void)fadeWindow:(NSWindow*)window withEffect:(NSString*)effect
{

    NSViewAnimation* anim;
    NSMutableDictionary* animInfo;

    animInfo = [NSMutableDictionary dictionaryWithCapacity:2];
    animInfo[NSViewAnimationTargetKey] = window;
    animInfo[NSViewAnimationEffectKey] = effect;

    anim = [[NSViewAnimation alloc] initWithViewAnimations:@[ animInfo ]];
    anim.animationBlockingMode = NSAnimationNonblockingThreaded;
    anim.animationCurve = NSAnimationEaseIn;
    anim.duration = 0.3;

    [anim startAnimation];
}

- (void)fullscreenWindowMoved:(NSNotification*)__unused notification
{
    // triggered when fullscreen window changes spaces
    NSRect screen_frame = mFullScreenWindow.screen.frame;
    [mFullScreenWindow setFrame:screen_frame display:YES animate:NO];
}

- (void)windowWillClose:(NSNotification*)__unused notification
{
    if (mESView != nil)
    {
        [mESView stopAnimation];
    }
}

- (BOOL)showPreferences:(id)__unused sender
{
    //@TODO: is the full screen check needed? disabling for now
    if (/*!mIsFullScreen &&*/ mESView && [mESView hasConfigureSheet])
    {
        [mESView stopAnimation];
        mInSheet = YES;
        [self beginSheet:[mESView configureSheet]
            completionHandler:^(NSModalResponse returnCode) {
                [self didEndSheet:[self->mESView configureSheet]
                       returnCode:returnCode
                      contextInfo:nil];
            }];
        /*[NSApp beginSheet: [mESView configureSheet]
            modalForWindow: self
            modalDelegate: self
            didEndSelector: @selector(didEndSheet:returnCode:contextInfo:)
            contextInfo: nil];*/
    }
    return YES;
}

- (void)didEndSheet:(NSWindow*)sheet
         returnCode:(NSModalResponse)__unused returnCode
        contextInfo:(void*)__unused contextInfo
{
    [sheet orderOut:self];

    mInSheet = NO;

    if (mESView != nil)
    {
        [mESView startAnimation];
        [mESView windowDidResize];
    }
}

- (void)windowDidResize:(NSNotification*)__unused notification
{
    if (mInSheet)
        return;

    if (mESView != nil)
        [mESView windowDidResize];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)__unused
    theApplication
{
    return YES;
}

- (BOOL)canBecomeKeyWindow
{
    return YES;
}

- (ESWindow*)originalWindow
{
    return mOriginalWindow;
}

- (void)setOriginalWindow:(ESWindow*)window
{
    mOriginalWindow = window;
}

- (BOOL)isFullScreen
{
    return mIsFullScreen;
}

- (void)keyDown:(NSEvent*)ev
{
    BOOL handled = NO;

    NSString* characters = ev.charactersIgnoringModifiers;
    unsigned int characterIndex,
        characterCount = (unsigned int)characters.length;

    for (characterIndex = 0; characterIndex < characterCount; characterIndex++)
    {
        unichar c = [characters characterAtIndex:characterIndex];
        switch (c)
        {
            /*case 0x1B: //ESC key
                    {
        BOOL isFS = (([[NSApplication sharedApplication]
        currentSystemPresentationOptions] & NSApplicationPresentationFullScreen)
        == NSApplicationPresentationFullScreen);

        if ( isFS )
                            {
                                    [self switchFullScreen:self];
                            }
                    }
                    handled = YES;
                    break;*/

        default:
            break;
        }
    }

    // If we didn't handle the key press, send it to the parent class
    if (handled == NO)
        [super keyDown:ev];
}

@end
