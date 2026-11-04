//
//  ScreensaverInstaller.mm
//  infinidream
//
//  Register wallpaper .appex (Desktop) and screensaver .appex (Idle) as
//  separate processes. They must not share a Provider / player.
//

#import "ScreensaverInstaller.h"

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
    g_Log->Info("ScreensaverInstaller: Checking if extension installation is needed...");

    BOOL wallpaperOK = [self installWallpaper];
    if (wallpaperOK) {
        g_Log->Info("ScreensaverInstaller: Wallpaper registered");
    } else if ([self embeddedWallpaperPath]) {
        g_Log->Warning("ScreensaverInstaller: Wallpaper appex present but not listed yet");
    } else {
        g_Log->Error(
            "ScreensaverInstaller: No wallpaper appex in PlugIns — "
            "Embed Wallpaper Extension Run Script likely skipped "
            "(install xcodegen: brew install xcodegen)");
    }

    BOOL saverOK = [self installScreensaver];
    if (saverOK) {
        g_Log->Info("ScreensaverInstaller: Screensaver registered");
    } else if ([self embeddedExtensionPath] &&
               [[NSFileManager defaultManager] fileExistsAtPath:[self embeddedExtensionPath]]) {
        g_Log->Warning("ScreensaverInstaller: Screensaver appex present but not listed yet");
    } else {
        g_Log->Error(
            "ScreensaverInstaller: No screensaver appex in PlugIns — "
            "Embed Screensaver Extension Run Script likely skipped");
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

- (nullable NSString *)embeddedWallpaperPath {
    NSString *plugins = [[NSBundle mainBundle] builtInPlugInsPath];
    if (!plugins)
        return nil;
#ifdef STAGE
    NSString *path = [plugins stringByAppendingPathComponent:@"infinidream-stage-wallpaper.appex"];
#else
    NSString *path = [plugins stringByAppendingPathComponent:@"infinidream-wallpaper.appex"];
#endif
    return [[NSFileManager defaultManager] fileExistsAtPath:path] ? path : nil;
}

- (NSString *)extensionBundleIdentifier {
#ifdef STAGE
    return @"ai.infinidream.stage.app.screensaver";
#else
    return @"ai.infinidream.alpha.app.screensaver";
#endif
}

- (NSString *)wallpaperBundleIdentifier {
#ifdef STAGE
    return @"ai.infinidream.stage.app.wallpaper";
#else
    return @"ai.infinidream.alpha.app.wallpaper";
#endif
}

- (BOOL)pluginkitListsBundleID:(NSString *)bid extensionPoint:(NSString *)point {
    NSTask *task = [[NSTask alloc] init];
    task.executableURL = [NSURL fileURLWithPath:@"/usr/bin/pluginkit"];
    task.arguments = @[ @"-m", @"-v", @"-p", point ];
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

- (BOOL)isScreensaverInstalled {
    return [self pluginkitListsBundleID:[self extensionBundleIdentifier]
                         extensionPoint:@"com.apple.screensaver"];
}

- (BOOL)isWallpaperInstalled {
    return [self pluginkitListsBundleID:[self wallpaperBundleIdentifier]
                         extensionPoint:@"com.apple.wallpaper"];
}

- (nullable NSString *)getInstalledVersion {
    return [self getBundledVersion];
}

- (nullable NSString *)getBundledVersion {
    NSString *path = [self embeddedExtensionPath];
    if (!path || ![[NSFileManager defaultManager] fileExistsAtPath:path])
        path = [self embeddedWallpaperPath];
    if (!path)
        return nil;
    NSBundle *bundle = [NSBundle bundleWithPath:path];
    return bundle.infoDictionary[@"CFBundleShortVersionString"];
}

- (BOOL)shouldUpdate {
    NSString *path = [self embeddedExtensionPath];
    if (!path || ![[NSFileManager defaultManager] fileExistsAtPath:path])
        path = [self embeddedWallpaperPath];
    if (!path || ![[NSFileManager defaultManager] fileExistsAtPath:path])
        return NO;
    return YES;
}

- (BOOL)runPluginkitAdd:(NSString *)extensionPath electBundleID:(nullable NSString *)bid {
    NSString *lsregister =
        @"/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister";
    if ([[NSFileManager defaultManager] isExecutableFileAtPath:lsregister]) {
        NSTask *ls = [[NSTask alloc] init];
        ls.executableURL = [NSURL fileURLWithPath:lsregister];
        ls.arguments = @[ @"-f", extensionPath ];
        NSPipe *nullPipe = [NSPipe pipe];
        ls.standardOutput = nullPipe;
        ls.standardError = nullPipe;
        [ls launchAndReturnError:nil];
        [ls waitUntilExit];
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

    if (bid.length > 0) {
        NSTask *elect = [[NSTask alloc] init];
        elect.executableURL = [NSURL fileURLWithPath:@"/usr/bin/pluginkit"];
        elect.arguments = @[ @"-e", @"use", @"-i", bid ];
        NSPipe *electPipe = [NSPipe pipe];
        elect.standardOutput = electPipe;
        elect.standardError = electPipe;
        [elect launchAndReturnError:nil];
        [elect waitUntilExit];
    }

    return task.terminationStatus == 0;
}

- (BOOL)installScreensaver {
    NSString *extensionPath = [self embeddedExtensionPath];
    if (!extensionPath ||
        ![[NSFileManager defaultManager] fileExistsAtPath:extensionPath]) {
        return NO;
    }

    BOOL added = [self runPluginkitAdd:extensionPath
                         electBundleID:[self extensionBundleIdentifier]];
    BOOL registered = [self isScreensaverInstalled];
    return registered || added;
}

- (BOOL)installWallpaper {
    NSString *extensionPath = [self embeddedWallpaperPath];
    if (!extensionPath)
        return NO;

    BOOL added = [self runPluginkitAdd:extensionPath
                         electBundleID:[self wallpaperBundleIdentifier]];
    BOOL registered = [self isWallpaperInstalled];
    if (!registered)
        g_Log->Warning("ScreensaverInstaller: wallpaper not listed yet after pluginkit -a");
    return registered || added;
}

- (BOOL)ensureRegistered {
    BOOL wallpaper = [self installWallpaper];
    BOOL screensaver = [self installScreensaver];
    return wallpaper || screensaver || [self isScreensaverInstalled] || [self isWallpaperInstalled];
}

- (void)enableScreensaverIfNeeded {
    if (![self installScreensaver] && ![self isScreensaverInstalled]) {
        if (g_Log)
            g_Log->Info("ScreensaverInstaller: Cannot enable Idle — screensaver extension not registered");
        return;
    }

    if (g_Log)
        g_Log->Info("ScreensaverInstaller: Checking if Idle (screensaver) needs to be enabled");

    ScreensaverHelper *helper = [ScreensaverHelper shared];
    if (![helper isInfinidreamActive]) {
        if (g_Log)
            g_Log->Info("ScreensaverInstaller: infinidream is not Idle, setting it now");

        NSString *currentScreensaver = [helper getActiveScreensaverName];
        if (currentScreensaver && g_Log) {
            g_Log->Info("ScreensaverInstaller: Current Idle provider: %s",
                        [currentScreensaver UTF8String]);
        }

        [helper setInfinidreamAsActiveWithCompletion:^(BOOL success, NSError *error) {
            if (success) {
                if (g_Log)
                    g_Log->Info("ScreensaverInstaller: Successfully set screensaver appex as Idle");
            } else {
                if (g_Log)
                    g_Log->Error("ScreensaverInstaller: Failed to set Idle: %s",
                                 error ? [[error localizedDescription] UTF8String] : "Unknown error");
            }
        }];
    } else {
        if (g_Log)
            g_Log->Info("ScreensaverInstaller: infinidream is already the Idle provider");
    }
}

- (void)enableWallpaperIfNeeded {
    if (![self installWallpaper]) {
        if (![self ensureRegistered]) {
            if (g_Log)
                g_Log->Info("ScreensaverInstaller: Cannot enable wallpaper — extension not registered");
            return;
        }
    }

    if (g_Log)
        g_Log->Info("ScreensaverInstaller: Checking if Desktop wallpaper needs to be enabled");

    ScreensaverHelper *helper = [ScreensaverHelper shared];
    if (![helper isInfinidreamWallpaperActive]) {
        if (g_Log)
            g_Log->Info("ScreensaverInstaller: infinidream is not Desktop wallpaper, setting it now");

        [helper setInfinidreamAsWallpaperWithCompletion:^(BOOL success, NSError *error) {
            if (success) {
                if (g_Log)
                    g_Log->Info("ScreensaverInstaller: Successfully set wallpaper appex as Desktop");
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
