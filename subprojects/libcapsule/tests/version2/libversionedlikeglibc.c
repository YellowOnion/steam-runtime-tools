// Copyright © 2020 Collabora Ltd
// SPDX-License-Identifier: LGPL-2.1-or-later

__attribute__((__visibility__("default"))) int symbol1(int);
int symbol1(int x) { return x; }
__asm__(".symver _original_symbol2,symbol2@LIBVERSIONED1");
__attribute__((__visibility__("default"))) int _original_symbol2(int);
int _original_symbol2(int x) { return x; }
__asm__(".symver _new_symbol2,symbol2@@LIBVERSIONED2");
__attribute__((__visibility__("default"))) int _new_symbol2(int);
int _new_symbol2(int x) { return x + 1; }
