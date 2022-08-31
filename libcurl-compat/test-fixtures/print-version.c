/*
 * Copyright © 2021 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 *
 * Print the version number of whichever libcurl ABI was linked into
 * the executable. This same source file is compiled multiple times
 * with different ABIs.
 */

#include <stdio.h>

const char *curl_version (void);

int main (void)
{
  printf ("%s\n", curl_version ());
  return 0;
}
