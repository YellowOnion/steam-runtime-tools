/*
 * Copyright © 2021 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 *
 * A mock implementation of libcurl that exports both the historical
 * Debian ABI and the upstream ABI, at the same time.
 * No known distribution actually does this, but arguably Debian's
 * libcurl-gnutls.so.4 *should* behave like this.
 *
 * To make the behaviour of this mock library consistent with how libcurl
 * will actually behave, we need to import at least one symbol from glibc;
 * see ../scout/mock-curl.c for details.
 */

#include <unistd.h>

__attribute__((__visibility__("default"))) const char *_v3_curl_version (void);
__attribute__((__visibility__("default"))) const char *_v4_curl_version (void);

const char *
_v3_curl_version (void)
{
  (void) access ("both/3", F_OK);
  return "mock system libcurl (CURL_GNUTLS_3 ABI)";
}

const char *
_v4_curl_version (void)
{
  (void) access ("both/4", F_OK);
  return "mock system libcurl (CURL_GNUTLS_4 ABI)";
}

__asm__(".symver _v3_curl_version, curl_version@CURL_GNUTLS_3");
__asm__(".symver _v4_curl_version, curl_version@@CURL_GNUTLS_4");
