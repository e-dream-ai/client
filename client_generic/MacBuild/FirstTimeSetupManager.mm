//
//  FirstTimeSetupManager.mm
//  infinidream
//
//  Manages the first-time setup modal presentation
//

#import "FirstTimeSetupManager.h"
#import "First startup/StartupWindowController.h"
#import "ESScreensaver.h"
#import "ESWindow.h"
#include "Log.h"
#include "EDreamClient.h"

@interface FirstTimeSetupManager ()
@property (strong, nonatomic) StartupWindowController *startupWindowController;
@end

@implementation FirstTimeSetupManager

+ (instancetype)sharedManager {
    static FirstTimeSetupManager *sharedManager = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        sharedManager = [[FirstTimeSetupManager alloc] init];
    });
    return sharedManager;
}

- (void)showFirstTimeSetupIfNeeded {
    // Check if first time setup has been completed
    bool firstTimeSetupCompleted = ESScreensaver_GetBoolSetting("settings.app.firsttimesetup", false);
    
    // Show startup window if first time setup hasn't been completed OR if user is not logged in
    if (!firstTimeSetupCompleted || !EDreamClient::IsLoggedIn()) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [self presentFirstTimeSetup];
        });
    }
}

- (void)showFirstTimeSetupForWindow:(NSWindow *)parentWindow {
    dispatch_async(dispatch_get_main_queue(), ^{
        [self presentFirstTimeSetupForWindow:parentWindow];
    });
}

- (void)presentFirstTimeSetup {
    // Find the main window to use as parent
    NSWindow *mainWindow = nil;
    for (NSWindow *window in [NSApplication sharedApplication].windows) {
        if (window.isVisible && !window.isSheet) {
            mainWindow = window;
            break;
        }
    }
    
    if (mainWindow) {
        [self presentFirstTimeSetupForWindow:mainWindow];
    } else {
        g_Log->Warning("No main window found for first-time setup presentation");
    }
}

- (void)presentFirstTimeSetupForWindow:(NSWindow *)parentWindow {
    // Create the startup window controller
    self.startupWindowController = [[StartupWindowController alloc] initWithWindowNibName:@"StartupWindowController"];
    
    if (!self.startupWindowController.window) {
        g_Log->Error("Failed to load StartupWindowController window");
        return;
    }
    
    // Center the window
    [self.startupWindowController.window center];
    
    // Make sure the window controller is properly set
    if (self.startupWindowController.window.windowController != self.startupWindowController) {
        g_Log->Warning("Window controller not properly set, fixing...");
        [self.startupWindowController.window setWindowController:self.startupWindowController];
    }
    
    // Make the window key and order front
    [self.startupWindowController.window makeKeyAndOrderFront:nil];
    
    // Delay modal presentation to allow window to fully initialize
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.1 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        // Run as modal
        NSModalResponse response = [NSApp runModalForWindow:self.startupWindowController.window];
        
        // Handle completion after modal is dismissed
        [self handleModalCompletion:response forParentWindow:parentWindow];
    });
}

- (void)handleModalCompletion:(NSModalResponse)response forParentWindow:(NSWindow *)parentWindow {
    // After modal is dismissed, ensure the parent window is visible
    [parentWindow makeKeyAndOrderFront:nil];
    
    // Handle response based on how modal was closed
    if (response == NSModalResponseOK) {
        g_Log->Info("First-time setup completed successfully");
        
        // If user completed setup/login successfully, notify the system
        if (EDreamClient::IsLoggedIn()) {
            g_Log->Info("User is logged in - restarting player");
            // TODO: Restart player or refresh content
        }
    } else if (response == NSModalResponseCancel) {
        g_Log->Info("First-time setup was cancelled by user");
    } else {
        g_Log->Info("First-time setup modal closed with response: %ld", (long)response);
    }
    
    // If user completed setup/login, notify the ESScreensaverView to refresh
    if (EDreamClient::IsLoggedIn() && [parentWindow isKindOfClass:[ESWindow class]]) {
        ESWindow *esWindow = (ESWindow *)parentWindow;
        if ([esWindow respondsToSelector:@selector(mESView)] && esWindow->mESView) {
            [esWindow->mESView windowDidResize];
        }
    }
    
    // Clean up
    self.startupWindowController = nil;
}

@end
