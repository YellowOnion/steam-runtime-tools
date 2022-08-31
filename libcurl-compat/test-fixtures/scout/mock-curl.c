/*
 * Copyright © 2021 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 *
 * A stand-in for the copy of libcurl found in scout.
 *
 * To make the behaviour of this mock library consistent with how libcurl
 * will actually behave (across multiple compilers), we need to make sure
 * it imports at least one versioned symbol from glibc.
 *
 * This is because if the library has no version information at all (either
 * imported or exported), dlvsym(handle, symbol, anything) will return the
 * unversioned symbol; but if the library imports at least one versioned
 * symbol, which in practice a non-trivial library always will, then
 * dlvsym() does not have that fallback behaviour. Experimentally, clang
 * emits a call to a versioned glibc function even in trivial libraries,
 * but gcc does not.
 *
 * access() is a convenient symbol to use, because it will also show up
 * in strace output, which is useful if you have lost track of which mock
 * library you're loading.
 */

#include <unistd.h>

__attribute__((__visibility__("default"))) const char *curl_version (void);

const char *
curl_version (void)
{
  (void) access ("scout", F_OK);
  return "mockup of libcurl from scout";
}
