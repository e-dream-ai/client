//
//  PlatformUtils_Linux.cpp
//  infinidream — Linux platform utilities
//

#include "PlatformUtils.h"
#include "PlatformUtils_Internal.h"
#include "Log.h"

#include <arpa/inet.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <iomanip>
#include <mutex>
#include <netinet/in.h>
#include <openssl/md5.h>
#include <pthread.h>
#include <queue>
#include <sstream>
#include <string>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <thread>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <unistd.h>

// ---------------------------------------------------------------------------
// UI scale factor — set once at display init, read by ImGui dialog code.
// ---------------------------------------------------------------------------
static float g_platformUIScale = 1.0f;

void PlatformUtils_InitUIScale()
{
    // Seed from desktop env vars — set by GNOME/KDE/etc. on HiDPI Wayland sessions.
    // X11 init in DisplayVulkan will later override with the actual DPI value.
    const char* gdk = std::getenv("GDK_SCALE");
    if (gdk)
    {
        float v = std::atof(gdk);
        if (v >= 1.0f) { g_platformUIScale = v; return; }
    }
    const char* qt = std::getenv("QT_SCALE_FACTOR");
    if (qt)
    {
        float v = std::atof(qt);
        if (v >= 1.0f) { g_platformUIScale = v; return; }
    }
}

float PlatformUtils_GetUIScale()
{
    return g_platformUIScale;
}

void PlatformUtils_SetUIScale(float scale)
{
    // Clamp to a sane range; fractional values (e.g. 1.25, 1.5) are valid.
    g_platformUIScale = std::max(1.0f, std::min(scale, 4.0f));
}

// ---------------------------------------------------------------------------
// Internet reachability: is there a default route? (issue #757)
//
// This is called every 2 s from the render thread (RefreshSlowDiagnostics), so it
// must not block. It used to wait for a TCP handshake with 8.8.8.8:53, which
// stalled the render loop 100-370 ms each time, and for 2 s when offline. Like
// SCNetworkReachability on Mac, this asks the kernel whether there is a network
// path, without sending anything. Whether the server is up shows in HTTP and
// WebSocket errors.
// ---------------------------------------------------------------------------
namespace
{
constexpr unsigned kRtfUp = 0x0001;     // RTF_UP
constexpr unsigned kRtfReject = 0x0200; // RTF_REJECT: "unreachable" routes

// /proc/net/route: Iface Destination Gateway Flags RefCnt Use Metric Mask ...
bool HasIPv4DefaultRoute()
{
    std::ifstream f("/proc/net/route");
    std::string line;
    std::getline(f, line); // header
    while (std::getline(f, line))
    {
        std::istringstream ss(line);
        std::string iface, dest, gateway, mask;
        unsigned flags = 0, refcnt = 0, use = 0, metric = 0;
        if (!(ss >> iface >> dest >> gateway >> std::hex >> flags >> std::dec >> refcnt >>
              use >> metric >> mask))
            continue;
        if (iface != "lo" && dest == "00000000" && mask == "00000000" &&
            (flags & kRtfUp) && !(flags & kRtfReject))
            return true;
    }
    return false;
}

// /proc/net/ipv6_route: dest dest_plen src src_plen next_hop metric refcnt use flags iface
bool HasIPv6DefaultRoute()
{
    std::ifstream f("/proc/net/ipv6_route");
    std::string line;
    while (std::getline(f, line))
    {
        std::istringstream ss(line);
        std::string dest, destPlen, src, srcPlen, nextHop, metric, refcnt, use, iface;
        unsigned flags = 0;
        if (!(ss >> dest >> destPlen >> src >> srcPlen >> nextHop >> metric >> refcnt >>
              use >> std::hex >> flags >> iface))
            continue;
        if (iface != "lo" && destPlen == "00" &&
            dest == std::string(32, '0') && (flags & kRtfUp) && !(flags & kRtfReject))
            return true;
    }
    return false;
}
} // namespace

