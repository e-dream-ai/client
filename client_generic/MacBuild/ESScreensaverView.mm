#import <OpenGL/OpenGL.h>

#import "ESScreensaverView.h"
#import "ESScreensaver.h"
#import <Bugsnag/Bugsnag.h>
#import <Sparkle/Sparkle.h>
#include <csignal>

#include "dlfcn.h"
#include "libgen.h"
#include "Log.h"
#include "DisplayOutput.h"
#include "PlatformUtils.h"

@implementation ESScreensaverView
{
    BOOL m_bStarted;
    int m_displayIdx;
}

- (instancetype)initWithFrame:(NSRect)frame isPreview:(BOOL)isPreview
{
#ifdef SCREEN_SAVER
    //[Bugsnag start];
    self = [super initWithFrame:frame isPreview:isPreview];
#else
    //[SPUStandardUpdaterController init];
    
    self = [super initWithFrame:frame];
#endif
    //os_log(OS_LOG_DEFAULT, "EDTEST  INIT");


    m_updater = NULL;
    m_sparkleUpdater = NULL;
    m_updateAvailable = NO;

    m_isFullScreen = !isPreview;
    m_isStopped = YES;

    m_isPreview = isPreview;
    DEBUG_LOG("INIT");

    // Initialize Sparkle updater for both app and screensaver
    // For screensaver: explicitly specify the bundle so Sparkle can find Info.plist
    // For app: full functionality with menu item connection
    
    NSLog(@"ESScreensaverView: Initializing Sparkle...");
    
#ifdef SCREEN_SAVER
    NSLog(@"ESScreensaverView: Running in SCREEN_SAVER mode");
    // For screensaver: use explicit bundle initialization
    // This ensures Sparkle reads SUPublicEDKey and other settings from our bundle
    m_updater = nil;
    m_sparkleUpdater = nil;
    
    @try {
        NSBundle *screensaverBundle = [NSBundle bundleForClass:[self class]];
        NSLog(@"Sparkle: initializing for screensaver bundle: %@ at %@", 
              screensaverBundle.bundleIdentifier, screensaverBundle.bundlePath);
        
        SPUStandardUserDriver *userDriver = [[SPUStandardUserDriver alloc] initWithHostBundle:screensaverBundle delegate:nil];
        SPUUpdater *updater = [[SPUUpdater alloc] initWithHostBundle:screensaverBundle
                                                   applicationBundle:screensaverBundle
                                                          userDriver:userDriver
                                                            delegate:self];
        
        NSError *error = nil;
        if ([updater startUpdater:&error]) {
            m_sparkleUpdater = updater;
            m_sparkleUpdater.automaticallyChecksForUpdates = YES;
            NSLog(@"Sparkle: successfully started for screensaver");
            
            // Trigger an immediate background check for updates
            [m_sparkleUpdater checkForUpdatesInBackground];
            NSLog(@"Sparkle: initiated background update check");
        } else {
            NSLog(@"Sparkle: failed to start - %@", error.localizedDescription);
        }
    } @catch (NSException *exception) {
        NSLog(@"Sparkle: exception during initialization - %@: %@", exception.name, exception.reason);
    }
#else
    NSLog(@"ESScreensaverView: Running in APP mode");
    // For app: use standard controller
    m_sparkleUpdater = nil;
    m_updater = [[SPUStandardUpdaterController alloc] initWithStartingUpdater:YES
                                                              updaterDelegate:self
                                                           userDriverDelegate:nil];
    
    if (m_updater) {
        [m_updater.updater checkForUpdatesInBackground];
    }
    
    // Connect "Check for Updates..." menu item to the updater (app only)
    [self connectCheckForUpdatesMenuItem];
#endif

    if (self)
    {
        // So we modify the setup routines just a little bit to get our
        // new OpenGL screensaver working.

        // Create the new frame
        NSRect newFrame = frame;
        // Slam the origin values
        newFrame.origin.x = 0.0;
        newFrame.origin.y = 0.0;

        if (ESScreensaver_GetBoolSetting("settings.player.preserve_AR", true)) {
            if (newFrame.size.width/newFrame.size.height < (16.0f / 9.0f) )
            {
                newFrame.size = CGSizeMake(newFrame.size.width, newFrame.size.width * 9.0f / 16.0f);
                newFrame.origin.y = (frame.size.height - newFrame.size.height) / 2;
            } else {
                newFrame.size = CGSizeMake(newFrame.size.height * 16.0f / 9.0f, newFrame.size.height);
                newFrame.origin.x = (frame.size.width - newFrame.size.width) / 2;
            }
        }

        theRect = newFrame;
        {
            // Make sure we autoresize
            [self setAutoresizesSubviews:YES];
            view = NULL;
#ifdef SCREEN_SAVER
            [self setAnimationTimeInterval:-1];
#endif
        }
        
        
    }
    // Finally return our newly-initialized self
    return self;
}

