#pragma once

// Keep the SDK's feature set and ABI. Change only the TLS record content limits.
#if !defined(MBEDTLS_SSL_MAX_CONTENT_LEN) || MBEDTLS_SSL_MAX_CONTENT_LEN != 16384 || \
    defined(MBEDTLS_SSL_IN_CONTENT_LEN) || defined(MBEDTLS_SSL_OUT_CONTENT_LEN)
#error Unexpected Arduino SDK TLS record configuration
#endif
#undef MBEDTLS_SSL_MAX_CONTENT_LEN
#define MBEDTLS_SSL_IN_CONTENT_LEN 16384
#define MBEDTLS_SSL_OUT_CONTENT_LEN 4096
