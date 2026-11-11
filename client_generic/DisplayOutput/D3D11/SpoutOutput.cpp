#include "SpoutOutput.h"

#include "Log.h"
#include "Player.h"
#include "Settings.h"
#include "base.h"

#include <SpoutDX/SpoutDX.h>

#include <memory>

namespace
{
std::unique_ptr<spoutDX> g_spout;
ID3D11Device* g_spoutDevice = nullptr;

void StopSender()
{
    if (!g_spout)
        return;

    g_spout->ReleaseSender();
    g_spout->CloseDirectX11();
    g_spout.reset();
    g_spoutDevice = nullptr;
    g_Log->Info("Spout sender stopped");
}

bool EnsureSender(ID3D11Device* device)
{
    if (g_spout && g_spoutDevice == device)
        return true;

    StopSender();

    if (!device)
        return false;

    auto spout = std::make_unique<spoutDX>();
    if (!spout->OpenDirectX11(device))
    {
        g_Log->Error("Failed to open Spout DirectX11 device");
        return false;
    }

    // Set the name once before the first SendTexture creates the shared
    // sender. Calling SetSenderName again after registration would rename to
    // infinidream_N because our own sender already exists.
    if (!spout->SetSenderName("infinidream"))
    {
        g_Log->Error("Failed to set Spout sender name");
        spout->CloseDirectX11();
        return false;
    }

    g_spout = std::move(spout);
    g_spoutDevice = device;
    g_Log->Info("Spout sender started (infinidream)");
    return true;
}
} // namespace

bool SpoutOutput_Update(ID3D11Device* device)
{
    if (g_Player().IsScreenSaverRun() ||
        !g_Settings()->Get("settings.player.spout_output", false))
    {
        StopSender();
        return false;
    }

    return EnsureSender(device);
}

void SpoutOutput_Publish(ID3D11Texture2D* texture)
{
    if (!g_spout || !texture)
        return;

    // SendTexture CopyResource/Flush on the shared immediate context.
    std::lock_guard<std::recursive_mutex> d3dLock(GetD3D11ImmediateContextMutex());
    if (!g_spout->SendTexture(texture))
        g_Log->Warning("Spout SendTexture failed");
}

void SpoutOutput_Stop(ID3D11Device* device)
{
    if (!device || g_spoutDevice != device)
        return;
    StopSender();
}
