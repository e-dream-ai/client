#import <ScreenSaver/ScreenSaver.h>

#include <boost/thread.hpp>
#include <memory>

#if USE_METAL
#import "ESMetalView.h"
#else
#import "ESOpenGLView.h"
#endif
#import "ESConfiguration.h"
#import "Sparkle/Sparkle.h"

@interface ESScreensaverView
#ifdef SCREEN_SAVER
    : ScreenSaverView
#else
    : NSView
#endif
      <SUUpdaterDelegate
#ifdef USE_METAL
       ,
       MTKViewDelegate
#endif
       >
{
    // So what do you need to make an OpenGL screen saver? Just an NSOpenGLView
    // (or subclass thereof) So we'll put one in here.
    NSRect theRect;
#if USE_METAL
    ESMetalView *view;
#else
    ESOpenGLView *view;
#endif
    NSTimer *animationTimer;
    dispatch_group_t m_animationDispatchGroup;
    BOOL m_isStopped;

    BOOL m_isPreview;

    BOOL m_isFullScreen;

#ifdef SCREEN_SAVER
    BOOL m_isHidden;
#endif

    ESConfiguration *m_config;

    SUUpdater *m_updater;
    std::unique_ptr<boost::barrier> m_beginFrameBarrier;
    std::unique_ptr<boost::barrier> m_endFrameBarrier;
}

- (id)initWithFrame:(NSRect)frame isPreview:(BOOL)isPreview;
- (void)startAnimation;
- (void)stopAnimation;
- (void)animateOneFrame;

- (BOOL)hasConfigureSheet;
- (NSWindow *)configureSheet;
- (void)flagsChanged:(NSEvent *)ev;
- (void)keyDown:(NSEvent *)ev;

- (void)_beginThread;
- (void)_endThread;
- (void)_animationThread;

- (void)windowDidResize;

- (void)updaterWillRelaunchApplication:(SUUpdater *)updater;
- (void)updater:(SUUpdater *)updater didFindValidUpdate:(SUAppcastItem *)update;

- (BOOL)fullscreen;
- (void)setFullScreen:(BOOL)fullscreen;

@end
