#import <ScreenSaver/ScreenSaver.h>

#include <atomic>

#if USE_METAL
#import "ESMetalView.h"
#else
#import "ESOpenGLView.h"
#endif
#import "ESConfiguration.h"
#ifndef SCREEN_SAVER
#import "Sparkle/Sparkle.h"
#endif

@class EDreamUpdaterDelegate;

@interface ESScreensaverView
#ifdef SCREEN_SAVER
    : ScreenSaverView
#else
    : NSView
#endif
#ifndef SCREEN_SAVER
      <SPUUpdaterDelegate,
       SPUStandardUserDriverDelegate
#ifdef USE_METAL
       ,
       MTKViewDelegate
#endif
       >
#else /* SCREEN_SAVER */
#ifdef USE_METAL
      <ESMetalViewDelegate>
#else
      <NSObject>
#endif
#endif /* SCREEN_SAVER */
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
    std::atomic<bool> m_isStopped;

    BOOL m_isPreview;

    BOOL m_isFullScreen;

#ifdef SCREEN_SAVER
    BOOL m_isHidden;
#endif

    ESConfiguration* m_config;

#ifndef SCREEN_SAVER
    SPUStandardUpdaterController* m_updater;
    SPUUpdater* m_sparkleUpdater;
    SPUStandardUserDriver* m_sparkleUserDriver;
#endif
    BOOL m_updateAvailable;
}

- (id)initWithFrame:(NSRect)frame isPreview:(BOOL)isPreview;
- (void)startAnimation;
- (void)stopAnimation;
/// Pause animation and frame thread without full shutdown (e.g. for Preferences sheet). Use resumeAnimationFromSheet to resume.
- (void)pauseAnimationForSheet;
/// Resume after pauseAnimationForSheet. Returns YES if it resumed, NO if no-op (caller should call startAnimation only when NO).
- (BOOL)resumeAnimationFromSheet;
- (void)animateOneFrame;

- (BOOL)hasConfigureSheet;
- (NSWindow*)configureSheet;
- (void)flagsChanged:(NSEvent*)ev;
- (void)keyDown:(NSEvent*)ev;

- (void)_beginThread;
- (void)_endThread;

- (void)windowDidResize;

- (BOOL)fullscreen;
- (void)setFullScreen:(BOOL)fullscreen;

#ifndef SCREEN_SAVER
- (void)updaterWillRelaunchApplication:(SPUUpdater*)updater;
- (void)checkForUpdates:(id)sender;
- (SPUStandardUpdaterController*)updaterController;
- (void)connectCheckForUpdatesMenuItem;
#endif

- (BOOL)isUpdateAvailable;

@end