- (void)startAnimation
{
    //g_Log->Error("Test error 2");
    if (view == NULL)
    {
#ifdef SCREEN_SAVER
        ESMetalView* metalView =

            [[ESMetalView alloc] initWithFrame:theRect];
#else
        NSArray<id<MTLDevice>>* devices = MTLCopyAllDevices();
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

        MTKView* metalView =

            [[MTKView alloc] initWithFrame:theRect device:selectedDevice];
#endif
        metalView.delegate = self;
        metalView.preferredFramesPerSecond = 60;

        view = metalView;

        if (view)
        {
            // We make it a subview of the screensaver view
            [self addSubview:view];
        }
    }
    if (view != NULL)
    {
        m_displayIdx = ESScreenSaver_AddGraphicsContext((__bridge void*)view);
    }

    uint32_t width = (uint32_t)theRect.size.width;
    uint32_t height = (uint32_t)theRect.size.height;

#ifdef SCREEN_SAVER
    m_isHidden = NO;
#endif

    if (!m_bStarted)
    {

        if (!ESScreensaver_Start(m_isPreview, width, height))
            return;

#ifdef SCREEN_SAVER
        g_Log->Info("Saver startAnimation on %f %f ", self.frame.size.width, self.frame.size.height);
#endif
       
        [self _beginThread];
        m_bStarted = YES;
    }
    
#ifdef SCREEN_SAVER
    [super startAnimation];
#endif
}

- (void)stopAnimation
{
#ifdef SCREEN_SAVER
    [NSCursor unhide];
#endif
    if (m_bStarted)
    {
        [self _endThread];

        ESScreensaver_Stop();

        ESScreensaver_Deinit();
        m_bStarted = NO;
    }

#ifdef SCREEN_SAVER
    if (m_isHidden)
        [NSCursor unhide];
    m_isHidden = NO;
    [super stopAnimation];
#endif
}

- (void)animateOneFrame
{
    //[self setAnimationTimeInterval:-1];

    //[animationLock unlock];

    // ESScreensaver_DoFrame();

    //[view setNeedsDisplay:YES];
}

- (void)_animationThread
{
    DEBUG_LOG("ANIMATIONTHREAD");
    while (!m_isStopped && !ESScreensaver_Stopped())
    {
        @autoreleasepool
        {
            if (!ESScreensaver_DoFrame(m_displayIdx, *m_beginFrameBarrier,
                                       *m_endFrameBarrier))
                break;
#ifdef SCREEN_SAVER
            if (!m_isPreview && CGCursorIsVisible())
            {
                [NSCursor hide];
                m_isHidden = YES;
            }
#endif
            // if (m_isStopped)
            // break;

            // if (view != NULL)
            //[view setNeedsDisplay:YES];
        }
    }
}

static void signnal_handler(int signal)
{
    if (signal == SIGSEGV || signal == SIGTERM || signal == SIGKILL)
    {
        g_Log->Info("RECEIVED SIGSEGV");
        //g_Log->Shutdown();
        exit(0);
    }
}

- (void)_beginThread
{
    //[animationLock lock];
    DEBUG_LOG("BEGINTHREAD");
    m_isStopped = NO;
    m_beginFrameBarrier = std::make_unique<boost::barrier>(2);
    m_endFrameBarrier = std::make_unique<boost::barrier>(2);
    m_animationDispatchGroup = dispatch_group_create();
    m_frameUpdateQueue = dispatch_queue_create("Frame Update", NULL);
    [NSWorkspace.sharedWorkspace.notificationCenter
        addObserver:self
           selector:@selector(willStop:)
               name:NSWorkspaceWillSleepNotification
             object:nil];
    [NSDistributedNotificationCenter.defaultCenter
        addObserver:self
           selector:@selector(onSleepNote:)
               name:NSNotificationName(@"com.apple.screensaver.willstop")
             object:nil];
    std::signal(SIGSEGV, signnal_handler);

    dispatch_async(m_frameUpdateQueue, ^{
        PlatformUtils::SetThreadName("FrameUpdate");
        dispatch_group_enter(self->m_animationDispatchGroup);
        [self _animationThread];
        dispatch_group_leave(self->m_animationDispatchGroup);
    });
}

