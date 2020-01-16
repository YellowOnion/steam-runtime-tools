__attribute__((__visibility__("default"))) int symbol1(int);
int symbol1(int x) { return x; }
__asm__(".symver _original_symbol2,symbol2@LIBVERSIONED1");
__attribute__((__visibility__("default"))) int _original_symbol2(int);
int _original_symbol2(int x) { return x; }
__asm__(".symver symbol2,symbol2@@LIBVERSIONED2");
__attribute__((__visibility__("default"))) int symbol2(int);
int symbol2(int x) { return x + 1; }
