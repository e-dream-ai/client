//
//  ScreensaverInstaller.h
//  infinidream
//
//  Handles automatic installation and updating of the bundled screensaver
//

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface ScreensaverInstaller : NSObject

/// Shared singleton instance
+ (instancetype)sharedInstaller;

/// Main method - checks if installation/update is needed and performs it
- (void)installScreensaverIfNeeded;

/// Check if the screensaver is currently installed
- (BOOL)isScreensaverInstalled;

/// Get the version of the currently installed screensaver (nil if not installed)
- (nullable NSString *)getInstalledVersion;

/// Get the version of the bundled screensaver
- (nullable NSString *)getBundledVersion;

/// Check if the bundled version is newer than installed version
- (BOOL)shouldUpdate;

/// Install/update the screensaver to user's Screen Savers directory
- (BOOL)installScreensaver;

/// Enable the screensaver in System Preferences if it's installed
- (void)enableScreensaverIfNeeded;

@end

NS_ASSUME_NONNULL_END