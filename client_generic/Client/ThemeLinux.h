#ifndef THEMELINUX_H
#define THEMELINUX_H

// The system light/dark choice, from the XDG Desktop Portal (org.freedesktop.appearance), which
// GNOME, KDE and other desktops implement. Dark when the desktop doesn't say.
namespace ThemeLinux {

// Starts the background thread that reads the choice and follows changes to it.
void Start();

// Cheap enough to call every frame from the render thread.
bool IsDark();

} // namespace ThemeLinux

#endif // THEMELINUX_H
