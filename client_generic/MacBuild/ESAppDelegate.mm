#ifdef INFINIDREAM_APP_DELEGATE_DEBUG

#import "ESAppDelegate.h"
#include <AppKit/AppKit.h>
#include <signal.h>
#include <sys/sysctl.h>
#import "ESWindow.h"

@implementation ESAppDelegate
{
    dispatch_source_t _sigtermSource;
    dispatch_source_t _sigintSource;
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification
{
    [[NSProcessInfo processInfo] disableSuddenTermination];
    NSLog(@"Sudden termination disabled");

    // Get the main window from the NIB
    for (NSWindow *window in [[NSApplication sharedApplication] windows]) {
        if ([window isKindOfClass:[ESWindow class]]) {
            self.mainWindow = (ESWindow *)window;
            NSLog(@"ESAppDelegate - mainWindow set: %@", self.mainWindow);
            break;
        }
    }

    // Install Apple Event handler to catch quit events from Dock
    [[NSAppleEventManager sharedAppleEventManager]
        setEventHandler:self
            andSelector:@selector(handleQuitEvent:withReplyEvent:)
          forEventClass:kCoreEventClass
             andEventID:kAEQuitApplication];
    NSLog(@"Apple Event handler installed for kAEQuitApplication");

    // Install GCD signal handlers
    [self setupSignalHandlers];
}

- (void)setupSignalHandlers
{
    // Check if running under debugger
    static bool isDebuggerAttached = false;
    struct kinfo_proc info;
    size_t info_size = sizeof(info);
    int name[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid()};

    if (sysctl(name, 4, &info, &info_size, NULL, 0) == 0) {
        isDebuggerAttached = (info.kp_proc.p_flag & P_TRACED) != 0;
    }

    if (isDebuggerAttached) {
        NSLog(@"⚠️ Debugger detected - signal handlers disabled for development");
        return;
    }

    // Ignore signals - GCD will handle them
    signal(SIGTERM, SIG_IGN);
    signal(SIGINT, SIG_IGN);

    // Create dispatch source for SIGTERM on GLOBAL queue (not main queue!)
    _sigtermSource = dispatch_source_create(DISPATCH_SOURCE_TYPE_SIGNAL,
                                            SIGTERM,
                                            0,
                                            dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0));

    dispatch_source_set_event_handler(_sigtermSource, ^{
        NSLog(@"🚨 SIGTERM received via GCD!");
        [self closeWindow];

        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(1.0 * NSEC_PER_SEC)),
                      dispatch_get_main_queue(), ^{
            NSLog(@"Exiting after SIGTERM cleanup");
            exit(0);
        });
    });

    dispatch_resume(_sigtermSource);

    // Create dispatch source for SIGINT on GLOBAL queue (not main queue!)
    _sigintSource = dispatch_source_create(DISPATCH_SOURCE_TYPE_SIGNAL,
                                           SIGINT,
                                           0,
                                           dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0));

    dispatch_source_set_event_handler(_sigintSource, ^{
        NSLog(@"🚨 SIGINT received via GCD!");
        [self closeWindow];

        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(1.0 * NSEC_PER_SEC)),
                      dispatch_get_main_queue(), ^{
            NSLog(@"Exiting after SIGINT cleanup");
            exit(0);
        });
    });

    dispatch_resume(_sigintSource);

    NSLog(@"✓ GCD signal handlers installed");
}





- (void)closeWindow
{
    NSLog(@"closeWindow called");

    // Stop animation FIRST to set m_isStopped flag and unblock barriers
    if (self.mainWindow && self.mainWindow->mESView) {
        NSLog(@"Stopping animation to unblock barriers...");
        [self.mainWindow->mESView stopAnimation];
    }

    // Close window if visible
    if (self.mainWindow && [self.mainWindow isVisible]) {
        NSLog(@"Closing main window");
        [self.mainWindow close];
    } else {
        NSLog(@"No window to close (window is nil or not visible)");
    }
}

- (void)handleQuitEvent:(NSAppleEventDescriptor *)event withReplyEvent:(NSAppleEventDescriptor *)replyEvent
{
    NSLog(@"handleQuitEvent called!");
    [self closeWindow];

    // Now terminate
    [[NSApplication sharedApplication] terminate:nil];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)__unused theApplication
{
    return NO;
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender
{
    NSLog(@"applicationShouldTerminate called");
    [self closeWindow];
    return NSTerminateNow;
}

- (void)applicationWillTerminate:(NSNotification *)notification
{
    NSLog(@"applicationWillTerminate called");
}



@end


#endif //INFINIDREAM_APP_DELEGATE_DEBUG