bool PlatformUtils::IsInternetReachable()
{
    return HasIPv4DefaultRoute() || HasIPv6DefaultRoute();
}

// ---------------------------------------------------------------------------
// BuildData.json helpers
// ---------------------------------------------------------------------------
static std::string ReadBuildDataValue(const std::string& key)
{
    // Try next to the executable first, then CWD
    std::string exePath = PlatformUtils::GetAppPath();
    auto pos = exePath.rfind('/');
    std::string dir = (pos != std::string::npos) ? exePath.substr(0, pos + 1) : "./";

    std::ifstream f(dir + "BuildData.json");
    if (!f.is_open())
        f.open("BuildData.json");
    if (!f.is_open())
        return "";

    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());

    // Minimal JSON key lookup: "KEY":"VALUE"
    std::string search = "\"" + key + "\"";
    auto it = content.find(search);
    if (it == std::string::npos)
        return "";
    it += search.size();

    // Skip whitespace and colon
    while (it < content.size() && (content[it] == ' ' || content[it] == ':'))
        ++it;
    if (it >= content.size() || content[it] != '"')
        return "";
    ++it; // skip opening quote

    std::string value;
    while (it < content.size() && content[it] != '"')
        value += content[it++];
    return value;
}

std::string PlatformUtils::GetBuildDate()   { return ReadBuildDataValue("BUILD_DATE"); }
std::string PlatformUtils::GetGitRevision() { return ReadBuildDataValue("REVISION"); }

std::string PlatformUtils::GetAppVersion()
{
    std::string v = ReadBuildDataValue("VERSION");
    return v.empty() ? "0.0.0" : v;
}

// The distro, e.g. "ubuntu 26.04", since the kernel version says little about the
// system. Rolling releases have no VERSION_ID, so that's just "arch". os-release
// values may be quoted; this also keeps anything else out of the HTTP header.
static std::string GetDistro()
{
    std::ifstream f("/etc/os-release");
    if (!f.is_open())
        f.open("/usr/lib/os-release");

    const auto clean = [](std::string value)
    {
        value.erase(std::remove_if(value.begin(), value.end(),
                                   [](unsigned char c)
                                   {
                                       return !std::isalnum(c) && c != '.' &&
                                              c != '_' && c != '-';
                                   }),
                    value.end());
        return value;
    };

    std::string id, versionId, line;
    while (std::getline(f, line))
    {
        if (line.rfind("ID=", 0) == 0)
            id = clean(line.substr(3));
        else if (line.rfind("VERSION_ID=", 0) == 0)
            versionId = clean(line.substr(11));
    }
    if (id.empty())
        return "";
    return versionId.empty() ? id : id + " " + versionId;
}

std::string PlatformUtils::GetPlatformName()
{
    static const std::string name = []
    {
        struct utsname u{};
        std::string result = "Linux";
        if (uname(&u) == 0)
            result += std::string(" ") + u.machine;
        const std::string distro = GetDistro();
        if (!distro.empty())
            result += " " + distro;
        return result;
    }();
    return name;
}

// ---------------------------------------------------------------------------
// App / working directory
// ---------------------------------------------------------------------------
std::string PlatformUtils::GetAppPath()
{
    char buf[4096] = {};
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0)
        buf[len] = '\0';
    return std::string(buf);
}

std::string PlatformUtils::GetWorkingDir()
{
    std::string exe = GetAppPath();
    auto pos = exe.rfind('/');
    return (pos != std::string::npos) ? exe.substr(0, pos + 1) : "./";
}

// ---------------------------------------------------------------------------
// URL, cursor, mouse
// ---------------------------------------------------------------------------
void PlatformUtils::OpenURLExternally(std::string_view _url)
{
    std::string cmd = "xdg-open '";
    cmd += _url;
    cmd += "' &";
    (void)system(cmd.c_str());
}

static bool s_cursorHidden = false;

void PlatformUtils::SetCursorHidden(bool _hidden)
{
    s_cursorHidden = _hidden;
    // Actual X11 cursor manipulation is done in CDisplayVulkan
}

