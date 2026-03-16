//
//  FirstTimeSetupManager.h
//  infinidream
//
//  Manages the first-time setup modal presentation
//

#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>

NS_ASSUME_NONNULL_BEGIN

@interface FirstTimeSetupManager : NSObject

+ (instancetype)sharedManager;

- (void)showFirstTimeSetupIfNeeded;
- (void)showFirstTimeSetupForWindow:(NSWindow *)parentWindow;

@end

NS_ASSUME_NONNULL_END
