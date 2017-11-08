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
              "    const char **nowrap;           %p"  "\n"  \
              "    char **combined_exclude;       %p"  "\n"  \
              "    char **combined_export;        %p"  "\n"  \
              "    char **combined_nowrap;        %p"  "\n"  \
              "  };"                                   "\n"  \
              "  capsule_namespace *ns;           %p"  "\n"  \
              "  {"                                    "\n"  \
              "    Lmid_t      ns;               %ld"  "\n"  \
              "    char        *prefix;           %s"  "\n"  \
              "  };"                                   "\n"  \
              "};"                                     "\n", \
              cap                         ,                  \
              cap->dl_handle              ,                  \
              cap->meta                   ,                  \
              cap->meta->soname           ,                  \
              cap->meta->default_prefix   ,                  \
              cap->meta->exclude          ,                  \
              cap->meta->export           ,                  \
              cap->meta->nowrap           ,                  \
              cap->meta->combined_exclude ,                  \
              cap->meta->combined_export  ,                  \
              cap->meta->combined_nowrap  ,                  \
              cap->ns                     ,                  \
              cap->ns->ns                 ,                  \
              cap->ns->prefix             ); })

static ptr_list *namespaces = NULL;

ptr_list *_capsule_list  = NULL;
dlsymfunc capsule_dl_symbol = NULL;
dlopnfunc capsule_dl_open   = NULL;

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
    ns->nowrap      = ptr_list_alloc( 4 );

    ptr_list_push_ptr( namespaces, ns );

    return ns;
}

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
            cap = xcalloc( 1, sizeof(struct _capsule) );
            cap->ns = get_namespace( meta->default_prefix, meta->soname );
            DEBUG( DEBUG_CAPSULE,
                   "Creating new capsule %p for metadata %p (%s … %s)",
                   cap, meta, cap->ns->prefix, meta->soname );
            cap->meta = meta;
            cap->seen.all  = ptr_list_alloc( 32 );
            cap->seen.some = ptr_list_alloc( 32 );
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
 *
 * Return @list as an array of strings. The strings are not copied, so
 * the result is only valid as long as the strings are.
 *
 * Returns: (transfer container) (array zero-terminated=1) (element-type utf8):
 *  a shallow copy of @list
 */
static char **
cook_list (ptr_list *list)
{
    char **cooked = calloc( list->next + 1, sizeof(char *) );

    for( size_t j = 0; j < list->next; j++ )
        *(cooked + j) = (char *)ptr_list_nth_ptr( list, j );

    *(cooked + list->next) = NULL;

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
add_new_strings_to_ptrlist (ptr_list *list, const char **strings)
{
    for( char **c = (char **)strings; c && *c; c++ )
        ptr_list_add_ptr( list, *c, str_equal );
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

    // merge the string lists for each active prefix:
    // ie all excludes for /host should be in one exclude list,
    // all nowrap entries for /host should bein another list, all
    // excludes for /badgerbadger should be in another, etc etc:
    for( size_t i = 0; i < _capsule_list->next; i++ )
    {
        capsule cap = ptr_list_nth_ptr( _capsule_list, i );

        if( !cap )
            continue;

        add_new_strings_to_ptrlist( cap->ns->exclusions, cap->meta->exclude );
        add_new_strings_to_ptrlist( cap->ns->exports,    cap->meta->export  );
        add_new_strings_to_ptrlist( cap->ns->nowrap,     cap->meta->nowrap  );
    }

    // now squash the meta data ptr_lists into char** that
    // the underlying infrastructure actually uses:
    for( size_t i = 0; i < _capsule_list->next; i++ )
    {
        capsule cap = ptr_list_nth_ptr( _capsule_list, i );

        if( !cap )
            continue;

        free_strv( cap->meta->combined_exclude );
        free_strv( cap->meta->combined_export  );
        free_strv( cap->meta->combined_nowrap  );

        cap->meta->combined_exclude = cook_list( cap->ns->exclusions );
        cap->meta->combined_export  = cook_list( cap->ns->exports );
        cap->meta->combined_nowrap  = cook_list( cap->ns->nowrap );
    }
}

static void __attribute__ ((constructor)) _init_capsule (void)
{
    _capsule_list = ptr_list_alloc( 16 );

    set_debug_flags( secure_getenv("CAPSULE_DEBUG") );

    update_metadata( NULL );

    capsule_dl_symbol = dlsym( RTLD_DEFAULT, "dlsym"  );
    capsule_dl_open   = dlsym( RTLD_DEFAULT, "dlopen" );

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
        DUMP_STRV( excluded, other->meta->combined_exclude );
        DUMP_STRV( exported, other->meta->combined_export  );
        DUMP_STRV( nowrap,   other->meta->combined_nowrap  );
    }

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

    // free+null all the non-static memory in the metadata struct
    CLEAR( free_strv, meta->combined_exclude );
    CLEAR( free_strv, meta->combined_export  );
    CLEAR( free_strv, meta->combined_nowrap  );
    meta->handle = NULL;

    CLEAR( ptr_list_free, cap->seen.all  );
    CLEAR( ptr_list_free, cap->seen.some );

    // poison the capsule struct and free it
    memset( cap, 'X', sizeof(struct _capsule) );
    free( cap );
}
