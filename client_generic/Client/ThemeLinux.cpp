#include "ThemeLinux.h"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

#include <dlfcn.h>

namespace {

std::atomic<bool> g_isDark{true};

// libdbus is loaded at runtime so neither the build nor the AppImage depends on it: it's the
// system's copy, and without one there is no portal to ask anyway. These are its stable ABI.
struct DBusConnection;
struct DBusMessage;
struct DBusMessageIter
{
    alignas(void*) char storage[128]; // the library's struct is smaller
};
constexpr int kDBusBusSession = 0;
constexpr int kDBusTypeInvalid = 0;
constexpr int kDBusTypeString = 's';
constexpr int kDBusTypeUint32 = 'u';
constexpr int kDBusTypeVariant = 'v';
constexpr int kReadTimeoutMs = 5000;

constexpr const char* kPortalService = "org.freedesktop.portal.Desktop";
constexpr const char* kPortalPath = "/org/freedesktop/portal/desktop";
constexpr const char* kSettingsInterface = "org.freedesktop.portal.Settings";
constexpr const char* kAppearanceNamespace = "org.freedesktop.appearance";
constexpr const char* kColorSchemeKey = "color-scheme";

struct DBusApi
{
    DBusConnection* (*bus_get_private)(int, void*);
    void (*bus_add_match)(DBusConnection*, const char*, void*);
    void (*connection_set_exit_on_disconnect)(DBusConnection*, unsigned);
    DBusMessage* (*connection_send_with_reply_and_block)(DBusConnection*, DBusMessage*, int, void*);
    unsigned (*connection_read_write)(DBusConnection*, int);
    DBusMessage* (*connection_pop_message)(DBusConnection*);
    DBusMessage* (*message_new_method_call)(const char*, const char*, const char*, const char*);
    unsigned (*message_append_args)(DBusMessage*, int, ...);
    unsigned (*message_is_signal)(DBusMessage*, const char*, const char*);
    void (*message_unref)(DBusMessage*);
    unsigned (*message_iter_init)(DBusMessage*, DBusMessageIter*);
    int (*message_iter_get_arg_type)(DBusMessageIter*);
    void (*message_iter_get_basic)(DBusMessageIter*, void*);
    void (*message_iter_recurse)(DBusMessageIter*, DBusMessageIter*);
    unsigned (*message_iter_next)(DBusMessageIter*);
};

template <typename Fn> bool LoadSymbol(void* lib, const char* name, Fn& fn)
{
    fn = reinterpret_cast<Fn>(dlsym(lib, name));
    return fn != nullptr;
}

bool LoadDBus(DBusApi& api)
{
    void* lib = dlopen("libdbus-1.so.3", RTLD_NOW | RTLD_LOCAL);
    return lib && LoadSymbol(lib, "dbus_bus_get_private", api.bus_get_private) &&
           LoadSymbol(lib, "dbus_bus_add_match", api.bus_add_match) &&
           LoadSymbol(lib, "dbus_connection_set_exit_on_disconnect", api.connection_set_exit_on_disconnect) &&
           LoadSymbol(lib, "dbus_connection_send_with_reply_and_block", api.connection_send_with_reply_and_block) &&
           LoadSymbol(lib, "dbus_connection_read_write", api.connection_read_write) &&
           LoadSymbol(lib, "dbus_connection_pop_message", api.connection_pop_message) &&
           LoadSymbol(lib, "dbus_message_new_method_call", api.message_new_method_call) &&
           LoadSymbol(lib, "dbus_message_append_args", api.message_append_args) &&
           LoadSymbol(lib, "dbus_message_is_signal", api.message_is_signal) &&
           LoadSymbol(lib, "dbus_message_unref", api.message_unref) &&
           LoadSymbol(lib, "dbus_message_iter_init", api.message_iter_init) &&
           LoadSymbol(lib, "dbus_message_iter_get_arg_type", api.message_iter_get_arg_type) &&
           LoadSymbol(lib, "dbus_message_iter_get_basic", api.message_iter_get_basic) &&
           LoadSymbol(lib, "dbus_message_iter_recurse", api.message_iter_recurse) &&
           LoadSymbol(lib, "dbus_message_iter_next", api.message_iter_next);
}

// color-scheme is 1 for dark, 2 for light, 0 for no preference. GNOME's Light style can report 0,
// so there it means light; anywhere else 0 tells us nothing and we stay dark.
bool ColorSchemeIsDark(uint32_t colorScheme)
{
    if (colorScheme == 2)
        return false;
    if (colorScheme == 0)
    {
        const char* desktop = std::getenv("XDG_CURRENT_DESKTOP");
        return !(desktop && std::strstr(desktop, "GNOME"));
    }
    return true;
}

// The value sits inside one variant in SettingChanged and two in a Read reply.
void ApplyColorScheme(const DBusApi& api, DBusMessageIter* iter)
{
    DBusMessageIter inner[3];
    int depth = 0;
    while (api.message_iter_get_arg_type(iter) == kDBusTypeVariant && depth < 3)
    {
        api.message_iter_recurse(iter, &inner[depth]);
        iter = &inner[depth++];
    }
    if (api.message_iter_get_arg_type(iter) != kDBusTypeUint32)
        return;
    uint32_t colorScheme = 0;
    api.message_iter_get_basic(iter, &colorScheme);
    g_isDark.store(ColorSchemeIsDark(colorScheme), std::memory_order_relaxed);
}

void HandleSignal(const DBusApi& api, DBusMessage* message)
{
    DBusMessageIter iter;
    const char* value = nullptr;
    if (!api.message_is_signal(message, kSettingsInterface, "SettingChanged") ||
        !api.message_iter_init(message, &iter) || api.message_iter_get_arg_type(&iter) != kDBusTypeString)
        return;
    api.message_iter_get_basic(&iter, &value);
    if (std::strcmp(value, kAppearanceNamespace) != 0 || !api.message_iter_next(&iter) ||
        api.message_iter_get_arg_type(&iter) != kDBusTypeString)
        return;
    api.message_iter_get_basic(&iter, &value);
    if (std::strcmp(value, kColorSchemeKey) == 0 && api.message_iter_next(&iter))
        ApplyColorScheme(api, &iter);
}

void ReadColorScheme(const DBusApi& api, DBusConnection* connection)
{
    // Read rather than ReadOne, which needs a newer portal than Ubuntu 22.04 has.
    DBusMessage* request = api.message_new_method_call(kPortalService, kPortalPath, kSettingsInterface, "Read");
    if (!request)
        return;
    const char* ns = kAppearanceNamespace;
    const char* key = kColorSchemeKey;
    api.message_append_args(request, kDBusTypeString, &ns, kDBusTypeString, &key, kDBusTypeInvalid);
    DBusMessage* reply = api.connection_send_with_reply_and_block(connection, request, kReadTimeoutMs, nullptr);
    api.message_unref(request);
    if (!reply)
        return;
    DBusMessageIter iter;
    if (api.message_iter_init(reply, &iter))
        ApplyColorScheme(api, &iter);
    api.message_unref(reply);
}

void WatchColorScheme()
{
    DBusApi api;
    if (!LoadDBus(api))
        return;
    DBusConnection* connection = api.bus_get_private(kDBusBusSession, nullptr);
    if (!connection)
        return;
    // libdbus otherwise calls _exit() on the whole app when the session bus goes away.
    api.connection_set_exit_on_disconnect(connection, 0);

    api.bus_add_match(connection, "type='signal',interface='org.freedesktop.portal.Settings',member='SettingChanged'",
                      nullptr);
    ReadColorScheme(api, connection);

    // Keeps the last known answer if the bus goes away.
    while (api.connection_read_write(connection, -1))
    {
        while (DBusMessage* message = api.connection_pop_message(connection))
        {
            HandleSignal(api, message);
            api.message_unref(message);
        }
    }
}

} // namespace

namespace ThemeLinux {

void Start()
{
    static std::once_flag started;
    std::call_once(started, [] { std::thread(WatchColorScheme).detach(); });
}

bool IsDark()
{
    return g_isDark.load(std::memory_order_relaxed);
}

} // namespace ThemeLinux
