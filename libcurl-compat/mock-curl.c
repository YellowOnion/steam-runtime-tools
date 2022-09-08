/*
 * Copyright © 2021 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 *
 * A mock implementation of libcurl with just about enough libcurl ABI
 * to get a verdef in its ELF headers.
 */

__attribute__((__visibility__("default"))) const char *curl_version (void);

const char *
curl_version (void)
{
  return "mock libcurl used to link the shim library";
}
