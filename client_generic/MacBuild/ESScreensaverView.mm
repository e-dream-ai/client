#import <OpenGL/OpenGL.h>

#import "ESScreensaverView.h"
#import "ESScreensaver.h"

#include <csignal>

#include "dlfcn.h"
#include "libgen.h"
#include "Log.h"
#include "DisplayOutput.h"

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
    NSLog(@"char: %@ - %@", ev.charactersIgnoringModifiers, ev.characters);
    unsigned int characterIndex,
        characterCount = (unsigned int)characters.length;

    for (characterIndex = 0; characterIndex < characterCount; characterIndex++)
    {
        unichar c = [characters characterAtIndex:characterIndex];
        using namespace DisplayOutput;
        switch (c)
        {
        case NSRightArrowFunctionKey:
            ESScreensaver_AppendKeyEvent(CKeyEvent::eKeyCode::KEY_RIGHT);
            handled = YES;
            break;

        case NSLeftArrowFunctionKey:
            ESScreensaver_AppendKeyEvent(CKeyEvent::eKeyCode::KEY_LEFT);
            handled = YES;
            break;

        case NSUpArrowFunctionKey:
            ESScreensaver_AppendKeyEvent(CKeyEvent::eKeyCode::KEY_UP);
            handled = YES;
            break;

        case NSDownArrowFunctionKey:
            ESScreensaver_AppendKeyEvent(CKeyEvent::eKeyCode::KEY_DOWN);
            handled = YES;
            break;

        case NSF1FunctionKey:
            ESScreensaver_AppendKeyEvent(CKeyEvent::eKeyCode::KEY_F1);
            handled = YES;
            break;

        case NSF2FunctionKey:
            ESScreensaver_AppendKeyEvent(CKeyEvent::eKeyCode::KEY_F2);
            handled = YES;
            break;

        case NSF3FunctionKey:
            ESScreensaver_AppendKeyEvent(CKeyEvent::eKeyCode::KEY_F3);
            handled = YES;
            break;

        case NSF4FunctionKey:
            ESScreensaver_AppendKeyEvent(CKeyEvent::eKeyCode::KEY_F4);
            handled = YES;
            break;

        case NSF8FunctionKey:
            ESScreensaver_AppendKeyEvent(CKeyEvent::eKeyCode::KEY_F8);
            handled = YES;
            break;

        case u',':
            ESScreensaver_AppendKeyEvent(CKeyEvent::eKeyCode::KEY_Comma);
            handled = YES;
            break;

        case u'.':
            ESScreensaver_AppendKeyEvent(CKeyEvent::eKeyCode::KEY_Period);
            handled = YES;
            break;

        case u'c':
            ESScreensaver_AppendKeyEvent(CKeyEvent::eKeyCode::KEY_C);
            handled = YES;
            break;

        case u'a':
            ESScreensaver_AppendKeyEvent(CKeyEvent::eKeyCode::KEY_A);
            handled = YES;
            break;

        case u'd':
            ESScreensaver_AppendKeyEvent(CKeyEvent::eKeyCode::KEY_D);
            handled = YES;
            break;

        default:
            break;
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
