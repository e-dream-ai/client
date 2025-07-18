//
//  StartupWindowController.m
//  infinidream
//
//  Created by Guillaume Louel on 17/06/2025.
//

#import "StartupWindowController.h"
#import "EmailStepViewController.h"
#import "CodeStepViewController.h"
#import "ThanksStepViewController.h"
#import <QuartzCore/QuartzCore.h>

@interface StartupWindow : NSWindow
@end

@implementation StartupWindow

- (BOOL)canBecomeKeyWindow {
    return YES;
}

- (BOOL)canBecomeMainWindow {
    return YES;
}

@end


@interface StartupWindowController ()
@property (weak) IBOutlet NSView *containerView;
@property (strong) EmailStepViewController *emailStepVC;
@property (strong) CodeStepViewController *codeStepVC;
@property (strong) ThanksStepViewController *thanksStepVC;
@property (strong) NSViewController *currentStepVC;

@end

@implementation StartupWindowController

- (void)windowDidLoad {
    [super windowDidLoad];
    
    // Make it a modal
    self.window.level = NSModalPanelWindowLevel;
    
    self.emailStepVC = [[EmailStepViewController alloc] init];
    self.codeStepVC = [[CodeStepViewController alloc] init];
    self.thanksStepVC = [[ThanksStepViewController alloc] init];
    
    // Show first step
    [self showEmailStep];
}

- (void)switchToViewController:(NSViewController *)newVC {
    // NSAnimationContext version with completion handler
    NSViewController *oldVC = self.currentStepVC;
    
    if (oldVC) {
        [NSAnimationContext runAnimationGroup:^(NSAnimationContext *context) {
            context.duration = 0.3;
            context.timingFunction = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseInEaseOut];
            [[oldVC.view animator] setAlphaValue:0.0];
        } completionHandler:^{
            [oldVC.view removeFromSuperview];
            [self addAndCenterNewView:newVC];
        }];
    } else {
        [self addAndCenterNewView:newVC];
    }
    
    // Basic non animated swap
    /*
    // Remove current view
    if (self.currentStepVC) {
        [self.currentStepVC.view removeFromSuperview];
    }
    
    // Add new view
    self.currentStepVC = newVC;

    // Center view controller
    NSSize containerSize = self.containerView.bounds.size;
    NSSize viewSize = newVC.view.frame.size;
    
    NSRect centeredFrame = NSMakeRect(
        (containerSize.width - viewSize.width) / 2,
        (containerSize.height - viewSize.height) / 2,
        viewSize.width,
        viewSize.height
    );
    
    newVC.view.frame = centeredFrame;
    [self.containerView addSubview:newVC.view];
    */
}

- (void)addAndCenterNewView:(NSViewController *)newVC {
    self.currentStepVC = newVC;
    
    // Center the view
    NSSize containerSize = self.containerView.bounds.size;
    NSSize viewSize = newVC.view.frame.size;
    
    NSRect centeredFrame = NSMakeRect(
        (containerSize.width - viewSize.width) / 2,
        (containerSize.height - viewSize.height) / 2,
        viewSize.width,
        viewSize.height
    );
    
    newVC.view.frame = centeredFrame;
    newVC.view.alphaValue = 0.0; // Start transparent
    [self.containerView addSubview:newVC.view];
    
    // Fade in new view using NSAnimationContext
    [NSAnimationContext runAnimationGroup:^(NSAnimationContext *context) {
        context.duration = 0.3;
        context.timingFunction = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseInEaseOut];
        [[newVC.view animator] setAlphaValue:1.0];
    } completionHandler:nil];
}

- (void)showEmailStep {
    [self switchToViewController:self.emailStepVC];
}

- (void)showCodeStep {
    [self switchToViewController:self.codeStepVC];
}

- (void)showThanksStep {
    [self switchToViewController:self.thanksStepVC];
}

- (IBAction)dismissSheet:(id)sender {
    [self.window.sheetParent endSheet:self.window returnCode:NSModalResponseCancel];
}
@end
