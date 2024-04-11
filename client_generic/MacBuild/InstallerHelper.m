//
//  InstallerHelper.m
//  e-dream App
//
//  Created by Guillaume Louel on 11/04/2024.
//

#import "InstallerHelper.h"
#import <Cocoa/Cocoa.h>
#import <ZipArchive.h>

NSString *ShellQuotedString(NSString *string) {
    return [NSString stringWithFormat:@"'%@'", [string stringByReplacingOccurrencesOfString:@"'" withString:@"'\\''"]];
}

@implementation InstallerHelper

// MARK: - Not in download folder Helpers
+ (void) ensureNotInDownloadFolder {
    // Get current launch URL
    NSString *currentPath = [[NSBundle mainBundle] bundlePath];
    NSURL *currentURL = [NSURL fileURLWithPath:currentPath];
    
    NSString *downloadsPath = NSSearchPathForDirectoriesInDomains(NSDownloadsDirectory, NSUserDomainMask, YES)[0];
    NSURL *downloadsURL = [NSURL fileURLWithPath:[downloadsPath stringByAppendingPathComponent:@"e-dream.app"]];
    
    if ([currentURL isEqual:downloadsURL]) {
        // The app is in the Downloads folder.
        NSString *applicationsPath = @"/Applications";
        NSError *error;
        
        NSFileManager *fileManager = [NSFileManager defaultManager];
        NSString *appPath = [applicationsPath stringByAppendingPathComponent:@"e-dream.app"];
        NSURL *appURL = [NSURL fileURLWithPath: appPath];
        
        if ([fileManager fileExistsAtPath:appPath]) {
            if (![fileManager removeItemAtPath:appPath error:&error]) {
                NSLog(@"Could not delete old application.");
            }
        }
            
        if ([fileManager moveItemAtURL:currentURL toURL:appURL error: &error]) {
            NSLog(@"Application moved successfully.");
            
            // Reload the app from /Applications
            [self relaunch:appPath];
            
            [NSApplication.sharedApplication terminate:nil];
        } else {
            NSLog(@"Error moving application: %@", error);
        }
    } else {
        NSLog(@"Not in download folder, resuming");
    }
}

+ (void) relaunch:(NSString*) destinationPath {
    // The shell script waits until the original app process terminates.
    // This is done so that the relaunched app opens as the front-most app.
    int pid = [[NSProcessInfo processInfo] processIdentifier];

    // Command run just before running open /final/path
    NSString *preOpenCmd = @"";

    NSString *quotedDestinationPath = ShellQuotedString(destinationPath);

    // OS X >=10.5:
    // Before we launch the new app, clear xattr:com.apple.quarantine to avoid
    // duplicate "scary file from the internet" dialog.
    if (floor(NSAppKitVersionNumber) > NSAppKitVersionNumber10_5) {
        // Add the -r flag on 10.6
        preOpenCmd = [NSString stringWithFormat:@"/usr/bin/xattr -d -r com.apple.quarantine %@", quotedDestinationPath];
    }
    else {
        preOpenCmd = [NSString stringWithFormat:@"/usr/bin/xattr -d com.apple.quarantine %@", quotedDestinationPath];
    }

    NSString *script = [NSString stringWithFormat:@"(while /bin/kill -0 %d >&/dev/null; do /bin/sleep 0.1; done; %@; /usr/bin/open %@) &", pid, preOpenCmd, quotedDestinationPath];

    [NSTask launchedTaskWithLaunchPath:@"/bin/sh" arguments:[NSArray arrayWithObjects:@"-c", script, nil]];
}

// MARK: - ScreenSaver installer helpers
+ (void) installScreenSaver {
    NSString* saverUserPath = [self saverUserPath];
    NSString* saverAllUsersPath = [self saverAllUsersPath];
    NSError *error;
    
    // First make sure it's not installed for a single user
    if ([[NSFileManager defaultManager] fileExistsAtPath:saverUserPath]) {
        NSLog(@"Removing installed saver");
        if (![[NSFileManager defaultManager] removeItemAtPath:saverUserPath error:&error]) {
            NSLog(@"Could not delete old saver");
        } else {
            NSLog(@"Saver removed from user folder");
        }
    }
    
    bool shouldInstall = true;

    // Fetch our own version, we use the same version for the screen saver and the app, 
    // which makes it easier.
    //
    // App version X.Y.Z bundles screensaver X.Y.Z
    NSString *bundleVersion = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleVersion"];
    NSLog(@"Bundle Version: %@", bundleVersion);

    // Now we check if the saver is installed for all users
    if ([[NSFileManager defaultManager] fileExistsAtPath:saverAllUsersPath]) {
        // Ok, now compare the version to this bundles version
        NSDictionary *infoDictionary = [[NSFileManager defaultManager] attributesOfItemAtPath:saverAllUsersPath error:nil];
        NSString *installedVersion = [infoDictionary objectForKey:@"NSFileVersionNumber"];
        NSLog(@"Installed Version: %@", installedVersion);
       
        if ([bundleVersion isEqualToString:installedVersion]) {
            // Already installed, skip install
            NSLog(@"Already installed, skipping");
            shouldInstall = false;
        } else {
            // Delete the old version
            if (![[NSFileManager defaultManager] removeItemAtPath:saverAllUsersPath error:&error]) {
                NSLog(@"Could not delete old saver");
                //@TODO report in bugsnag
                return;
            } else {
                NSLog(@"Old saver removed from all user folder");
            }
        }
    }
    
    if (shouldInstall) {
        NSLog(@"Installing saver");
        
        // Make sure we have a tmp path in Application Support
        NSArray *paths = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES);
        NSString *applicationSupportDirectory = [[paths firstObject] stringByAppendingPathComponent:@"e-dream"];
        NSLog(@"applicationSupportDirectory: '%@'", applicationSupportDirectory);
        
        if (![[NSFileManager defaultManager] fileExistsAtPath:applicationSupportDirectory]) {
            if (![[NSFileManager defaultManager] createDirectoryAtPath:applicationSupportDirectory
              withIntermediateDirectories:true
                               attributes:nil
                                    error:&error]) {
                NSLog(@"Couldn't create e-dream folder in app support, aborting");
                //@TODO report in bugsnag
                return;
            } else {
                NSLog(@"Application folder created");
            }
        } else {
            NSLog(@"Application folder already exists");
        }
        
        NSString* localZip = [[NSBundle mainBundle] pathForResource:@"e-dream.saver" ofType:@".zip"];
        if (localZip == nil) {
            NSLog(@"No saver zip found in bundle");
            //@TODO report in bugsnag
            return;
        }
        NSLog(@"Local zip : %@", localZip);
        if (![[NSFileManager defaultManager] copyItemAtPath:localZip toPath:[applicationSupportDirectory stringByAppendingPathComponent:@"e-dream.saver.zip"] error:&error]) {
            NSLog(@"Could not copy zip to app support");
            //@TODO report in bugsnag
            return;
        }
    }
    
    [SSZipArchive unzipFileAtPath:localZip toDestination:saverAllUsersPath];
    
    return;
}

+ (NSString*) saverAllUsersPath {
    return [NSString stringWithFormat:@"%@/Screen Savers/%@", NSSearchPathForDirectoriesInDomains(NSLibraryDirectory, NSLocalDomainMask, YES)[0], @"e-dream.saver"];
}
+ (NSString*) saverUserPath {
    return [NSString stringWithFormat:@"%@/Screen Savers/%@", NSSearchPathForDirectoriesInDomains(NSLibraryDirectory, NSUserDomainMask, YES)[0], @"e-dream.saver"];
}
@end
