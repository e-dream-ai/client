#import "SyphonOutput.h"

#import <Syphon/Syphon.h>

#include "Log.h"
#include "Settings.h"

namespace
{
SyphonMetalServer* g_syphonServer = nil;
id<MTLDevice> g_syphonDevice = nil;

void StopServer()
{
    if (g_syphonServer)
    {
        [g_syphonServer stop];
        g_syphonServer = nil;
        g_syphonDevice = nil;
        g_Log->Info("Syphon server stopped");
    }
}

bool EnsureServer(id<MTLDevice> device)
{
    if (g_syphonServer && g_syphonDevice == device)
        return true;

    StopServer();

    if (!device)
        return false;

    g_syphonServer =
        [[SyphonMetalServer alloc] initWithName:@"infinidream"
                                        device:device
                                       options:nil];
    if (!g_syphonServer)
    {
        g_Log->Error("Failed to create Syphon Metal server");
        return false;
    }

    g_syphonDevice = device;
    g_Log->Info("Syphon server started (infinidream)");
    return true;
}
} // namespace

bool SyphonOutput_Update(id<MTLDevice> device)
{
    if (!g_Settings()->Get("settings.player.syphon_output", false))
    {
        StopServer();
        return false;
    }
    if (!EnsureServer(device))
        return false;
    // Keep the server alive for discovery, but only capture and copy frames
    // when someone is listening (SyphonMetalServer guidance for display-rate
    // publishers).
    return g_syphonServer.hasClients;
}

void SyphonOutput_Publish(id<MTLTexture> texture,
                          id<MTLCommandBuffer> commandBuffer)
{
    if (!g_syphonServer || !texture || !commandBuffer)
        return;

    const NSRect region =
        NSMakeRect(0, 0, (CGFloat)texture.width, (CGFloat)texture.height);
    // Metal framebuffer origin is top-left; Syphon clients expect bottom-left
    // unless flipped is YES.
    [g_syphonServer publishFrameTexture:texture
                        onCommandBuffer:commandBuffer
                            imageRegion:region
                                flipped:YES];
}

bool SyphonOutput_HasClients(void)
{
    return g_syphonServer != nil && g_syphonServer.hasClients;
}

void SyphonOutput_Stop(void)
{
    StopServer();
}