- (void)willStop:(NSNotification*)notification
{
#ifdef SCREEN_SAVER
    g_Log->Info("Killed by system from willStop");
    if (@available(macOS 14.0, *)) {
        g_Log->Info("Deinit");
        ESScreensaver_Deinit();
        g_Log->Info("Log shutdown, will exit in 2 seconds");
        g_Log->Shutdown();
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
            exit(0);
        });
    }
#endif
}

- (void)onSleepNote:(NSNotification*)notif
{
#ifdef SCREEN_SAVER
    g_Log->Info("Killed by system from onSleepNote %s", notif.name.UTF8String);
    NSLog(@"notif.object:%@", notif.object);

    if (@available(macOS 14.0, *)) {
        g_Log->Info("Deinit");
        ESScreensaver_Deinit();
        g_Log->Info("Log shutdown, will exit");
        g_Log->Shutdown();
        exit(0);
    }
#endif
}

- (void)_endThread
{
    m_beginFrameBarrier->wait();
    m_isStopped = YES;
    m_endFrameBarrier->wait();

    dispatch_group_wait(m_animationDispatchGroup, DISPATCH_TIME_FOREVER);
}

- (void)windowDidResize
{
    // Set background to black for letterboxing/pillarboxing
    if (self.window) {
        self.window.backgroundColor = [NSColor blackColor];
    }

    // Calculate 16:9 constrained frame within the window
    NSRect videoFrame = self.frame;
    float frameAR = self.frame.size.width / self.frame.size.height;
    float targetAR = 16.0f / 9.0f;

    // Only adjust if aspect ratio differs significantly from 16:9
    if (fabsf(frameAR - targetAR) > 0.01f)
    {
        NSSize constrainedSize = self.frame.size;

        if (frameAR < targetAR)  // Frame is taller than 16:9 (e.g., 16:10)
        {
            // Constrain height and center vertically
            constrainedSize.height = constrainedSize.width * 9.0f / 16.0f;
            CGFloat yOffset = (self.frame.size.height - constrainedSize.height) / 2.0f;
            videoFrame = NSMakeRect(0, yOffset, constrainedSize.width, constrainedSize.height);
        }
        else  // Frame is wider than 16:9
        {
            // Constrain width and center horizontally
            constrainedSize.width = constrainedSize.height * 16.0f / 9.0f;
            CGFloat xOffset = (self.frame.size.width - constrainedSize.width) / 2.0f;
            videoFrame = NSMakeRect(xOffset, 0, constrainedSize.width, constrainedSize.height);
        }
    }

    view.frame = videoFrame;

    ESScreensaver_ForceWidthAndHeight((uint32_t)videoFrame.size.width,
                                      (uint32_t)videoFrame.size.height);
}

- (BOOL)hasConfigureSheet
{
    return YES;
}

- (NSWindow*)configureSheet
{
    if (!m_config)
    {
        m_config =
            [[ESConfiguration alloc] initWithWindowNibName:@"ElectricSheep"];
    }

    return m_config.window;
}

- (void)flagsChanged:(NSEvent*)ev
{
    if (ev.keyCode == 63) // FN Key
        return;

    [super flagsChanged:ev];
}

