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
    
    // Present as sheet instead of modal
    [parentWindow beginSheet:self.startupWindowController.window 
           completionHandler:^(NSModalResponse response) {
               // Handle completion after sheet is dismissed
               [self handleSheetCompletion:response forParentWindow:parentWindow];
           }];
}

- (void)handleSheetCompletion:(NSModalResponse)response forParentWindow:(NSWindow *)parentWindow {
    // Handle response based on how sheet was closed
    if (response == NSModalResponseOK) {
        g_Log->Info("First-time setup completed successfully");
        
        // If user completed setup/login successfully, restart the player
        if (EDreamClient::IsLoggedIn()) {
            g_Log->Info("User is logged in - restarting player");
            [self restartPlayerForWindow:parentWindow];
        }
    } else if (response == NSModalResponseCancel) {
        g_Log->Info("First-time setup was cancelled by user");
        [self restartPlayerForWindow:parentWindow];
    } else {
        g_Log->Info("First-time setup sheet closed with response: %ld", (long)response);
    }
    
    // Clean up
    self.startupWindowController = nil;
}

- (void)restartPlayerForWindow:(NSWindow *)parentWindow {
    // Restart the player similar to how ESConfiguration does it
    if ([parentWindow isKindOfClass:[ESWindow class]]) {
        ESWindow *esWindow = (ESWindow *)parentWindow;

        if (esWindow->mESView) {
            // Stop current animation
            [esWindow->mESView stopAnimation];
            
            // Refresh window size
            [esWindow->mESView windowDidResize];
            
            // Restart animation
            [esWindow->mESView startAnimation];
            
            // Restore first responder to handle key events
            [esWindow makeFirstResponder:esWindow->mESView];
            
            g_Log->Info("Player restarted successfully");
        }
    }
}

@end
