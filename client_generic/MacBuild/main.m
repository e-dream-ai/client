//
//  main.m
//

#import <Cocoa/Cocoa.h>
#import <Bugsnag/Bugsnag.h>
#include <stdlib.h>

#ifdef INFINIDREAM_APP_DELEGATE_DEBUG
#import "ESAppDelegate.h"
#endif

int main(int argc, char* argv[])
{
    // Test runs (client#733) crash on purpose; keep those out of the production reports.
    if (!getenv("INFINIDREAM_DATA_DIR"))
        [Bugsnag start];

#ifdef INFINIDREAM_APP_DELEGATE_DEBUG
    ESAppDelegate *appDelegate = [[ESAppDelegate alloc] init];
    [[NSApplication sharedApplication] setDelegate:appDelegate];
#endif

    return NSApplicationMain(argc, (const char**)argv);
}
