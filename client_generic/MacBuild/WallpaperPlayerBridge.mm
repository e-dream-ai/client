//
//  WallpaperPlayerBridge.mm
//

#import "WallpaperPlayerBridge.h"
#import "MetalDrawableAdapter.h"
#import "ESScreensaver.h"

#include <atomic>

static std::atomic<int> s_activeSessions{0};

@implementation WallpaperPlayerBridge

+ (int)startWithAdapter:(MetalDrawableAdapter *)adapter
                  width:(uint32_t)width
                 height:(uint32_t)height
                preview:(BOOL)preview
{
    if (!adapter || width < 1 || height < 1)
        return -1;

    int displayIdx = ESScreenSaver_AddGraphicsContext((__bridge void *)adapter);
    if (displayIdx < 0)
        return -1;
    if (!ESScreensaver_Start(preview ? true : false, width, height))
        return -1;

    // Always size *this* unit. Start() only Forces unit 0 on first engine
    // bring-up; a secondary Desktop/Settings-preview session must not resize
    // unit 0.
    ESScreensaver_ForceWidthAndHeightForDisplay(displayIdx, width, height);
    s_activeSessions.fetch_add(1);
    return displayIdx;
}

+ (void)doFrame:(int)displayIdx
{
    if (displayIdx < 0)
        return;
    ESScreensaver_DoFrame(displayIdx);
}

+ (void)forceWidth:(uint32_t)width height:(uint32_t)height displayIdx:(int)displayIdx
{
    if (displayIdx < 0 || width < 1 || height < 1)
        return;
    ESScreensaver_ForceWidthAndHeightForDisplay(displayIdx, width, height);
}

+ (void)setVideoRotationDegrees:(uint32_t)degrees
{
    ESScreensaver_SetVideoRotation(degrees);
}

+ (void)stopSessionForDisplay:(int)displayIdx
{
    if (displayIdx >= 0)
        ESScreensaver_RemoveGraphicsContext(displayIdx);

    int prev = s_activeSessions.fetch_sub(1);
    if (prev <= 1) {
        s_activeSessions.store(0);
        ESScreensaver_Stop();
        ESScreensaver_Deinit();
    }
}

@end
