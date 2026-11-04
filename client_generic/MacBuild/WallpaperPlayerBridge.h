//
//  WallpaperPlayerBridge.h
//  ObjC bridge from wallpaper Swift sessions to ESScreensaver_* player APIs.
//

#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>

@class MetalDrawableAdapter;

NS_ASSUME_NONNULL_BEGIN

@interface WallpaperPlayerBridge : NSObject

/// Start (or attach) the shared engine for one remote surface.
/// Returns display index, or -1 on failure.
+ (int)startWithAdapter:(MetalDrawableAdapter *)adapter
                  width:(uint32_t)width
                 height:(uint32_t)height
                preview:(BOOL)preview;

+ (void)doFrame:(int)displayIdx;
+ (void)forceWidth:(uint32_t)width height:(uint32_t)height displayIdx:(int)displayIdx;
+ (void)setVideoRotationDegrees:(uint32_t)degrees;

/// Stop one session; Deinit engine when the last session ends.
+ (void)stopSession;

@end

NS_ASSUME_NONNULL_END
