#import <OpenGL/OpenGL.h>

#import "ESScreensaverView.h"
#import "ESScreensaver.h"

#include <csignal>

#include "dlfcn.h"
#include "libgen.h"
#include "Log.h"
#include "DisplayOutput.h"
#include "PlatformUtils.h"

bool bStarted = false;

@implementation ESScreensaverView

- (instancetype)initWithFrame:(NSRect)frame isPreview:(BOOL)isPreview
{
#ifdef SCREEN_SAVER
    self = [super initWithFrame:frame isPreview:isPreview];
#else
    self = [super initWithFrame:frame];
#endif

    m_updater = NULL;

    m_isFullScreen = !isPreview;
    m_isStopped = YES;

    m_isPreview = isPreview;
    m_beginFrameBarrier = std::make_unique<boost::barrier>(2);
    m_endFrameBarrier = std::make_unique<boost::barrier>(2);
    m_animationDispatchGroup = dispatch_group_create();
    m_frameUpdateQueue = dispatch_queue_create("Frame Update", NULL);

#ifdef SCREEN_SAVER
    // if (isPreview)
#endif
    {
        CFBundleRef bndl = CopyDLBundle_ex();
        NSBundle* nsbndl;

        if (bndl != NULL)
        {
            NSURL* url = (NSURL*)CFBridgingRelease(CFBundleCopyBundleURL(bndl));

            nsbndl = [NSBundle bundleWithPath:url.path];

            m_updater = [SUUpdater updaterForBundle:nsbndl];

            m_updater.delegate = self;

            if (m_updater && m_updater.automaticallyChecksForUpdates)
            {
                [m_updater checkForUpdateInformation];
            }

            CFRelease(bndl);
        }
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

        theRect = newFrame;
        // Now alloc and init the view, right from within the screen saver's
        // initWithFrame:

        // If the view is valid, we continue
        // if(view)
        {
            // Make sure we autoresize
            [self setAutoresizesSubviews:YES];
            // So if our view is valid...

            view = NULL;

            // Do some setup on our context and view
            //[view prepareOpenGL];
            // Then we set our animation loop timer
            //[self setAnimationTimeInterval:1/60.0];
#ifdef SCREEN_SAVER
            [self setAnimationTimeInterval:-1];
#endif
            // Since our BasicOpenGLView class does it's setup in awakeFromNib,
            // we call that here. Note that this could be any method you want to
            // use as the setup routine for your view.
            //[view awakeFromNib];
        }
        // else // Log an error if we fail here
        //  NSLog(@"Error: e-dream Screen Saver failed to initialize
        //  NSOpenGLView!");
    }
    // Finally return our newly-initialized self
    return self;
}

