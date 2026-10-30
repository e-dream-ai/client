#import <OpenGL/OpenGL.h>

#import "ESScreensaverView.h"
#import "ESScreensaver.h"
#import <Bugsnag/Bugsnag.h>
#ifndef SCREEN_SAVER
#import <Sparkle/Sparkle.h>
#endif
#include <csignal>

#include "dlfcn.h"
#include "libgen.h"
#include "Log.h"
#include "DisplayOutput.h"
#include <atomic>

#ifdef SCREEN_SAVER
// All ScreenSaverView instances in one appex process share one gClient/g_Player.
// Refcount so one view's stopAnimation cannot Deinit while other displays
// are still running.
static std::atomic<int> s_activeEngineViews{0};
#endif

@implementation ESScreensaverView
{
    BOOL m_bStarted;
    BOOL m_bPausedForSheet;  // YES when paused for Preferences sheet (no full shutdown)
    BOOL m_shouldRotate;     // YES when rotating the dream 90 deg for a portrait display
    int m_displayIdx;
#ifdef SCREEN_SAVER
    BOOL m_graphicsContextAdded;  // AddGraphicsContext once per view (startAnimation may re-enter)
    NSTimer *m_screenCheckTimer;  // Fallback when macOS never calls stopAnimation
    NSSize m_lastLaidOutSize;     // Avoid ForceWidthAndHeight on every layout pass
#endif
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


    m_updateAvailable = NO;
#ifndef SCREEN_SAVER
    m_updater = NULL;
    m_sparkleUpdater = NULL;
    m_sparkleUserDriver = nil;
#endif

    m_isFullScreen = !isPreview;
    m_isStopped = YES;
    m_bPausedForSheet = NO;
    m_displayIdx = 0;
#ifdef SCREEN_SAVER
    m_graphicsContextAdded = NO;
    m_screenCheckTimer = nil;
    m_lastLaidOutSize = NSZeroSize;
#endif

    m_isPreview = isPreview;
    DEBUG_LOG("INIT");

#ifdef SCREEN_SAVER
    NSLog(@"ESScreensaverView: Running in SCREEN_SAVER appex mode (preview=%d)", (int)isPreview);
#else
    NSLog(@"ESScreensaverView: Running in APP mode — initializing Sparkle");
    m_sparkleUpdater = nil;
    m_updater = [[SPUStandardUpdaterController alloc] initWithStartingUpdater:YES
                                                              updaterDelegate:self
                                                           userDriverDelegate:self];

    if (m_updater) {
        SPUUpdater *appUpdater = m_updater.updater;
        // Hide "Skip this version" in update dialog (Sparkle 2.x allowsSkippingUpdates)
        if ([appUpdater respondsToSelector:NSSelectorFromString(@"setAllowsSkippingUpdates:")]) {
            [appUpdater setValue:@NO forKey:@"allowsSkippingUpdates"];
        }
        [appUpdater checkForUpdatesInBackground];
    }

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

        // "Rotate on portrait displays": when enabled and the screen is taller
        // than it is wide, the dream is turned 90 deg (handled in the shader).
        // The constrained box then targets 9:16 instead of 16:9.
        m_shouldRotate =
            ESScreensaver_GetBoolSetting("settings.player.rotate_portrait", false) &&
            (frame.size.width < frame.size.height);

        if (ESScreensaver_GetBoolSetting("settings.player.preserve_AR", true)) {
            float targetAR = m_shouldRotate ? (9.0f / 16.0f) : (16.0f / 9.0f);
            if (newFrame.size.width / newFrame.size.height > targetAR)
            {
                // Wider than the target box: constrain width, center horizontally.
                newFrame.size = CGSizeMake(newFrame.size.height * targetAR, newFrame.size.height);
                newFrame.origin.x = (frame.size.width - newFrame.size.width) / 2;
            } else {
                // Taller than the target box: constrain height, center vertically.
                newFrame.size = CGSizeMake(newFrame.size.width, newFrame.size.width / targetAR);
                newFrame.origin.y = (frame.size.height - newFrame.size.height) / 2;
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

#ifdef SCREEN_SAVER
// Seconds a non-preview view's window may stay off-screen before we decide no
// host will ever show or stop us. Generous so slow fades / display wake don't trip it.
static const NSTimeInterval kOrphanOffscreenSeconds = 10.0;

// YES only when the window exists and WindowServer reports it off-screen.
// A nil window is treated as unknown (that's what false-positived under
// System Settings Preview before), never as orphaned.
static BOOL ESWindowIsKnownOffscreen(NSWindow *win)
{
    if (!win || win.windowNumber <= 0)
        return NO;
    CFArrayRef info = CGWindowListCopyWindowInfo(kCGWindowListOptionIncludingWindow,
                                                 (CGWindowID)win.windowNumber);
    if (!info)
        return NO;
    BOOL offscreen = NO;
    if (CFArrayGetCount(info) > 0) {
        NSDictionary *d = (__bridge NSDictionary *)CFArrayGetValueAtIndex(info, 0);
        offscreen = ![d[(__bridge NSString *)kCGWindowIsOnscreen] boolValue];
    }
    CFRelease(info);
    return offscreen;
}
#endif

#ifdef SCREEN_SAVER
// Recompute letterboxed video rect from the current view bounds. Init often
// sees NSZeroRect; WallpaperAgent sizes the remote view after attach.
- (void)_recomputeVideoRectFromBounds
{
    NSRect frame = self.bounds;
    NSRect newFrame = frame;
    newFrame.origin.x = 0.0;
    newFrame.origin.y = 0.0;

    m_shouldRotate =
        ESScreensaver_GetBoolSetting("settings.player.rotate_portrait", false) &&
        (frame.size.width < frame.size.height);

    if (ESScreensaver_GetBoolSetting("settings.player.preserve_AR", true) &&
        frame.size.width > 0.0 && frame.size.height > 0.0) {
        float targetAR = m_shouldRotate ? (9.0f / 16.0f) : (16.0f / 9.0f);
        if (newFrame.size.width / newFrame.size.height > targetAR)
        {
            newFrame.size = CGSizeMake(newFrame.size.height * targetAR, newFrame.size.height);
            newFrame.origin.x = (frame.size.width - newFrame.size.width) / 2;
        } else {
            newFrame.size = CGSizeMake(newFrame.size.width, newFrame.size.width / targetAR);
            newFrame.origin.y = (frame.size.height - newFrame.size.height) / 2;
        }
    }
    theRect = newFrame;
}
#endif

- (void)startAnimation
{
#ifdef SCREEN_SAVER
    // Appex hosts (System Settings + WallpaperAgent) may run two processes.
    // Secondary keeps busy/offline mode via the instance lock — do not exit.
    if (!m_isPreview && ESScreensaver_IsSecondaryInstance()) {
        g_Log->Info("Secondary screensaver instance — starting in multiple-instances mode");
    }

    // Host attaches at 0×0 then sizes ~15–25ms later. Starting the shared engine
    // at 0×0 yields a dead Metal drawable and races teardown when the host
    // briefly detaches before sizing (SIGSEGV during buffering in logs).
    if (NSWidth(self.bounds) < 1.0 || NSHeight(self.bounds) < 1.0) {
        if (g_Log) {
            g_Log->Info("Saver startAnimation deferred (bounds %.0fx%.0f, preview=%d)",
                        self.bounds.size.width, self.bounds.size.height,
                        (int)m_isPreview);
        }
        [super startAnimation];
        return;
    }

    [self _recomputeVideoRectFromBounds];
#endif

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
#ifdef SCREEN_SAVER
    // macOS may re-enter startAnimation; only register the Metal view once or we
    // pile up duplicate displays and stall shared playback.
    if (view != NULL && !m_graphicsContextAdded)
    {
        m_displayIdx = ESScreenSaver_AddGraphicsContext((__bridge void*)view);
        m_graphicsContextAdded = YES;
    }
#else
    if (view != NULL)
    {
        m_displayIdx = ESScreenSaver_AddGraphicsContext((__bridge void*)view);
    }
#endif

    uint32_t width = (uint32_t)theRect.size.width;
    uint32_t height = (uint32_t)theRect.size.height;

#ifdef SCREEN_SAVER
    m_isHidden = NO;
#endif

    if (!m_bStarted)
    {

        if (!ESScreensaver_Start(m_isPreview, width, height))
            return;

        // Apply portrait rotation now that the renderer exists.
        ESScreensaver_SetVideoRotation(m_shouldRotate ? 90 : 0);

#ifdef SCREEN_SAVER
        g_Log->Info("Saver startAnimation on %f %f (preview=%d)",
                    self.bounds.size.width, self.bounds.size.height, (int)m_isPreview);
#endif
       
        [self _beginThread];
        m_bStarted = YES;
#ifdef SCREEN_SAVER
        s_activeEngineViews.fetch_add(1);
        [self _startScreenConnectionMonitor];
#endif
    }
    
#ifdef SCREEN_SAVER
    [super startAnimation];
#endif
}

- (void)stopAnimation
{
#ifdef SCREEN_SAVER
    [self _stopScreenConnectionMonitor];
    [NSCursor unhide];
    [self _stopRedrawTimer];
#endif
    m_bPausedForSheet = NO;
    if (m_bStarted)
    {
        [self _endThread];
        m_bStarted = NO;

#ifdef SCREEN_SAVER
        // Only tear down the shared engine when the last view stops. Settings
        // thumbnail stop must not Kill Preview / real-saver playback.
        int remaining = s_activeEngineViews.fetch_sub(1) - 1;
        if (remaining < 0) {
            s_activeEngineViews.store(0);
            remaining = 0;
        }
        if (remaining == 0) {
            [self _tearDownKeepAlives];
            ESScreensaver_Stop();
            ESScreensaver_Deinit();
            // WallpaperAgent keeps the appex process across sessions — do not exit.
            g_Log->Info("stopAnimation: last view stopped, engine torn down (process stays)");
        } else {
            g_Log->Info("Saver stopAnimation: %d view(s) still active, keeping engine",
                        remaining);
        }
#else
        ESScreensaver_Stop();
        ESScreensaver_Deinit();
#endif
    }

#ifdef SCREEN_SAVER
    if (m_isHidden)
        [NSCursor unhide];
    m_isHidden = NO;
    [super stopAnimation];
#endif
}

- (void)pauseAnimationForSheet
{
#ifdef SCREEN_SAVER
    [self _stopScreenConnectionMonitor];
#endif
    if (!m_bStarted || m_bPausedForSheet)
        return;
    [self _endThread];
    ESScreensaver_Stop();
    m_bPausedForSheet = YES;
    m_bStarted = NO;
}

- (BOOL)resumeAnimationFromSheet
{
    if (!m_bPausedForSheet)
        return NO;
    ESScreensaver_Resume();
    [self _beginThread];
    m_bStarted = YES;
    m_bPausedForSheet = NO;
#ifdef SCREEN_SAVER
    [self _startScreenConnectionMonitor];
    [super startAnimation];
#endif
    return YES;  // Caller must NOT call startAnimation (would re-add display and retrigger full startup)
}

- (void)animateOneFrame
{
}

static void signnal_handler(int signal)
{
    // Do not catch SIGSEGV — that masked real crashes (screensaver was dying
    // mid-buffer with "RECEIVED SIGSEGV" and never filling frames). Only handle
    // graceful terminate requests.
    if (signal == SIGTERM)
    {
        if (g_Log)
            g_Log->Info("RECEIVED SIGTERM");
        exit(0);
    }
}

- (void)_beginThread
{
    DEBUG_LOG("BEGINTHREAD");
    m_isStopped = NO;
#ifdef SCREEN_SAVER
    // Avoid duplicate observers if start/resume re-enters.
    [NSWorkspace.sharedWorkspace.notificationCenter removeObserver:self];
    [NSDistributedNotificationCenter.defaultCenter removeObserver:self];
#endif
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
    // didstop is more reliably delivered than willstop on some macOS versions.
    [NSDistributedNotificationCenter.defaultCenter
        addObserver:self
           selector:@selector(onSleepNote:)
               name:NSNotificationName(@"com.apple.screensaver.didstop")
             object:nil];
    std::signal(SIGTERM, signnal_handler);
}

- (void)willStop:(NSNotification*)notification
{
#ifdef SCREEN_SAVER
    // Machine sleep — pause playback; WallpaperAgent may reuse this process.
    if (m_isPreview)
        return;
    g_Log->Info("willStop (sleep) — stopping animation without exiting process");
    [self stopAnimation];
#endif
}

- (void)onSleepNote:(NSNotification*)notif
{
#ifdef SCREEN_SAVER
    // Screensaver dismiss (willstop/didstop). Tear down views; keep process alive
    // so WallpaperAgent does not treat exit as a crash and relaunch an orphan.
    if (m_isPreview)
        return;
    g_Log->Info("screensaver stop note %s — stopping animation (no exit)",
                notif.name.UTF8String);
    [self stopAnimation];
#endif
}

#ifdef SCREEN_SAVER
// Host attaches at 0×0 then resizes; stopAnimation is often omitted when views
// are released. Drive start/stop from window attach like AppexSaverMinimal.
- (void)viewDidMoveToWindow
{
    [super viewDidMoveToWindow];
    if (self.window != nil) {
        if (!m_bStarted && !m_bPausedForSheet)
            [self startAnimation]; // no-ops until bounds are non-zero
    } else {
        if (m_bStarted)
            [self stopAnimation];
    }
}

// remoteViewSizeChanged lands as a layout pass; start (or resize) once we have
// a real frame so Metal/engine are not created at 0×0.
- (void)layout
{
    [super layout];
    if (self.window == nil)
        return;
    NSSize size = self.bounds.size;
    if (size.width < 1.0 || size.height < 1.0)
        return;
    if (!m_bStarted && !m_bPausedForSheet) {
        [self startAnimation];
        m_lastLaidOutSize = size;
    } else if (m_bStarted && !NSEqualSizes(size, m_lastLaidOutSize)) {
        m_lastLaidOutSize = size;
        [self windowDidResize];
    }
}
#endif

#pragma mark - Keep-alive teardown + dismiss fallback

// Host may omit stopAnimation when releasing views; viewDidMoveToWindow and
// willstop/didstop tear down playback. Process stays alive under WallpaperAgent.

- (void)_stopRedrawTimer
{
#ifdef SCREEN_SAVER
#ifdef USE_METAL
    if ([view respondsToSelector:@selector(stopRedrawTimer)]) {
        [view stopRedrawTimer];
    }
#endif
#endif
}

- (void)_tearDownKeepAlives
{
#ifdef SCREEN_SAVER
    [self _stopRedrawTimer];
#endif
}

- (void)_startScreenConnectionMonitor
{
#ifdef SCREEN_SAVER
    // Orphan watchdog for a view left attached off-screen with no stop from host.
    if (m_isPreview)
        return;
    [self _stopScreenConnectionMonitor];
    __block NSTimeInterval offscreenFor = 0;
    __weak ESScreensaverView *weakSelf = self;
    m_screenCheckTimer = [NSTimer scheduledTimerWithTimeInterval:2.0 repeats:YES block:^(NSTimer *t) {
        ESScreensaverView *strongSelf = weakSelf;
        if (!strongSelf) { [t invalidate]; return; }
        // Asleep displays can report windows off-screen; don't count that time.
        if (CGDisplayIsAsleep(CGMainDisplayID()) ||
            !ESWindowIsKnownOffscreen(strongSelf.window)) {
            offscreenFor = 0;
            return;
        }
        offscreenFor += t.timeInterval;
        if (offscreenFor >= kOrphanOffscreenSeconds) {
            g_Log->Info("Saver window off-screen for %.0fs — stopping animation (no exit)",
                        offscreenFor);
            [strongSelf stopAnimation];
            [t invalidate];
        }
    }];
#endif
}

- (void)_stopScreenConnectionMonitor
{
#ifdef SCREEN_SAVER
    if (m_screenCheckTimer) {
        [m_screenCheckTimer invalidate];
        m_screenCheckTimer = nil;
    }
#endif
}

- (void)_endThread
{
    m_isStopped = YES;
#ifdef SCREEN_SAVER
    [NSWorkspace.sharedWorkspace.notificationCenter removeObserver:self];
    [NSDistributedNotificationCenter.defaultCenter removeObserver:self];
#endif
}

- (void)windowDidResize
{
    NSRect videoFrame = self.frame;

    // "Rotate on portrait displays": when enabled and the display is taller
    // than it is wide (aspect < 1), the dream is turned 90 degrees so the
    // landscape content fits a portrait screen. The shader does the actual
    // rotation; here we just pick the target box aspect (9:16 when rotated).
    BOOL shouldRotate =
        ESScreensaver_GetBoolSetting("settings.player.rotate_portrait", false) &&
        (self.frame.size.width < self.frame.size.height);

    // Set background to black for letterboxing/pillarboxing
    if (self.window) {
        self.window.backgroundColor = [NSColor blackColor];
    }

    // Only apply aspect ratio constraint if the setting is enabled
    if (ESScreensaver_GetBoolSetting("settings.player.preserve_AR", true)) {
        float frameAR = self.frame.size.width / self.frame.size.height;
        // Rotated content is 9:16; otherwise the dream is 16:9.
        float targetAR = shouldRotate ? (9.0f / 16.0f) : (16.0f / 9.0f);

        // Only adjust if aspect ratio differs significantly from the target
        if (fabsf(frameAR - targetAR) > 0.01f)
        {
            NSSize constrainedSize = self.frame.size;

            if (frameAR > targetAR)  // Frame is wider than the target box
            {
                // Constrain width and center horizontally
                constrainedSize.width = constrainedSize.height * targetAR;
                CGFloat xOffset = (self.frame.size.width - constrainedSize.width) / 2.0f;
                videoFrame = NSMakeRect(xOffset, 0, constrainedSize.width, constrainedSize.height);
            }
            else  // Frame is taller than the target box
            {
                // Constrain height and center vertically
                constrainedSize.height = constrainedSize.width / targetAR;
                CGFloat yOffset = (self.frame.size.height - constrainedSize.height) / 2.0f;
                videoFrame = NSMakeRect(0, yOffset, constrainedSize.width, constrainedSize.height);
            }
        }
    } else {
        // When preserve AR is disabled, fill the entire window
        videoFrame = self.frame;
    }

    view.frame = videoFrame;

    ESScreensaver_SetVideoRotation(shouldRotate ? 90 : 0);
    ESScreensaver_ForceWidthAndHeight((uint32_t)videoFrame.size.width,
                                      (uint32_t)videoFrame.size.height);
}

- (BOOL)hasConfigureSheet
{
#ifdef SCREEN_SAVER
    return NO;  // Hide Options button in System Settings when running as screensaver
#else
    return YES; // App can open settings dialog
#endif
}

- (NSWindow*)configureSheet
{
    if (!m_config)
    {
        m_config =
            [[ESConfiguration alloc] initWithWindowNibName:@"SettingsDialog"];
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
        // Convert to lowercase to support both 'c' and 'C' (shift+c)
        if (c >= 'A' && c <= 'Z')
            c = c + ('a' - 'A');
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

// Check if an update is available
- (BOOL)isUpdateAvailable
{
    return m_updateAvailable;
}

#ifndef SCREEN_SAVER
// Called immediately before relaunching.
- (void)updaterWillRelaunchApplication:(SPUUpdater*)__unused updater
{
    if (m_config != NULL)
        [NSApp endSheet:m_config.window];
    [self stopAnimation];
}

- (void)updater:(SPUUpdater *)updater didFindValidUpdate:(SUAppcastItem *)item
{
    m_updateAvailable = YES;
    ESScreensaver_SetUpdateAvailable(true);
    NSLog(@"Sparkle found valid update: %@", item.displayVersionString);
}

- (void)updaterDidNotFindUpdate:(SPUUpdater *)updater
{
    m_updateAvailable = NO;
    ESScreensaver_SetUpdateAvailable(false);
    NSLog(@"Sparkle did not find an update");
}

- (nullable NSString *)feedURLStringForUpdater:(SPUUpdater *)updater
{
    NSBundle *bundle = [NSBundle bundleForClass:[self class]];
    NSString *feedURL = [bundle objectForInfoDictionaryKey:@"SUFeedURL"];
    if (feedURL && feedURL.length > 0) {
        NSLog(@"Sparkle feed URL from bundle: %@", feedURL);
        return feedURL;
    }
#ifdef DEBUG
    feedURL = @"https://infinidream.ai/stage/appcast.xml";
#else
    feedURL = @"https://infinidream.ai/alpha/appcast.xml";
#endif
    NSLog(@"Sparkle feed URL (fallback): %@", feedURL);
    return feedURL;
}

- (BOOL)updaterShouldPromptForPermissionToCheckForUpdates:(SPUUpdater *)updater
{
    return YES;
}

#pragma mark - SPUStandardUserDriverDelegate (hide "Skip this version" button)

- (BOOL)standardUserDriverShouldHandleShowingScheduledUpdate:(SUAppcastItem *)update andInImmediateFocus:(BOOL)immediateFocus
{
    return YES;
}

static void hideSkipButtonInView(NSView *view)
{
    if (!view) return;
    if ([view isKindOfClass:[NSButton class]]) {
        NSButton *btn = (NSButton *)view;
        NSString *title = [btn title];
        if ([title rangeOfString:@"Skip" options:NSCaseInsensitiveSearch].location != NSNotFound) {
            [btn setHidden:YES];
        }
    }
    for (NSView *sub in [view subviews]) {
        hideSkipButtonInView(sub);
    }
}

- (void)standardUserDriverWillHandleShowingUpdate:(BOOL)handleShowingUpdate forUpdate:(SUAppcastItem *)update state:(SPUUserUpdateState *)state
{
    id userDriver = m_updater ? m_updater.userDriver : nil;
    if (!userDriver) return;

    __weak id weakDriver = userDriver;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.15 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        id driver = weakDriver;
        if (!driver) return;
        NSWindowController *alert = [driver valueForKey:@"activeUpdateAlert"];
        if (!alert || ![alert isKindOfClass:[NSWindowController class]]) return;
        NSWindow *window = [alert window];
        if (!window) return;
        NSView *contentView = [window contentView];
        hideSkipButtonInView(contentView);
    });
}

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
    NSMenu *mainMenu = [NSApp mainMenu];
    if (!mainMenu) return;

    for (NSMenuItem *topItem in mainMenu.itemArray) {
        NSMenu *submenu = topItem.submenu;
        if (!submenu) continue;

        for (NSMenuItem *item in submenu.itemArray) {
            if ([item.title isEqualToString:@"Check for Updates..."] ||
                [item.title hasPrefix:@"Check for Updates"]) {
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
    if (!m_isStopped && m_bStarted
#ifdef SCREEN_SAVER
        && NSWidth(self.bounds) >= 1.0 && NSHeight(self.bounds) >= 1.0
#endif
        )
    {
        @autoreleasepool
        {
            ESScreensaver_DoFrame(m_displayIdx);
#ifdef SCREEN_SAVER
            if (!m_isPreview && CGCursorIsVisible())
            {
                [NSCursor hide];
                m_isHidden = YES;
            }
#endif
        }
    }
}

@end
