#import "WritingToolsSafeControls.h"
#import <objc/runtime.h>

@implementation WritingToolsSafeTextField

- (id)validRequestorForSendType:(NSString *)sendType returnType:(NSString *)returnType {
    // Block all service requests that might trigger Writing Tools
    if (sendType != nil || returnType != nil) {
        return nil;
    }
    
    return [super validRequestorForSendType:sendType returnType:returnType];
}

// Also override the text view's service validation if it has one
- (NSTextView *)currentEditor {
    NSTextView *editor = (NSTextView *)[super currentEditor];
    if (editor) {
        // Create a custom text view that blocks services
        static dispatch_once_t onceToken;
        dispatch_once(&onceToken, ^{
            Method original = class_getInstanceMethod([NSTextView class], 
                                                     @selector(validRequestorForSendType:returnType:));
            Method custom = class_getInstanceMethod([WritingToolsSafeTextField class], 
                                                   @selector(textView_validRequestorForSendType:returnType:));
            if (original && custom) {
                method_exchangeImplementations(original, custom);
            }
        });
    }
    return editor;
}

- (id)textView_validRequestorForSendType:(NSString *)sendType returnType:(NSString *)returnType {
    // This will be called on NSTextView instances due to swizzling
    if (sendType != nil || returnType != nil) {
        return nil;
    }
    // Call the original implementation (which is now swapped)
    return [self textView_validRequestorForSendType:sendType returnType:returnType];
}

@end

@implementation WritingToolsSafeFormCell

+ (void)load {
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        // NSFormCell doesn't implement validRequestorForSendType, so we need to find the parent class that does
        Class targetClass = nil;
        Method originalMethod = nil;
        
        // Check common parent classes
        NSArray *classesToCheck = @[[NSCell class], [NSControl class], [NSView class], [NSResponder class]];
        
        for (Class checkClass in classesToCheck) {
            Method method = class_getInstanceMethod(checkClass, @selector(validRequestorForSendType:returnType:));
            if (method) {
                targetClass = checkClass;
                originalMethod = method;
                break;
            }
        }
        
        if (targetClass && originalMethod) {
            Method safe = class_getInstanceMethod([WritingToolsSafeFormCell class], @selector(validRequestorForSendType:returnType:));
            if (safe) {
                method_exchangeImplementations(originalMethod, safe);
            }
        }
    });
}

- (id)validRequestorForSendType:(NSString *)sendType returnType:(NSString *)returnType {
    // Block all service requests that might trigger Writing Tools
    if (sendType != nil || returnType != nil) {
        return nil;
    }
    
    // This calls the original implementation due to method swizzling
    return [self validRequestorForSendType:sendType returnType:returnType];
}

@end