- (void)keyDown:(NSEvent*)ev
{
    BOOL handled = NO;

    NSString* characters = ev.charactersIgnoringModifiers;
    //NSLog(@"char: %@ - %@", ev.charactersIgnoringModifiers, ev.characters);
    unsigned int characterIndex,
        characterCount = (unsigned int)characters.length;

    for (characterIndex = 0; characterIndex < characterCount; characterIndex++)
    {
        unichar c = [characters characterAtIndex:characterIndex];
        using namespace DisplayOutput;

        std::map<unichar, CKeyEvent::eKeyCode> keyMap = {
            {NSRightArrowFunctionKey, CKeyEvent::eKeyCode::KEY_RIGHT},
            {NSLeftArrowFunctionKey, CKeyEvent::eKeyCode::KEY_LEFT},
            {NSUpArrowFunctionKey, CKeyEvent::eKeyCode::KEY_UP},
            {NSDownArrowFunctionKey, CKeyEvent::eKeyCode::KEY_DOWN},
            {NSF1FunctionKey, CKeyEvent::eKeyCode::KEY_F1},
            {NSF2FunctionKey, CKeyEvent::eKeyCode::KEY_F2},
            //            {NSF3FunctionKey, CKeyEvent::eKeyCode::KEY_F3},
            //            {NSF4FunctionKey, CKeyEvent::eKeyCode::KEY_F4},
            //            {NSF5FunctionKey, CKeyEvent::eKeyCode::KEY_F5},
            //            {NSF6FunctionKey, CKeyEvent::eKeyCode::KEY_F6},
            //            {NSF7FunctionKey, CKeyEvent::eKeyCode::KEY_F7},
            //            {NSF8FunctionKey, CKeyEvent::eKeyCode::KEY_F8},
            //            {NSF9FunctionKey, CKeyEvent::eKeyCode::KEY_F9},
            //            {NSF10FunctionKey, CKeyEvent::eKeyCode::KEY_F10},
            //            {NSF11FunctionKey, CKeyEvent::eKeyCode::KEY_F11},
            //            {NSF12FunctionKey, CKeyEvent::eKeyCode::KEY_F12},
            //            {NSDeleteFunctionKey, CKeyEvent::eKeyCode::KEY_DELETE},
            //            {NSHomeFunctionKey, CKeyEvent::eKeyCode::KEY_HOME},
            //            {NSEndFunctionKey, CKeyEvent::eKeyCode::KEY_END},
            //            {NSPageUpFunctionKey, CKeyEvent::eKeyCode::KEY_PAGEUP},
            //            {NSPageDownFunctionKey, CKeyEvent::eKeyCode::KEY_PAGEDOWN},
            {'0', CKeyEvent::eKeyCode::KEY_0},
            {'1', CKeyEvent::eKeyCode::KEY_1},
            {'2', CKeyEvent::eKeyCode::KEY_2},
            {'3', CKeyEvent::eKeyCode::KEY_3},
            {'4', CKeyEvent::eKeyCode::KEY_4},
            {'5', CKeyEvent::eKeyCode::KEY_5},
            {'6', CKeyEvent::eKeyCode::KEY_6},
            {'7', CKeyEvent::eKeyCode::KEY_7},
            {'8', CKeyEvent::eKeyCode::KEY_8},
            {'9', CKeyEvent::eKeyCode::KEY_9},
            //{'\b', CKeyEvent::eKeyCode::KEY_BACKSPACE},
            //{'\r', CKeyEvent::eKeyCode::KEY_ENTER},
            //{'\t', CKeyEvent::eKeyCode::KEY_TAB},
            //{' ', CKeyEvent::eKeyCode::KEY_SPACE},
            {'a', CKeyEvent::eKeyCode::KEY_A},
            {'b', CKeyEvent::eKeyCode::KEY_B},
            {'c', CKeyEvent::eKeyCode::KEY_C},
            {'d', CKeyEvent::eKeyCode::KEY_D},
            //            {'e', CKeyEvent::eKeyCode::KEY_E},
            //            {'f', CKeyEvent::eKeyCode::KEY_F},
            //            {'g', CKeyEvent::eKeyCode::KEY_G},
            {'h', CKeyEvent::eKeyCode::KEY_H},
            //            {'i', CKeyEvent::eKeyCode::KEY_I},
            {'j', CKeyEvent::eKeyCode::KEY_J},
            {'k', CKeyEvent::eKeyCode::KEY_K},
            {'l', CKeyEvent::eKeyCode::KEY_L},
            //            {'m', CKeyEvent::eKeyCode::KEY_M},
            {'n', CKeyEvent::eKeyCode::KEY_N},
            //            {'o', CKeyEvent::eKeyCode::KEY_O},
            //            {'p', CKeyEvent::eKeyCode::KEY_P},
            //            {'q', CKeyEvent::eKeyCode::KEY_Q},
            {'r', CKeyEvent::eKeyCode::KEY_R},
            {'s', CKeyEvent::eKeyCode::KEY_S},
            //            {'t', CKeyEvent::eKeyCode::KEY_T},
            //            {'u', CKeyEvent::eKeyCode::KEY_U},
            {'v', CKeyEvent::eKeyCode::KEY_V},
            {'w', CKeyEvent::eKeyCode::KEY_W},
            //            {'x', CKeyEvent::eKeyCode::KEY_X},
            //            {'y', CKeyEvent::eKeyCode::KEY_Y},
            //            {'z', CKeyEvent::eKeyCode::KEY_Z}
        };
        // Assuming 'keyCode' is the key code received
        auto it = keyMap.find(c);
        if (it != keyMap.end())
        {
            ESScreensaver_AppendKeyEvent(it->second);
            handled = YES;
        }
    }
    // If we didn't handle the key press, send it to the parent class
    if (handled == NO)
        [super keyDown:ev];
}

