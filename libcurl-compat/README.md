The libraries in this directory work around ABI changes in packaged
versions of libcurl.

The libcurl in Debian/Ubuntu diverged from upstream between about 2005
and 2018, which unfortunately includes the time at which Steam Runtime
1 'scout' started. Modern versions of libcurl have versioned symbols
attached to a verdef `CURL_OPENSSL_4`, but Debian introduced versioned
symbols before upstream did, and used `CURL_OPENSSL_3`. Unfortunately,
both variants have their SONAME set to `libcurl.so.4`, and the Debian
build was symlinked as both `libcurl.so.4` and `libcurl.so.3`. Newer
versions of Debian have switched to `libcurl.so.4` being the upstream ABI
with verdef `CURL_OPENSSL_4`, which is the same as on other distributions
such as Arch.

The scout branch of the Steam Runtime needs to be compatible with both,
so that we can run scout games (which expect that loading `libcurl.so.4`
will give them the `CURL_OPENSSL_3` ABI), but we can also run tools from
the host system like `valgrind` (which expect that loading `libcurl.so.4`
will give them the `CURL_OPENSSL_4` ABI).

Similarly, the GNUTLS variant of libcurl in Debian/Ubuntu, which exists
for historical licensing reasons, has diverged from the upstream ABI:
modern upstream versions of libcurl have versioned symbols attached to a
verdef `CURL_GNUTLS_4`, but Debian introduced versioned symbols before
upstream did, and used `CURL_GNUTLS_3`. Unlike the OpenSSL variant,
this divergence still exists in current Debian/Ubuntu releases as of 2022.
Again, both variants have their SONAME set to `libcurl-gnutls.so.4`,
and the Debian build is symlinked as both `libcurl-gnutls.so.4` and
`libcurl-gnutls.so.3`. This is less of a problem in practice, because the
GNUTLS build is mainly used within Debian/Ubuntu, with non-Debian-derived
distributions and third-party games both usually preferring `libcurl.so.4`
(built with OpenSSL) rather than `libcurl-gnutls.so.4` (built with
GNUTLS).

The approach taken by the compatibility libraries in this directory is
that the Steam Runtime's `setup.sh` is expected to run the `compat-setup`
tool during container setup. For each of the two known variants of libcurl
(OpenSSL and GNUTLS), and for each x86 word size (`i386` and `x86_64`):

  * If the OS libcurl is compatible with scout's (true in practice for
    Debian GNUTLS, and historically also OpenSSL), then we use the OS
    libcurl as the implementation of both `libcurl*.so.4` and
    `libcurl*.so.3` early in the `LD_LIBRARY_PATH`, ahead of scout's.
    This means we have a libcurl that is newer than scout's, but
    compatible with it.
  * Or, if the OS libcurl is compatible with the upstream ABI (true in
    practice for Arch and modern Debian OpenSSL, for example), then we
    use our shim library `libsteam-runtime-shim-libcurl*.so.4` as the
    implementation of both `libcurl*.so.4` and `libcurl*.so.3`, early in
    the `LD_LIBRARY_PATH`. This is the more complicated part:
      * The `libsteam-runtime-shim library has been linked to think that
        it depends on both `libsteam-runtime-system-libcurl*.so.4` and
        `libsteam-runtime-our-libcurl*.so.4`, *in that order*, using
        the ELF `DT_NEEDED` headers.
        We can't make it depend on `libcurl*.so.4` and/or `libcurl*.so.3`,
        both because those names are ambiguous (we cannot know whether
        they are going to export the upstream ABI or the Debian-specific
        ABI), and because the shim library needs to be able to act as
        the implementation of those names.
      * The shim library has verdefs claiming to export the ABIs of both
        upstream libcurl and scout libcurl.
      * When dependent libraries'/programs' symbols are looked up, glibc
        won't find them in the shim library (because it's empty), but it
        will continue searching the shim library's dependencies.
      * If the dependent library/program wants the upstream ABI
        `CURL_OPENSSL_4`, it will find its symbol in the `-system` library,
        which is a symlink to the real OS libcurl.
      * Or, if the dependent library/program wants the scout-compatible ABI
        `CURL_OPENSSL_3`, it will find its symbol in the `-our` library,
        which is a symlink to scout's libcurl.
      * Or, if the dependent library/program was linked without versioned
        symbols and therefore doesn't express a preference, because of how
        we chose the link order, it will find its symbol in the `-system`
        library.
  * Or, if the OS libcurl is absent or incompatible, then we use scout's
    libcurl unconditionally. This is the same thing we did before the
    introduction of this compatibility mechanism.
