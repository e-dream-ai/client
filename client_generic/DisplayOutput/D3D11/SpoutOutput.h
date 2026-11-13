#ifndef _SPOUTOUTPUT_H_
#define _SPOUTOUTPUT_H_

#include <d3d11.h>

// Call once per primary-display frame. Starts a SpoutDX sender named
// "infinidream" while settings.player.spout_output is on (so receivers can
// discover it) and stops it when the setting is off. Returns true when the
// caller should capture and publish this frame. Spout has no hasClients
// signal, so capture runs every frame while the setting is enabled.
bool SpoutOutput_Update(ID3D11Device* device);
// Publish a frame. Only call after SpoutOutput_Update returned true.
void SpoutOutput_Publish(ID3D11Texture2D* texture);
// Stop only if this device owns the global sender (primary renderer teardown).
// Secondary-display renderers share the same process-global Spout state but never
// open it; their destructors must not kill an active primary sender.
void SpoutOutput_Stop(ID3D11Device* device);

#endif // _SPOUTOUTPUT_H_
