//
//  EmailStepViewController.m
//  infinidream
//
//  Created by Guillaume Louel on 17/06/2025.
//


#import "EmailStepViewController.h"
#import "StartupWindowController.h"
#import "ESScreensaver.h"

#include "EDreamClient.h"
#include "ServerConfig.h"
#include "PlatformUtils.h"

@interface EmailStepViewController ()
@property (weak) IBOutlet NSTextField *emailTextField;
@property (weak) IBOutlet NSButton *sendCodeButton;
@property (weak) IBOutlet NSProgressIndicator *progressIndicator;
@property (weak) IBOutlet NSTextField *errorLabel;

@end

@implementation EmailStepViewController

- (NSString *)authFallbackMessageForSendCodeResult:(const EDreamClient::SendCodeResult &)result
{
    if (!result.message.empty()) {
        NSString *msg = [NSString stringWithUTF8String:result.message.c_str()];
        if (msg.length > 0) {
            return msg;
        }
    }
    return @"Failed to send verification code.";
}

- (NSString *)serverErrorDescriptionForSendCodeResult:(const EDreamClient::SendCodeResult &)result
{
    NSMutableString *body = [NSMutableString stringWithString:@"Try again later."];
    if (!result.message.empty()) {
        NSString *serverText = [NSString stringWithUTF8String:result.message.c_str()];
        if (serverText.length > 0) {
            [body appendString:@" "];
            [body appendString:serverText];
        }
    }
    return body;
}

- (void)showSendCodeFailurePopupForResult:(const EDreamClient::SendCodeResult &)result
{
    const bool isClientErrorHttp = (result.httpCode >= 400 && result.httpCode < 500);
    const bool isServerErrorHttp = (result.httpCode >= 500);
    NSString *title;
    NSString *message;
    if (isClientErrorHttp) {
        title = @"Unable to send code";
        message = @"We couldn't send a verification email. Make sure your email address is correct, then tap Send code again.";
        if (!result.message.empty()) {
            NSString *serverText = [NSString stringWithUTF8String:result.message.c_str()];
            if (serverText.length > 0) {
                message = [NSString stringWithFormat:@"%@\n\n%@", message, serverText];
            }
        }
    } else if (isServerErrorHttp) {
        title = @"Server Error";
        message = [self serverErrorDescriptionForSendCodeResult:result];
    } else {
        title = @"Authentication Error";
        message = [self authFallbackMessageForSendCodeResult:result];
    }
    [self showError:message];
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = title;
    alert.informativeText = message;
    alert.alertStyle = NSAlertStyleWarning;
    [alert addButtonWithTitle:@"OK"];
    if (self.view.window) {
        [alert beginSheetModalForWindow:self.view.window completionHandler:nil];
    } else {
        [alert runModal];
    }
}

- (void)viewDidLoad {
    [super viewDidLoad];
    
    
    // Load existing email if any
    CFStringRef emailCF = ESScreensaver_CopyGetStringSetting("settings.generator.nickname", "");
    if (emailCF) {
        NSString *email = (__bridge_transfer NSString *)emailCF;
        self.emailTextField.stringValue = email;
    }
    
    // Hide error label/progress
    self.errorLabel.hidden = YES;
    self.progressIndicator.hidden = YES;
}

- (void)viewDidAppear {
    [super viewDidAppear];
    [self.view.window makeFirstResponder:self.emailTextField];
}

- (IBAction)sendCode:(id)sender {
/*
     // Basic move to next step for testing
    StartupWindowController *windowController = (StartupWindowController *)self.view.window.windowController;
    [windowController showCodeStep];
    return;
  */
    NSString *email = self.emailTextField.stringValue;
    
    // Basic validation
    if (email.length == 0) {
        [self showError:@"Please enter your email address"];
        return;
    }
    
    // Simple email validation
    NSString *emailRegex = @"[A-Z0-9a-z._%+-]+@[A-Za-z0-9.-]+\\.[A-Za-z]{2,}";
    NSPredicate *emailTest = [NSPredicate predicateWithFormat:@"SELF MATCHES %@", emailRegex];
    if (![emailTest evaluateWithObject:email]) {
        [self showError:@"Please enter a valid email address"];
        return;
    }
    
    // Save email to settings
    ESScreensaver_SetStringSetting("settings.generator.nickname", email.UTF8String);
    
    // Show loading state
    self.sendCodeButton.enabled = NO;
    if (self.progressIndicator) {
        self.progressIndicator.hidden = NO;
        [self.progressIndicator startAnimation:nil];
    }
    if (self.errorLabel) {
        self.errorLabel.hidden = YES;
    }
    
    // Run SendCode in background to avoid blocking UI
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        // For testing: 5 second pause instead of actual call
        //[NSThread sleepForTimeInterval:5.0];
        // Simulate success for testing
        //struct { bool success; std::string message; } result = { true, "" };

        auto result = EDreamClient::SendCode();
        
        // Return to main thread for UI updates
        dispatch_async(dispatch_get_main_queue(), ^{
            // Hide loading state
            self.sendCodeButton.enabled = YES;
            if (self.progressIndicator) {
                [self.progressIndicator stopAnimation:nil];
                self.progressIndicator.hidden = YES;
            }
            
            if (result.success) {
                g_Log->Info("Move to code step");
                // Move to next step
                StartupWindowController *windowController = (StartupWindowController *)self.view.window.windowController;
                [windowController showCodeStep];
            } else {
                [self showSendCodeFailurePopupForResult:result];
            }
        });
    });
}

- (void)showError:(NSString *)error {
    if (self.errorLabel) {
        self.errorLabel.stringValue = error;
        self.errorLabel.hidden = NO;
    } else {
        // Fallback to alert if no error label
        NSAlert *alert = [[NSAlert alloc] init];
        alert.messageText = @"Error";
        alert.informativeText = error;
        alert.alertStyle = NSAlertStyleWarning;
        [alert addButtonWithTitle:@"OK"];
        [alert runModal];
    }
}

- (IBAction)openCreateAccount:(id)sender {
#ifdef STAGE
    NSURL* helpURL = [NSURL URLWithString:@"https://stage.infinidream.ai/account"];
#else
    NSURL* helpURL = [NSURL URLWithString:@"https://alpha.infinidream.ai/account"];
#endif
    [[NSWorkspace sharedWorkspace] openURL:helpURL];
}

@end
