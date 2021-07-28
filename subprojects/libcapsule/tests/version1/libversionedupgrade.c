// Copyright © 2020 Collabora Ltd
// SPDX-License-Identifier: LGPL-2.1-or-later

__attribute__((__visibility__("default"))) int symbol1(int);
int symbol1(int x) { return x; }
__attribute__((__visibility__("default"))) int symbol2(int);
int symbol2(int x) { return x; }
