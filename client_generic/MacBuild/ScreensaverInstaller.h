//
//  ScreensaverInstaller.h
//  infinidream
//
//  Registers the embedded screensaver .appex with PlugInKit and enables it
//  as screensaver and/or desktop wallpaper via PaperSaver.
//

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface ScreensaverInstaller : NSObject

/// Shared singleton instance
+ (instancetype)sharedInstaller;

/// Register the embedded .appex with PlugInKit when missing/outdated.
- (void)installScreensaverIfNeeded;

/// Check if the screensaver is currently installed
- (BOOL)isScreensaverInstalled;

/// Get the version of the currently installed screensaver (nil if not installed)
- (nullable NSString *)getInstalledVersion;

/// Get the version of the bundled screensaver
- (nullable NSString *)getBundledVersion;

/// Check if the bundled version is newer than installed version
- (BOOL)shouldUpdate;

/// Register the embedded .appex with PlugInKit (pluginkit -a).
- (BOOL)installScreensaver;

/// Set as the active system screensaver (Idle slot) if not already.
- (void)enableScreensaverIfNeeded;

/// Set as the System Settings desktop wallpaper (Desktop slot) if not already.
- (void)enableWallpaperIfNeeded;

@end

NS_ASSUME_NONNULL_END