// Called immediately before relaunching.
- (void)updaterWillRelaunchApplication:(SPUUpdater*)__unused updater
{
    // Close any open configuration sheet
    if (m_config != NULL)
        [NSApp endSheet:m_config.window];
    
    // Stop animation and cleanup before relaunch
    [self stopAnimation];
}

// Called when Sparkle finds a valid update
- (void)updater:(SPUUpdater *)updater didFindValidUpdate:(SUAppcastItem *)item
{
    m_updateAvailable = YES;
    ESScreensaver_SetUpdateAvailable(true);
    NSLog(@"Sparkle found valid update: %@", item.displayVersionString);
}

// Called when Sparkle does not find an update
- (void)updaterDidNotFindUpdate:(SPUUpdater *)updater
{
    m_updateAvailable = NO;
    ESScreensaver_SetUpdateAvailable(false);
    NSLog(@"Sparkle did not find an update");
}

// Provide the appcast URL for Sparkle (required for screensaver bundle)
- (nullable NSString *)feedURLStringForUpdater:(SPUUpdater *)updater
{
    // First try to get URL from bundle's Info.plist
    NSBundle *bundle = [NSBundle bundleForClass:[self class]];
    NSString *feedURL = [bundle objectForInfoDictionaryKey:@"SUFeedURL"];
    
    if (feedURL && feedURL.length > 0) {
        NSLog(@"Sparkle feed URL from bundle: %@", feedURL);
        return feedURL;
    }
    
    // Fallback to hardcoded URL based on build configuration
#ifdef DEBUG
    feedURL = @"https://infinidream.ai/stage/appcast.xml";
#else
    feedURL = @"https://infinidream.ai/alpha/appcast.xml";
#endif
    
    NSLog(@"Sparkle feed URL (fallback): %@", feedURL);
    return feedURL;
}

// For screensaver: don't prompt user for permission, just check silently
- (BOOL)updaterShouldPromptForPermissionToCheckForUpdates:(SPUUpdater *)updater
{
#ifdef SCREEN_SAVER
    // Screensaver should never show permission dialogs
    return NO;
#else
    // App can show the standard permission dialog
    return YES;
#endif
}

// Check if an update is available
- (BOOL)isUpdateAvailable
{
    return m_updateAvailable;
}

#ifndef SCREEN_SAVER
- (void)checkForUpdates:(id)sender
{
    if (m_updater != nil) {
        [m_updater checkForUpdates:sender];
    }
}

- (SPUStandardUpdaterController*)updaterController
{
    return m_updater;
}

- (void)connectCheckForUpdatesMenuItem
{
    // Find the "Check for Updates..." menu item and connect it to the updater
    NSMenu *mainMenu = [NSApp mainMenu];
    if (!mainMenu) return;
    
    // Search through all menu items
    for (NSMenuItem *topItem in mainMenu.itemArray) {
        NSMenu *submenu = topItem.submenu;
        if (!submenu) continue;
        
        for (NSMenuItem *item in submenu.itemArray) {
            if ([item.title isEqualToString:@"Check for Updates..."] ||
                [item.title hasPrefix:@"Check for Updates"]) {
                // Connect to our updater controller
                item.target = m_updater;
                item.action = @selector(checkForUpdates:);
                NSLog(@"Connected 'Check for Updates' menu item to Sparkle updater");
                return;
            }
        }
    }
    NSLog(@"Warning: Could not find 'Check for Updates' menu item");
}
#endif


- (BOOL)fullscreen
{
    return m_isFullScreen;
}

- (void)setFullScreen:(BOOL)fullscreen
{
    m_isFullScreen = fullscreen;
}

- (void)mtkView:(MTKView*)view drawableSizeWillChange:(CGSize)size
{
}
#ifdef SCREEN_SAVER
- (void)drawInMetalView:(ESMetalView*)view
#else
- (void)drawInMTKView:(MTKView*)view
#endif
{
    DEBUG_LOG("DRAW_IN_METAL_VIEW");
    if (!m_isStopped)
    {
        m_beginFrameBarrier->wait();
        m_endFrameBarrier->wait();
    }
}

@end
