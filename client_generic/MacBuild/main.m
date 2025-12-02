//
//  main.m
//

#import <Cocoa/Cocoa.h>
#import <Bugsnag/Bugsnag.h>

#ifdef INFINIDREAM_APP_DELEGATE_DEBUG
#import "ESAppDelegate.h"
#endif

int main(int argc, char* argv[])
{
    [Bugsnag start];

#ifdef INFINIDREAM_APP_DELEGATE_DEBUG
    ESAppDelegate *appDelegate = [[ESAppDelegate alloc] init];
    [[NSApplication sharedApplication] setDelegate:appDelegate];
#endif

    return NSApplicationMain(argc, (const char**)argv);
}
