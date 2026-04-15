/* Public-domain MD5; API matches legacy GNU md5_buffer. */
#ifndef CLIENT_GENERIC_COMMON_MD5_H
#define CLIENT_GENERIC_COMMON_MD5_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Compute MD5 of buffer; resblock must hold 16 bytes. */
void md5_buffer(const char *buffer, size_t len, void *resblock);

#ifdef __cplusplus
}
#endif

#endif
