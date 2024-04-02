//
//  main.m
//

#import <Cocoa/Cocoa.h>

int main(int argc, char* argv[])
{
    // Ensure we are not launching from App folder, if so move to /Applications and relaunch
    
    // Get current launch URL
    NSString *currentPath = [[NSBundle mainBundle] bundlePath];
    NSURL *currentURL = [NSURL fileURLWithPath:currentPath];
    
    NSString *downloadsPath = NSSearchPathForDirectoriesInDomains(NSDownloadsDirectory, NSUserDomainMask, YES)[0];
    NSURL *downloadsURL = [NSURL fileURLWithPath:downloadsPath];
    
    if ([currentURL isEqual:downloadsURL]) {
        // The app is in the Downloads folder.
        NSString *applicationsPath = @"/Applications";
        NSError *error;
        
        NSFileManager *fileManager = [NSFileManager defaultManager];
        if ([fileManager moveItemAtURL:currentURL toURL:[NSURL fileURLWithPath:[applicationsPath stringByAppendingPathComponent:@"e-dream.app"]]
                                   error: &error
              ]) {
            NSLog(@"Application moved successfully.");
            
            // Reload the app from /Applications
            NSApplication *app = [NSApplication sharedApplication];
            [NSWorkspace.sharedWorkspace launchApplicationAtURL:[NSURL fileURLWithPath:[applicationsPath stringByAppendingPathComponent:@"e-dream.app"]] options:NSWorkspaceLaunchDefault configuration:nil error:&error];
             
            [NSApplication.sharedApplication terminate:nil];
        } else {
            NSLog(@"Error moving application: %@", error);
        }
    }
    
    return NSApplicationMain(argc, (const char**)argv);
}
