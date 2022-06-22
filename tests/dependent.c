// Copyright © 2022 Collabora Ltd
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "dependency.h"

__attribute__((__visibility__("default"))) const char *dependent( void );

const char *
dependent( void )
{
    return dependency();
}
