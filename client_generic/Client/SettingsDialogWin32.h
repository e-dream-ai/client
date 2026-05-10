#pragma once

#ifdef WIN32

#include <d3d11.h>
#include <Windows.h>

void SettingsDialogWin32_Register();
void SettingsDialogWin32_SetOverlayAllowed(bool allow);
/// Populate all audio-reactive globals from the settings file.
/// Call this whenever a panel that uses those globals becomes visible.
void SettingsDialogWin32_LoadAudioSettings();
bool SettingsDialogWin32_HasPendingOrVisible();
bool SettingsDialogWin32_IsVisible();
void SettingsDialogWin32_Toggle();

/// Close settings without saving and tear down ImGui immediately (for another overlay on the same HWND).
void SettingsDialogWin32_DismissWithoutSaveForExternalOverlay();

/// If ImGui consumed the message, returns true and sets *outResult (return from WndProc).
bool SettingsDialogWin32_TryConsumeWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                           LRESULT* outResult);

/// Draw settings UI on top of the current RTV. Returns true if Present should run even when
/// the main frame did not draw content.
bool SettingsDialogWin32_RenderIfNeeded(ID3D11Device* device, ID3D11DeviceContext* ctx,
                                        ID3D11RenderTargetView* rtv, float viewportW, float viewportH);

/// Close-time 16:9 snap is deferred to the message pump via this WM_APP message because
/// SetWindowPos sends WM_SIZE synchronously, which would invalidate the in-flight RTV that
/// RendererDX11 passes by raw pointer to RenderIfNeeded — see SnapWindowTo16By9IfNeeded.
constexpr UINT kSettingsDialogWin32SnapAfterCloseMsg = WM_APP + 1;
void SettingsDialogWin32_HandleDeferredSnap();

// Audio reactive settings - shared with AudioPanelWin32
extern bool  g_audioReactive;
extern float g_audioBassMult;
extern float g_audioMidMult;
extern float g_audioHighMult;
extern float g_audioPeakDecay;
extern float g_audioPeakFloor;
extern float g_audioDarkBrightness;
extern float g_audioOnsetThreshold;

// FPS
extern bool  g_audioFpsEnabled;
extern float g_audioFpsWeightCentroid;
extern float g_audioFpsWeightMid;
extern float g_audioFpsWeightHigh;
extern float g_audioFpsWeightBass;
extern float g_audioFpsMin;
extern float g_audioFpsMax;
extern float g_audioFpsAttack;
extern float g_audioFpsRelease;
extern bool  g_audioFpsUseCentroid;
extern bool  g_audioFpsUseMid;
extern bool  g_audioFpsUseHigh;
extern bool  g_audioFpsUseBass;

// Cuts
extern bool  g_audioCutTransientEnabled;
extern float g_audioCutTransientThreshold;
extern int   g_audioCutTransientStyle;
extern bool  g_audioCutKickEnabled;
extern float g_audioCutKickThreshold;
extern int   g_audioCutKickStyle;
extern bool  g_audioCutSnareEnabled;
extern float g_audioCutSnareThreshold;
extern int   g_audioCutSnareStyle;
extern bool  g_audioCutBeatEnabled;
extern int   g_audioCutBeatN;
extern int   g_audioCutBeatStyle;
extern bool  g_audioCutVolumeEnabled;
extern float g_audioCutVolumeThreshold;
extern int   g_audioCutVolumeStyle;
extern float g_audioCutGlobalCooldown;
extern float g_audioCutMinVolume;

// Mixing
extern bool  g_audioMixEnabled;
extern int   g_audioMixSource;
extern float g_audioMixMin;
extern float g_audioMixMax;
extern float g_audioMixSmooth;

#endif
