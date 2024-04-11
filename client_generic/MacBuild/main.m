//
//  main.m
//

#import "InstallerHelper.h"
#import <Cocoa/Cocoa.h>

// MARK: - MacOS app client entry point
int main(int argc, char* argv[])
{
    // Ensure we are not launching from App folder, if so move to /Applications and relaunch
    [InstallerHelper ensureNotInDownloadFolder];
    
    // Then install the screensaver if needed and cleanup any manual install
    [InstallerHelper installScreenSaver];
    
    // After that, launch the app
    return NSApplicationMain(argc, (const char**)argv);
}

