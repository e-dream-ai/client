#import <ScreenSaver/ScreenSaver.h>

#include <boost/thread.hpp>
#include <memory>
#include <atomic>

#if USE_METAL
#import "ESMetalView.h"
#else
#import "ESOpenGLView.h"
#endif
#import "ESConfiguration.h"
#import "Sparkle/Sparkle.h"

@class EDreamUpdaterDelegate;

@interface ESScreensaverView
#ifdef SCREEN_SAVER
    : ScreenSaverView
#else
    : NSView
#endif
      <SPUUpdaterDelegate
#ifdef USE_METAL
#ifdef SCREEN_SAVER
       ,
       ESMetalViewDelegate
#else  /*SCREEN_SAVER*/
       ,
       MTKViewDelegate
#endif /*SCREEN_SAVER*/
#endif /*USE_METAL*/
       >
{
    // So what do you need to make an OpenGL screen saver? Just an NSOpenGLView
    // (or subclass thereof) So we'll put one in here.
    NSRect theRect;
#if USE_METAL
#ifdef SCREEN_SAVER
    ESMetalView* view;
#else
    MTKView* view;
#endif /*SCREEN_SAVER*/
#else  /*USE_METAL*/
    ESOpenGLView* view;
#endif /*USE_METAL*/
    NSTimer* animationTimer;
    dispatch_group_t m_animationDispatchGroup;
    dispatch_queue_t m_frameUpdateQueue;
    std::atomic<bool> m_isStopped;

    BOOL m_isPreview;

    BOOL m_isFullScreen;

#ifdef SCREEN_SAVER
    BOOL m_isHidden;
#endif

    ESConfiguration* m_config;

    SPUStandardUpdaterController* m_updater;
    SPUUpdater* m_sparkleUpdater;  // Direct updater reference for screensaver
    BOOL m_updateAvailable;  // Track if Sparkle found a valid update
    std::unique_ptr<boost::barrier> m_beginFrameBarrier;
    std::unique_ptr<boost::barrier> m_endFrameBarrier;
}

- (id)initWithFrame:(NSRect)frame isPreview:(BOOL)isPreview;
- (void)startAnimation;
- (void)stopAnimation;
- (void)animateOneFrame;

- (BOOL)hasConfigureSheet;
- (NSWindow*)configureSheet;
- (void)flagsChanged:(NSEvent*)ev;
- (void)keyDown:(NSEvent*)ev;

- (void)_beginThread;
- (void)_endThread;
- (void)_animationThread;

- (void)windowDidResize;

- (void)updaterWillRelaunchApplication:(SPUUpdater*)updater;

- (BOOL)fullscreen;
- (void)setFullScreen:(BOOL)fullscreen;

#ifndef SCREEN_SAVER
- (void)checkForUpdates:(id)sender;
- (SPUStandardUpdaterController*)updaterController;
- (void)connectCheckForUpdatesMenuItem;
#endif

- (BOOL)isUpdateAvailable;

@end