bool PlatformUtils_GetCursorHidden() { return s_cursorHidden; }

static std::function<void(int, int)> s_mouseCallback;

void PlatformUtils::SetOnMouseMovedCallback(std::function<void(int, int)> _callback)
{
    s_mouseCallback = std::move(_callback);
}

std::function<void(int, int)>& PlatformUtils_GetMouseCallback()
{
    return s_mouseCallback;
}

static std::function<std::string()> s_clipboardReader;

void PlatformUtils_SetClipboardReader(std::function<std::string()> reader)
{
    s_clipboardReader = std::move(reader);
}

std::string PlatformUtils_ReadClipboard()
{
    return s_clipboardReader ? s_clipboardReader() : std::string();
}

// ---------------------------------------------------------------------------
// Thread name
// ---------------------------------------------------------------------------
void PlatformUtils::SetThreadName(std::string_view _name)
{
    std::string name(_name);
    if (name.size() > 15)
        name = name.substr(0, 15); // pthread limit
    pthread_setname_np(pthread_self(), name.c_str());
}

// ---------------------------------------------------------------------------
// Main-thread dispatch queue
// ---------------------------------------------------------------------------
static std::mutex s_dispatchMutex;
static std::queue<std::function<void()>> s_dispatchQueue;

void PlatformUtils::DispatchOnMainThread(std::function<void()> _func)
{
    std::lock_guard<std::mutex> lock(s_dispatchMutex);
    s_dispatchQueue.push(std::move(_func));
}

void PlatformUtils_DrainMainThreadQueue()
{
    std::queue<std::function<void()>> local;
    {
        std::lock_guard<std::mutex> lock(s_dispatchMutex);
        std::swap(local, s_dispatchQueue);
    }
    while (!local.empty())
    {
        local.front()();
        local.pop();
    }
}

// ---------------------------------------------------------------------------
// Error reporting
// ---------------------------------------------------------------------------
void PlatformUtils::NotifyError(std::string_view errorMessage)
{
    fprintf(stderr, "[ERROR] %.*s\n",
            static_cast<int>(errorMessage.size()), errorMessage.data());
}

// ---------------------------------------------------------------------------
// MD5 (identical to Mac implementation — OpenSSL)
// ---------------------------------------------------------------------------
std::string PlatformUtils::CalculateFileMD5(const std::string& filepath)
{
    std::ifstream file(filepath, std::ios::binary);
    if (!file)
        return "";

    MD5_CTX context;
    MD5_Init(&context);

    char buffer[4096];
    while (file.read(buffer, sizeof(buffer)))
        MD5_Update(&context, buffer, static_cast<unsigned long>(file.gcount()));
    if (file.gcount() > 0)
        MD5_Update(&context, buffer, static_cast<unsigned long>(file.gcount()));

    unsigned char digest[MD5_DIGEST_LENGTH];
    MD5_Final(digest, &context);

    std::ostringstream ss;
    for (int i = 0; i < MD5_DIGEST_LENGTH; ++i)
        ss << std::hex << std::setw(2) << std::setfill('0')
           << static_cast<int>(digest[i]);
    return ss.str();
}

// ---------------------------------------------------------------------------
// CDelayedDispatch
// ---------------------------------------------------------------------------
CDelayedDispatch::CDelayedDispatch(std::function<void()> _func)
    : m_DispatchTime(0), m_Func(std::move(_func))
{}

void CDelayedDispatch::Cancel()
{
    m_DispatchTime = 0;
}

void CDelayedDispatch::DispatchAfter(uint64_t seconds)
{
    uint64_t dispatchTime = static_cast<uint64_t>(time(nullptr)) + seconds;
    m_DispatchTime        = dispatchTime;

    std::thread([this, dispatchTime, seconds]() {
        std::this_thread::sleep_for(std::chrono::seconds(seconds));
        if (m_DispatchTime == dispatchTime)
            PlatformUtils::DispatchOnMainThread(m_Func);
    }).detach();
}
