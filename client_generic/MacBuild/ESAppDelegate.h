#import <Cocoa/Cocoa.h>

@class ESWindow;

// Signal handler from main.m
#ifdef __cplusplus
extern "C" {
#endif
void signal_handler(int sig);
#ifdef __cplusplus
}
#endif

@interface ESAppDelegate : NSObject <NSApplicationDelegate>

@property (weak, nonatomic) ESWindow *mainWindow;

- (void)closeWindow;

@end