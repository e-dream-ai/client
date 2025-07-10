#import <Cocoa/Cocoa.h>

// Custom NSTextField that blocks Writing Tools service requests to prevent app hangs
@interface WritingToolsSafeTextField : NSTextField
@end

// Utility class that automatically protects NSFormCell via method swizzling
@interface WritingToolsSafeFormCell : NSFormCell
@end