//
//  InstallerHelper.h
//  e-dream App
//
//  Created by Guillaume Louel on 11/04/2024.
//

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface InstallerHelper : NSObject
+ (void)ensureNotInDownloadFolder;
+ (void)relaunch:(NSString*)destinationPath;
+ (void)installScreenSaver;

+ (NSString*)saverAllUserPath;
+ (NSString*)saverUserPath;


@end



NS_ASSUME_NONNULL_END
