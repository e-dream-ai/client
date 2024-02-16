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
static NSMutableArray<ESWindow*>* s_ExtraWindows;

static void ShowPreferencesCallback()
{
    dispatch_async(dispatch_get_main_queue(), ^{
        [s_pWindow showPreferences:nil];
    });
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

    //[self setFrame:[self frameRectForContentRect:frame] display:NO];
    //[self setFrame:NSScreen.mainScreen.visibleFrame display:NO];
    frame.origin.x = [NSScreen mainScreen].frame.size.width / 2 - 640;
    frame.origin.y = [NSScreen mainScreen].frame.size.height / 2 - 360;

    [self makeKeyWindow];

    self.collectionBehavior = NSWindowCollectionBehaviorFullScreenPrimary;

    s_ExtraWindows = [[NSMutableArray alloc] init];
    [s_ExtraWindows addObject:self];

    s_pWindow = self;
    [self initWindowProperties];
    ESSetShowPreferencesCallback(ShowPreferencesCallback);
}

- (void)initWindowProperties
{
    //    NSWindowCollectionBehavior behavior = [self collectionBehavior];
    //    behavior &= ~NSWindowCollectionBehaviorFullScreenAuxiliary;
    //    [self setCollectionBehavior:behavior];
    //    NSRect winFrame = frame;
    //    winFrame.origin = CGPointMake(winFrame.origin.x + screen.frame.origin.x, winFrame.origin.y + screen.frame.origin.y);
    //    [window setFrame:winFrame display:YES];
    ESScreensaverView* esView =
        [[ESScreensaverView alloc] initWithFrame:self.frame isPreview:NO];

    if (esView != nil)
    {
        self.contentView = esView;

        esView.autoresizingMask = (NSViewWidthSizable | NSViewHeightSizable);

        [esView setAutoresizesSubviews:YES];

        [self makeFirstResponder:esView];

        [self makeKeyAndOrderFront:nil];

        [esView startAnimation];
    }
    self.delegate = self;
    self->mESView = esView;
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
    [super toggleFullScreen:sender];
}

- (void)windowWillEnterFullScreen:(NSNotification*)notification
{
    for (ESWindow* win in s_ExtraWindows)
    {
        [win toggleFullScreen:notification];
        [win windowDidResize:notification];
    }
}

- (void)windowWillExitFullScreen:(NSNotification*)notification
{
    for (ESWindow* win in s_ExtraWindows)
    {
        [win toggleFullScreen:notification];
        [win windowDidResize:notification];
    }
}

- (void)windowDidEnterFullScreen:(NSNotification*)notification
{
    mIsFullScreen = true;
    ESScreensaver_SetIsFullScreen(mIsFullScreen);
}

- (void)windowDidExitFullScreen:(NSNotification*)notification
{
    mIsFullScreen = false;
    ESScreensaver_SetIsFullScreen(mIsFullScreen);
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
                                          styleMask:NSWindowStyleMaskBorderless
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

- (IBAction)newWindow:(id)sender
{
    ESWindow* window = [[ESWindow alloc]
        initWithContentRect:CGRectMake(self.frame.origin.x + 50,
                                       self.frame.origin.y - 50, 1280, 720)
                  styleMask:NSWindowStyleMaskTitled |
                            NSWindowStyleMaskResizable |
                            NSWindowStyleMaskClosable |
                            NSWindowStyleMaskMiniaturizable |
                            NSWindowStyleMaskFullSizeContentView
                    backing:NSBackingStoreBuffered
                      defer:NO];
    [window initWindowProperties];
    [s_ExtraWindows addObject:window];
}
@end
