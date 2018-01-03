#include <capsule/capsule.h>
#include "capsule/capsule-private.h"
#include "utils/utils.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <dlfcn.h>
#include <string.h>
#include <ctype.h>
#include <sys/param.h>

#include <unistd.h>

#define CAP_ENV_PREFIX "CAPSULE_"

#define DUMP_STRV(what, css) \
    ({ int _i = 0; for( char **_c = css; _c && *_c; _c++, _i++ ) \
            DEBUG( DEBUG_CAPSULE, "  ->%s[ %02d ]: %s", #what, _i, *_c ); })

#define DUMP_CAPSULE(x, cap) ({  \
        DEBUG( DEBUG_CAPSULE, "\nCAPSULE #%d\n", (int) x ); \
        DEBUG( DEBUG_CAPSULE, "                        \n" \
              "struct _capsule %p"                     "\n"  \
              "{"                                      "\n"  \
              "  void        *dl_handle;          %p"  "\n"  \
              "  capsule_metadata *meta;          %p"  "\n"  \
              "  {"                                    "\n"  \
              "    const char  *soname;           %s"  "\n"  \
              "    const char  *default_prefix;   %s"  "\n"  \
              "    const char **exclude;          %p"  "\n"  \
              "    const char **export;           %p"  "\n"  \
              "  };"                                   "\n"  \
              "  capsule_namespace *ns;           %p"  "\n"  \
              "  {"                                    "\n"  \
              "    Lmid_t      ns;               %ld"  "\n"  \
              "    char        *prefix;           %s"  "\n"  \
              "    char **combined_exclude;       %p"  "\n"  \
              "    char **combined_export;        %p"  "\n"  \
              "  };"                                   "\n"  \
              "};"                                     "\n", \
              cap                         ,                  \
              cap->dl_handle              ,                  \
              cap->meta                   ,                  \
              cap->meta->soname           ,                  \
              cap->meta->default_prefix   ,                  \
              cap->meta->exclude          ,                  \
              cap->meta->export           ,                  \
              cap->ns                     ,                  \
              cap->ns->ns                 ,                  \
              cap->ns->prefix             ,                  \
              cap->ns->combined_exclude   ,                  \
              cap->ns->combined_export);})

/*
 * never_encapsulated:
 *
 * Array of SONAMEs that never have a private copy inside a capsule.
 * Only one copy of each of these will be loaded, and they will always
 * be loaded without respecting the #capsule_namespace's @prefix, even
 * if loaded from inside a capsule.
 *
 * This currently contains the libraries built by the glibc source package.
 */
static const char * const never_encapsulated[] =
{
    "libBrokenLocale.so.1",
    "libanl.so.1",
    "libc.so.6",
    "libcidn.so.1",
    "libcrypt.so.1",
    "libdl.so.2",
    "libm.so.6",
    "libmvec.so.1",
    "libnsl.so.1",
    "libpthread.so.0",
    "libresolv.so.2",
    "librt.so.1",
    "libthread_db.so.1",
    "libutil.so.1",
};

static ptr_list *namespaces = NULL;

ptr_list *_capsule_list  = NULL;
dlsymfunc _capsule_original_dlsym = NULL;
dlopnfunc _capsule_original_dlopen = NULL;

freefunc   _capsule_original_free    = NULL;
mallocfunc _capsule_original_malloc  = NULL;
callocfunc _capsule_original_calloc  = NULL;
rallocfunc _capsule_original_realloc = NULL;
palignfunc _capsule_original_pmalign = NULL;

static int str_equal (const void *a, const void *b)
{
    if( a == b )
        return 1;

    if( !a || !b )
        return 0;

    return strcmp( (char *)a, (char *)b ) == 0;
}

static const char *get_prefix_nocopy (const char *dflt, const char *soname);

static capsule_namespace *
get_namespace (const char *default_prefix, const char *soname)
{
    const char *prefix = get_prefix_nocopy( default_prefix, soname );
    capsule_namespace *ns;

    // Normalize so that libraries with prefix NULL, "" or "/" are all
    // treated as equivalent
    if( prefix == NULL || prefix[0] == '\0' )
        prefix = "/";

    if( namespaces == NULL )
        namespaces = ptr_list_alloc( 4 );

    for( size_t x = 0; x < namespaces->next; x++ )
    {
        ns = ptr_list_nth_ptr( namespaces, x );

        if( !ns || strcmp( prefix, ns->prefix ) )
            continue;

        return ns;
    }

    ns = calloc( 1, sizeof(capsule_namespace) );
    ns->ns          = LM_ID_NEWLM;
    ns->prefix      = strdup( prefix );
    ns->exclusions  = ptr_list_alloc( 4 );
    ns->exports     = ptr_list_alloc( 4 );
    ns->mem         = calloc( 1, sizeof(capsule_memory) );

    ptr_list_push_ptr( namespaces, ns );

    return ns;
}

// items with names starting with '-' are initialised by hand
// and will be skipped in the automatic section below:
static capsule_item alloc_func[] =
{
    { "-dlopen"       , (capsule_addr) NULL                       },
    { "-free"         , (capsule_addr) NULL                       },
    { "-realloc"      , (capsule_addr) NULL                       },
    { "malloc"        , (capsule_addr) &_capsule_original_malloc  },
    { "calloc"        , (capsule_addr) &_capsule_original_calloc  },
    { "posix_memalign", (capsule_addr) &_capsule_original_pmalign },
    { NULL }
};

static void
get_capsule_metadata (struct link_map *map, const char *only)
{
    ElfW(Addr) base = map->l_addr;
    ElfW(Dyn) *dyn  = map->l_ld;
    ElfW(Dyn) *entry;
    ElfW(Sym) *symbol;

    const void *strtab = NULL;
    const void *symtab = NULL;

    if( map->l_name == NULL || *map->l_name == '\0' )
        return;

    for( entry = dyn; entry->d_tag != DT_NULL; entry++ )
    {
        switch( entry->d_tag )
        {
          case DT_SYMTAB:
            symtab = (const void *) fix_addr( (void *)base, entry->d_un.d_ptr );
            break;

          case DT_STRTAB:
            strtab = (const void *) fix_addr( (void *)base, entry->d_un.d_ptr );
            break;

          default:
            break;
        }
    }

    if( !strtab || !symtab )
        return;

    for( symbol = (ElfW(Sym) *)symtab;
         ( (ELFW_ST_TYPE(symbol->st_info) < STT_NUM) &&
           (ELFW_ST_BIND(symbol->st_info) < STB_NUM) );
         symbol++ )
    {
        capsule_metadata *meta = NULL;
        capsule cap = NULL;
        char *name = (char *)strtab + symbol->st_name;

        if( !name || strcmp( "capsule_meta", name ) )
            continue;

        meta = (capsule_metadata *)fix_addr( (void *)base, symbol->st_value );

        // if we were asked for a specific soname's meta data then ignore
        // everything else:
        if( only && strcmp( only, meta->soname ) )
            continue;

        // not a version of the ABI we understand? skip it.
        if( meta->capsule_abi != 0 )
            continue;

        cap = meta->handle;

        if( cap == NULL )
        {
            // Functions we must override in the DSOs
            // inside the capsule (mostly to take account of the fact that
            // they're pulled in from a tree with a filesystem prefix like /host)
            // NOTE: the shim address here isn't used, but we give it the same
            // value as the real function address so it's never accidentally
            // a value the capsule code will care about:
            int i = 0;
            static const capsule_item no_wrapper = { NULL };
            const capsule_item int_dlopen_wrapper =
            {
                "dlopen", (capsule_addr) meta->int_dlopen,
                (capsule_addr) meta->int_dlopen,
            };

            const capsule_item int_free_wrapper =
            {
                "free",
                (capsule_addr) meta->int_free,
                (capsule_addr) meta->int_free,
            };

            cap = xcalloc( 1, sizeof(struct _capsule) );
            cap->ns = get_namespace( meta->default_prefix, meta->soname );
            DEBUG( DEBUG_CAPSULE,
                   "Creating new capsule %p for metadata %p (%s … %s)",
                   cap, meta, cap->ns->prefix, meta->soname );
            cap->meta = meta;
            cap->seen.all  = ptr_list_alloc( 32 );
            cap->seen.some = ptr_list_alloc( 32 );

            cap->internal_wrappers[ 0 ] = int_dlopen_wrapper;
            cap->internal_wrappers[ 1 ] = int_free_wrapper;

            for( i = 2; alloc_func[ i ].name != NULL; i++ )
            {
                capsule_item *capi = &cap->internal_wrappers[ i ];
                void        **slot = (void **) alloc_func[ i ].real;

                capi->name = alloc_func[ i ].name;
                capi->shim = capi->real = (capsule_addr) *slot;
            }

            cap->internal_wrappers[ i ] = no_wrapper;
            meta->handle = cap;
            ptr_list_push_ptr( _capsule_list, cap );
        }

        DEBUG( DEBUG_CAPSULE,
               "found metadata for %s … %s at %p (capsule: %p)",
               cap->ns->prefix, meta->soname, meta, cap );
        break;
    }
}

/**
 * cook_list:
 * @list: a ptr_list containing statically-allocated strings
 * @extras: (array length=n_extras): extra entries to be added
 * @n_extras: number of extra entries
 *
 * Return @list + @extras as an array of strings. The strings are not
 * copied, so the result is only valid as long as the strings are.
 *
 * Returns: (transfer container) (array zero-terminated=1) (element-type utf8):
 *  a shallow copy of everything in either @list or @extras
 */
static char **
cook_list (ptr_list *list,
           const char * const *extras,
           size_t n_extras)
{
    char **cooked = calloc( list->next + n_extras + 1, sizeof(char *) );

    for( size_t j = 0; j < list->next; j++ )
        cooked[j] = (char *)ptr_list_nth_ptr( list, j );

    for( size_t j = 0; j < n_extras; j++ )
        cooked[j + list->next] = (char *)extras[j];

    *(cooked + list->next + n_extras) = NULL;

    return cooked;
}

/**
 * add_new_strings_to_ptrlist:
 * @list: a ptr_list
 * @strings: (transfer none) (array zero-terminated=1) (nullable):
 *
 * Add each string in @strings to @list, unless a string with the same
 * content is already present. The strings must continue to exist as long
 * as @list does.
 */
static void
add_new_strings_to_ptrlist (ptr_list *list, const char * const *strings)
{
    for( const char * const *c = strings; c && *c; c++ )
        ptr_list_add_ptr( list, (char *) *c, str_equal );
}

/**
 * free_strv:
 * @strv: (transfer container) (array zero-terminated=1) (element-type utf8):
 *
 * Free @strv, but not its contents.
 */
static void
free_strv (char **strv)
{
    if( !strv )
        return;

    free( strv );
}

static void update_namespaces (void);

static void
update_metadata (const char *match)
{
    struct link_map *map;
    void *handle;

    handle = dlmopen( LM_ID_BASE, NULL, RTLD_LAZY|RTLD_NOLOAD );
    dlinfo( handle, RTLD_DI_LINKMAP, &map );

    // we're not guaranteed to be at the start of the link map chain:
    while( map->l_prev )
        map = map->l_prev;

    // pick up any capsule metadata we can see:
    // if the soname (match) is NULL, this means _all_ metadata,
    // otherwise just the metadata that is string-equal:
    if (map->l_next)
        for( struct link_map *m = map; m; m = m->l_next )
            get_capsule_metadata( m, match );

    update_namespaces();
}

static void
update_namespaces (void)
{
    if( !namespaces )
        return;

    // wipe out the namespaces' merged lists of exclusions etc. -
    // they contain strings that point into capsule metadata that might
    // no longer be valid, if we dlclosed a shim library
    for( size_t i = 0; i < namespaces->next; i++ )
    {
        capsule_namespace *ns = ptr_list_nth_ptr( namespaces, i );

        if( !ns )
            continue;

        DEBUG( DEBUG_CAPSULE, "Resetting namespace #%zu %p \"%s\"",
               i, ns, ns->prefix );

        // We don't free the actual strings because we don't own them;
        // just truncate the list to 0 entries
        ns->exclusions->next = 0;
        ns->exports->next = 0;
    }

    // merge the string lists for each active prefix:
    // ie all excludes for /host should be in one exclude list,
    // all export entries for /host should bein another list, all
    // excludes for /badgerbadger should be in another, etc etc:
    for( size_t i = 0; i < _capsule_list->next; i++ )
    {
        capsule cap = ptr_list_nth_ptr( _capsule_list, i );

        if( !cap )
            continue;

        DEBUG( DEBUG_CAPSULE,
               "Collecting strings from capsule #%zu %p \"%s\" into namespace "
               "%p \"%s\"",
               i, cap, cap->meta->soname, cap->ns, cap->ns->prefix );

        add_new_strings_to_ptrlist( cap->ns->exclusions, cap->meta->exclude );
        add_new_strings_to_ptrlist( cap->ns->exports,    cap->meta->export  );
    }

    // now squash the meta data ptr_lists into char** that
    // the underlying infrastructure actually uses:
    for( size_t i = 0; i < namespaces->next; i++ )
    {
        capsule_namespace *ns = ptr_list_nth_ptr( namespaces, i );

        if( !ns )
            continue;

        free_strv( ns->combined_exclude );
        free_strv( ns->combined_export  );

        ns->combined_exclude = cook_list( ns->exclusions,
                                          never_encapsulated,
                                          N_ELEMENTS( never_encapsulated ) );
        ns->combined_export  = cook_list( ns->exports, NULL, 0 );
    }
}

static void __attribute__ ((constructor)) _init_capsule (void)
{
    _capsule_list = ptr_list_alloc( 16 );

    set_debug_flags( secure_getenv("CAPSULE_DEBUG") );

    // these are needed if there is > 1 libc instance:
    _capsule_original_free    = dlsym( RTLD_DEFAULT, "free"    );
    _capsule_original_malloc  = dlsym( RTLD_DEFAULT, "malloc"  );
    _capsule_original_calloc  = dlsym( RTLD_DEFAULT, "calloc"  );
    _capsule_original_realloc = dlsym( RTLD_DEFAULT, "realloc" );
    _capsule_original_pmalign = dlsym( RTLD_DEFAULT, "posix_memalign" );

    update_metadata( NULL );

    _capsule_original_dlsym = dlsym( RTLD_DEFAULT, "dlsym"  );
    _capsule_original_dlopen = dlsym( RTLD_DEFAULT, "dlopen" );

    if( !(debug_flags & DEBUG_CAPSULE) )
        return;

    for( unsigned int x = 0; x < _capsule_list->next; x++ )
    {
        capsule cap = _capsule_list->loc[ x ].ptr;

        if( !cap )
            continue;

        const char **soname = cap->meta->exclude;

        DEBUG( DEBUG_CAPSULE, "[%02d] %s metadata:\n", x, cap->meta->soname );

        while( soname && *soname )
            fprintf( stderr, "    %s\n", *(soname++) );
    }
}

char *
capsule_get_prefix (const char *dflt, const char *soname)
{
    const char *prefix = get_prefix_nocopy( dflt, soname );

    if (prefix != NULL)
        return xstrdup( prefix );

    return NULL;
}

static const char *
get_prefix_nocopy (const char *dflt, const char *soname)
{
    char env_var[PATH_MAX] = CAP_ENV_PREFIX;
    const size_t offs = strlen( CAP_ENV_PREFIX );
    size_t x = 0;
    const char *prefix = NULL;

    for( ; (x < strlen(soname)) && (x + offs < PATH_MAX); x++ )
    {
        char c = *(soname + x);

        env_var[ x + offs ] = isalnum( c ) ? toupper( c ) : '_';
    }

    safe_strncpy( &env_var[ x + offs ], "_PREFIX", PATH_MAX - (x + offs + 1) );
    env_var[ PATH_MAX - 1 ] = '\0';

    DEBUG( DEBUG_CAPSULE, "checking %s\n", &env_var[0] );
    if( (prefix = secure_getenv( &env_var[0] )) )
    {
        DEBUG( DEBUG_SEARCH, "Capsule prefix is %s: %s", &env_var[0], prefix );
        return prefix;
    }

    DEBUG( DEBUG_CAPSULE, "checking %s\n", CAP_ENV_PREFIX "PREFIX" );
    if( (prefix = secure_getenv( CAP_ENV_PREFIX "PREFIX" )) )
    {
        DEBUG( DEBUG_SEARCH, "Capsule prefix is "CAP_ENV_PREFIX"PREFIX: %s",
               prefix );
        return prefix;
    }

    if( dflt )
    {
        DEBUG( DEBUG_SEARCH, "Capsule prefix is built-in: %s", dflt );
        return dflt;
    }

    DEBUG( DEBUG_SEARCH, "Capsule prefix is missing" );
    return NULL;
}

static capsule
get_capsule_by_soname (const char *soname)
{
    for( size_t n = 0; n < _capsule_list->next; n++ )
    {
        capsule cap = ptr_list_nth_ptr( _capsule_list, n );

        if( !cap || strcmp( cap->meta->soname, soname ) )
            continue;

        DUMP_CAPSULE(n, cap);
        return cap;
    }

    return NULL;
}

capsule
capsule_init (const char *soname)
{
    capsule cap;
    void *dso;
    int   capsule_errno = 0;
    char *capsule_error = NULL;

    DEBUG( DEBUG_CAPSULE, "Initializing shim library %s", soname );

    cap = get_capsule_by_soname( soname );

    if( !cap )
    {
        DEBUG( DEBUG_CAPSULE, "no metadata for %s registered: "
               "may be a dlopened capsule", soname );
        DEBUG( DEBUG_CAPSULE, "updating capsule metadata" );
        update_metadata( soname );
        cap = get_capsule_by_soname( soname );
    }

    if( !cap )
    {
        fprintf( stderr,
                 "libcapsule: %s: Fatal error: cannot initialize shim "
                 "library (capsule_meta not found)\n",
                 soname );
        abort();
    }

    for( size_t i = 0; i < _capsule_list->next; i++ )
    {
        capsule other = ptr_list_nth_ptr( _capsule_list, i );

        if( !other )
            continue;

        DEBUG( DEBUG_CAPSULE, " ");
        DUMP_CAPSULE( i, other );
        DUMP_STRV( excluded, other->ns->combined_exclude );
        DUMP_STRV( exported, other->ns->combined_export  );
    }

    dso = _capsule_load( cap, cap->internal_wrappers,
                         &capsule_errno, &capsule_error );

    if( !dso )
    {
        fprintf( stderr, "libcapsule: fatal error: %s: %s\n",
                 soname, capsule_error );
        abort();
    }

    int rloc = _capsule_relocate( cap, &capsule_error );

    if( rloc != 0 ) // relocation failed. we're dead.
    {
        fprintf( stderr,
                 "libcapsule: fatal error: %s could not install "
                 "relocations\n",
                 soname );
        abort();
    }

    rloc = _capsule_relocate_dlopen( cap, &capsule_error );
    if( rloc != 0 ) // we may survive this depending on how dlopen is used
       fprintf( stderr,
                "libcapsule: warning: %s could not install dlopen() "
                "dlopen wrappers. This error may or may not be "
                "fatal later.\n",
                soname );

    return cap;
}

#define CLEAR(f,x) ({ f( x ); x = NULL; })

void
capsule_close (capsule cap)
{
    capsule_metadata *meta = cap->meta;

    DEBUG( DEBUG_CAPSULE, "Uninitializing shim library %s", meta->soname );

    // scrub all entries in the manifest pointing to this metadata
    for( size_t n = 0; n < _capsule_list->next; n++ )
    {
        capsule other = ptr_list_nth_ptr( _capsule_list, n );

        if( other == cap )
        {
            _capsule_list->loc[ n ].ptr = NULL;
        }
        else if( other != NULL )
        {
            // There should only be one capsule per capsule_metadata
            assert( other->meta != meta );
        }
    }

    // Remove any pointers from the namespaces into this capsule
    update_namespaces();

    meta->handle = NULL;

    CLEAR( ptr_list_free, cap->seen.all  );
    CLEAR( ptr_list_free, cap->seen.some );

    // poison the capsule struct and free it
    memset( cap, 'X', sizeof(struct _capsule) );
    free( cap );
}
