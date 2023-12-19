//
//  PlatformUtils.c
//  e-dream
//
//  Created by Tibi Hencz on 12.12.2023.
//

#import <Foundation/Foundation.h>
#import <SystemConfiguration/SystemConfiguration.h>

#import "ESScreensaverView.h"

#include <string_view>

#include "Log.h"
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

static std::string GetDictValue(std::string_view key)
{
    static NSDictionary* dict = nil;
    if (dict == nil)
    {
        NSBundle* bundle = [NSBundle bundleForClass:ESScreensaverView.class];
        NSString* path = [bundle pathForResource:@"BuildData" ofType:@"json"];
        NSData* data = [NSData dataWithContentsOfFile:path];
        NSError* error;
        dict = [NSJSONSerialization JSONObjectWithData:data
                                               options:0
                                                 error:&error];
        if (error)
        {
            g_Log->Error("Error deserializing BuildData.json: %s",
                         error.localizedDescription.UTF8String);
            return "";
        }
    }
    NSString* str =
        [dict objectForKey:[NSString stringWithUTF8String:key.data()]];
    if (!str)
        return "";
    return str.UTF8String;
}

std::string PlatformUtils::GetBuildDate() { return GetDictValue("BUILD_DATE"); }

std::string PlatformUtils::GetGitRevision() { return GetDictValue("REVISION"); }

std::string PlatformUtils::GetAppVersion()
{
    NSBundle* bundle = [NSBundle bundleForClass:ESScreensaverView.class];
    NSString* str =
        [bundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"];
    return str.UTF8String;
}
