#ifndef _SYPHONOUTPUT_H_
#define _SYPHONOUTPUT_H_

#ifdef __OBJC__
#import <Metal/Metal.h>

// Call once per primary-display frame. Starts a SyphonMetal server named
// "infinidream" while settings.player.syphon_output is on (so receivers can
// discover it) and stops it when the setting is off. Returns true only when
// at least one client is attached: the caller should capture and publish this
// frame. With no clients this is just a settings read and a flag check.
bool SyphonOutput_Update(id<MTLDevice> device);
// Publish a frame. Only call after SyphonOutput_Update returned true.
void SyphonOutput_Publish(id<MTLTexture> texture,
                          id<MTLCommandBuffer> commandBuffer);
// True when the server is running and at least one client is attached.
bool SyphonOutput_HasClients(void);
void SyphonOutput_Stop(void);

#endif // __OBJC__

#endif // _SYPHONOUTPUT_H_
