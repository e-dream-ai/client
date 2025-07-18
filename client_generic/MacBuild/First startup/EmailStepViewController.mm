//
//  EmailStepViewController.m
//  infinidream
//
//  Created by Guillaume Louel on 17/06/2025.
//


#import "EmailStepViewController.h"
#import "StartupWindowController.h"
#import "ESScreensaver.h"
#import "WritingToolsSafeControls.h"

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

- (void)viewDidLoad {
    [super viewDidLoad];
    
    // Disable Writing Tools to prevent hangs
    if (@available(macOS 15.2, *)) {
        self.emailTextField.allowsWritingTools = NO;
    }
    
    // Replace with WritingToolsSafe version
    self.emailTextField = [self replaceTextFieldWithSafeVersion:self.emailTextField];
    
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
                // Show error
                NSString *errorMessage = [NSString stringWithUTF8String:result.message.c_str()];
                [self showError:errorMessage ?: @"Failed to send verification code"];
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

- (NSTextField *)replaceTextFieldWithSafeVersion:(NSTextField *)originalField {
    if (!originalField) return nil;
    
    // Create a new WritingToolsSafe text field with the same properties
    WritingToolsSafeTextField *safeField = [[WritingToolsSafeTextField alloc] initWithFrame:originalField.frame];
    
    // Copy all the important properties
    safeField.stringValue = originalField.stringValue;
    safeField.placeholderString = originalField.placeholderString;
    safeField.font = originalField.font;
    safeField.textColor = originalField.textColor;
    safeField.backgroundColor = originalField.backgroundColor;
    safeField.bordered = originalField.bordered;
    safeField.bezeled = originalField.bezeled;
    safeField.editable = originalField.editable;
    safeField.selectable = originalField.selectable;
    safeField.enabled = originalField.enabled;
    safeField.hidden = originalField.hidden;
    safeField.alignment = originalField.alignment;
    safeField.tag = originalField.tag;
    safeField.identifier = originalField.identifier;
    safeField.toolTip = originalField.toolTip;
    
    // Copy cell properties
    safeField.cell.scrollable = originalField.cell.scrollable;
    safeField.cell.wraps = originalField.cell.wraps;
    [safeField.cell setUsesSingleLineMode:[originalField.cell usesSingleLineMode]];
    
    // Disable Writing Tools
    if ([safeField respondsToSelector:@selector(setAllowsWritingTools:)]) {
        safeField.allowsWritingTools = NO;
    }
    
    // Copy target/action if it exists
    if (originalField.target && originalField.action) {
        safeField.target = originalField.target;
        safeField.action = originalField.action;
    }
    
    // Replace in the view hierarchy
    NSView *superview = originalField.superview;
    if (superview) {
        [superview replaceSubview:originalField with:safeField];
    }
    
    return safeField;
}

@end
