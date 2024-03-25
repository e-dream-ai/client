#import <OpenGL/OpenGL.h>

#import "ESScreensaverView.h"
#import "ESScreensaver.h"

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
    self = [super initWithFrame:frame isPreview:isPreview];
#else
    self = [super initWithFrame:frame];
#endif
    //os_log(OS_LOG_DEFAULT, "EDTEST  INIT");


    m_updater = NULL;

    m_isFullScreen = !isPreview;
    m_isStopped = YES;

    m_isPreview = isPreview;
    DEBUG_LOG("INIT");

#ifdef SCREEN_SAVER
    // if (isPreview)
#endif
    {

        //        m_updater =
        //            [[SPUStandardUpdaterController alloc] initWithStartingUpdater:YES
        //                                                          updaterDelegate:nil
        //                                                       userDriverDelegate:nil];
        //        [m_updater startUpdater];
    }

    if (self)
    {
        // So we modify the setup routines just a little bit to get our
        // new OpenGL screensaver working.

        // Create the new frame
        NSRect newFrame = frame;
        // Slam the origin values
        newFrame.origin.x = 0.0;
        newFrame.origin.y = 0.0;

        ESScreensaver_InitClientStorage();

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

        ESScreensaver_DeinitClientStorage();

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
        //@TODO: remove these if unnecessary
        //ESScreensaver_InitClientStorage();

        m_displayIdx = ESScreenSaver_AddGraphicsContext((__bridge void*)view);
        //ESScreensaver_DeinitClientStorage();
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
        g_Log->Shutdown();
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
    g_Log->Info("Killed by system from willStop");
    g_Log->Shutdown();
    ESScreensaver_Deinit();
    exit(0);
}

- (void)onSleepNote:(NSNotification*)notif
{
    g_Log->Info("Killed by system from onSleepNote %s", notif.name.UTF8String);
    NSLog(@"notif.object:%@", notif.object);
//    g_Log->Shutdown();
//    ESScreensaver_Deinit();
//    exit(0);
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
    view.frame = self.frame;

    ESScreensaver_ForceWidthAndHeight((uint32_t)self.frame.size.width,
                                      (uint32_t)self.frame.size.height);
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
            //            {'b', CKeyEvent::eKeyCode::KEY_B},
            {'c', CKeyEvent::eKeyCode::KEY_C},
            {'d', CKeyEvent::eKeyCode::KEY_D},
            //            {'e', CKeyEvent::eKeyCode::KEY_E},
            //            {'f', CKeyEvent::eKeyCode::KEY_F},
            //            {'g', CKeyEvent::eKeyCode::KEY_G},
            //            {'h', CKeyEvent::eKeyCode::KEY_H},
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
    if (m_config != NULL)
        [NSApp endSheet:m_config.window];
}

- (void)doUpdate:(NSTimer*)timer
{
    // SUAppcastItem* update = timer.userInfo;

    //    if (!m_isFullScreen)
    //        [m_updater checkForUpdates:nil];
    //    else
    //        ESScreensaver_SetUpdateAvailable(
    //            update.displayVersionString.UTF8String);
}

// Sent when a valid update is found by the update driver.
- (void)updater:(SPUUpdater*)__unused updater
    didFindValidUpdate:(SUAppcastItem*)update
{
    [NSTimer scheduledTimerWithTimeInterval:1.0
                                     target:self
                                   selector:@selector(doUpdate:)
                                   userInfo:update
                                    repeats:NO];
}

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
