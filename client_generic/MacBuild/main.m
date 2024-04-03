//
//  main.m
//

#import <Cocoa/Cocoa.h>

// MacOS Platform helpers that run at startup to handle apps launched after being
// unzipped in Downloads folder
void EnsureNotInDownloadFolder();
NSString *ShellQuotedString(NSString *string);
void Relaunch(NSString *destinationPath);

/// MARK: - MacOS app client entry point
int main(int argc, char* argv[])
{
    // Ensure we are not launching from App folder, if so move to /Applications and relaunch
    EnsureNotInDownloadFolder();
    
    return NSApplicationMain(argc, (const char**)argv);
}

// Helpers
void EnsureNotInDownloadFolder() {
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
            Relaunch(appPath);
            
            [NSApplication.sharedApplication terminate:nil];
        } else {
            NSLog(@"Error moving application: %@", error);
        }
    }
}

NSString *ShellQuotedString(NSString *string) {
    return [NSString stringWithFormat:@"'%@'", [string stringByReplacingOccurrencesOfString:@"'" withString:@"'\\''"]];
}

void Relaunch(NSString *destinationPath) {
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