- (void)startAnimation
{
    /*NSMutableArray *displays = [NSMutableArray arrayWithCapacity:5];

    [displays addObject:[NSScreen mainScreen]];

    uint32_t cnt = [[NSScreen screens] count];

    for (int i=0; i<cnt; i++)
    {
            NSScreen *scr = [[NSScreen screens] objectAtIndex:i];

             if (scr !=  [NSScreen mainScreen])
                    [displays addObject:scr];
    }

    ESScreensaver_InitClientStorage();

    Sint32_t scr = ESScreensaver_GetIntSetting("settings.player.screen", 0);

    ESScreensaver_DeinitClientStorage();

    if (scr >= cnt)
            scr = cnt - 1;

    //main screen only for now?
    if (!m_isPreview && [[self window] screen] != [displays objectAtIndex:scr])
    {
   return;
    }
    else
    {*/
    if (view == NULL)
    {
        /*NSRect newRect;

        newRect.size.height = theRect.size.height;

        newRect.size.width = 800.0 * ( theRect.size.height / 592.0 );

        newRect.origin.x = theRect.origin.x + ( theRect.size.width -
        newRect.size.width ) / 2.0;

        newRect.origin.y = theRect.origin.y;

        theRect = newRect;*/

        ESMetalView* metalView =

            [[ESMetalView alloc] initWithFrame:theRect];
        metalView.delegate = self;
        metalView.preferredFramesPerSecond = 60;

        view = metalView;

        if (view)
        {
            // We make it a subview of the screensaver view
            [self addSubview:view];
        }
    }
    //}
    if (view != NULL)
    {
        //@TODO: remove these if unnecessary
        //ESScreensaver_InitClientStorage();

        ESScreenSaver_AddGraphicsContext((__bridge void*)view);
        //ESScreensaver_DeinitClientStorage();
    }

    uint32_t width = (uint32_t)theRect.size.width;
    uint32_t height = (uint32_t)theRect.size.height;

#ifdef SCREEN_SAVER
    m_isHidden = NO;
#endif

    if (!bStarted)
    {

        if (!ESScreensaver_Start(m_isPreview, width, height))
            return;

        bStarted = true;

        [self _beginThread];
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
    if (bStarted)
    {
        [self _endThread];

        ESScreensaver_Stop();

        ESScreensaver_Deinit();

        bStarted = false;
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

    while (!m_isStopped && !ESScreensaver_Stopped())
    {
        @autoreleasepool
        {
            if (!ESScreensaver_DoFrame(*m_beginFrameBarrier,
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

    m_isStopped = NO;
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

- (void)onSleepNote:(NSNotification*)NSNotification
{
    g_Log->Info("Killed by system from onSleepNote");
    g_Log->Shutdown();
    ESScreensaver_Deinit();
    exit(0);
}

- (void)_endThread
{
    m_isStopped = YES;
    m_beginFrameBarrier->wait();
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
            //            {'0', CKeyEvent::eKeyCode::KEY_0},
            //            {'1', CKeyEvent::eKeyCode::KEY_1},
            //            {'2', CKeyEvent::eKeyCode::KEY_2},
            //            {'3', CKeyEvent::eKeyCode::KEY_3},
            //            {'4', CKeyEvent::eKeyCode::KEY_4},
            //            {'5', CKeyEvent::eKeyCode::KEY_5},
            //            {'6', CKeyEvent::eKeyCode::KEY_6},
            //            {'7', CKeyEvent::eKeyCode::KEY_7},
            //            {'8', CKeyEvent::eKeyCode::KEY_8},
            //            {'9', CKeyEvent::eKeyCode::KEY_9},
            //            {'\b', CKeyEvent::eKeyCode::KEY_BACKSPACE},
            //            {'\r', CKeyEvent::eKeyCode::KEY_ENTER},
            //            {'\t', CKeyEvent::eKeyCode::KEY_TAB},
            //            {' ', CKeyEvent::eKeyCode::KEY_SPACE},
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
            //            {'k', CKeyEvent::eKeyCode::KEY_K},
            {'l', CKeyEvent::eKeyCode::KEY_L},
            //            {'m', CKeyEvent::eKeyCode::KEY_M},
            //            {'n', CKeyEvent::eKeyCode::KEY_N},
            //            {'o', CKeyEvent::eKeyCode::KEY_O},
            //            {'p', CKeyEvent::eKeyCode::KEY_P},
            //            {'q', CKeyEvent::eKeyCode::KEY_Q},
            //            {'r', CKeyEvent::eKeyCode::KEY_R},
            //            {'s', CKeyEvent::eKeyCode::KEY_S},
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
- (void)updaterWillRelaunchApplication:(SUUpdater*)__unused updater
{
    if (m_config != NULL)
        [NSApp endSheet:m_config.window];
}

- (void)doUpdate:(NSTimer*)timer
{
    SUAppcastItem* update = timer.userInfo;

    if (!m_isFullScreen)
        [m_updater checkForUpdatesInBackground];
    else
        ESScreensaver_SetUpdateAvailable(
            update.displayVersionString.UTF8String);
}

// Sent when a valid update is found by the update driver.
- (void)updater:(SUUpdater*)__unused updater
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

- (void)drawInMTKView:(nonnull MTKView*)view
{
    if (!m_isStopped)
    {
        m_beginFrameBarrier->wait();
        m_endFrameBarrier->wait();
    }
}

@end
