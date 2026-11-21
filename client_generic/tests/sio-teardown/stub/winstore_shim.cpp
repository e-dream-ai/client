// Windows only, linked into sio_teardown_test by run.py. sio_beast.cpp trusts the
// Windows certificate store and falls back to OpenSSL's default verify paths only if
// loading it fails. Failing it here makes the client honor SSL_CERT_FILE (the test's
// throwaway CA) without a code change. An object file's definition wins over
// libssl.lib's import stub.
#include <openssl/ssl.h>

extern "C" int SSL_CTX_load_verify_store(SSL_CTX*, const char*)
{
    return 0;
}
