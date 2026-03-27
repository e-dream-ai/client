//
//  CodeStepViewController.h
//  infinidream
//
//  Created by Guillaume Louel on 17/06/2025.
//

#import <Cocoa/Cocoa.h>

NS_ASSUME_NONNULL_BEGIN

@interface CodeStepViewController : NSViewController

/// Clears OTP, error state, and verify UI for a new code request or "start again" flow.
- (void)resetVerificationInput;

@end

NS_ASSUME_NONNULL_END
