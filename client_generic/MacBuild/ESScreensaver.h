#include <CoreFoundation/CoreFoundation.h>
#include <boost/thread.hpp>
#include <string_view>

#include "base.h"
#include "DisplayOutput.h"

CFBundleRef CopyDLBundle_ex(void);

int ESScreenSaver_AddGraphicsContext(void* _graphicsContext);

bool ESScreensaver_Start(bool _bPreview, uint32_t _width, uint32_t _height);
bool ESScreensaver_DoFrame(int _displayIdx, boost::barrier& _beginFrameBarrier,
                           boost::barrier& _endFrameBarrier);
void ESScreensaver_Stop(void);
bool ESScreensaver_Stopped(void);
void ESScreensaver_ForceWidthAndHeight(uint32_t _width, uint32_t _height);
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

void ESScreensaver_SetUpdateAvailable(const char* verinfo);

size_t ESScreensaver_GetFlockSizeMBs(const char* mp4path, int sheeptype);
