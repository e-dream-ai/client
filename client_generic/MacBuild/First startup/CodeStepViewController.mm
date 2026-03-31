//
//  CodeStepViewController.m
//  infinidream
//
//  Created by Guillaume Louel on 17/06/2025.
//

#import "CodeStepViewController.h"
#import "StartupWindowController.h"
#import "ESScreensaver.h"

#include "EDreamClient.h"
#include "Log.h"

@interface CodeStepViewController () <NSTextFieldDelegate>
@property (weak) IBOutlet NSTextField *otpTextField;
@property (weak) IBOutlet NSButton *verifyButton;
@property (weak) IBOutlet NSProgressIndicator *progressIndicator;
@property (weak) IBOutlet NSTextField *errorLabel;

@end

@implementation CodeStepViewController

- (void)resetVerificationInput {
    if (self.otpTextField) {
        self.otpTextField.stringValue = @"";
        self.otpTextField.enabled = YES;
    }
    if (self.verifyButton) {
        self.verifyButton.enabled = NO;
    }
    if (self.progressIndicator) {
        [self.progressIndicator stopAnimation:nil];
        self.progressIndicator.hidden = YES;
    }
    if (self.errorLabel) {
        self.errorLabel.hidden = YES;
    }
}

- (NSString *)popupMessageForValidateResult:(const EDreamClient::ValidateCodeResult&)result
{
    if (!result.message.empty()) {
        NSString *msg = [NSString stringWithUTF8String:result.message.c_str()];
        if (msg.length > 0) {
            return msg;
        }
    }
    
    if (result.reason == EDreamClient::ValidationFailureReason::InvalidSession) {
        return @"Invalid verification code. Please try again.";
    }
    
    if (result.httpCode >= 500) {
        return [NSString stringWithFormat:@"Server error (HTTP %d). Please try again.", result.httpCode];
    }
    
    return @"Backend is temporarily unavailable. Please try again shortly.";
}

- (NSString *)serverErrorDescriptionForResult:(const EDreamClient::ValidateCodeResult&)result
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

- (void)showValidationFailurePopupForResult:(const EDreamClient::ValidateCodeResult&)result
{
    const bool isClientErrorHttp =
        (result.httpCode >= 400 && result.httpCode < 500);
    const bool isServerErrorHttp = (result.httpCode >= 500);
    NSString *title;
    NSString *message;
    if (isClientErrorHttp) {
        title = @"Invalid Code";
        message = @"Check for typos and check to be sure you have the most recent code. Try again or start over";
    } else if (isServerErrorHttp) {
        title = @"Server Error";
        message = [self serverErrorDescriptionForResult:result];
    } else {
        title = @"Authentication Error";
        message = [self popupMessageForValidateResult:result];
    }
    
    // Keep inline error text (per requirement), plus popup so it's not missed.
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
    
    self.otpTextField.delegate = self;
    self.verifyButton.enabled = NO; // Disable until 6 digits entered
    
    // Hide error/progress
    self.errorLabel.hidden = YES;
    self.progressIndicator.hidden = YES;
}

- (void)viewDidAppear {
    [super viewDidAppear];
    [self.view.window makeFirstResponder:self.otpTextField];
}

- (IBAction)verifyButtonClicked:(id)sender {
    /*
    // Auto move to thanks step
    StartupWindowController *windowController = (StartupWindowController *)self.view.window.windowController;
    [windowController showThanksStep];
    */
     
    NSString *code = self.otpTextField.stringValue;

    // Show loading state
    self.verifyButton.enabled = NO;
    self.otpTextField.enabled = NO;
    if (self.progressIndicator) {
        self.progressIndicator.hidden = NO;
        [self.progressIndicator startAnimation:nil];
    }
    if (self.errorLabel) {
        self.errorLabel.hidden = YES;
    }
    
    // Run ValidateCode in background to avoid blocking UI
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        // For testing: 5 second pause instead of actual call
        //[NSThread sleepForTimeInterval:5.0];
        //bool success = true; // Simulate success for testing
        EDreamClient::ValidateCodeResult validateResult =
            EDreamClient::ValidateCodeDetailed(std::string(code.UTF8String));
        
        // Return to main thread for UI updates
        dispatch_async(dispatch_get_main_queue(), ^{
            // Hide loading state
            self.verifyButton.enabled = YES;
            self.otpTextField.enabled = YES;
            if (self.progressIndicator) {
                [self.progressIndicator stopAnimation:nil];
                self.progressIndicator.hidden = YES;
            }
            
            if (validateResult.success) {
                // Mark first time setup as complete
                ESScreensaver_SetBoolSetting("settings.app.firsttimesetup", true);
                
                // Notify that we've signed in
                EDreamClient::DidSignIn();
                
                // Move to thanks step
                StartupWindowController *windowController = (StartupWindowController *)self.view.window.windowController;
                [windowController showThanksStep];
            } else {
                [self showValidationFailurePopupForResult:validateResult];
                [self.view.window makeFirstResponder:self.otpTextField];
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

- (IBAction)tryAgain:(id)sender {
    StartupWindowController *windowController = (StartupWindowController *)self.view.window.windowController;
    [windowController showEmailStep];
}


#pragma mark - NSTextFieldDelegate
- (BOOL)control:(NSControl *)control textShouldBeginEditing:(NSText *)fieldEditor {
    return YES;
}

- (BOOL)control:(NSControl *)control textView:(NSTextView *)textView doCommandBySelector:(SEL)commandSelector {
    // Handle return key
    if (commandSelector == @selector(insertNewline:)) {
        if (self.otpTextField.stringValue.length == 6) {
            [self verifyButtonClicked:nil];
        }
        return YES;
    }
    return NO;
}

- (void)controlTextDidChange:(NSNotification *)notification {
    NSTextField *textField = notification.object;
    NSString *text = textField.stringValue;
    
    // Remove non-digits
    NSString *numbersOnly = [text stringByReplacingOccurrencesOfString:@"[^0-9]"
                                                            withString:@""
                                                               options:NSRegularExpressionSearch
                                                                 range:NSMakeRange(0, text.length)];
    
    // Limit to 6 digits
    if (numbersOnly.length > 6) {
        numbersOnly = [numbersOnly substringToIndex:6];
    }
    
    // Update field if changed
    if (![text isEqualToString:numbersOnly]) {
        textField.stringValue = numbersOnly;
    }
    
    // Enable/disable verify button
    self.verifyButton.enabled = (numbersOnly.length == 6);
   
}

@end
