//
//  ScreensaverInstaller.mm
//  infinidream
//
//  Handles automatic installation and updating of the bundled screensaver
//

#import "ScreensaverInstaller.h"

// Bridging header is auto-generated based on product module name
#ifdef STAGE
#import "infinidream_stage-Swift.h"
#else
#import "infinidream-Swift.h"
#endif

#include "Log.h"

@implementation ScreensaverInstaller

+ (instancetype)sharedInstaller {
    static ScreensaverInstaller *sharedInstaller = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        sharedInstaller = [[ScreensaverInstaller alloc] init];
    });
    return sharedInstaller;
}

- (void)installScreensaverIfNeeded {
    g_Log->Info("ScreensaverInstaller: Checking if screensaver installation is needed...");
    
    // Check if we should install/update
    if (![self isScreensaverInstalled] || [self shouldUpdate]) {
        g_Log->Info("ScreensaverInstaller: Installation/update needed, proceeding...");
        
        if ([self installScreensaver]) {
            g_Log->Info("ScreensaverInstaller: Screensaver installed/updated successfully");
        } else {
            g_Log->Error("ScreensaverInstaller: Failed to install/update screensaver");
        }
    } else {
        g_Log->Info("ScreensaverInstaller: Screensaver is up to date, no action needed");
    }
}

- (BOOL)isScreensaverInstalled {
    NSString *installPath = [self getInstallationPath];
    return [[NSFileManager defaultManager] fileExistsAtPath:installPath];
}

- (NSString *)getInstallationPath {
    NSString *homeDirectory = NSHomeDirectory();
    return [homeDirectory stringByAppendingPathComponent:@"Library/Screen Savers/infinidream.saver"];
}

- (NSString *)getBundledScreensaverPath {
    NSString *bundlePath = [[NSBundle mainBundle] pathForResource:@"infinidream" ofType:@"saver"];
    return bundlePath;
}

- (nullable NSString *)getInstalledVersion {
    if (![self isScreensaverInstalled]) {
        return nil;
    }
    
    NSString *installPath = [self getInstallationPath];
    NSString *infoPlistPath = [installPath stringByAppendingPathComponent:@"Contents/Info.plist"];
    
    NSDictionary *infoPlist = [NSDictionary dictionaryWithContentsOfFile:infoPlistPath];
    if (!infoPlist) {
        g_Log->Warning("ScreensaverInstaller: Could not read installed screensaver Info.plist");
        return nil;
    }
    
    return infoPlist[@"CFBundleShortVersionString"];
}

- (nullable NSString *)getBundledVersion {
    NSString *bundlePath = [self getBundledScreensaverPath];
    if (!bundlePath) {
        g_Log->Error("ScreensaverInstaller: Could not find bundled screensaver");
        return nil;
    }
    
    NSString *infoPlistPath = [bundlePath stringByAppendingPathComponent:@"Contents/Info.plist"];
    
    NSDictionary *infoPlist = [NSDictionary dictionaryWithContentsOfFile:infoPlistPath];
    if (!infoPlist) {
        g_Log->Warning("ScreensaverInstaller: Could not read bundled screensaver Info.plist");
        return nil;
    }
    
    return infoPlist[@"CFBundleShortVersionString"];
}

- (BOOL)shouldUpdate {
    NSString *installedVersion = [self getInstalledVersion];
    NSString *bundledVersion = [self getBundledVersion];
    
    if (!installedVersion || !bundledVersion) {
        // If we can't determine versions, err on the side of caution and don't update
        return NO;
    }
    
    // Simple string comparison should work for most version formats (e.g., "1.2.3")
    // For more sophisticated version comparison, we could use NSVersionOfRunTimeLibrary or a custom comparator
    NSComparisonResult result = [bundledVersion compare:installedVersion options:NSNumericSearch];
    
    g_Log->Info("ScreensaverInstaller: Version comparison - Bundled: %s, Installed: %s", 
                [bundledVersion UTF8String], [installedVersion UTF8String]);
    
    return result == NSOrderedDescending; // bundled version is greater than installed version
}

- (BOOL)installScreensaver {
    NSString *sourcePath = [self getBundledScreensaverPath];
    if (!sourcePath) {
        g_Log->Error("ScreensaverInstaller: Could not find bundled screensaver to install");
        return NO;
    }
    
    NSString *destinationPath = [self getInstallationPath];
    NSString *destinationDirectory = [destinationPath stringByDeletingLastPathComponent];
    
    NSFileManager *fileManager = [NSFileManager defaultManager];
    NSError *error = nil;
    
    // Create Screen Savers directory if it doesn't exist
    if (![fileManager fileExistsAtPath:destinationDirectory]) {
        g_Log->Info("ScreensaverInstaller: Creating Screen Savers directory");
        if (![fileManager createDirectoryAtPath:destinationDirectory
                    withIntermediateDirectories:YES
                                     attributes:nil
                                          error:&error]) {
            g_Log->Error("ScreensaverInstaller: Failed to create directory: %s", 
                        [error.localizedDescription UTF8String]);
            return NO;
        }
    }
    
    // Remove existing installation if present
    if ([fileManager fileExistsAtPath:destinationPath]) {
        g_Log->Info("ScreensaverInstaller: Removing existing installation");
        if (![fileManager removeItemAtPath:destinationPath error:&error]) {
            g_Log->Error("ScreensaverInstaller: Failed to remove existing installation: %s", 
                        [error.localizedDescription UTF8String]);
            return NO;
        }
    }
    
    // Copy the screensaver bundle
    g_Log->Info("ScreensaverInstaller: Copying screensaver from %s to %s", 
                [sourcePath UTF8String], [destinationPath UTF8String]);
    
    if (![fileManager copyItemAtPath:sourcePath toPath:destinationPath error:&error]) {
        g_Log->Error("ScreensaverInstaller: Failed to copy screensaver: %s", 
                    [error.localizedDescription UTF8String]);
        return NO;
    }
    
    g_Log->Info("ScreensaverInstaller: Successfully installed screensaver");
    return YES;
}

- (void)enableScreensaverIfNeeded {
    // Check if screensaver is installed first
    if (![self isScreensaverInstalled]) {
        if (g_Log) g_Log->Info("ScreensaverInstaller: Cannot enable - screensaver not installed");
        return;
    }
    
    if (g_Log) g_Log->Info("ScreensaverInstaller: Checking if screensaver needs to be enabled");
    
    // Check if infinidream is already the active screensaver
    ScreensaverHelper *helper = [ScreensaverHelper shared];
    if (![helper isInfinidreamActive]) {
        if (g_Log) g_Log->Info("ScreensaverInstaller: infinidream is not active, setting it now");
        
        // Get current active screensaver name for logging
        NSString *currentScreensaver = [helper getActiveScreensaverName];
        if (currentScreensaver) {
            if (g_Log) g_Log->Info("ScreensaverInstaller: Current active screensaver is: %s", 
                                   [currentScreensaver UTF8String]);
        }
        
        // Set infinidream as the active screensaver
        [helper setInfinidreamAsActiveWithCompletion:^(BOOL success, NSError *error) {
            if (success) {
                if (g_Log) g_Log->Info("ScreensaverInstaller: Successfully set infinidream as active screensaver");
            } else {
                if (g_Log) g_Log->Error("ScreensaverInstaller: Failed to set infinidream as active: %s", 
                                        error ? [[error localizedDescription] UTF8String] : "Unknown error");
            }
        }];
    } else {
        if (g_Log) g_Log->Info("ScreensaverInstaller: infinidream is already the active screensaver");
    }
}

@end
