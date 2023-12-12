//
//  PlatformUtils.c
//  e-dream
//
//  Created by Tibi Hencz on 12.12.2023.
//

#import <Foundation/Foundation.h>
#import <SystemConfiguration/SystemConfiguration.h>

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
