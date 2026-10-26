//
// ScreenSaverPrivate.h
//
// Declarations for ScreenSaver.framework classes that app-extension screensavers
// subclass. The framework exports these (see ScreenSaver.tbd) but public headers
// do not declare them.
//
// Adapted from AerialScreensaver/AppexSaverMinimal (MIT). Undocumented private
// API — can change in any macOS release.
//

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

#pragma mark - ScreenSaverExtension

/// Principal class of a screensaver app extension (NSExtensionPrincipalClass).
@interface ScreenSaverExtension : NSObject

- (instancetype)init;

@end

#pragma mark - ScreenSaverViewController

/// Provides the screensaver view (ScreenSaverViewControllerClass in Info.plist).
@interface ScreenSaverViewController : NSViewController

- (void)startAnimation;
- (void)stopAnimation;

/// Host resizes the remote view; call super if overridden.
- (BOOL)remoteViewSizeChanged:(NSSize)size transaction:(nullable id)transaction;

@end

#pragma mark - ScreenSaverConfigurationViewController

/// Options sheet when SSEHasConfigureSheet is true.
@interface ScreenSaverConfigurationViewController : NSViewController

- (void)configureSheetWillPresent;
- (void)configureSheetDidEnd;

@end

NS_ASSUME_NONNULL_END
