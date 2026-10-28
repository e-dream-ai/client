//
//  ScreensaverInstaller.mm
//  infinidream
//
//  Registers the embedded screensaver .appex with PlugInKit and enables it
//  via PaperSaver.
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

- (NSString *)embeddedExtensionPath {
    NSString *plugins = [[NSBundle mainBundle] builtInPlugInsPath];
    if (!plugins)
        return nil;
#ifdef STAGE
    return [plugins stringByAppendingPathComponent:@"infinidream-stage.appex"];
#else
    return [plugins stringByAppendingPathComponent:@"infinidream.appex"];
#endif
}

- (NSString *)extensionBundleIdentifier {
#ifdef STAGE
    return @"ai.infinidream.stage.app.screensaver";
#else
    return @"ai.infinidream.alpha.app.screensaver";
#endif
}

- (BOOL)isScreensaverInstalled {
    NSString *bid = [self extensionBundleIdentifier];
    NSTask *task = [[NSTask alloc] init];
    task.executableURL = [NSURL fileURLWithPath:@"/usr/bin/pluginkit"];
    task.arguments = @[ @"-m", @"-v", @"-p", @"com.apple.screensaver" ];
    NSPipe *pipe = [NSPipe pipe];
    task.standardOutput = pipe;
    task.standardError = pipe;
    NSError *error = nil;
    if (![task launchAndReturnError:&error]) {
        g_Log->Warning("ScreensaverInstaller: pluginkit query failed: %s",
                       error.localizedDescription.UTF8String);
        return NO;
    }
    [task waitUntilExit];
    NSData *data = [[pipe fileHandleForReading] readDataToEndOfFile];
    NSString *output = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding] ?: @"";
    return [output containsString:bid];
}

- (nullable NSString *)getInstalledVersion {
    // PlugInKit listing includes (version); fall back to embedded Info.plist.
    return [self getBundledVersion];
}

- (nullable NSString *)getBundledVersion {
    NSString *path = [self embeddedExtensionPath];
    if (!path)
        return nil;
    NSBundle *bundle = [NSBundle bundleWithPath:path];
    return bundle.infoDictionary[@"CFBundleShortVersionString"];
}

- (BOOL)shouldUpdate {
    // Always re-register so pluginkit picks up the appex from the current app
    // bundle path after Sparkle updates (duplicate registrations prefer the
    // newest /Applications copy).
    NSString *path = [self embeddedExtensionPath];
    if (!path || ![[NSFileManager defaultManager] fileExistsAtPath:path])
        return NO;
    return YES;
}

- (BOOL)installScreensaver {
    NSString *extensionPath = [self embeddedExtensionPath];
    if (!extensionPath ||
        ![[NSFileManager defaultManager] fileExistsAtPath:extensionPath]) {
        g_Log->Error("ScreensaverInstaller: Embedded .appex not found at %s",
                     extensionPath ? extensionPath.UTF8String : "(nil)");
        return NO;
    }

    g_Log->Info("ScreensaverInstaller: pluginkit -a %s", extensionPath.UTF8String);

    NSTask *task = [[NSTask alloc] init];
    task.executableURL = [NSURL fileURLWithPath:@"/usr/bin/pluginkit"];
    task.arguments = @[ @"-a", extensionPath ];
    NSPipe *pipe = [NSPipe pipe];
    task.standardOutput = pipe;
    task.standardError = pipe;
    NSError *error = nil;
    if (![task launchAndReturnError:&error]) {
        g_Log->Error("ScreensaverInstaller: pluginkit -a failed: %s",
                     error.localizedDescription.UTF8String);
        return NO;
    }
    [task waitUntilExit];

    NSData *data = [[pipe fileHandleForReading] readDataToEndOfFile];
    NSString *output = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
    if (output.length > 0)
        g_Log->Info("ScreensaverInstaller: pluginkit output: %s", output.UTF8String);

    // Non-zero can still mean success for pluginkit; verify via query.
    BOOL registered = [self isScreensaverInstalled];
    if (!registered)
        g_Log->Warning("ScreensaverInstaller: pluginkit -a returned but extension not listed yet");
    return registered || task.terminationStatus == 0;
}

- (BOOL)ensureRegistered {
    // Always re-run pluginkit -a so Sparkle-updated app paths replace stale
    // registrations (listing by bundle ID alone can still point at an old copy).
    return [self installScreensaver];
}

- (void)enableScreensaverIfNeeded {
    if (![self ensureRegistered]) {
        if (g_Log)
            g_Log->Info("ScreensaverInstaller: Cannot enable screensaver — extension not registered");
        return;
    }

    if (g_Log)
        g_Log->Info("ScreensaverInstaller: Checking if screensaver needs to be enabled");

    ScreensaverHelper *helper = [ScreensaverHelper shared];
    if (![helper isInfinidreamActive]) {
        if (g_Log)
            g_Log->Info("ScreensaverInstaller: infinidream is not active, setting it now");

        NSString *currentScreensaver = [helper getActiveScreensaverName];
        if (currentScreensaver && g_Log) {
            g_Log->Info("ScreensaverInstaller: Current active screensaver is: %s",
                        [currentScreensaver UTF8String]);
        }

        [helper setInfinidreamAsActiveWithCompletion:^(BOOL success, NSError *error) {
            if (success) {
                if (g_Log)
                    g_Log->Info("ScreensaverInstaller: Successfully set infinidream as active screensaver");
            } else {
                if (g_Log)
                    g_Log->Error("ScreensaverInstaller: Failed to set infinidream as active: %s",
                                 error ? [[error localizedDescription] UTF8String] : "Unknown error");
            }
        }];
    } else {
        if (g_Log)
            g_Log->Info("ScreensaverInstaller: infinidream is already the active screensaver");
    }
}

- (void)enableWallpaperIfNeeded {
    if (![self ensureRegistered]) {
        if (g_Log)
            g_Log->Info("ScreensaverInstaller: Cannot enable wallpaper — extension not registered");
        return;
    }

    if (g_Log)
        g_Log->Info("ScreensaverInstaller: Checking if wallpaper needs to be enabled");

    ScreensaverHelper *helper = [ScreensaverHelper shared];
    if (![helper isInfinidreamWallpaperActive]) {
        if (g_Log)
            g_Log->Info("ScreensaverInstaller: infinidream is not wallpaper, setting it now");

        [helper setInfinidreamAsWallpaperWithCompletion:^(BOOL success, NSError *error) {
            if (success) {
                if (g_Log)
                    g_Log->Info("ScreensaverInstaller: Successfully set infinidream as wallpaper");
            } else {
                if (g_Log)
                    g_Log->Error("ScreensaverInstaller: Failed to set wallpaper: %s",
                                 error ? [[error localizedDescription] UTF8String] : "Unknown error");
            }
        }];
    } else {
        if (g_Log)
            g_Log->Info("ScreensaverInstaller: infinidream is already the desktop wallpaper");
    }
}

@end
