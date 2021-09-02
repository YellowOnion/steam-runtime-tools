/*
 * Copyright © 2021 Collabora Ltd.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "pressure-vessel/missing.h"

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wmissing-noreturn"
#elif defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wsuggest-attribute=noreturn"
#endif

#include "subprojects/bubblewrap/bind-mount.h"
#include "subprojects/bubblewrap/network.h"
#include "subprojects/bubblewrap/utils.h"

#include "subprojects/bubblewrap/bind-mount.c"
#include "subprojects/bubblewrap/network.c"
#include "subprojects/bubblewrap/utils.c"

#include "subprojects/bubblewrap/bubblewrap.c"
