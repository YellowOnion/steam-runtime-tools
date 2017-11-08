#include <capsule/capsule.h>
#include "capsule/capsule-private.h"
#include "utils/utils.h"
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

#define DUMP_METADATA(x,m) ({  \
        DEBUG( DEBUG_CAPSULE, "\nMETADATA #%d\n", (int) x ); \
        DEBUG( DEBUG_CAPSULE, "                        \n" \
              "struct _capsule_metadata %p"          "\n"  \
              "{"                                    "\n"  \
              "  Lmid_t namespace;              %ld" "\n"  \
              "  const char  *soname;           %s"  "\n"  \
              "  const char  *default_prefix;   %s"  "\n"  \
              "  char        *active_prefix;    %s"  "\n"  \
              "  const char **exclude;          %p"  "\n"  \
              "  const char **export;           %p"  "\n"  \
              "  const char **nowrap;           %p"  "\n"  \
              "  char **combined_exclude;       %p"  "\n"  \
              "  char **combined_export;        %p"  "\n"  \
              "  char **combined_nowrap;        %p"  "\n"  \
              "};"                                   "\n", \
              m,                                           \
              m->namespace        ,                        \
              m->soname           ,                        \
              m->default_prefix   ,                        \
              m->active_prefix    ,                        \
              m->exclude          ,                        \
              m->export           ,                        \
              m->nowrap           ,                        \
              m->combined_exclude ,                        \
              m->combined_export  ,                        \
              m->combined_nowrap  ); })

typedef struct _capsule_namespace
{
    const char *prefix;
    ptr_list *exclusions;
    ptr_list *exports;
    ptr_list *nowrap;
} capsule_namespace;

static ptr_list *namespaces = NULL;

ptr_list *capsule_manifest  = NULL;
dlsymfunc capsule_dl_symbol = NULL;
dlopnfunc capsule_dl_open   = NULL;

static int ptr_equal (const void *a, const void *b)
{
    return (a == b) ? 1 : 0;
}

static int str_equal (const void *a, const void *b)
{
    if( a == b )
        return 1;

    if( !a || !b )
        return 0;

    return strcmp( (char *)a, (char *)b ) == 0;
}

static capsule_namespace *
get_namespace (const char *prefix)
{
    capsule_namespace *ns;

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
    ns->prefix      = prefix;
    ns->exclusions  = ptr_list_alloc( 4 );
    ns->exports     = ptr_list_alloc( 4 );
    ns->nowrap      = ptr_list_alloc( 4 );

    ptr_list_push_ptr( namespaces, ns );

    return ns;
}

static void
get_capsule_metadata (struct link_map *map, ptr_list *info, const char *only)
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

        meta->namespace = LM_ID_NEWLM;
        meta->closed    = 0;

        if( meta->active_prefix )
            free( meta->active_prefix );

        meta->active_prefix =
          capsule_get_prefix( meta->default_prefix, meta->soname );

        ptr_list_add_ptr( info, meta, ptr_equal );
        DEBUG( DEBUG_CAPSULE, "found metatdata for %s … %s at %p",
               meta->active_prefix, meta->soname, meta );
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
            get_capsule_metadata( m, capsule_manifest, match );

    // merge the string lists for each active prefix:
    // ie all excludes for /host should be in one exclude list,
    // all nowrap entries for /host should bein another list, all
    // excludes for /badgerbadger should be in another, etc etc:
    for( size_t i = 0; i < capsule_manifest->next; i++ )
    {
        capsule_metadata *cm = ptr_list_nth_ptr( capsule_manifest, i );

        if( !cm || cm->closed )
            continue;

        capsule_namespace *ns = get_namespace( cm->active_prefix );

        add_new_strings_to_ptrlist( ns->exclusions, cm->exclude );
        add_new_strings_to_ptrlist( ns->exports,    cm->export  );
        add_new_strings_to_ptrlist( ns->nowrap,     cm->nowrap  );
    }

    // now squash the meta data ptr_lists into char** that
    // the underlying infrastructure actually uses:
    for( size_t i = 0; i < capsule_manifest->next; i++ )
    {
        capsule_metadata *cm = ptr_list_nth_ptr( capsule_manifest, i );

        if( !cm || cm->closed )
            continue;

        capsule_namespace *ns = get_namespace( cm->active_prefix );

        free_strv( cm->combined_exclude );
        free_strv( cm->combined_export  );
        free_strv( cm->combined_nowrap  );

        cm->combined_exclude = cook_list( ns->exclusions );
        cm->combined_export  = cook_list( ns->exports );
        cm->combined_nowrap  = cook_list( ns->nowrap );
    }
}

