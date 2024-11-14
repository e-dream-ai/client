//
//  PlatformUtils.c
//  e-dream
//
//  Created by Tibi Hencz on 12.12.2023.
//

#import <Foundation/Foundation.h>
#import <SystemConfiguration/SystemConfiguration.h>
#import <Bugsnag/Bugsnag.h>

#import "ESScreensaverView.h"

#include <string_view>
#include <pthread.h>
#include <CoreFoundation/CoreFoundation.h>

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

std::string PlatformUtils::GetPlatformName() {
    return "native macOS";
}

std::string PlatformUtils::GetWorkingDir() {
    // We have to access the bundle this way so it works in screensaver mode
    NSBundle* bundle = [NSBundle bundleForClass:ESScreensaverView.class];
    NSString* resourcePath = [[bundle resourcePath] stringByAppendingString:@"/"];
    return std::string([resourcePath UTF8String]);
}

void PlatformUtils::SetCursorHidden(bool _hidden)
{
    if (_hidden)
    {
        [NSCursor hide];
    }
    else
    {
        [NSCursor unhide];
    }
}

void PlatformUtils::SetOnMouseMovedCallback(
    std::function<void(int, int)> _callback)
{
    if (_callback)
    {
        // Set up mouse moved event listener using Objective-C
        [NSEvent
            addLocalMonitorForEventsMatchingMask:NSEventMaskMouseMoved
                                         handler:^(NSEvent* event) {
                                             NSPoint mouseLocation =
                                                 [event locationInWindow];
                                             _callback((int)mouseLocation.x,
                                                       (int)mouseLocation.y);
                                             return event;
                                         }];
    }
}

void PlatformUtils::OpenURLExternally(std::string_view _url)
{
    NSString* str = [[NSString alloc] initWithBytes:_url.data()
                                             length:_url.length()
                                           encoding:NSUTF8StringEncoding];
    NSURL* linkURL = [NSURL URLWithString:str];
    [[NSWorkspace sharedWorkspace] openURL:linkURL];
}

void PlatformUtils::SetThreadName(std::string_view _name)
{
    if (_name.end())
        pthread_setname_np(std::string{_name}.data());
    else
        pthread_setname_np(_name.data());
}

void PlatformUtils::DispatchOnMainThread(std::function<void()> _func)
{
    dispatch_async(dispatch_get_main_queue(), ^{
        _func();
    });
}

std::string PlatformUtils::GetAppPath() {
    CFURLRef bundleURL = CFBundleCopyBundleURL(CFBundleGetMainBundle());
    CFStringRef cfPath = CFURLCopyFileSystemPath(bundleURL, kCFURLPOSIXPathStyle);
    std::string path(CFStringGetCStringPtr(cfPath, kCFStringEncodingUTF8));
    
    CFRelease(cfPath);
    CFRelease(bundleURL);
    
    return path;
}


void PlatformUtils::NotifyError(std::string_view errorMessage) {
    NSString *nsErrorMessage = [NSString stringWithUTF8String:errorMessage.data()];

    // Report the error to Bugsnag
    //[Bugsnag notify:[NSException exceptionWithName:@"Error" reason:nsErrorMessage userInfo:nil]];
}


CDelayedDispatch::CDelayedDispatch(std::function<void()> _func)
    : m_DispatchTime(0), m_Func(_func)
{
}

void CDelayedDispatch::Cancel() { m_DispatchTime = 0; }

void CDelayedDispatch::DispatchAfter(uint64_t seconds)
{
    dispatch_time_t thisDispatchTime =
        dispatch_time(DISPATCH_TIME_NOW, (int64_t)(seconds * NSEC_PER_SEC));
    m_DispatchTime = thisDispatchTime;
    dispatch_after(thisDispatchTime, dispatch_get_main_queue(), ^{
        if (m_DispatchTime == thisDispatchTime)
        {
            m_Func();
        }
    });
}

