//
//  PlatformUtils.c
//  e-dream
//
//  Created by Tibi Hencz on 12.12.2023.
//

#import <Foundation/Foundation.h>
#import <SystemConfiguration/SystemConfiguration.h>

#import "ESScreensaverView.h"

#include "PlatformUtils.h"

bool PlatformUtils::IsInternetReachable()
{
    // Create a reachability reference
    SCNetworkReachabilityRef reachability =
        SCNetworkReachabilityCreateWithName(NULL, "www.apple.com");

    if (reachability != NULL)
    {
        // Flags to store the reachability status
        SCNetworkReachabilityFlags flags;

        // Check the reachability of the target
        if (SCNetworkReachabilityGetFlags(reachability, &flags))
        {
            // Check if the target is reachable and connected via the internet
            BOOL isReachable =
                ((flags & kSCNetworkReachabilityFlagsReachable) != 0);
            BOOL needsConnection =
                ((flags & kSCNetworkReachabilityFlagsConnectionRequired) != 0);
            return (isReachable && !needsConnection);
        }
    }

    return NO;
}

static NSBundle* GetBundle()
{
    return [NSBundle bundleForClass:ESScreensaverView.class];
}

std::string PlatformUtils::GetBuildDate()
{
    NSString* str = [GetBundle() objectForInfoDictionaryKey:@"BUILD_DATE"];
    return str.UTF8String;
}

std::string PlatformUtils::GetGitRevision()
{
    NSString* str = [GetBundle() objectForInfoDictionaryKey:@"REVISION"];
    return str.UTF8String;
}

std::string PlatformUtils::GetAppVersion()
{
    NSString* str = [GetBundle() objectForInfoDictionaryKey:@"CFBundleShortVersionString"];
    return str.UTF8String;
}