static void __attribute__ ((constructor)) _init_capsule (void)
{
    capsule_manifest = ptr_list_alloc( 16 );

    set_debug_flags( secure_getenv("CAPSULE_DEBUG") );

    update_metadata( NULL );

    capsule_dl_symbol = dlsym( RTLD_DEFAULT, "dlsym"  );
    capsule_dl_open   = dlsym( RTLD_DEFAULT, "dlopen" );

    if( !(debug_flags & DEBUG_CAPSULE) )
        return;

    for( unsigned int x = 0; x < capsule_manifest->next; x++ )
    {
        capsule_metadata *cm = capsule_manifest->loc[ x ].ptr;

        if( !cm || cm->closed )
            continue;

        const char **soname = cm->exclude;

        DEBUG( DEBUG_CAPSULE, "[%02d] %s metadata:\n", x, cm->soname );

        while( soname && *soname )
            fprintf( stderr, "    %s\n", *(soname++) );
    }
}

char *
capsule_get_prefix (const char *dflt, const char *soname)
{
    char env_var[PATH_MAX] = CAP_ENV_PREFIX;
    const size_t offs = strlen( CAP_ENV_PREFIX );
    size_t x = 0;
    char *prefix = NULL;

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
        return strdup( prefix );
    }

    DEBUG( DEBUG_CAPSULE, "checking %s\n", CAP_ENV_PREFIX "PREFIX" );
    if( (prefix = secure_getenv( CAP_ENV_PREFIX "PREFIX" )) )
    {
        DEBUG( DEBUG_SEARCH, "Capsule prefix is "CAP_ENV_PREFIX"PREFIX: %s",
               prefix );
        return strdup( prefix );
    }

    if( dflt )
    {
        DEBUG( DEBUG_SEARCH, "Capsule prefix is built-in: %s", dflt );
        return strdup( dflt );
    }

    DEBUG( DEBUG_SEARCH, "Capsule prefix is missing" );
    return NULL;
}

static capsule_metadata *
get_cached_metadata (const char *soname)
{
    size_t i = 0;
    capsule_metadata *meta = NULL;

    for( size_t n = 0; n < capsule_manifest->next; n++ )
    {
        capsule_metadata *cm = ptr_list_nth_ptr( capsule_manifest, n );

        if( !cm || cm->closed || strcmp( cm->soname, soname ) )
            continue;

        i = n;
        meta = cm;
        break;
    }

    if(meta)
        DUMP_METADATA(i, meta);

    return meta;
}

capsule
capsule_init (const char *soname)
{
    capsule handle = xcalloc( 1, sizeof(struct _capsule) );
    capsule_metadata *meta = NULL;

    meta = get_cached_metadata( soname );

    if( !meta )
    {
        DEBUG( DEBUG_CAPSULE, "no metadata for %s registered: "
               "may be a dlopened capsule", soname );
        DEBUG( DEBUG_CAPSULE, "updating capsule metadata" );
        update_metadata( soname );
        meta = get_cached_metadata( soname );
    }

    if( meta )
    {
        handle->prefix  = meta->active_prefix;

        for( size_t i = 0; i < capsule_manifest->next; i++ )
        {
            capsule_metadata *cm = ptr_list_nth_ptr( capsule_manifest, i );

            if( !cm || cm->closed )
                continue;

            DEBUG( DEBUG_CAPSULE, " ");
            DUMP_METADATA( i, cm );
            DUMP_STRV( excluded, cm->combined_exclude );
            DUMP_STRV( exported, cm->combined_export  );
            DUMP_STRV( nowrap,   cm->combined_nowrap  );
        }
    }
    // in principle we should be able to make both reloc calls
    // efficient in the same do-not-redo-your-work way, but for
    // some reason the unrestricted relocation breaks if we turn
    // this one on. setting the tracker to NULL disables for now.
    handle->seen.all  = ptr_list_alloc( 32 );
    handle->seen.some = ptr_list_alloc( 32 );

    // poke the initialised metadata back into the handle
    // and vice versa:
    handle->meta = meta;
    meta->handle = handle;

    meta->closed = 0;

    return handle;
}

#define CLEAR(f,x) ({ f( x ); x = NULL; })

void
capsule_close (capsule cap)
{
    capsule_metadata *meta = cap->meta;

    // free+null all the non-static memory in the metadata struct
    CLEAR( free_strv, meta->combined_exclude );
    CLEAR( free_strv, meta->combined_export  );
    CLEAR( free_strv, meta->combined_nowrap  );
    CLEAR( free     , meta->active_prefix    );

    CLEAR( ptr_list_free, cap->seen.all  );
    CLEAR( ptr_list_free, cap->seen.some );
    CLEAR( free         , meta->handle   );

    // flag it as closed just in case something tries to peek at it
    meta->closed = 1;

    // scrub all entries in the manifest pointing to this metadata
    for( size_t n = 0; n < capsule_manifest->next; n++ )
    {
        capsule_metadata *cm = ptr_list_nth_ptr( capsule_manifest, n );

        if( cm == meta )
            capsule_manifest->loc[ n ].ptr = NULL;
    }

}
