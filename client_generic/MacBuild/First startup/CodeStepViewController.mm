//
//  CodeStepViewController.m
//  infinidream
//
//  Created by Guillaume Louel on 17/06/2025.
//

#import "CodeStepViewController.h"
#import "StartupWindowController.h"
#import "ESScreensaver.h"
#import "WritingToolsSafeControls.h"

#include "EDreamClient.h"
#include "Log.h"

@interface CodeStepViewController () <NSTextFieldDelegate>
@property (weak) IBOutlet NSTextField *otpTextField;
@property (weak) IBOutlet NSButton *verifyButton;
@property (weak) IBOutlet NSProgressIndicator *progressIndicator;
@property (weak) IBOutlet NSTextField *errorLabel;

@end

@implementation CodeStepViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    
    // Disable Writing Tools to prevent hangs
    if (@available(macOS 15.2, *)) {
        self.otpTextField.allowsWritingTools = NO;
    }
    
    // Replace with WritingToolsSafe version
    self.otpTextField = [self replaceTextFieldWithSafeVersion:self.otpTextField];
    
    self.otpTextField.delegate = self;
    self.verifyButton.enabled = NO; // Disable until 6 digits entered
    
    // Hide error/progress
    self.errorLabel.hidden = YES;
    self.progressIndicator.hidden = YES;
    // Focus on the text field
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
        bool success = EDreamClient::ValidateCode(std::string(code.UTF8String));
        
        // Return to main thread for UI updates
        dispatch_async(dispatch_get_main_queue(), ^{
            // Hide loading state
            self.verifyButton.enabled = YES;
            self.otpTextField.enabled = YES;
            if (self.progressIndicator) {
                [self.progressIndicator stopAnimation:nil];
                self.progressIndicator.hidden = YES;
            }
            
            if (success) {
                // Mark first time setup as complete
                ESScreensaver_SetBoolSetting("settings.app.firsttimesetup", true);
                
                // Notify that we've signed in
                EDreamClient::DidSignIn();
                
                // Move to thanks step
                StartupWindowController *windowController = (StartupWindowController *)self.view.window.windowController;
                [windowController showThanksStep];
            } else {
                // Show error
                [self showError:@"Invalid verification code. Please try again."];
                
                // Clear the text field for retry
                self.otpTextField.stringValue = @"";
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
    // move back
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
    
    // Copy delegate if it exists
    if (originalField.delegate) {
        safeField.delegate = originalField.delegate;
    }
    
    // Replace in the view hierarchy
    NSView *superview = originalField.superview;
    if (superview) {
        [superview replaceSubview:originalField with:safeField];
    }
    
    return safeField;
}

@end
