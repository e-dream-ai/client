#include <CoreFoundation/CoreFoundation.h>
#include <string_view>

#include "base.h"
#include "DisplayOutput.h"

CFBundleRef CopyDLBundle_ex(void);

int ESScreenSaver_AddGraphicsContext(void* _graphicsContext);

bool ESScreensaver_Start(bool _bPreview, uint32_t _width, uint32_t _height);
bool ESScreensaver_DoFrame(int _displayIdx);
/// Tell the display its screen's refresh rate. Returns 1 if it changed, 0 if not,
/// -1 if the display doesn't exist yet.
int ESScreensaver_SetRefreshRate(int _displayIdx, double _hz);
void ESScreensaver_Stop(void);
void ESScreensaver_Resume(void);
bool ESScreensaver_Stopped(void);
/// Take the instance lock and attach the log (once). The saver calls this from
/// startAnimation, not at bundle load; the app's constructor already did it.
void ESScreensaver_ClaimInstance(void);
/// True when another screensaver process holds the exclusive instance lock,
/// e.g. the extra legacyScreenSaver that opening System Settings spawns.
/// False when we hold it, or when the app holds it (then we play offline).
bool ESScreensaver_IsGhostInstance(void);
void ESScreensaver_ForceWidthAndHeight(uint32_t _width, uint32_t _height);
void ESScreensaver_SetVideoRotation(uint32_t _degrees);
void ESScreensaver_Deinit(void);

void ESScreensaver_AppendKeyEvent(DisplayOutput::CKeyEvent::eKeyCode keyCode);

CFStringRef ESScreensaver_GetVersion(void);

CFStringRef ESScreensaver_CopyGetStringSetting(const char* url,
                                               std::string_view defval);
SInt32 ESScreensaver_GetIntSetting(const char* url, const SInt32 defval);
bool ESScreensaver_GetBoolSetting(const char* url, const bool defval);
double ESScreensaver_GetDoubleSetting(const char* url, const double defval);

void ESScreensaver_SetStringSetting(const char* url, const char* val);
void ESScreensaver_SetIntSetting(const char* url, const SInt32 val);
void ESScreensaver_SetBoolSetting(const char* url, const bool val);
void ESScreensaver_SetDoubleSetting(const char* url, const double val);

void ESScreensaver_SaveSettings(void);

//void ESScreensaver_InitClientStorage(void);
CFStringRef ESScreensaver_CopyGetRoot(void);
//void ESScreensaver_DeinitClientStorage(void);
void ESScreensaver_SetIsFullScreen(bool);

size_t ESScreensaver_GetFlockSizeMBs(const char* mp4path, int sheeptype);

// Update availability functions
void ESScreensaver_SetUpdateAvailable(bool available);
bool ESScreensaver_IsUpdateAvailable(void);
