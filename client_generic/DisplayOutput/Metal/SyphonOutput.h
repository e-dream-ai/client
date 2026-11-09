#ifndef _SYPHONOUTPUT_H_
#define _SYPHONOUTPUT_H_

#ifdef __OBJC__
#import <Metal/Metal.h>

// Publishes a Metal texture to a SyphonMetal server named "infinidream"
// when settings.player.syphon_output is enabled. No-op (and tears down any
// existing server) when disabled.
void SyphonOutput_PublishIfEnabled(id<MTLTexture> texture,
                                   id<MTLCommandBuffer> commandBuffer,
                                   id<MTLDevice> device);
void SyphonOutput_Stop(void);

#endif // __OBJC__

#endif // _SYPHONOUTPUT_H_
