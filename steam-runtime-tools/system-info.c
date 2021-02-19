/*
 * Copyright © 2019 Collabora Ltd.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "steam-runtime-tools/system-info.h"

/* Include these at the beginning so that every backports of
 * G_DEFINE_AUTOPTR_CLEANUP_FUNC will be visible */
#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/json-glib-backports-internal.h"

#include "steam-runtime-tools/architecture.h"
#include "steam-runtime-tools/architecture-internal.h"
#include "steam-runtime-tools/cpu-feature-internal.h"
#include "steam-runtime-tools/desktop-entry-internal.h"
#include "steam-runtime-tools/graphics.h"
#include "steam-runtime-tools/graphics-internal.h"
#include "steam-runtime-tools/json-utils-internal.h"
#include "steam-runtime-tools/library-internal.h"
#include "steam-runtime-tools/locale-internal.h"
#include "steam-runtime-tools/os-internal.h"
#include "steam-runtime-tools/resolve-in-sysroot-internal.h"
#include "steam-runtime-tools/runtime-internal.h"
#include "steam-runtime-tools/steam-internal.h"
#include "steam-runtime-tools/utils-internal.h"
#include "steam-runtime-tools/xdg-portal-internal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <json-glib/json-glib.h>

/**
 * SECTION:system-info
 * @title: System information
 * @short_description: Cached information about the system
 * @include: steam-runtime-tools/steam-runtime-tools.h
 *
 * #SrtSystemInfo is an opaque object representing information about
 * the system. Information is retrieved "lazily"; when it has been
 * retrieved, it is cached until the #SrtSystemInfo is destroyed.
 *
 * This is a reference-counted object: use g_object_ref() and
 * g_object_unref() to manage its lifecycle.
 *
 * The #SrtSystemInfo object is not thread-aware. It should be considered
 * to be "owned" by the thread that created it. Only the thread that
 * "owns" the #SrtSystemInfo may call its methods.
 * Other threads may create their own parallel #SrtSystemInfo object and
 * use that instead, if desired.
 *
 * The majority of the #SrtSystemInfo API involves child processes, and
 * requires `SIGCHLD` to be handled (somehow) by the host process:
 * `SIGCHLD` must not have been ignored (by `sigaction(2)` with `SIG_IGN`)
 * or blocked (for example with `sigprocmask(2)`) in the thread that is
 * acting on the #SrtSystemInfo. If this cannot be guaranteed, please run
 * `steam-runtime-system-info(1)` as a child process and inspect its
 * JSON output instead of calling library functions directly.
 *
 * Ownership can be transferred to other threads by an operation that
 * implies a memory barrier, such as g_atomic_pointer_set() or
 * g_object_ref(), but after this is done the previous owner must not
 * continue to call methods.
 */

typedef enum
{
  TRI_NO = FALSE,
  TRI_YES = TRUE,
  TRI_MAYBE = -1
} Tristate;

struct _SrtSystemInfo
{
  /*< private >*/
  GObject parent;
  /* "" if we have tried and failed to auto-detect */
  gchar *expectations;
  /* Root directory to inspect, usually "/" */
  gchar *sysroot;
  /* Environment variables */
  gchar **env;
  /* Path to find helper executables, or %NULL to use $SRT_HELPERS_PATH
   * or the installed helpers */
  gchar *helpers_path;
  /* Multiarch tuples that are used for helper executables in cases where it
   * shouldn't matter. The first element is considered the primary multiarch. */
  GArray *multiarch_tuples;
  /* If %TRUE, #SrtSystemInfo cannot be changed anymore */
  gboolean immutable_values;
  GHashTable *cached_hidden_deps;
  SrtSteam *steam_data;
  SrtXdgPortal *xdg_portal_data;
  struct
  {
    /* GQuark => MaybeLocale */
    GHashTable *cached_locales;
    SrtLocaleIssues issues;
    gboolean have_issues;
  } locales;
  struct
  {
    /* path != NULL or issues != NONE indicates we have already checked
     * the Steam Runtime */
    gchar *path;
    gchar *expected_version;
    gchar *version;
    SrtRuntimeIssues issues;
  } runtime;
  struct
  {
    GList *egl;
    GList *vulkan;
    gboolean have_egl;
    gboolean have_vulkan;
  } icds;
  struct
  {
    GList *vulkan_explicit;
    GList *vulkan_implicit;
    gboolean have_vulkan_explicit;
    gboolean have_vulkan_implicit;
  } layers;
  struct
  {
    gchar **values;
    gchar **messages;
    gboolean have_data;
  } overrides;
  struct
  {
    gchar **values_32;
    gchar **messages_32;
    gchar **values_64;
    gchar **messages_64;
    gboolean have_data;
  } pinned_libs;
  struct
  {
    SrtContainerType type;
    gchar *host_directory;
    gboolean have_data;
  } container;
  struct
  {
    GList *values;
    gboolean have_data;
  } desktop_entry;
  struct
  {
    SrtX86FeatureFlags x86_features;
    SrtX86FeatureFlags x86_known;
  } cpu_features;
  SrtOsRelease os_release;
  SrtTestFlags test_flags;
  Tristate can_write_uinput;
  /* cached_driver_environment != NULL indicates we have already checked the
   * driver-selection environment variables */
  gchar **cached_driver_environment;
  /* (element-type Abi) */
  GPtrArray *abis;
  int sysroot_fd;
};

struct _SrtSystemInfoClass
{
  /*< private >*/
  GObjectClass parent_class;
};

enum {
  PROP_0,
  PROP_EXPECTATIONS,
  N_PROPERTIES
};

G_DEFINE_TYPE (SrtSystemInfo, srt_system_info, G_TYPE_OBJECT)

typedef struct
{
  SrtLocale *locale;
  GError *error;
} MaybeLocale;

static MaybeLocale *
maybe_locale_new_positive (SrtLocale *locale)
{
  MaybeLocale *self;

  g_return_val_if_fail (SRT_IS_LOCALE (locale), NULL);

  self = g_slice_new0 (MaybeLocale);
  self->locale = g_object_ref (locale);
  return self;
}

static MaybeLocale *
maybe_locale_new_negative (GError *error)
{
  MaybeLocale *self;

  g_return_val_if_fail (error != NULL, NULL);

  self = g_slice_new0 (MaybeLocale);
  self->error = g_error_copy (error);
  return self;
}

static void
maybe_locale_free (gpointer p)
{
  MaybeLocale *self = p;

  g_clear_object (&self->locale);
  g_clear_error (&self->error);
  g_slice_free (MaybeLocale, self);
}

typedef struct
{
  GList *modules;
  gboolean available;
} ModuleList;

typedef struct
{
  GQuark multiarch_tuple;
  const SrtKnownArchitecture *known_architecture;
  gchar *runtime_linker_resolved;
  GError *runtime_linker_error;
  Tristate can_run;
  GHashTable *cached_results;
  SrtLibraryIssues cached_combined_issues;
  gboolean libraries_cache_available;

  GHashTable *cached_graphics_results;
  SrtGraphicsIssues cached_combined_graphics_issues;
  gboolean graphics_cache_available;

  ModuleList graphics_modules[NUM_SRT_GRAPHICS_MODULES];
} Abi;

static Abi *
ensure_abi_unless_immutable (SrtSystemInfo *self,
                             const char *multiarch_tuple)
{
  const SrtKnownArchitecture *iter;
  GQuark quark;
  guint i;
  Abi *abi = NULL;

  quark = g_quark_from_string (multiarch_tuple);

  for (i = 0; i < self->abis->len; i++)
    {
      abi = g_ptr_array_index (self->abis, i);

      if (abi->multiarch_tuple == quark)
        return abi;
    }

  if (self->immutable_values)
    return NULL;

  abi = g_slice_new0 (Abi);
  abi->multiarch_tuple = quark;

  for (iter = _srt_architecture_get_known ();
       iter->multiarch_tuple != NULL;
       iter++)
    {
      if (strcmp (iter->multiarch_tuple, multiarch_tuple) == 0)
        {
          abi->known_architecture = iter;
          break;
        }
    }

  abi->can_run = TRI_MAYBE;
  abi->cached_results = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
  abi->cached_combined_issues = SRT_LIBRARY_ISSUES_NONE;
  abi->libraries_cache_available = FALSE;
  abi->cached_graphics_results = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_object_unref);
  abi->cached_combined_graphics_issues = SRT_GRAPHICS_ISSUES_NONE;

  for (i = 0; i < G_N_ELEMENTS (abi->graphics_modules); i++)
    {
      abi->graphics_modules[i].modules = NULL;
      abi->graphics_modules[i].available = FALSE;
    }

  /* transfer ownership to self->abis */
  g_ptr_array_add (self->abis, abi);
  return abi;
}

static void
abi_free (gpointer self)
{
  Abi *abi = self;
  gsize i;

  if (abi->cached_results != NULL)
    g_hash_table_unref (abi->cached_results);

  if (abi->cached_graphics_results != NULL)
    g_hash_table_unref (abi->cached_graphics_results);

  for (i = 0; i < G_N_ELEMENTS (abi->graphics_modules); i++)
    g_list_free_full (abi->graphics_modules[i].modules, g_object_unref);

  g_free (abi->runtime_linker_resolved);
  g_clear_error (&abi->runtime_linker_error);
  g_slice_free (Abi, self);
}

static void
srt_system_info_init (SrtSystemInfo *self)
{
  GQuark primary;
  if (strcmp (_SRT_MULTIARCH, "") == 0)
    /* This won't *work* but at least it's non-empty... */
    primary = g_quark_from_static_string ("UNKNOWN");
  else
    primary = g_quark_from_static_string (_SRT_MULTIARCH);

  self->multiarch_tuples = g_array_sized_new (TRUE, TRUE, sizeof (GQuark), 1);
  g_array_prepend_val (self->multiarch_tuples, primary);

  self->can_write_uinput = TRI_MAYBE;
  self->sysroot_fd = -1;

  /* Assume that in practice we will usually add two ABIs: amd64 and i386 */
  self->abis = g_ptr_array_new_full (2, abi_free);

  _srt_os_release_init (&self->os_release);

  self->container.type = SRT_CONTAINER_TYPE_UNKNOWN;

  srt_system_info_set_sysroot (self, "/");
  srt_system_info_set_environ (self, NULL);
}

static void
srt_system_info_get_property (GObject *object,
                              guint prop_id,
                              GValue *value,
                              GParamSpec *pspec)
{
  SrtSystemInfo *self = SRT_SYSTEM_INFO (object);

  switch (prop_id)
    {
      case PROP_EXPECTATIONS:
        g_value_set_string (value, self->expectations);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_system_info_set_property (GObject *object,
                              guint prop_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
  SrtSystemInfo *self = SRT_SYSTEM_INFO (object);

  switch (prop_id)
    {
      case PROP_EXPECTATIONS:
        /* Construct-only */
        g_return_if_fail (self->expectations == NULL);
        self->expectations = g_value_dup_string (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
forget_desktop_entries (SrtSystemInfo *self)
{
  self->desktop_entry.have_data = FALSE;
  g_list_free_full (self->desktop_entry.values, g_object_unref);
  self->desktop_entry.values = NULL;
}

/*
 * Forget any cached information about container information.
 */
static void
forget_container_info (SrtSystemInfo *self)
{
  g_clear_pointer (&self->container.host_directory, g_free);
  self->container.type = SRT_CONTAINER_TYPE_UNKNOWN;
  self->container.have_data = FALSE;
}

/*
 * Forget any cached information about locales.
 */
static void
forget_locales (SrtSystemInfo *self)
{
  g_clear_pointer (&self->locales.cached_locales, g_hash_table_unref);
  self->locales.issues = SRT_LOCALE_ISSUES_NONE;
  self->locales.have_issues = FALSE;
}

/*
 * Forget any cached information about the Steam Runtime.
 */
static void
forget_runtime (SrtSystemInfo *self)
{
  g_clear_pointer (&self->runtime.path, g_free);
  g_clear_pointer (&self->runtime.version, g_free);
  g_clear_pointer (&self->runtime.expected_version, g_free);
  self->runtime.issues = SRT_RUNTIME_ISSUES_NONE;
}

/*
 * Forget any cached information about the OS.
 */
static void
forget_os (SrtSystemInfo *self)
{
  _srt_os_release_clear (&self->os_release);
  forget_runtime (self);
}

/*
 * Forget any cached information about the Steam installation.
 */
static void
forget_steam (SrtSystemInfo *self)
{
  forget_runtime (self);
  if (self->steam_data != NULL)
    g_object_unref (self->steam_data);
  self->steam_data = NULL;
}

/*
 * Forget any cached information about ICDs.
 */
static void
forget_icds (SrtSystemInfo *self)
{
  self->icds.have_egl = FALSE;
  g_list_free_full (self->icds.egl, g_object_unref);
  self->icds.egl = NULL;
  self->icds.have_vulkan = FALSE;
  g_list_free_full (self->icds.vulkan, g_object_unref);
  self->icds.vulkan = NULL;
}

/*
 * Forget any cached information about layers.
 */
static void
forget_layers (SrtSystemInfo *self)
{
  self->layers.have_vulkan_explicit = FALSE;
  self->layers.have_vulkan_implicit = FALSE;
  g_list_free_full (self->layers.vulkan_explicit, g_object_unref);
  self->layers.vulkan_explicit = NULL;
  g_list_free_full (self->layers.vulkan_implicit, g_object_unref);
  self->layers.vulkan_implicit = NULL;
}

/*
 * Forget any cached information about overrides.
 */
static void
forget_overrides (SrtSystemInfo *self)
{
  g_clear_pointer (&self->overrides.values, g_strfreev);
  g_clear_pointer (&self->overrides.messages, g_strfreev);
  self->overrides.have_data = FALSE;
}

/*
 * Forget any cached information about pinned libraries.
 */
static void
forget_pinned_libs (SrtSystemInfo *self)
{
  g_clear_pointer (&self->pinned_libs.values_32, g_strfreev);
  g_clear_pointer (&self->pinned_libs.messages_32, g_strfreev);
  g_clear_pointer (&self->pinned_libs.values_64, g_strfreev);
  g_clear_pointer (&self->pinned_libs.messages_64, g_strfreev);
  self->pinned_libs.have_data = FALSE;
}

/*
 * Forget any cached information about xdg portals.
 */
static void
forget_xdg_portal (SrtSystemInfo *self)
{
  g_clear_object (&self->xdg_portal_data);
}

static void
srt_system_info_finalize (GObject *object)
{
  SrtSystemInfo *self = SRT_SYSTEM_INFO (object);

  forget_desktop_entries (self);
  forget_container_info (self);
  forget_icds (self);
  forget_layers (self);
  forget_locales (self);
  forget_os (self);
  forget_overrides (self);
  forget_pinned_libs (self);
  forget_runtime (self);
  forget_steam (self);
  forget_xdg_portal (self);

  g_clear_pointer (&self->abis, g_ptr_array_unref);
  g_free (self->expectations);
  g_free (self->helpers_path);
  g_free (self->sysroot);
  glnx_close_fd (&self->sysroot_fd);
  g_strfreev (self->env);
  g_clear_pointer (&self->cached_driver_environment, g_strfreev);
  if (self->cached_hidden_deps)
    g_hash_table_unref (self->cached_hidden_deps);

  G_OBJECT_CLASS (srt_system_info_parent_class)->finalize (object);
}

static GParamSpec *properties[N_PROPERTIES] = { NULL };

static void
srt_system_info_class_init (SrtSystemInfoClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->get_property = srt_system_info_get_property;
  object_class->set_property = srt_system_info_set_property;
  object_class->finalize = srt_system_info_finalize;

  properties[PROP_EXPECTATIONS] =
    g_param_spec_string ("expectations", "Expectations",
                         "Path to a directory containing information "
                         "about the properties we expect the system "
                         "to have, or NULL if unknown",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPERTIES, properties);
}

/**
 * srt_system_info_new:
 * @expectations: (nullable) (type filename): Path to a directory
 *  containing details of the state that the system is expected to have
 *
 * Return a new #SrtSystemInfo.
 *
 * The @expectations directory should contain a subdirectory for each
 * supported CPU architecture, named for the multiarch tuple as printed
 * by `gcc -print-multiarch` in the Steam Runtime (in practice this means
 * %SRT_ABI_I386 or %SRT_ABI_X86_64).
 *
 * The per-architecture directories may contain files whose names end with
 * `.symbols`. Those files are interpreted as describing libraries that
 * the runtime environment should support, in
 * [deb-symbols(5)](https://manpages.debian.org/deb-symbols.5) format.
 *
 * Returns: (transfer full): A new #SrtSystemInfo. Free with g_object_unref()
 */
SrtSystemInfo *
srt_system_info_new (const char *expectations)
{
  g_return_val_if_fail (_srt_check_not_setuid (), NULL);
  g_return_val_if_fail ((expectations == NULL ||
                         g_file_test (expectations, G_FILE_TEST_IS_DIR)),
                        NULL);
  return g_object_new (SRT_TYPE_SYSTEM_INFO,
                       "expectations", expectations,
                       NULL);
}

static gchar ** _srt_system_info_driver_environment_from_report (JsonObject *json_obj);
static SrtContainerType _srt_system_info_get_container_info_from_report (JsonObject *json_obj,
                                                                         gchar **host_path);
static gchar ** _srt_system_info_get_pinned_libs_from_report (JsonObject *json_obj,
                                                              const gchar *which,
                                                              gchar ***messages);

static void
get_runtime_linker_from_report (SrtSystemInfo *self,
                                Abi *abi,
                                JsonObject *json_arch_obj)
{
  JsonObject *runtime_linker_obj;
  const char *path;
  const char *resolved;

  g_return_if_fail (abi->runtime_linker_resolved == NULL);
  g_return_if_fail (abi->runtime_linker_error == NULL);

  if (abi->known_architecture == NULL)
    return;

  if (abi->known_architecture->interoperable_runtime_linker == NULL)
    return;

  if (!json_object_has_member (json_arch_obj, "runtime-linker"))
    return;

  runtime_linker_obj = json_object_get_object_member (json_arch_obj, "runtime-linker");

  path = json_object_get_string_member_with_default (runtime_linker_obj,
                                                     "path", NULL);
  resolved = json_object_get_string_member_with_default (runtime_linker_obj,
                                                         "resolved", NULL);

  if (json_object_has_member (runtime_linker_obj, "error"))
    {
      GQuark error_domain;
      int error_code;
      const char *error_message;

      error_domain = g_quark_from_string (json_object_get_string_member_with_default (runtime_linker_obj, "error-domain", NULL));
      error_code = json_object_get_int_member_with_default (runtime_linker_obj,
                                                            "error-code",
                                                            -1);
      error_message = json_object_get_string_member_with_default (runtime_linker_obj,
                                                                  "error",
                                                                  NULL);

      if (error_domain == 0)
        {
          error_domain = SRT_ARCHITECTURE_ERROR;
          error_code = SRT_ARCHITECTURE_ERROR_INTERNAL_ERROR;
        }

      if (error_message == NULL)
        error_message = "Unknown error";

      g_set_error_literal (&abi->runtime_linker_error, error_domain, error_code,
                           error_message);
    }
  else if (path == NULL)
    {
      /* no information */
      return;
    }
  else if (g_strcmp0 (path, abi->known_architecture->interoperable_runtime_linker) != 0)
    {
      g_set_error (&abi->runtime_linker_error, SRT_ARCHITECTURE_ERROR,
                   SRT_ARCHITECTURE_ERROR_INTERNAL_ERROR,
                   "Expected \"%s\" in report, but got \"%s\"",
                   abi->known_architecture->interoperable_runtime_linker,
                   path);
    }
  else
    {
      abi->runtime_linker_resolved = g_strdup (resolved);
    }
}

/**
 * srt_system_info_new_from_json:
 * @path: (not nullable) (type filename): Path to a JSON report
 * @error: Used to raise an error on failure
 *
 * Return a new #SrtSystemInfo with the info parsed from an existing JSON
 * report.
 * The #SrtSystemInfo will be immutable: whatever information was in the JSON
 * report, that will be the only information that is available.
 *
 * Returns: (transfer full): A new #SrtSystemInfo. Free with g_object_unref()
 */
SrtSystemInfo *
srt_system_info_new_from_json (const char *path,
                               GError **error)
{
  static const char * const no_strings[] = { NULL };
  SrtSystemInfo *info = NULL;
  JsonObject *json_obj = NULL;
  JsonObject *json_sub_obj = NULL;
  JsonParser *parser = NULL;
  JsonNode *node = NULL;

  g_return_val_if_fail (_srt_check_not_setuid (), NULL);
  g_return_val_if_fail (path != NULL, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  info = srt_system_info_new (NULL);
  srt_system_info_set_environ (info, (gchar * const *) no_strings);

  parser = json_parser_new ();

  if (!json_parser_load_from_file (parser, path, error))
    {
      g_clear_object (&info);
      goto out;
    }

  node = json_parser_get_root (parser);
  if (node == NULL || !JSON_NODE_HOLDS_OBJECT (node))
    {
      if (error)
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "Expected to find a JSON object in the provided JSON");
      g_clear_object (&info);
      goto out;
    }

  json_obj = json_node_get_object (node);

  info->can_write_uinput = json_object_get_boolean_member_with_default (json_obj,
                                                                        "can-write-uinput",
                                                                        FALSE);

  info->steam_data = _srt_steam_get_from_report (json_obj);

  info->runtime.issues = SRT_RUNTIME_ISSUES_UNKNOWN;
  if (json_object_has_member (json_obj, "runtime"))
    {
      json_sub_obj = json_object_get_object_member (json_obj, "runtime");

      info->runtime.path = g_strdup (json_object_get_string_member_with_default (json_sub_obj,
                                                                                 "path",
                                                                                 NULL));

      info->runtime.version = g_strdup (json_object_get_string_member_with_default (json_sub_obj,
                                                                                    "version",
                                                                                    NULL));

      info->pinned_libs.have_data = TRUE;

      info->runtime.issues = _srt_runtime_get_issues_from_report (json_sub_obj);

      info->pinned_libs.values_32 = _srt_system_info_get_pinned_libs_from_report (json_sub_obj,
                                                                                  "pinned_libs_32",
                                                                                  &info->pinned_libs.messages_32);
      info->pinned_libs.values_64 = _srt_system_info_get_pinned_libs_from_report (json_sub_obj,
                                                                                  "pinned_libs_64",
                                                                                  &info->pinned_libs.messages_64);
    }

  _srt_os_release_populate_from_report (json_obj, &info->os_release);

  info->container.type = _srt_system_info_get_container_info_from_report (json_obj,
                                                                          &info->container.host_directory);

  info->cached_driver_environment = _srt_system_info_driver_environment_from_report (json_obj);

  if (json_object_has_member (json_obj, "architectures"))
    {
      JsonObject *json_arch_obj = NULL;
      GList *multiarch_tuples = NULL;
      json_sub_obj = json_object_get_object_member (json_obj, "architectures");

      multiarch_tuples = json_object_get_members (json_sub_obj);
      for (GList *l = multiarch_tuples; l != NULL; l = l->next)
        {
          Abi *abi = NULL;

          if (!json_object_has_member (json_sub_obj, l->data))
            continue;

          json_arch_obj = json_object_get_object_member (json_sub_obj, l->data);

          abi = ensure_abi_unless_immutable (info, l->data);

          abi->can_run = _srt_architecture_can_run_from_report (json_arch_obj);

          abi->libraries_cache_available = TRUE;
          abi->cached_combined_issues = _srt_library_get_issues_from_report (json_arch_obj);

          get_runtime_linker_from_report (info, abi, json_arch_obj);

          _srt_graphics_get_from_report (json_arch_obj,
                                         l->data,
                                         &abi->cached_graphics_results);

          abi->graphics_modules[SRT_GRAPHICS_DRI_MODULE].modules = _srt_dri_driver_get_from_report (json_arch_obj);
          abi->graphics_modules[SRT_GRAPHICS_DRI_MODULE].available = TRUE;

          abi->graphics_modules[SRT_GRAPHICS_VAAPI_MODULE].modules = _srt_va_api_driver_get_from_report (json_arch_obj);
          abi->graphics_modules[SRT_GRAPHICS_VAAPI_MODULE].available = TRUE;

          abi->graphics_modules[SRT_GRAPHICS_VDPAU_MODULE].modules = _srt_vdpau_driver_get_from_report (json_arch_obj);
          abi->graphics_modules[SRT_GRAPHICS_VDPAU_MODULE].available = TRUE;

          abi->graphics_modules[SRT_GRAPHICS_GLX_MODULE].modules = _srt_glx_icd_get_from_report (json_arch_obj);
          abi->graphics_modules[SRT_GRAPHICS_GLX_MODULE].available = TRUE;
        }
    }

  info->locales.have_issues = TRUE;
  info->locales.issues = _srt_locale_get_issues_from_report (json_obj);

  if (info->locales.cached_locales == NULL)
    info->locales.cached_locales = g_hash_table_new_full (NULL, NULL, NULL,
                                                          maybe_locale_free);

  if (json_object_has_member (json_obj, "locales"))
    {
      JsonObject *json_locale_obj = NULL;
      GList *locales_members = NULL;
      json_sub_obj = json_object_get_object_member (json_obj, "locales");

      locales_members = json_object_get_members (json_sub_obj);
      for (GList *l = locales_members; l != NULL; l = l->next)
        {
          SrtLocale *locale = NULL;
          MaybeLocale *maybe;
          const gchar *requested_name = NULL;
          GError *locale_error = NULL;
          json_locale_obj = json_object_get_object_member (json_sub_obj, l->data);

          requested_name = g_strcmp0 (l->data, "<default>") == 0 ? "" : l->data;

          locale = _srt_locale_get_locale_from_report (json_locale_obj, l->data, &locale_error);
          if (locale != NULL)
            {
              maybe = maybe_locale_new_positive (locale);
              g_object_unref (locale);
            }
          else
            {
              maybe = maybe_locale_new_negative (locale_error);
              g_clear_error (&locale_error);
            }

          g_hash_table_replace (info->locales.cached_locales,
                                GUINT_TO_POINTER (g_quark_from_string (requested_name)),
                                maybe);
        }
    }

  info->icds.have_egl = TRUE;
  info->icds.egl = _srt_get_egl_from_json_report (json_obj);

  info->icds.have_vulkan = TRUE;
  info->icds.vulkan = _srt_get_vulkan_from_json_report (json_obj);

  info->layers.have_vulkan_explicit = TRUE;
  info->layers.vulkan_explicit = _srt_get_explicit_vulkan_layers_from_json_report (json_obj);
  info->layers.have_vulkan_implicit = TRUE;
  info->layers.vulkan_implicit = _srt_get_implicit_vulkan_layers_from_json_report (json_obj);

  info->desktop_entry.have_data = TRUE;
  info->desktop_entry.values = _srt_get_steam_desktop_entries_from_json_report (json_obj);

  info->xdg_portal_data = _srt_xdg_portal_get_info_from_report (json_obj);

  info->cpu_features.x86_features = _srt_feature_get_x86_flags_from_report (json_obj,
                                                                            &info->cpu_features.x86_known);

  info->immutable_values = TRUE;

out:
  if (parser != NULL)
    g_object_unref (parser);

  return info;
}

/**
 * srt_system_info_can_run:
 * @self: A #SrtSystemInfo object
 * @multiarch_tuple: A multiarch tuple defining an ABI, as printed
 *  by `gcc -print-multiarch` in the Steam Runtime
 *
 * Check whether an executable for the given ABI can be run.
 *
 * For this check (and all similar checks) to work as intended, the
 * contents of the `libsteam-runtime-tools-0-helpers:i386` package must
 * be available in the same directory hierarchy as the
 * `libsteam-runtime-tools-0` shared library, something like this:
 *
 * |[
 * any directory/
 *      lib/
 *          x86_64-linux-gnu/
 *              libsteam-runtime-tools-0.so.0
 *      libexec/
 *          steam-runtime-tools-0/
 *              i386-linux-gnu-*
 *              x86_64-linux-gnu-*
 * ]|
 *
 * Returns: %TRUE if executables belonging to @multiarch_tuple can be run
 */
gboolean
srt_system_info_can_run (SrtSystemInfo *self,
                         const char *multiarch_tuple)
{
  Abi *abi = NULL;

  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), FALSE);
  g_return_val_if_fail (multiarch_tuple != NULL, FALSE);

  abi = ensure_abi_unless_immutable (self, multiarch_tuple);

  if (abi == NULL)
    return FALSE;

  if (abi->can_run == TRI_MAYBE)
    {
      if (_srt_architecture_can_run (self->env, self->helpers_path, multiarch_tuple))
        abi->can_run = TRI_YES;
      else
        abi->can_run = TRI_NO;
    }

  return (abi->can_run == TRI_YES);
}

/**
 * srt_system_info_can_write_to_uinput:
 * @self: a #SrtSystemInfo object
 *
 * Return %TRUE if the current user can write to `/dev/uinput`.
 * This is required for the Steam client to be able to emulate gamepads,
 * keyboards, mice and other input devices based on input from the
 * Steam Controller or a remote streaming client.
 *
 * Returns: %TRUE if `/dev/uinput` can be opened for writing
 */
gboolean
srt_system_info_can_write_to_uinput (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), FALSE);

  if (self->can_write_uinput == TRI_MAYBE && !self->immutable_values)
    {
      int fd = open ("/dev/uinput", O_WRONLY | O_NONBLOCK);

      if (fd >= 0)
        {
          g_debug ("Successfully opened /dev/uinput for writing");
          self->can_write_uinput = TRI_YES;
          close (fd);
        }
      else
        {
          g_debug ("Failed to open /dev/uinput for writing: %s",
                   g_strerror (errno));
          self->can_write_uinput = TRI_NO;
        }
    }

  return (self->can_write_uinput == TRI_YES);
}

static gint
library_compare (SrtLibrary *a, SrtLibrary *b)
{
  return g_strcmp0 (srt_library_get_requested_name (a),
                    srt_library_get_requested_name (b));
}

static gint
graphics_compare (SrtGraphics *a, SrtGraphics *b)
{
  int aKey = _srt_graphics_hash_key (srt_graphics_get_window_system (a),
                                     srt_graphics_get_rendering_interface (a));
  int bKey = _srt_graphics_hash_key (srt_graphics_get_window_system (b),
                                     srt_graphics_get_rendering_interface (b));
  return (aKey < bKey) ? -1 : (aKey > bKey);
}

/* Path components from ${prefix} to steamrt expectations */
#define STEAMRT_EXPECTATIONS "lib", "steamrt", "expectations"

static gboolean
ensure_expectations (SrtSystemInfo *self)
{
  g_return_val_if_fail (_srt_check_not_setuid (), FALSE);
  g_return_val_if_fail (!self->immutable_values, FALSE);

  if (self->expectations == NULL)
    {
      const char *runtime;
      gchar *def = NULL;

      runtime = g_environ_getenv (self->env, "STEAM_RUNTIME");

      if (runtime != NULL && runtime[0] == '/')
        {
          def = g_build_filename (runtime, "usr", STEAMRT_EXPECTATIONS,
                                  NULL);
        }

      if (def == NULL)
        {
          runtime = _srt_find_myself (NULL, NULL);

          if (runtime != NULL)
            def = g_build_filename (runtime, STEAMRT_EXPECTATIONS, NULL);
        }

      if (def == NULL)
        def = g_build_filename ("/usr", STEAMRT_EXPECTATIONS, NULL);

      if (g_file_test (def, G_FILE_TEST_IS_DIR))
        self->expectations = g_steal_pointer (&def);
      else
        self->expectations = g_strdup ("");

      g_free (def);
    }

  return self->expectations[0] != '\0';
}

static void
ensure_hidden_deps (SrtSystemInfo *self)
{
  g_return_if_fail (!self->immutable_values);

  if (self->cached_hidden_deps == NULL)
    {
      JsonParser *parser = NULL;
      JsonNode *node = NULL;
      JsonArray *libraries_array = NULL;
      JsonArray *hidden_libraries_array = NULL;
      JsonObject *object;
      gchar *soname = NULL;
      gchar *path = NULL;
      GPtrArray *arr = NULL;
      GError *error = NULL;

      self->cached_hidden_deps = g_hash_table_new_full (g_str_hash,
                                                        g_str_equal,
                                                        g_free,
                                                        (GDestroyNotify) g_strfreev);

      if (!ensure_expectations (self))
        {
          g_debug ("Hidden dependencies parsing skipped because of unknown expectations");
          goto out;
        }

      path = g_build_filename (self->expectations, "steam-runtime-abi.json", NULL);

      /* Currently, in a standard Steam installation, we have the abi JSON one level up
      * from the expectations folder */
      if (!g_file_test (path, G_FILE_TEST_EXISTS))
        {
          g_free (path);
          path = g_build_filename (self->expectations, "..", "steam-runtime-abi.json", NULL);
        }

      parser = json_parser_new ();
      if (!json_parser_load_from_file (parser, path, &error))
        {
          g_debug ("Error parsing the expected JSON object in \"%s\": %s", path, error->message);
          goto out;
        }

      node = json_parser_get_root (parser);
      object = json_node_get_object (node);

      if (!json_object_has_member (object, "shared_libraries"))
        {
          g_debug ("No \"shared_libraries\" in the JSON object \"%s\"", path);
          goto out;
        }

      libraries_array = json_object_get_array_member (object, "shared_libraries");
      /* If there are no libraries in the parsed JSON file we simply return */
      if (libraries_array == NULL || json_array_get_length (libraries_array) == 0)
        goto out;

      for (guint i = 0; i < json_array_get_length (libraries_array); i++)
        {
          node = json_array_get_element (libraries_array, i);
          if (!JSON_NODE_HOLDS_OBJECT (node))
            continue;

          object = json_node_get_object (node);

          GList *members = json_object_get_members (object);
          if (members == NULL)
            continue;

          soname = g_strdup (members->data);
          g_list_free (members);

          object = json_object_get_object_member (object, soname);
          if (!json_object_has_member (object, "hidden_dependencies"))
            {
              g_free (soname);
              continue;
            }

          hidden_libraries_array = json_object_get_array_member (object, "hidden_dependencies");
          if (hidden_libraries_array == NULL || json_array_get_length (hidden_libraries_array) == 0)
            {
              g_free (soname);
              continue;
            }

          arr = g_ptr_array_new_full (json_array_get_length (hidden_libraries_array) + 1, g_free);

          for (guint j = 0; j < json_array_get_length (hidden_libraries_array); j++)
            g_ptr_array_add (arr, g_strdup (json_array_get_string_element (hidden_libraries_array, j)));

          g_ptr_array_add (arr, NULL);

          g_debug ("%s soname hidden dependencies have been parsed", soname);
          g_hash_table_insert (self->cached_hidden_deps, g_steal_pointer (&soname), g_ptr_array_free (arr, FALSE));
        }

      out:
        g_free (path);
        g_clear_error (&error);
        if (parser)
          g_clear_object (&parser);
    }
}

static void
ensure_overrides_cached (SrtSystemInfo *self)
{
  g_return_if_fail (_srt_check_not_setuid ());
  g_return_if_fail (!self->immutable_values);

  if (!self->overrides.have_data)
    {
      static const char * const paths[] = {
          "overrides",
          "usr/lib/pressure-vessel/overrides",
      };
      g_autoptr(GError) error = NULL;
      g_autofree gchar *output = NULL;
      g_autofree gchar *messages = NULL;
      g_autofree gchar *runtime = NULL;
      const gchar *argv[] = {"find", NULL, "-ls", NULL};
      int exit_status = -1;
      gsize i;

      self->overrides.have_data = TRUE;

      runtime = srt_system_info_dup_runtime_path (self);
      /* Skip checking the overridden folder if we are not in a pressure-vessel
       * Steam Runtime container */
      if (g_strcmp0 (runtime, "/") != 0)
        return;

      for (i = 0; i < G_N_ELEMENTS (paths); i++)
        {
          if (_srt_file_test_in_sysroot (self->sysroot, self->sysroot_fd,
                                         paths[i], G_FILE_TEST_EXISTS))
            {
              argv[1] = paths[i];
              break;
            }
        }

      if (argv[1] == NULL)
        return;

      if (!g_spawn_sync (self->sysroot,     /* working directory */
                        (gchar **) argv,
                        self->env,
                        G_SPAWN_SEARCH_PATH,
                        _srt_child_setup_unblock_signals,
                        NULL,    /* user data */
                        &output, /* stdout */
                        &messages, /* stderr */
                        &exit_status,
                        &error))
        {
          g_debug ("An error occurred calling the \"find\" binary: %s", error->message);
          self->overrides.messages = g_new0 (gchar *, 2);
          self->overrides.messages[0] = g_strdup_printf ("%s %d: %s", g_quark_to_string (error->domain), error->code, error->message);
          self->overrides.messages[1] = NULL;
          return;
        }

      if (exit_status != 0)
        g_debug ("... wait status %d", exit_status);

      if (output != NULL)
        {
          g_strchomp (output);
          self->overrides.values = g_strsplit (output, "\n", -1);
        }

      if (messages != NULL)
        {
          g_strchomp (messages);
          self->overrides.messages = g_strsplit (messages, "\n", -1);
        }
    }
}

/**
 * srt_system_info_list_pressure_vessel_overrides:
 * @self: The #SrtSystemInfo object
 * @messages: (optional) (out) (array zero-terminated=1) (transfer full): If
 *  not %NULL, used to return a %NULL-terminated array of diagnostic
 *  messages. Free with g_strfreev().
 *
 * If running in a Steam Runtime container using `pressure-vessel`,
 * list the libraries from the container that have been overridden
 * with libraries from the host system.
 *
 * The output is intended to be human-readable debugging information,
 * rather than something to use programmatically, and its format is
 * not guaranteed. It is currently in `find -ls` format.
 *
 * Similarly, @messages is intended to be human-readable debugging
 * information. It is currently whatever was output on standard error
 * by `find -ls`.
 *
 * Returns: (array zero-terminated=1) (transfer full) (nullable): A
 *  %NULL-terminated array of libraries that have been overridden,
 *  or %NULL if this process does not appear to be in a pressure-vessel
 *  container. Free with g_strfreev().
 */
gchar **
srt_system_info_list_pressure_vessel_overrides (SrtSystemInfo *self,
                                                gchar ***messages)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  if (!self->immutable_values)
    ensure_overrides_cached (self);

  if (messages != NULL)
    *messages = g_strdupv (self->overrides.messages);

  return g_strdupv (self->overrides.values);
}

static void
ensure_pinned_libs_cached (SrtSystemInfo *self)
{
  gchar *output = NULL;
  gchar *messages = NULL;
  gchar *runtime = NULL;
  int exit_status = -1;
  GError *error = NULL;

  g_return_if_fail (_srt_check_not_setuid ());

  if (!self->pinned_libs.have_data && !self->immutable_values)
    {
      runtime = srt_system_info_dup_runtime_path (self);

      self->pinned_libs.have_data = TRUE;

      if (runtime == NULL || g_strcmp0 (runtime, "/") == 0)
        return;

      const gchar *argv[] = {"find", "pinned_libs_32", "-ls", NULL};

      if (!g_spawn_sync (runtime, /* working directory */
                        (gchar **) argv,
                        self->env,
                        G_SPAWN_SEARCH_PATH,
                        _srt_child_setup_unblock_signals,
                        NULL,    /* user data */
                        &output, /* stdout */
                        &messages, /* stderr */
                        &exit_status,
                        &error))
        {
          g_debug ("An error occurred calling the \"find\" binary: %s", error->message);
          self->pinned_libs.messages_32 = g_new0 (gchar *, 2);
          self->pinned_libs.messages_32[0] = g_strdup_printf ("%s %d: %s", g_quark_to_string (error->domain), error->code, error->message);
          self->pinned_libs.messages_32[1] = NULL;
          goto out;
        }

      if (exit_status != 0)
        g_debug ("... wait status %d", exit_status);

      if (output != NULL)
        {
          g_strchomp (output);
          self->pinned_libs.values_32 = g_strsplit (output, "\n", -1);
        }

      if (messages != NULL)
        {
          g_strchomp (messages);
          self->pinned_libs.messages_32 = g_strsplit (messages, "\n", -1);
        }

      g_free (output);
      g_free (messages);

      /* Do the same check for `pinned_libs_64` */
      argv[1] = "pinned_libs_64";

      if (!g_spawn_sync (runtime,    /* working directory */
                        (gchar **) argv,
                        self->env,
                        G_SPAWN_SEARCH_PATH,
                        _srt_child_setup_unblock_signals,
                        NULL,    /* user data */
                        &output, /* stdout */
                        &messages, /* stderr */
                        &exit_status,
                        &error))
        {
          g_debug ("An error occurred calling the \"find\" binary: %s", error->message);
          self->pinned_libs.messages_64 = g_new0 (gchar *, 2);
          self->pinned_libs.messages_64[0] = g_strdup_printf ("%s %d: %s", g_quark_to_string (error->domain), error->code, error->message);
          self->pinned_libs.messages_64[1] = NULL;
          goto out;
        }

      if (exit_status != 0)
        g_debug ("... wait status %d", exit_status);

      if (output != NULL)
        {
          g_strchomp (output);
          self->pinned_libs.values_64 = g_strsplit (output, "\n", -1);
        }

      if (messages != NULL)
        {
          g_strchomp (messages);
          self->pinned_libs.messages_64 = g_strsplit (messages, "\n", -1);
        }
    }

  out:
    g_clear_error (&error);
    g_free (output);
    g_free (messages);
    g_free (runtime);
}

/**
 * srt_system_info_list_pinned_libs_32:
 * @self: The #SrtSystemInfo object
 * @messages: (optional) (out) (array zero-terminated=1) (transfer full): If
 *  not %NULL, used to return a %NULL-terminated array of diagnostic
 *  messages. Free with g_strfreev().
 *
 * If running in an `LD_LIBRARY_PATH`-based Steam Runtime, return
 * information about %SRT_ABI_I386 libraries that have been "pinned".
 * Normally, the Steam Runtime infrastructure prefers to use shared
 * libraries from the host OS, if available, rather than the
 * library of the same `SONAME` from the Steam Runtime. However, if
 * a library in the Steam Runtime is newer then the version in the
 * host OS, or if it is known to be incompatible with newer
 * libraries with the same `SONAME`, then the library from the
 * Steam Runtime is said to have been "pinned": it is used with a
 * higher precedence than libraries from the host OS.
 *
 * If not in an `LD_LIBRARY_PATH`-based Steam Runtime, return %NULL.
 *
 * The output is intended to be human-readable debugging information,
 * rather than something to use programmatically, and its format is
 * not guaranteed. It is currently in `find -ls` format.
 *
 * Similarly, @messages is intended to be human-readable debugging
 * information. It is currently whatever was output on standard error
 * by `find -ls`.
 *
 * Returns: (array zero-terminated=1) (transfer full) (element-type utf8) (nullable):
 *  An array of strings, or %NULL if we were unable to call "find".
 *  Free with g_strfreev().
 */
gchar **
srt_system_info_list_pinned_libs_32 (SrtSystemInfo *self,
                                     gchar ***messages)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  ensure_pinned_libs_cached (self);

  if (messages != NULL)
    *messages = g_strdupv (self->pinned_libs.messages_32);

  return g_strdupv (self->pinned_libs.values_32);
}

/**
 * srt_system_info_list_pinned_libs_64:
 * @self: The #SrtSystemInfo object
 * @messages: (optional) (out) (array zero-terminated=1) (transfer full): If
 *  not %NULL, used to return a %NULL-terminated array of diagnostic
 *  messages. Free with g_strfreev().
 *
 * If running in an `LD_LIBRARY_PATH`-based Steam Runtime, return
 * information about %SRT_ABI_X86_64 libraries that have been "pinned".
 * Normally, the Steam Runtime infrastructure prefers to use shared
 * libraries from the host OS, if available, rather than the
 * library of the same `SONAME` from the Steam Runtime. However, if
 * a library in the Steam Runtime is newer then the version in the
 * host OS, or if it is known to be incompatible with newer
 * libraries with the same `SONAME`, then the library from the
 * Steam Runtime is said to have been "pinned": it is used with a
 * higher precedence than libraries from the host OS.
 *
 * If not in an `LD_LIBRARY_PATH`-based Steam Runtime, return %NULL.
 *
 * The output is intended to be human-readable debugging information,
 * rather than something to use programmatically, and its format is
 * not guaranteed. It is currently in `find -ls` format.
 *
 * Similarly, @messages is intended to be human-readable debugging
 * information. It is currently whatever was output on standard error
 * by `find -ls`.
 *
 * Returns: (array zero-terminated=1) (transfer full) (element-type utf8) (nullable):
 *  An array of strings, or %NULL if we were unable to call "find".
 *  Free with g_strfreev().
 */
gchar **
srt_system_info_list_pinned_libs_64 (SrtSystemInfo *self,
                                     gchar ***messages)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  ensure_pinned_libs_cached (self);

  if (messages != NULL)
    *messages = g_strdupv (self->pinned_libs.messages_64);

  return g_strdupv (self->pinned_libs.values_64);
}

/**
 * _srt_system_info_get_pinned_libs_from_report:
 * @json_obj: (not nullable): A JSON Object used to search for @which
 *  property
 * @which: (not nullable): The member to look up
 * @messages: (not nullable): Used to return human-readable debug information
 *
 * Returns: (array zero-terminated=1) (transfer full) (element-type utf8) (nullable):
 *  An array of strings with the found pinned libs from the @json_obj, or %NULL
 *  if @json_obj doesn't have a @which member. Free with g_strfreev().
 */
static gchar **
_srt_system_info_get_pinned_libs_from_report (JsonObject *json_obj,
                                              const gchar *which,
                                              gchar ***messages)
{
  JsonObject *json_pinned_obj = NULL;
  gchar **pinned_list = NULL;

  g_return_val_if_fail (json_obj != NULL, NULL);
  g_return_val_if_fail (which != NULL, NULL);
  g_return_val_if_fail (messages != NULL && *messages == NULL, NULL);

  if (json_object_has_member (json_obj, which))
    {
      json_pinned_obj = json_object_get_object_member (json_obj, which);

      pinned_list = _srt_json_object_dup_strv_member (json_pinned_obj,
                                                      "list",
                                                      "<invalid>");

      *messages = _srt_json_object_dup_strv_member (json_pinned_obj,
                                                    "messages",
                                                    "<invalid>");
    }

  return pinned_list;
}

/**
 * srt_system_info_check_libraries:
 * @self: The #SrtSystemInfo object to use.
 * @multiarch_tuple: A multiarch tuple like %SRT_ABI_I386, representing an ABI.
 * @libraries_out: (out) (optional) (element-type SrtLibrary) (transfer full):
 *  Used to return a #GList object where every element of said list is an
 *  #SrtLibrary object, representing every `SONAME` found from the expectations
 *  folder. Free with `g_list_free_full(libraries, g_object_unref)`.
 *
 * Check if the running system has all the expected libraries, and related symbols,
 * as listed in the `deb-symbols(5)` files `*.symbols` in the @multiarch
 * subdirectory of #SrtSystemInfo:expectations.
 *
 * Returns: A bitfield containing problems, or %SRT_LIBRARY_ISSUES_NONE
 *  if no problems were found.
 */
SrtLibraryIssues
srt_system_info_check_libraries (SrtSystemInfo *self,
                                 const gchar *multiarch_tuple,
                                 GList **libraries_out)
{
  Abi *abi = NULL;
  gchar *dir_path = NULL;
  const gchar *filename = NULL;
  gchar *symbols_file = NULL;
  size_t len = 0;
  ssize_t chars;
  GDir *dir = NULL;
  FILE *fp = NULL;
  GError *error = NULL;
  SrtLibraryIssues ret = SRT_LIBRARY_ISSUES_UNKNOWN;

  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), SRT_LIBRARY_ISSUES_UNKNOWN);
  g_return_val_if_fail (multiarch_tuple != NULL, SRT_LIBRARY_ISSUES_UNKNOWN);
  g_return_val_if_fail (libraries_out == NULL || *libraries_out == NULL,
                        SRT_LIBRARY_ISSUES_UNKNOWN);

  abi = ensure_abi_unless_immutable (self, multiarch_tuple);

  if (abi == NULL)
    return SRT_LIBRARY_ISSUES_CANNOT_LOAD;

  /* If we cached already the result, we return it */
  if (abi->libraries_cache_available)
    {
      if (libraries_out != NULL)
        {
          *libraries_out = g_list_sort (g_hash_table_get_values (abi->cached_results),
                                        (GCompareFunc) library_compare);
          g_list_foreach (*libraries_out, (GFunc) G_CALLBACK (g_object_ref), NULL);
        }

      return abi->cached_combined_issues;
    }

  if (self->immutable_values)
    return SRT_LIBRARY_ISSUES_UNKNOWN;

  if (!ensure_expectations (self))
    {
      /* We don't know which libraries to check. */
      return SRT_LIBRARY_ISSUES_UNKNOWN_EXPECTATIONS;
    }

  dir_path = g_build_filename (self->expectations, multiarch_tuple, NULL);
  dir = g_dir_open (dir_path, 0, &error);
  if (error)
    {
      g_debug ("An error occurred while opening the symbols directory: %s", error->message);
      g_clear_error (&error);
      ret = SRT_LIBRARY_ISSUES_UNKNOWN_EXPECTATIONS;
      goto out;
    }

  ensure_hidden_deps (self);

  while ((filename = g_dir_read_name (dir)))
    {
      char *line = NULL;

      if (!g_str_has_suffix (filename, ".symbols"))
        continue;

      symbols_file = g_build_filename (dir_path, filename, NULL);
      fp = fopen(symbols_file, "r");

      if (fp == NULL)
        {
          int saved_errno = errno;
          g_debug ("Error reading \"%s\": %s\n", symbols_file, strerror (saved_errno));
          goto out;
        }

      while ((chars = getline(&line, &len, fp)) != -1)
        {
          char *pointer_into_line = line;
          if (line[chars - 1] == '\n')
            line[chars - 1] = '\0';

          if (line[0] == '\0')
            continue;

          if (line[0] != '#' && line[0] != '*' && line[0] != '|' && line[0] != ' ')
            {
              /* This line introduces a new SONAME. We extract it and call
                * `_srt_check_library_presence` with the symbols file where we
                * found it, as an argument. */
              SrtLibrary *library = NULL;
              char *soname = g_strdup (strsep (&pointer_into_line, " \t"));
              gchar **hidden_deps = g_hash_table_lookup (self->cached_hidden_deps, soname);
              abi->cached_combined_issues |= _srt_check_library_presence (self->helpers_path,
                                                                          soname,
                                                                          multiarch_tuple,
                                                                          symbols_file,
                                                                          (const gchar * const *)hidden_deps,
                                                                          self->env,
                                                                          SRT_LIBRARY_SYMBOLS_FORMAT_DEB_SYMBOLS,
                                                                          &library);
              g_hash_table_insert (abi->cached_results, soname, library);
            }
        }
      free (line);
      g_clear_pointer (&symbols_file, g_free);
      g_clear_pointer (&fp, fclose);
    }

  abi->libraries_cache_available = TRUE;
  if (libraries_out != NULL)
    {
      *libraries_out = g_list_sort (g_hash_table_get_values (abi->cached_results),
                                    (GCompareFunc) library_compare);
      g_list_foreach (*libraries_out, (GFunc) G_CALLBACK (g_object_ref), NULL);
    }

  ret = abi->cached_combined_issues;

  out:
    g_clear_pointer (&symbols_file, g_free);
    g_clear_pointer (&dir_path, g_free);

    if (fp != NULL)
      g_clear_pointer (&fp, fclose);

    if (dir != NULL)
      g_dir_close (dir);

    return ret;
}

/**
 * srt_system_info_check_library:
 * @self: The #SrtSystemInfo object to use.
 * @multiarch_tuple: A multiarch tuple like %SRT_ABI_I386, representing an ABI.
 * @requested_name: (type filename): The `SONAME` of a shared library, for
 *  example `libjpeg.so.62`.
 * @more_details_out: (out) (optional) (transfer full): Used to return an
 *  #SrtLibrary object representing the shared library provided
 *  by @requested_name. Free with `g_object_unref()`.
 *
 * Check if @requested_name is available in the running system and whether
 * it conforms to the `deb-symbols(5)` files `*.symbols` in the @multiarch
 * subdirectory of #SrtSystemInfo:expectations.
 *
 * Returns: A bitfield containing problems, or %SRT_LIBRARY_ISSUES_NONE
 *  if no problems were found.
 */
SrtLibraryIssues
srt_system_info_check_library (SrtSystemInfo *self,
                               const gchar *multiarch_tuple,
                               const gchar *requested_name,
                               SrtLibrary **more_details_out)
{
  Abi *abi = NULL;
  SrtLibrary *library = NULL;
  const gchar *filename = NULL;
  gchar *symbols_file = NULL;
  gchar *dir_path = NULL;
  gchar *line = NULL;
  size_t len = 0;
  ssize_t chars;
  FILE *fp = NULL;
  SrtLibraryIssues issues;
  GDir *dir = NULL;
  GError *error = NULL;
  SrtLibraryIssues ret = SRT_LIBRARY_ISSUES_UNKNOWN;

  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), SRT_LIBRARY_ISSUES_UNKNOWN);
  g_return_val_if_fail (multiarch_tuple != NULL, SRT_LIBRARY_ISSUES_UNKNOWN);
  g_return_val_if_fail (requested_name != NULL, SRT_LIBRARY_ISSUES_UNKNOWN);
  g_return_val_if_fail (more_details_out == NULL || *more_details_out == NULL,
                        SRT_LIBRARY_ISSUES_UNKNOWN);

  abi = ensure_abi_unless_immutable (self, multiarch_tuple);

  if (abi == NULL)
    return SRT_LIBRARY_ISSUES_CANNOT_LOAD;

  /* If we have the result already in cache, we return it */
  library = g_hash_table_lookup (abi->cached_results, requested_name);
  if (library != NULL)
    {
      if (more_details_out != NULL)
        *more_details_out = g_object_ref (library);
      return srt_library_get_issues (library);
    }

  if (self->immutable_values)
    return SRT_LIBRARY_ISSUES_UNKNOWN;

  if (ensure_expectations (self))
    {
      dir_path = g_build_filename (self->expectations, multiarch_tuple, NULL);
      dir = g_dir_open (dir_path, 0, &error);

      if (error)
        {
          g_debug ("An error occurred while opening the symbols directory: %s", error->message);
          g_clear_error (&error);
        }
    }

  ensure_hidden_deps (self);

  while (dir != NULL && (filename = g_dir_read_name (dir)))
    {
      if (!g_str_has_suffix (filename, ".symbols"))
        continue;

      symbols_file = g_build_filename (dir_path, filename, NULL);
      fp = fopen(symbols_file, "r");

      if (fp == NULL)
        {
          int saved_errno = errno;
          g_debug ("Error reading \"%s\": %s\n", symbols_file, strerror (saved_errno));
          goto out;
        }

      while ((chars = getline(&line, &len, fp)) != -1)
        {
          char *pointer_into_line = line;
          if (line[chars - 1] == '\n')
            line[chars - 1] = '\0';

          if (line[0] == '\0')
            continue;

          if (line[0] != '#' && line[0] != '*' && line[0] != '|' && line[0] != ' ')
            {
              /* This line introduces a new SONAME, which might
                * be the one we are interested in. */
              char *soname_found = g_strdup (strsep (&pointer_into_line, " \t"));
              if (g_strcmp0 (soname_found, requested_name) == 0)
                {
                  gchar **hidden_deps = g_hash_table_lookup (self->cached_hidden_deps, requested_name);
                  issues = _srt_check_library_presence (self->helpers_path,
                                                        soname_found,
                                                        multiarch_tuple,
                                                        symbols_file,
                                                        (const gchar * const *)hidden_deps,
                                                        self->env,
                                                        SRT_LIBRARY_SYMBOLS_FORMAT_DEB_SYMBOLS,
                                                        &library);
                  g_hash_table_insert (abi->cached_results, soname_found, library);
                  abi->cached_combined_issues |= issues;
                  if (more_details_out != NULL)
                    *more_details_out = g_object_ref (library);
                  free (line);
                  ret = issues;
                  goto out;
                }
              free (soname_found);
            }
        }
      g_clear_pointer (&symbols_file, g_free);
      g_clear_pointer (&line, g_free);
      g_clear_pointer (&fp, fclose);
    }

  /* The SONAME's symbols file is not available.
   * We do instead a simple absence/presence check. */
  issues = _srt_check_library_presence (self->helpers_path,
                                        requested_name,
                                        multiarch_tuple,
                                        NULL,
                                        NULL,
                                        self->env,
                                        SRT_LIBRARY_SYMBOLS_FORMAT_DEB_SYMBOLS,
                                        &library);
  g_hash_table_insert (abi->cached_results, g_strdup (requested_name), library);
  abi->cached_combined_issues |= issues;
  if (more_details_out != NULL)
    *more_details_out = g_object_ref (library);

  ret = issues;

  out:
    g_clear_pointer (&symbols_file, g_free);
    g_clear_pointer (&dir_path, g_free);

    if (fp != NULL)
      g_clear_pointer (&fp, fclose);

    if (dir != NULL)
      g_dir_close (dir);

    return ret;
}

/*
 * Forget whether we can load libraries.
 */
static void
forget_libraries (SrtSystemInfo *self)
{
  gsize i;

  for (i = 0; i < self->abis->len; i++)
    {
      Abi *abi = g_ptr_array_index (self->abis, i);

      g_hash_table_remove_all (abi->cached_results);
      abi->cached_combined_issues = SRT_LIBRARY_ISSUES_NONE;
      abi->libraries_cache_available = FALSE;
    }
}

/*
 * Forget cached graphics results.
 */
static void
forget_graphics_results (SrtSystemInfo *self)
{
  gsize i;

  for (i = 0; i < self->abis->len; i++)
    {
      Abi *abi = g_ptr_array_index (self->abis, i);

      g_hash_table_remove_all (abi->cached_graphics_results);
      abi->cached_combined_graphics_issues = SRT_GRAPHICS_ISSUES_NONE;
      abi->graphics_cache_available = FALSE;
    }
}

/*
 * Forget any cached information about graphics modules.
 */
static void
forget_graphics_modules (SrtSystemInfo *self)
{
  gsize i, j;

  for (i = 0; i < self->abis->len; i++)
    {
      Abi *abi = g_ptr_array_index (self->abis, i);

      for (j = 0; j < G_N_ELEMENTS (abi->graphics_modules); j++)
        {
          g_list_free_full (g_steal_pointer (&abi->graphics_modules[j].modules),
                            g_object_unref);
          abi->graphics_modules[j].available = FALSE;
        }
    }
}

/**
 * srt_system_info_check_graphics:
 * @self: The #SrtSystemInfo object to use.
 * @multiarch_tuple: A multiarch tuple like %SRT_ABI_I386, representing an ABI.
 * @window_system: The window system to check.
 * @rendering_interface: The graphics renderng interface to check.
 * @details_out: (out) (optional) (transfer full): Used to return an
 *  #SrtGraphics object representing the items tested and results.
 *  Free with `g_object_unref()`.
 *
 * Returns: A bitfield containing problems, or %SRT_GRAPHICS_ISSUES_NONE
 *  if no problems were found.
 */
SrtGraphicsIssues
srt_system_info_check_graphics (SrtSystemInfo *self,
                                const char *multiarch_tuple,
                                SrtWindowSystem window_system,
                                SrtRenderingInterface rendering_interface,
                                SrtGraphics **details_out)
{
  Abi *abi = NULL;
  SrtGraphicsIssues issues;

  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), SRT_GRAPHICS_ISSUES_UNKNOWN);
  g_return_val_if_fail (multiarch_tuple != NULL, SRT_GRAPHICS_ISSUES_UNKNOWN);
  g_return_val_if_fail (details_out == NULL || *details_out == NULL,
                        SRT_GRAPHICS_ISSUES_UNKNOWN);
  g_return_val_if_fail (((unsigned) window_system) < SRT_N_WINDOW_SYSTEMS, SRT_GRAPHICS_ISSUES_UNKNOWN);
  g_return_val_if_fail (((unsigned) rendering_interface) < SRT_N_RENDERING_INTERFACES, SRT_GRAPHICS_ISSUES_UNKNOWN);

  abi = ensure_abi_unless_immutable (self, multiarch_tuple);

  if (abi == NULL)
    return SRT_GRAPHICS_ISSUES_UNKNOWN;

  /* If we have the result already in cache, we return it */
  int hash_key = _srt_graphics_hash_key (window_system, rendering_interface);
  SrtGraphics *graphics = g_hash_table_lookup (abi->cached_graphics_results, GINT_TO_POINTER(hash_key));
  if (graphics != NULL)
    {
      if (details_out != NULL)
        *details_out = g_object_ref (graphics);
      return srt_graphics_get_issues (graphics);
    }

  if (self->immutable_values)
    return SRT_GRAPHICS_ISSUES_UNKNOWN;

  graphics = NULL;
  issues = _srt_check_graphics (self->env,
                                self->helpers_path,
                                self->test_flags,
                                multiarch_tuple,
                                window_system,
                                rendering_interface,
                                &graphics);
  g_hash_table_insert (abi->cached_graphics_results, GINT_TO_POINTER(hash_key), graphics);
  abi->cached_combined_graphics_issues |= issues;
  if (details_out != NULL)
    *details_out = g_object_ref (graphics);

  return issues;
}

/**
 * srt_system_info_check_all_graphics:
 * @self: The #SrtSystemInfo object to use.
 * @multiarch_tuple: A multiarch tuple like %SRT_ABI_I386, representing an ABI.
*
 * Check whether various combinations of rendering interface and windowing
 * system are available. The specific combinations of rendering interface and
 * windowing system that are returned are not guaranteed, but will include at
 * least %SRT_RENDERING_INTERFACE_GL on %SRT_WINDOW_SYSTEM_GLX. Additional combinations
 * will be added in future versions of this library.
 *
 * Returns: (transfer full) (type SrtGraphics): A list of #SrtGraphics objects
 * representing the items tested and results.
 * Free with 'g_list_free_full(list, g_object_unref)`.
 */
GList * srt_system_info_check_all_graphics (SrtSystemInfo *self,
                                            const char *multiarch_tuple)
{
  Abi *abi = NULL;

  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);
  g_return_val_if_fail (multiarch_tuple != NULL, NULL);

  abi = ensure_abi_unless_immutable (self, multiarch_tuple);

  if (abi == NULL)
    return NULL;

  GList *list = NULL;

  /* If we cached already the result, we return it */
  if (abi->graphics_cache_available)
    {
      list = g_list_sort (g_hash_table_get_values (abi->cached_graphics_results),
                                    (GCompareFunc) graphics_compare);
      g_list_foreach (list, (GFunc) G_CALLBACK (g_object_ref), NULL);

      return list;
    }

  // Try each rendering interface
  // Try each window system

  abi->cached_combined_graphics_issues |=
    srt_system_info_check_graphics (self,
                                    multiarch_tuple,
                                    SRT_WINDOW_SYSTEM_GLX,
                                    SRT_RENDERING_INTERFACE_GL,
                                    NULL);

  abi->cached_combined_graphics_issues |=
    srt_system_info_check_graphics (self,
                                    multiarch_tuple,
                                    SRT_WINDOW_SYSTEM_EGL_X11,
                                    SRT_RENDERING_INTERFACE_GL,
                                    NULL);

  abi->cached_combined_graphics_issues |=
    srt_system_info_check_graphics (self,
                                    multiarch_tuple,
                                    SRT_WINDOW_SYSTEM_EGL_X11,
                                    SRT_RENDERING_INTERFACE_GLESV2,
                                    NULL);

  abi->cached_combined_graphics_issues |=
    srt_system_info_check_graphics (self,
                                    multiarch_tuple,
                                    SRT_WINDOW_SYSTEM_X11,
                                    SRT_RENDERING_INTERFACE_VULKAN,
                                    NULL);

  abi->cached_combined_graphics_issues |=
    srt_system_info_check_graphics (self,
                                    multiarch_tuple,
                                    SRT_WINDOW_SYSTEM_X11,
                                    SRT_RENDERING_INTERFACE_VDPAU,
                                    NULL);

  abi->cached_combined_graphics_issues |=
    srt_system_info_check_graphics (self,
                                    multiarch_tuple,
                                    SRT_WINDOW_SYSTEM_X11,
                                    SRT_RENDERING_INTERFACE_VAAPI,
                                    NULL);

  abi->graphics_cache_available = TRUE;

  list = g_list_sort (g_hash_table_get_values (abi->cached_graphics_results),
                      (GCompareFunc) graphics_compare);
  g_list_foreach (list, (GFunc) G_CALLBACK (g_object_ref), NULL);

  return list;
}

/**
 * srt_system_info_set_environ:
 * @self: The #SrtSystemInfo
 * @env: (nullable) (array zero-terminated=1) (element-type filename) (transfer none): An
 *  array of environment variables
 *
 * Use @env instead of the real environment variable block `environ`
 * when locating the Steam Runtime.
 *
 * If @env is %NULL, go back to using the real environment variables.
 *
 * This method is not valid to call on a #SrtSystemInfo that was
 * constructed with srt_system_info_new_from_json().
 */
void
srt_system_info_set_environ (SrtSystemInfo *self,
                             gchar * const *env)
{
  g_return_if_fail (SRT_IS_SYSTEM_INFO (self));
  g_return_if_fail (!self->immutable_values);

  forget_graphics_modules (self);
  forget_libraries (self);
  forget_graphics_results (self);
  forget_locales (self);
  forget_pinned_libs (self);
  forget_xdg_portal (self);
  g_clear_pointer (&self->cached_driver_environment, g_strfreev);
  g_strfreev (self->env);

  if (env != NULL)
    self->env = g_strdupv ((gchar **) env);
  else
    self->env = g_strdupv ((gchar **) _srt_peek_environ_nonnull ());

  /* Forget what we know about Steam because it is bounded to the environment. */
  forget_steam (self);
}

/**
 * srt_system_info_set_sysroot:
 * @self: The #SrtSystemInfo
 * @root: (nullable) (type filename) (transfer none): Path to the sysroot
 *
 * Use @root instead of the real root directory when investigating
 * system properties.
 *
 * If @root is %NULL, go back to using the real root.
 *
 * This method is not valid to call on a #SrtSystemInfo that was
 * constructed with srt_system_info_new_from_json().
 */
void
srt_system_info_set_sysroot (SrtSystemInfo *self,
                             const char *root)
{
  g_autoptr(GError) local_error = NULL;

  g_return_if_fail (SRT_IS_SYSTEM_INFO (self));
  g_return_if_fail (!self->immutable_values);

  if (root == NULL)
    root = "/";

  forget_container_info (self);
  forget_graphics_modules (self);
  forget_libraries (self);
  forget_graphics_results (self);
  forget_locales (self);
  forget_os (self);
  forget_overrides (self);
  g_free (self->sysroot);
  glnx_close_fd (&self->sysroot_fd);

  self->sysroot = g_strdup (root);

  if (!glnx_opendirat (-1,
                       self->sysroot,
                       FALSE,
                       &self->sysroot_fd,
                       &local_error))
    {
      g_debug ("Unable to open sysroot %s: %s",
               self->sysroot, local_error->message);
    }
}

static void
ensure_steam_cached (SrtSystemInfo *self)
{
  if (self->steam_data == NULL && !self->immutable_values)
    _srt_steam_check (self->env, &self->steam_data);
}

/**
 * srt_system_info_get_steam_issues:
 * @self: The #SrtSystemInfo object
 *
 * Detect and return any problems encountered with the Steam installation.
 *
 * Returns: Any problems detected with the Steam installation,
 *  or %SRT_STEAM_ISSUES_NONE if no problems were detected
 */
SrtSteamIssues
srt_system_info_get_steam_issues (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), SRT_STEAM_ISSUES_UNKNOWN);

  ensure_steam_cached (self);
  return srt_steam_get_issues (self->steam_data);
}

/**
 * srt_system_info_get_steam_details:
 * @self: The #SrtSystemInfo object
 *
 * Gather and return information about the Steam installation.
 *
 * Returns: (transfer full): An #SrtSteam object. Free with
 *  `g_object_unref ()`.
 */
SrtSteam *
srt_system_info_get_steam_details (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  ensure_steam_cached (self);
  return g_object_ref (self->steam_data);
}

/**
 * srt_system_info_dup_steam_installation_path:
 * @self: The #SrtSystemInfo object
 *
 * Return the absolute path to the Steam installation in use (the
 * directory containing `steam.sh` and `ubuntu12_32/` among other
 * files and directories).
 *
 * This directory is analogous to `C:\Program Files\Steam` in a
 * typical Windows installation of Steam, and is typically of the form
 * `/home/me/.local/share/Steam`. It is also known as the "Steam root",
 * and is canonically accessed via the symbolic link `~/.steam/root`
 * (known as the "Steam root link").
 *
 * Under normal circumstances, this is the same directory as
 * srt_system_info_dup_steam_data_path(). However, it is possible to
 * construct situations where they are different, for example when a
 * Steam developer tests a new client build in its own installation
 * directory in conjunction with an existing data directory from the
 * production client, or when Steam was first installed using a Debian
 * package that suffered from
 * [#916303](https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=916303)
 * (which resulted in `~/.steam/steam` being a plain directory, not a
 * symbolic link).
 *
 * If the Steam installation could not be found, flags will
 * be set in the result of srt_system_info_get_steam_issues() to indicate
 * why: at least %SRT_STEAM_ISSUES_CANNOT_FIND, and possibly others.
 *
 * Returns: (transfer full) (type filename) (nullable): The absolute path
 *  to the Steam installation, or %NULL if it could not be determined.
 *  Free with g_free().
 */
gchar *
srt_system_info_dup_steam_installation_path (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  ensure_steam_cached (self);
  return g_strdup (srt_steam_get_install_path (self->steam_data));
}

/**
 * srt_system_info_dup_steam_data_path:
 * @self: The #SrtSystemInfo object
 *
 * Return the absolute path to the Steam data directory in use (the
 * directory containing `appcache/`, `userdata/` and the default
 * `steamapps/` or `SteamApps/` installation path for games, among other
 * files and directories).
 *
 * This directory is analogous to `C:\Program Files\Steam` in a
 * typical Windows installation of Steam, and is typically of the form
 * `/home/me/.local/share/Steam`. It is canonically accessed via the
 * symbolic link `~/.steam/steam` (known as the "Steam data link").
 *
 * Under normal circumstances, this is the same directory as
 * srt_system_info_dup_steam_installation_path(). However, it is possible
 * to construct situations where they are different, for example when a
 * Steam developer tests a new client build in its own installation
 * directory in conjunction with an existing data directory from the
 * production client, or when Steam was first installed using a Debian
 * package that suffered from
 * [#916303](https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=916303)
 * (which resulted in `~/.steam/steam` being a plain directory, not a
 * symbolic link).
 *
 * If the Steam data could not be found, flags will
 * be set in the result of srt_system_info_get_steam_issues() to indicate
 * why: at least %SRT_STEAM_ISSUES_CANNOT_FIND_DATA, and possibly others.
 *
 * Returns: (transfer full) (type filename) (nullable): The absolute path
 *  to the Steam installation, or %NULL if it could not be determined.
 *  Free with g_free().
 */
gchar *
srt_system_info_dup_steam_data_path (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  ensure_steam_cached (self);
  return g_strdup (srt_steam_get_data_path (self->steam_data));
}

/**
 * srt_system_info_dup_steam_bin32_path:
 * @self: The #SrtSystemInfo object
 *
 * Return the absolute path to the Steam `ubuntu12_32` directory in use
 * (the directory containing `steam-runtime/` among other files and
 * directories).
 *
 * Under normal circumstances, this is a `ubuntu12_32` direct subdirectory
 * under the srt_system_info_dup_steam_installation_path().
 * Typically of the form `/home/me/.local/share/Steam/ubuntu12_32`.
 * It is canonically accessed via the symbolic link `~/.steam/bin32`.
 *
 * Returns: (transfer full) (type filename) (nullable): The absolute path
 *  to the Steam `ubuntu12_32` directory, or %NULL if it could not be
 *  determined. Free with g_free().
 */
gchar *
srt_system_info_dup_steam_bin32_path (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  ensure_steam_cached (self);
  return g_strdup (srt_steam_get_bin32_path (self->steam_data));
}

static void
ensure_os_cached (SrtSystemInfo *self)
{
  if (!self->os_release.populated
      && !self->immutable_values
      && self->sysroot_fd >= 0)
    _srt_os_release_populate (&self->os_release, self->sysroot,
                              self->sysroot_fd);
}

/**
 * srt_system_info_dup_os_build_id:
 * @self: The #SrtSystemInfo object
 *
 * Return a machine-readable identifier for the system image used as the
 * origin for a distribution, for example `0.20190925.0`. If called
 * from inside a Steam Runtime container, return the Steam Runtime build
 * ID, which currently looks like `0.20190925.0`.
 *
 * In operating systems that do not use image-based installation, such
 * as Debian, this will be %NULL.
 *
 * This is the `BUILD_ID` from os-release(5).
 *
 * Returns: (transfer full) (type utf8): The build ID, or %NULL if not known.
 *  Free with g_free().
 */
gchar *
srt_system_info_dup_os_build_id (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  ensure_os_cached (self);
  return g_strdup (self->os_release.build_id);
}

/**
 * srt_system_info_dup_os_id:
 * @self: The #SrtSystemInfo object
 *
 * Return a lower-case machine-readable operating system identifier,
 * for example `debian` or `arch`. If called from inside a Steam Runtime
 * container, return `steamrt`.
 *
 * This is the `ID` in os-release(5). If os-release(5) is not available,
 * future versions of this library might derive a similar ID from
 * lsb_release(1).
 *
 * Returns: (transfer full) (type utf8): The OS ID, or %NULL if not known.
 *  Free with g_free().
 */
gchar *
srt_system_info_dup_os_id (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  ensure_os_cached (self);
  return g_strdup (self->os_release.id);
}

static const char WHITESPACE[] = " \t\n\r";

/**
 * srt_system_info_dup_os_id_like:
 * @self: The #SrtSystemInfo object
 * @include_self: If %TRUE, include srt_system_info_dup_os_id() in the
 *  returned array (if known)
 *
 * Return an array of lower-case machine-readable operating system
 * identifiers similar to srt_system_info_dup_os_id() describing OSs
 * that this one resembles or is derived from.
 *
 * For example, the Steam Runtime 1 'scout' is derived from Ubuntu,
 * which is itself derived from Debian, so srt_system_info_dup_os_id_like()
 * would return `{ "debian", "ubuntu", NULL }` if @include_self is false,
 * `{ "steamrt", "debian", "ubuntu", NULL }` otherwise.
 *
 * This is the `ID_LIKE` field from os-release(5), possibly combined
 * with the `ID` field.
 *
 * Returns: (array zero-terminated=1) (transfer full) (element-type utf8) (nullable): An
 *  array of OS IDs, or %NULL if nothing is known.
 *  Free with g_strfreev().
 */
gchar **
srt_system_info_dup_os_id_like (SrtSystemInfo *self,
                                gboolean include_self)
{
  GPtrArray *builder;
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  ensure_os_cached (self);

  builder = g_ptr_array_new_with_free_func (g_free);

  if (self->os_release.id != NULL && include_self)
    g_ptr_array_add (builder, g_strdup (self->os_release.id));

  if (self->os_release.id_like != NULL)
    {
      GStrv split;
      gsize i;

      split = g_strsplit_set (self->os_release.id_like, WHITESPACE, -1);

      for (i = 0; split != NULL && split[i] != NULL; i++)
        g_ptr_array_add (builder, g_steal_pointer (&split[i]));

      /* We already transferred ownership of the contents */
      g_free (split);
    }

  if (builder->len > 0)
    {
      g_ptr_array_add (builder, NULL);
      return (gchar **) g_ptr_array_free (builder, FALSE);
    }
  else
    {
      g_ptr_array_free (builder, TRUE);
      return NULL;
    }
}

/**
 * srt_system_info_dup_os_name:
 * @self: The #SrtSystemInfo object
 *
 * Return a human-readable identifier for the operating system without
 * its version, for example `Debian GNU/Linux` or `Arch Linux`.
 *
 * This is the `NAME` in os-release(5). If os-release(5) is not
 * available, future versions of this library might derive a similar
 * name from lsb_release(1).
 *
 * Returns: (transfer full) (type utf8): The name, or %NULL if not known.
 *  Free with g_free().
 */
gchar *
srt_system_info_dup_os_name (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  ensure_os_cached (self);
  return g_strdup (self->os_release.name);
}

/**
 * srt_system_info_dup_os_pretty_name:
 * @self: The #SrtSystemInfo object
 *
 * Return a human-readable identifier for the operating system,
 * including its version if any, for example `Debian GNU/Linux 10 (buster)`
 * or `Arch Linux`.
 *
 * If the OS uses rolling releases, this will probably be the same as
 * or similar to srt_system_info_dup_os_name().
 *
 * This is the `PRETTY_NAME` in os-release(5). If os-release(5) is not
 * available, future versions of this library might derive a similar
 * name from lsb_release(1).
 *
 * Returns: (transfer full) (type utf8): The name, or %NULL if not known.
 *  Free with g_free().
 */
gchar *
srt_system_info_dup_os_pretty_name (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  ensure_os_cached (self);
  return g_strdup (self->os_release.pretty_name);
}

/**
 * srt_system_info_dup_os_variant:
 * @self: The #SrtSystemInfo object
 *
 * Return a human-readable identifier for the operating system variant,
 * for example `Workstation Edition`, `Server Edition` or
 * `Raspberry Pi Edition`. In operating systems that do not have
 * formal variants this will usually be %NULL.
 *
 * This is the `VARIANT` in os-release(5).
 *
 * Returns: (transfer full) (type utf8): The name, or %NULL if not known.
 *  Free with g_free().
 */
gchar *
srt_system_info_dup_os_variant (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  ensure_os_cached (self);
  return g_strdup (self->os_release.variant);
}

/**
 * srt_system_info_dup_os_variant_id:
 * @self: The #SrtSystemInfo object
 *
 * Return a lower-case machine-readable identifier for the operating system
 * variant in a form suitable for use in filenames, for example
 * `workstation`, `server` or `rpi`. In operating systems that do not
 * have formal variants this will usually be %NULL.
 *
 * This is the `VARIANT_ID` in os-release(5).
 *
 * Returns: (transfer full) (type utf8): The variant ID, or %NULL if not known.
 *  Free with g_free().
 */
gchar *
srt_system_info_dup_os_variant_id (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  ensure_os_cached (self);
  return g_strdup (self->os_release.variant_id);
}

/**
 * srt_system_info_dup_os_version_codename:
 * @self: The #SrtSystemInfo object
 *
 * Return a lower-case machine-readable identifier for the operating
 * system version codename, for example `buster` for Debian 10 "buster".
 * In operating systems that do not use codenames in machine-readable
 * contexts, this will usually be %NULL.
 *
 * This is the `VERSION_CODENAME` in os-release(5). If os-release(5) is not
 * available, future versions of this library might derive a similar
 * codename from lsb_release(1).
 *
 * Returns: (transfer full) (type utf8): The codename, or %NULL if not known.
 *  Free with g_free().
 */
gchar *
srt_system_info_dup_os_version_codename (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  ensure_os_cached (self);
  return g_strdup (self->os_release.version_codename);
}

/**
 * srt_system_info_dup_os_version_id:
 * @self: The #SrtSystemInfo object
 *
 * Return a machine-readable identifier for the operating system version,
 * for example `10` for Debian 10 "buster". In operating systems that
 * only have rolling releases, such as Arch Linux, or in OS branches
 * that behave like rolling releases, such as Debian unstable, this
 * will usually be %NULL.
 *
 * This is the `VERSION_ID` in os-release(5). If os-release(5) is not
 * available, future versions of this library might derive a similar
 * identifier from lsb_release(1).
 *
 * Returns: (transfer full) (type utf8): The ID, or %NULL if not known.
 *  Free with g_free().
 */
gchar *
srt_system_info_dup_os_version_id (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  ensure_os_cached (self);
  return g_strdup (self->os_release.version_id);
}

/**
 * srt_system_info_set_expected_runtime_version:
 * @self: The #SrtSystemInfo object
 * @version: (nullable): The expected version number, such as `0.20190711.3`,
 *  or %NULL if there is no particular expectation
 *
 * Set the expected version number of the Steam Runtime. Invalidate any
 * cached information about the Steam Runtime if it differs from the
 * previous expectation.
 */
void
srt_system_info_set_expected_runtime_version (SrtSystemInfo *self,
                                              const char *version)
{
  g_return_if_fail (SRT_IS_SYSTEM_INFO (self));

  if (self->immutable_values)
  {
    g_clear_pointer (&self->runtime.expected_version, g_free);
    self->runtime.expected_version = g_strdup (version);
    if (self->runtime.expected_version == NULL
        || self->runtime.version == NULL
        || g_strcmp0 (self->runtime.expected_version, self->runtime.version) == 0)
    {
      self->runtime.issues &= ~SRT_RUNTIME_ISSUES_UNEXPECTED_VERSION;
    }
    else
    {
      self->runtime.issues |= SRT_RUNTIME_ISSUES_UNEXPECTED_VERSION;
    }
  }
  else if (g_strcmp0 (version, self->runtime.expected_version) != 0)
  {
    forget_runtime (self);
    g_clear_pointer (&self->runtime.expected_version, g_free);
    self->runtime.expected_version = g_strdup (version);
  }
}

/**
 * srt_system_info_dup_expected_runtime_version:
 * @self: The #SrtSystemInfo object
 *
 * Returns: (transfer full) (type utf8): The expected version number of
 *  the Steam Runtime, or %NULL if no particular version is expected.
 *  Free with g_free().
 */
gchar *
srt_system_info_dup_expected_runtime_version (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  return g_strdup (self->runtime.expected_version);
}

static void
ensure_runtime_cached (SrtSystemInfo *self)
{
  if (self->immutable_values)
    return;

  ensure_os_cached (self);
  ensure_steam_cached (self);

  if (self->runtime.issues == SRT_RUNTIME_ISSUES_NONE &&
      self->runtime.path == NULL)
    {
      if (g_strcmp0 (self->os_release.id, "steamrt") == 0)
        {
          self->runtime.path = g_strdup ("/");
          self->runtime.version = g_strdup (self->os_release.build_id);

          if (self->runtime.expected_version != NULL
              && g_strcmp0 (self->runtime.expected_version, self->runtime.version) != 0)
            {
              self->runtime.issues |= SRT_RUNTIME_ISSUES_UNEXPECTED_VERSION;
            }

          if (self->runtime.version == NULL)
            {
              self->runtime.issues |= SRT_RUNTIME_ISSUES_NOT_RUNTIME;
            }
          else
            {
              const char *p;

              for (p = self->runtime.version; *p != '\0'; p++)
                {
                  if (!g_ascii_isdigit (*p) && *p != '.')
                    self->runtime.issues |= SRT_RUNTIME_ISSUES_UNOFFICIAL;
                }
            }
        }
      else
        {
          self->runtime.issues = _srt_runtime_check (srt_steam_get_bin32_path (self->steam_data),
                                                     self->runtime.expected_version,
                                                     self->env,
                                                     &self->runtime.version,
                                                     &self->runtime.path);
        }
    }
}

/**
 * srt_system_info_get_runtime_issues:
 * @self: The #SrtSystemInfo object
 *
 * Detect and return any problems encountered with the Steam Runtime.
 *
 * Returns: Any problems detected with the Steam Runtime,
 *  or %SRT_RUNTIME_ISSUES_NONE if no problems were detected
 */
SrtRuntimeIssues
srt_system_info_get_runtime_issues (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self),
                        SRT_RUNTIME_ISSUES_UNKNOWN);

  ensure_runtime_cached (self);
  return self->runtime.issues;
}

/**
 * srt_system_info_dup_runtime_path:
 * @self: The #SrtSystemInfo object
 *
 * Return the absolute path to the Steam Runtime in use.
 *
 * For the `LD_LIBRARY_PATH`-based Steam Runtime, this is the directory
 * containing `run.sh`, `version.txt` and similar files.
 *
 * If running in a Steam Runtime container or chroot, this function
 * returns `/` to indicate that the entire container is the Steam Runtime.
 *
 * This will typically be below
 * srt_system_info_dup_steam_installation_path(), unless overridden.
 *
 * If the Steam Runtime has been disabled or could not be found, at
 * least one flag will be set in the result of
 * srt_system_info_get_runtime_issues() to indicate why.
 *
 * Returns: (transfer full) (type filename) (nullable): The absolute path
 *  to the Steam Runtime, or %NULL if it could not be determined.
 *  Free with g_free().
 */
gchar *
srt_system_info_dup_runtime_path (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  ensure_runtime_cached (self);
  return g_strdup (self->runtime.path);
}

/**
 * srt_system_info_dup_runtime_version:
 * @self: The #SrtSystemInfo object
 *
 * Return the version number of the Steam Runtime
 * in use, for example `0.20190711.3`, or %NULL if it could not be
 * determined. This could either be the `LD_LIBRARY_PATH`-based Steam
 * Runtime, or a Steam Runtime container or chroot.
 *
 * If the Steam Runtime has been disabled or could not be found, or its
 * version number could not be read, then at least one flag will be set
 * in the result of srt_system_info_get_runtime_issues() to indicate why.
 *
 * Returns: (transfer full) (type utf8) (nullable): The version number of
 *  the Steam Runtime, or %NULL if it could not be determined.
 *  Free with g_free().
 */
gchar *
srt_system_info_dup_runtime_version (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  ensure_runtime_cached (self);
  return g_strdup (self->runtime.version);
}

/**
 * srt_system_info_set_helpers_path:
 * @self: The #SrtSystemInfo
 * @path: (nullable) (type filename) (transfer none): An absolute path
 *
 * Look for helper executables used to inspect the system state in @path,
 * instead of the normal installed location.
 *
 * If @path is %NULL, go back to using the installed location.
 *
 * This method is not valid to call on a #SrtSystemInfo that was
 * constructed with srt_system_info_new_from_json().
 */
void
srt_system_info_set_helpers_path (SrtSystemInfo *self,
                                  const gchar *path)
{
  g_return_if_fail (SRT_IS_SYSTEM_INFO (self));
  g_return_if_fail (!self->immutable_values);

  forget_graphics_modules (self);
  forget_libraries (self);
  forget_graphics_results (self);
  forget_locales (self);
  free (self->helpers_path);
  self->helpers_path = g_strdup (path);
}

/**
 * srt_system_info_get_primary_multiarch_tuple:
 * @self: The #SrtSystemInfo
 *
 * Return the multiarch tuple set by
 * srt_system_info_set_primary_multiarch_tuple() if any,
 * or the multiarch tuple corresponding to the steam-runtime-tools
 * library itself.
 *
 * Returns: (type filename) (transfer none): a Debian-style multiarch
 *  tuple such as %SRT_ABI_I386 or %SRT_ABI_X86_64
 */
const char *
srt_system_info_get_primary_multiarch_tuple (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  return g_quark_to_string (g_array_index (self->multiarch_tuples, GQuark, 0));
}

/**
 * srt_system_info_set_primary_multiarch_tuple:
 * @self: The #SrtSystemInfo
 * @tuple: (nullable) (type filename) (transfer none): A Debian-style
 *  multiarch tuple such as %SRT_ABI_X86_64
 *
 * Set the primary architecture that is used to find helper
 * executables where the architecture does not matter, such as
 * checking locales.
 * The primary architecture is the first element from the list of
 * ABIs, editable by `srt_system_info_set_multiarch_tuples()`.
 * If the list was not %NULL, the previous primary multiarch
 * tuple will be demoted to be foreign and the new primary
 * multiarch tuple will be moved/added to the top of the list.
 *
 * If @tuple is %NULL, revert to the default behaviour, which
 * is that the primary architecture is the architecture of the
 * steam-runtime-tools library.
 *
 * This method is not valid to call on a #SrtSystemInfo that was
 * constructed with srt_system_info_new_from_json().
 */
void
srt_system_info_set_primary_multiarch_tuple (SrtSystemInfo *self,
                                             const gchar *tuple)
{
  GQuark primary;
  g_return_if_fail (SRT_IS_SYSTEM_INFO (self));
  g_return_if_fail (!self->immutable_values);

  forget_locales (self);

  if (tuple)
    primary = g_quark_from_string (tuple);
  else if (strcmp (_SRT_MULTIARCH, "") == 0)
    primary = g_quark_from_static_string ("UNKNOWN");
  else
    primary = g_quark_from_static_string (_SRT_MULTIARCH);

  for (gsize i = 0; i < self->multiarch_tuples->len; i++)
    {
      GQuark stored_tuple = g_array_index (self->multiarch_tuples, GQuark, i);
      if (stored_tuple == primary)
        {
          g_array_remove_index (self->multiarch_tuples, i);
          break;
        }
    }

  g_array_prepend_val (self->multiarch_tuples, primary);
}

/**
 * srt_system_info_set_multiarch_tuples:
 * @self: The #SrtSystemInfo
 * @tuples: (nullable) (array zero-terminated=1) (element-type filename) (transfer none):
 *  An array of Debian-style multiarch tuples such as %SRT_ABI_X86_64 terminated by a
 *  %NULL entry, or %NULL
 *
 * Set the list of ABIs that will be checked by default in contexts
 * where multiple ABIs are inspected at the same time, such as
 * `srt_system_info_list_dri_drivers()`. The first one listed is
 * assumed to be the primary architecture, and is used to find helper
 * executables where the architecture does not matter, such as
 * checking locales. The second and subsequent (if any) are assumed
 * to be "foreign" architectures.
 *
 * If @tuples is %NULL or empty, revert to the default behaviour,
 * which is that the primary architecture is the architecture of the
 * steam-runtime-tools library, and there are no foreign architectures.
 *
 * For example, to check the x86_64 and i386 ABIs on an x86 platform
 * or the native ABI otherwise, you might use code like this:
 *
 * |[
 * #ifdef __x86_64__
 * const char
 * const tuples[] = { SRT_ABI_X86_64, SRT_ABI_I386, NULL };
 * #elif defined(__i386__)
 * const char
 * const tuples[] = { SRT_ABI_I386, SRT_ABI_X86_64, NULL };
 * #else
 * const char
 * const tuples[] = { NULL };
 * #endif
 *
 * srt_system_info_set_multiarch_tuples (info, tuples);
 * ]|
 *
 * This method is not valid to call on a #SrtSystemInfo that was
 * constructed with srt_system_info_new_from_json().
 */
void
srt_system_info_set_multiarch_tuples (SrtSystemInfo *self,
                                      const gchar * const *tuples)
{
  g_return_if_fail (SRT_IS_SYSTEM_INFO (self));
  g_return_if_fail (!self->immutable_values);

  forget_locales (self);

  g_array_set_size (self->multiarch_tuples, 0);

  if (tuples == NULL || *tuples == NULL)
    {
      GQuark primary;
      if (strcmp (_SRT_MULTIARCH, "") == 0)
        primary = g_quark_from_static_string ("UNKNOWN");
      else
        primary = g_quark_from_static_string (_SRT_MULTIARCH);

      g_array_prepend_val (self->multiarch_tuples, primary);
    }

  for (const gchar * const *tuples_iter = tuples;
       tuples_iter != NULL && *tuples_iter != NULL;
       tuples_iter++)
    {
      GQuark tuple = g_quark_from_string (*tuples_iter);
      g_array_append_val (self->multiarch_tuples, tuple);
    }
}

/**
 * srt_system_info_dup_multiarch_tuples:
 * @self: the system information object
 *
 * Return the ABIs for which this object will carry out checks,
 * in the form of multiarch tuples as printed by
 * `gcc -print-multiarch` in the Steam Runtime (in practice
 * this means %SRT_ABI_I386 or %SRT_ABI_X86_64).
 *
 * The ABIs that are to be checked can be set with
 * srt_system_info_set_multiarch_tuples().
 *
 * Returns: (array zero-terminated=1) (transfer full): The
 * ABIs used for various checks. Free with g_strfreev().
 */
GStrv srt_system_info_dup_multiarch_tuples (SrtSystemInfo *self)
{
  GStrv ret;
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  ret = g_new0 (gchar *, self->multiarch_tuples->len + 1);

  for (gsize i = 0; i < self->multiarch_tuples->len; i++)
    {
      GQuark tuple = g_array_index (self->multiarch_tuples, GQuark, i);
      ret[i] = g_strdup (g_quark_to_string (tuple));
    }

  ret[self->multiarch_tuples->len] = NULL;
  return ret;
}

/**
 * srt_system_info_get_locale_issues:
 * @self: The #SrtSystemInfo
 *
 * Check that the locale specified by environment variables, and some
 * other commonly-assumed locales, are available and suitable.
 *
 * Returns: A summary of issues found, or %SRT_LOCALE_ISSUES_NONE
 *  if no problems are detected
 */
SrtLocaleIssues
srt_system_info_get_locale_issues (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), SRT_LOCALE_ISSUES_UNKNOWN);

  if (!self->locales.have_issues && !self->immutable_values)
    {
      SrtLocale *locale = NULL;

      self->locales.issues = SRT_LOCALE_ISSUES_NONE;

      locale = srt_system_info_check_locale (self, "", NULL);

      if (locale == NULL)
        self->locales.issues |= SRT_LOCALE_ISSUES_DEFAULT_MISSING;
      else if (!srt_locale_is_utf8 (locale))
        self->locales.issues |= SRT_LOCALE_ISSUES_DEFAULT_NOT_UTF8;

      g_clear_object (&locale);

      locale = srt_system_info_check_locale (self, "C.UTF-8", NULL);

      if (locale == NULL || !srt_locale_is_utf8 (locale))
        self->locales.issues |= SRT_LOCALE_ISSUES_C_UTF8_MISSING;

      g_clear_object (&locale);

      locale = srt_system_info_check_locale (self, "en_US.UTF-8", NULL);

      if (locale == NULL || !srt_locale_is_utf8 (locale))
        self->locales.issues |= SRT_LOCALE_ISSUES_EN_US_UTF8_MISSING;

      g_clear_object (&locale);

      self->locales.have_issues = TRUE;

      /* We currently only look for I18NDIR data in /usr/share/i18n (the
       * glibc default path), so these checks only look there too.
       *
       * If we discover that some distros use a different default, then
       * we should enhance this check to iterate through a search path.
       *
       * Please keep this in sync with pressure-vessel-locale-gen. */

      if (!g_file_test ("/usr/share/i18n/SUPPORTED", G_FILE_TEST_IS_REGULAR))
        self->locales.issues |= SRT_LOCALE_ISSUES_I18N_SUPPORTED_MISSING;

      if (!g_file_test ("/usr/share/i18n/locales/en_US", G_FILE_TEST_IS_REGULAR))
        self->locales.issues |= SRT_LOCALE_ISSUES_I18N_LOCALES_EN_US_MISSING;
    }

  return self->locales.issues;
}

/**
 * srt_system_info_check_locale:
 * @self: The #SrtSystemInfo
 * @requested_name: The locale to request, for example `en_US.UTF-8`.
 *  This may be the empty string or %NULL to request the empty string
 *  as a locale, which uses environment variables like `$LC_ALL`.
 * @error: Used to return an error on failure
 *
 * Check whether the given locale can be set successfully.
 *
 * Returns: (transfer full) (nullable): A #SrtLocale object, or %NULL
 *  if the requested locale could not be set.
 *  Free with g_object_unref() if non-%NULL.
 */
SrtLocale *
srt_system_info_check_locale (SrtSystemInfo *self,
                              const char *requested_name,
                              GError **error)
{
  GQuark quark = 0;
  gpointer value = NULL;
  MaybeLocale *maybe;

  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  if (requested_name == NULL)
    quark = g_quark_from_string ("");
  else
    quark = g_quark_from_string (requested_name);

  if (self->locales.cached_locales == NULL)
    self->locales.cached_locales = g_hash_table_new_full (NULL, NULL, NULL,
                                                          maybe_locale_free);

  if (g_hash_table_lookup_extended (self->locales.cached_locales,
                                    GUINT_TO_POINTER (quark),
                                    NULL,
                                    &value))
    {
      maybe = value;
    }
  else if (self->immutable_values)
    {
      g_set_error (error, SRT_LOCALE_ERROR, SRT_LOCALE_ERROR_INTERNAL_ERROR,
                   "Information about the requested locale is missing");
      return NULL;
    }
  else
    {
      GError *local_error = NULL;
      SrtLocale *locale = NULL;

      locale = _srt_check_locale (self->env,
                                  self->helpers_path,
                                  srt_system_info_get_primary_multiarch_tuple (self),
                                  g_quark_to_string (quark),
                                  &local_error);

      if (locale != NULL)
        {
          maybe = maybe_locale_new_positive (locale);
          g_object_unref (locale);
        }
      else
        {
          maybe = maybe_locale_new_negative (local_error);
        }

      g_hash_table_replace (self->locales.cached_locales,
                            GUINT_TO_POINTER (quark),
                            maybe);
      g_clear_error (&local_error);
    }

  if (maybe->locale != NULL)
    {
      g_assert (SRT_IS_LOCALE (maybe->locale));
      g_assert (maybe->error == NULL);
      return g_object_ref (maybe->locale);
    }
  else
    {
      g_assert (maybe->error != NULL);
      g_set_error_literal (error,
                           maybe->error->domain,
                           maybe->error->code,
                           maybe->error->message);
      return NULL;
    }
}

/**
 * srt_system_info_set_test_flags:
 * @self: The #SrtSystemInfo
 * @flags: Flags altering behaviour, for use in automated tests
 *
 * Alter the behaviour of the #SrtSystemInfo to make automated tests
 * quicker or give better test coverage.
 *
 * This function should not be called in production code.
 */
void
srt_system_info_set_test_flags (SrtSystemInfo *self,
                                SrtTestFlags flags)
{
  g_return_if_fail (SRT_IS_SYSTEM_INFO (self));
  self->test_flags = flags;
}

/**
 * srt_system_info_list_egl_icds:
 * @self: The #SrtSystemInfo object
 * @multiarch_tuples: (nullable) (array zero-terminated=1) (element-type utf8):
 *  A list of multiarch tuples like %SRT_ABI_I386, representing ABIs, or %NULL
 *  to not look in any ABI-specific locations. This is currently only used if
 *  running in a Flatpak environment.
 *
 * List the available EGL ICDs, using the same search paths as GLVND.
 *
 * This function is not architecture-specific and may return a mixture
 * of ICDs for more than one architecture or ABI, because the way the
 * GLVND EGL loader works is to read a single search path for metadata
 * describing ICDs, then filter out the ones that are for the wrong
 * architecture at load time.
 *
 * Some of the entries in the result might describe a bare SONAME in the
 * standard library search path, which might exist for any or all
 * architectures simultaneously (this is the most common approach for EGL).
 * Other entries might describe the relative or absolute path to a
 * specific library, which will only be usable for the architecture for
 * which it was compiled.
 *
 * @multiarch_tuples is used if running in a Flatpak environment, to
 * match the search paths used by the freedesktop.org runtime's patched
 * GLVND.
 *
 * Returns: (transfer full) (element-type SrtEglIcd): A list of
 *  opaque #SrtEglIcd objects. Free with
 *  `g_list_free_full(icds, srt_egl_icd_unref)`.
 */
GList *
srt_system_info_list_egl_icds (SrtSystemInfo *self,
                               const char * const *multiarch_tuples)
{
  GList *ret = NULL;
  const GList *iter;

  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  if (!self->icds.have_egl && !self->immutable_values)
    {
      g_assert (self->icds.egl == NULL);
      self->icds.egl = _srt_load_egl_icds (self->sysroot, self->env,
                                           multiarch_tuples);
      self->icds.have_egl = TRUE;
    }

  for (iter = self->icds.egl; iter != NULL; iter = iter->next)
    ret = g_list_prepend (ret, g_object_ref (iter->data));

  return g_list_reverse (ret);
}

/**
 * srt_system_info_list_vulkan_icds:
 * @self: The #SrtSystemInfo object
 * @multiarch_tuples: (nullable) (array zero-terminated=1) (element-type utf8):
 *  A list of multiarch tuples like %SRT_ABI_I386, representing ABIs, or %NULL
 *  to not look in any ABI-specific locations. This is currently only used if
 *  running in a Flatpak environment.
 *
 * List the available Vulkan ICDs, using the same search paths as the
 * reference vulkan-loader.
 *
 * This function is not architecture-specific and may return a mixture
 * of ICDs for more than one architecture or ABI, because the way the
 * reference vulkan-loader works is to read a single search path for
 * metadata describing ICDs, then filter out the ones that are for the
 * wrong architecture at load time.
 *
 * Some of the entries in the result might describe a bare SONAME in the
 * standard library search path, which might exist for any or all
 * architectures simultaneously (for example, this approach is used for
 * the NVIDIA binary driver on Debian systems). Other entries might
 * describe the relative or absolute path to a specific library, which
 * will only be usable for the architecture for which it was compiled
 * (for example, this approach is used in Mesa).
 *
 * @multiarch_tuples is used if running in a Flatpak environment, to
 * match the search paths used by the freedesktop.org runtime's patched
 * vulkan-loader.
 *
 * Returns: (transfer full) (element-type SrtVulkanIcd): A list of
 *  opaque #SrtVulkanIcd objects. Free with
 *  `g_list_free_full(icds, srt_vulkan_icd_unref)`.
 */
GList *
srt_system_info_list_vulkan_icds (SrtSystemInfo *self,
                                  const char * const *multiarch_tuples)
{
  GList *ret = NULL;
  const GList *iter;

  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  if (!self->icds.have_vulkan && !self->immutable_values)
    {
      g_assert (self->icds.vulkan == NULL);
      self->icds.vulkan = _srt_load_vulkan_icds (self->sysroot, self->env,
                                                 multiarch_tuples);
      self->icds.have_vulkan = TRUE;
    }

  for (iter = self->icds.vulkan; iter != NULL; iter = iter->next)
    ret = g_list_prepend (ret, g_object_ref (iter->data));

  return g_list_reverse (ret);
}

/**
 * srt_system_info_list_explicit_vulkan_layers:
 * @self: The #SrtSystemInfo object
 *
 * List the available explicit Vulkan layers, using the same search paths as
 * the reference vulkan-loader.
 *
 * This function is not architecture-specific and may return a mixture
 * of layers for more than one architecture or ABI, because the way the
 * reference vulkan-loader works is to read a single search path for
 * metadata describing layers, then filter out the ones that are for the
 * wrong architecture at load time.
 *
 * Some of the entries in the result might describe a bare SONAME in the
 * standard library search path, which might exist for any or all
 * architectures simultaneously. Other entries might describe the relative
 * or absolute path to a specific library, which will only be usable for
 * the architecture for which it was compiled.
 *
 * Returns: (transfer full) (element-type SrtVulkanLayer): A list of
 *  opaque #SrtVulkanLayer objects. Free with
 *  `g_list_free_full(layers, g_object_unref)`.
 */
GList *
srt_system_info_list_explicit_vulkan_layers (SrtSystemInfo *self)
{
  GList *ret = NULL;
  const GList *iter;

  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  if (!self->layers.have_vulkan_explicit && !self->immutable_values)
    {
      g_assert (self->layers.vulkan_explicit == NULL);
      self->layers.vulkan_explicit = _srt_load_vulkan_layers (self->sysroot, self->env, TRUE);
      self->layers.have_vulkan_explicit = TRUE;
    }

  for (iter = self->layers.vulkan_explicit; iter != NULL; iter = iter->next)
    ret = g_list_prepend (ret, g_object_ref (iter->data));

  return g_list_reverse (ret);
}

/**
 * srt_system_info_list_implicit_vulkan_layers:
 * @self: The #SrtSystemInfo object
 *
 * List the available implicit Vulkan layers, using the same search paths as
 * the reference vulkan-loader.
 *
 * This function is not architecture-specific and may return a mixture
 * of layers for more than one architecture or ABI, because the way the
 * reference vulkan-loader works is to read a single search path for
 * metadata describing layers, then filter out the ones that are for the
 * wrong architecture at load time.
 *
 * Some of the entries in the result might describe a bare SONAME in the
 * standard library search path, which might exist for any or all
 * architectures simultaneously. Other entries might describe the relative
 * or absolute path to a specific library, which will only be usable for
 * the architecture for which it was compiled.
 *
 * Returns: (transfer full) (element-type SrtVulkanLayer): A list of
 *  opaque #SrtVulkanLayer objects. Free with
 *  `g_list_free_full(layers, g_object_unref)`.
 */
GList *
srt_system_info_list_implicit_vulkan_layers (SrtSystemInfo *self)
{
  GList *ret = NULL;
  const GList *iter;

  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  if (!self->layers.have_vulkan_implicit && !self->immutable_values)
    {
      g_assert (self->layers.vulkan_implicit == NULL);
      self->layers.vulkan_implicit = _srt_load_vulkan_layers (self->sysroot, self->env, FALSE);
      self->layers.have_vulkan_implicit = TRUE;
    }

  for (iter = self->layers.vulkan_implicit; iter != NULL; iter = iter->next)
    ret = g_list_prepend (ret, g_object_ref (iter->data));

  return g_list_reverse (ret);
}

/* Maybe they should implement a common GInterface or have a common
 * base class or something, but for now we do this the easy way */
static gboolean
graphics_module_is_extra (SrtGraphicsModule which,
                          gpointer object)
{
  switch (which)
    {
      case SRT_GRAPHICS_DRI_MODULE:
        return srt_dri_driver_is_extra (object);

      case SRT_GRAPHICS_VAAPI_MODULE:
        return srt_va_api_driver_is_extra (object);

      case SRT_GRAPHICS_VDPAU_MODULE:
        return srt_vdpau_driver_is_extra (object);

      /* We don't have extras for GLX, yet */
      case SRT_GRAPHICS_GLX_MODULE:
        return FALSE;

      case NUM_SRT_GRAPHICS_MODULES:
      default:
        g_return_val_if_reached (FALSE);
    }
}

static GList *
_srt_system_info_list_graphics_modules (SrtSystemInfo *self,
                                        const char *multiarch_tuple,
                                        SrtDriverFlags flags,
                                        SrtGraphicsModule which)
{
  Abi *abi = NULL;
  GList *ret = NULL;
  const GList *iter;

  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);
  g_return_val_if_fail (multiarch_tuple != NULL, NULL);
  g_return_val_if_fail ((int) which >= 0, NULL);
  g_return_val_if_fail ((int) which < NUM_SRT_GRAPHICS_MODULES, NULL);

  abi = ensure_abi_unless_immutable (self, multiarch_tuple);

  if (abi == NULL)
    return NULL;

  if (!abi->graphics_modules[which].available && !self->immutable_values)
    {
      abi->graphics_modules[which].modules = _srt_list_graphics_modules (self->sysroot,
                                                                         self->env,
                                                                         self->helpers_path,
                                                                         multiarch_tuple,
                                                                         which);
      abi->graphics_modules[which].available = TRUE;
    }

  for (iter = abi->graphics_modules[which].modules;
       iter != NULL;
       iter = iter->next)
    {
      if ((flags & SRT_DRIVER_FLAGS_INCLUDE_ALL) == 0 &&
          graphics_module_is_extra (which, iter->data))
        continue;

      ret = g_list_prepend (ret, g_object_ref (iter->data));
    }

  return g_list_reverse (ret);
}

/**
 * srt_system_info_list_dri_drivers:
 * @self: The #SrtSystemInfo object
 * @multiarch_tuple: (not nullable) (type filename): A Debian-style multiarch
 *  tuple such as %SRT_ABI_X86_64
 * @flags: Filter the list of DRI drivers accordingly to these flags.
 *  For example, "extra" drivers that are unlikely to be found by
 *  the Mesa DRI loader will only be included if the
 *  %SRT_DRIVER_FLAGS_INCLUDE_ALL flag is set.
 *
 * List of the available Mesa DRI modules.
 *
 * For the search $LIBGL_DRIVERS_PATH will be used if set.
 * Otherwise some implementation-dependent paths will be used instead.
 *
 * Note that if `$LIBGL_DRIVERS_PATH` is set, all drivers outside that
 * path will be treated as "extra", and omitted from the list unless
 * %SRT_DRIVER_FLAGS_INCLUDE_ALL is used.
 *
 * Returns: (transfer full) (element-type SrtDriDriver) (nullable): A list of
 *  opaque #SrtDriDriver objects, or %NULL if nothing was found. Free with
 *  `g_list_free_full(list, g_object_unref)`.
 */
GList *
srt_system_info_list_dri_drivers (SrtSystemInfo *self,
                                  const char *multiarch_tuple,
                                  SrtDriverFlags flags)
{
  return _srt_system_info_list_graphics_modules (self, multiarch_tuple, flags,
                                                 SRT_GRAPHICS_DRI_MODULE);
}

/**
 * srt_system_info_list_va_api_drivers:
 * @self: The #SrtSystemInfo object
 * @multiarch_tuple: (not nullable) (type filename): A Debian-style multiarch
 *  tuple such as %SRT_ABI_X86_64
 * @flags: Filter the list of VA-API drivers accordingly to these flags.
 *  For example, "extra" drivers that are unlikely to be found by
 *  the VA-API loader will only be included if the
 *  %SRT_DRIVER_FLAGS_INCLUDE_ALL flag is set.
 *
 * List of the available VA-API drivers.
 *
 * For the search $LIBVA_DRIVERS_PATH will be used if set.
 * Otherwise some implementation-dependent paths will be used instead.
 *
 * Note that if `$LIBVA_DRIVERS_PATH` is set, all drivers outside that
 * path will be treated as "extra", and omitted from the list unless
 * %SRT_DRIVER_FLAGS_INCLUDE_ALL is used.
 *
 * Returns: (transfer full) (element-type SrtVaApiDriver) (nullable): A list of
 *  opaque #SrtVaApiDriver objects, or %NULL if nothing was found. Free with
 *  `g_list_free_full(list, g_object_unref)`.
 */
GList *
srt_system_info_list_va_api_drivers (SrtSystemInfo *self,
                                     const char *multiarch_tuple,
                                     SrtDriverFlags flags)
{
  return _srt_system_info_list_graphics_modules (self, multiarch_tuple, flags,
                                                 SRT_GRAPHICS_VAAPI_MODULE);
}

/**
 * srt_system_info_list_vdpau_drivers:
 * @self: The #SrtSystemInfo object
 * @multiarch_tuple: (not nullable) (type filename): A Debian-style multiarch
 *  tuple such as %SRT_ABI_X86_64
 * @flags: Filter the list of VDPAU drivers accordingly to these flags.
 *  For example, "extra" drivers that are unlikely to be found by
 *  the VDPAU loader will only be included if the
 *  %SRT_DRIVER_FLAGS_INCLUDE_ALL flag is set.
 *
 * List of the available VDPAU drivers.
 *
 * For the search $VDPAU_DRIVER_PATH will be used if set.
 * Otherwise some implementation-dependent paths will be used instead.
 *
 * Note that if `$VDPAU_DRIVER_PATH` is set, all drivers outside that
 * path will be treated as "extra", and omitted from the list unless
 * %SRT_DRIVER_FLAGS_INCLUDE_ALL is used.
 *
 * Returns: (transfer full) (element-type SrtVaApiDriver) (nullable): A list of
 *  opaque #SrtVaApiDriver objects, or %NULL if nothing was found. Free with
 *  `g_list_free_full(list, g_object_unref)`.
 */
GList *
srt_system_info_list_vdpau_drivers (SrtSystemInfo *self,
                                    const char *multiarch_tuple,
                                    SrtDriverFlags flags)
{
  return _srt_system_info_list_graphics_modules (self, multiarch_tuple, flags,
                                                 SRT_GRAPHICS_VDPAU_MODULE);
}

/**
 * srt_system_info_list_glx_icds:
 * @self: The #SrtSystemInfo object
 * @multiarch_tuple: (not nullable) (type filename): A Debian-style multiarch
 *  tuple such as %SRT_ABI_X86_64
 * @flags: Filter the list of GLX ICDs accordingly to these flags.
 *  At the moment no filters are available, so there are no practical
 *  differences between %SRT_DRIVER_FLAGS_INCLUDE_ALL and
 *  %SRT_DRIVER_FLAGS_NONE.
 *
 * List the available GLX ICDs, in an unspecified order.
 * These are the drivers used by `libGL.so.1` or `libGLX.so.0`
 * if it is the loader library provided by
 * [GLVND](https://github.com/NVIDIA/libglvnd)
 * (if this is the case, srt_graphics_library_is_vendor_neutral() for
 * the combination of %SRT_WINDOW_SYSTEM_X11 and %SRT_RENDERING_INTERFACE_GL
 * will return %TRUE and indicate %SRT_GRAPHICS_LIBRARY_VENDOR_GLVND).
 *
 * Returns: (transfer full) (element-type SrtGlxIcd) (nullable): A list of
 *  opaque #SrtGlxIcd objects, or %NULL if nothing was found. Free with
 *  `g_list_free_full(list, g_object_unref)`.
 */
GList *
srt_system_info_list_glx_icds (SrtSystemInfo *self,
                               const char *multiarch_tuple,
                               SrtDriverFlags flags)
{
  return _srt_system_info_list_graphics_modules (self, multiarch_tuple, flags,
                                                 SRT_GRAPHICS_GLX_MODULE);
}

static void
ensure_driver_environment (SrtSystemInfo *self)
{
  g_return_if_fail (_srt_check_not_setuid ());

  if (self->cached_driver_environment == NULL && !self->immutable_values)
    {
      GPtrArray *builder;
      GRegex *regex;
      gchar **env_list = self->env;
      /* This is the list of well-known driver-selection environment variables,
       * plus __GLX_FORCE_VENDOR_LIBRARY_%d that will be searched with a regex.
       * Please keep in LC_ALL=C alphabetical order. */
      static const gchar * const drivers_env[] =
      {
        "ALSA_CONFIG_PATH",
        "AMD_VULKAN_ICD",
        "AUDIODEV",
        "BUMBLEBEE_SOCKET",
        "DISABLE_LAYER_AMD_SWITCHABLE_GRAPHICS_1",
        "DISABLE_MANGOHUD",
        "DISABLE_PRIMUS_LAYER",
        "DISABLE_VKBASALT",
        "DISPLAY",
        "DRI_PRIME",
        "EGL_PLATFORM",
        "ENABLE_DEVICE_CHOOSER_LAYER",
        "ENABLE_PRIMUS_LAYER",
        "ENABLE_VKBASALT",
        "ESPEAKER",
        "GST_PLUGIN_SYSTEM_PATH",
        "LADSPA_PATH",
        "LD_LIBRARY_PATH",
        "LD_PRELOAD",
        "LIBVA_DRIVER_NAME",
        "MANGOHUD",
        "MESA_GLSL_CACHE_DIR",
        "MESA_LOADER_DRIVER_OVERRIDE",
        "MESA_VK_DEVICE_SELECT",
        "MIDIDEV",
        "NODEVICE_SELECT",
        "PRIMUS_DISPLAY",
        "PRIMUS_LOAD_GLOBAL",
        "PRIMUS_SLEEP",
        "PRIMUS_SYNC",
        "PRIMUS_UPLOAD",
        "PRIMUS_VK_DISPLAYID",
        "PRIMUS_VK_MULTITHREADING",
        "PRIMUS_VK_RENDERID",
        "PRIMUS_libGL",
        "PRIMUS_libGLa",
        "PRIMUS_libGLd",
        "PULSE_CLIENTCONFIG",
        "PULSE_CONFIG_PATH",
        "PULSE_LATENCY_MSEC",
        "PULSE_RUNTIME_PATH",
        "PULSE_SERVER",
        "PULSE_SINK",
        "PULSE_SOURCE",
        "PULSE_STATE_PATH",
        "PULSE_SYSTEM",
        "SDL_ACCELEROMETER_AS_JOYSTICK",
        "SDL_AUDIODRIVER",
        "SDL_AUDIO_CHANNELS",
        "SDL_AUDIO_DEVICE_NAME",
        "SDL_AUDIO_FORMAT",
        "SDL_AUDIO_FREQUENCY",
        "SDL_AUDIO_SAMPLES",
        "SDL_AUTO_UPDATE_JOYSTICKS",
        "SDL_DYNAMIC_API",
        "SDL_ENABLE_STEAM_CONTROLLERS",
        "SDL_FRAMEBUFFER_ACCELERATION",
        "SDL_GAMECONTROLLERCONFIG",
        "SDL_GAMECONTROLLERCONFIG_FILE",
        "SDL_GAMECONTROLLERTYPE",
        "SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD",
        "SDL_GAMECONTROLLER_IGNORE_DEVICES",
        "SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT",
        "SDL_JOYSTICK_DEVICE",
        "SDL_JOYSTICK_DISABLE_UDEV",
        "SDL_JOYSTICK_HIDAPI",
        "SDL_JOYSTICK_HIDAPI_PS4",
        "SDL_JOYSTICK_HIDAPI_PS5",
        "SDL_JOYSTICK_HIDAPI_STEAM",
        "SDL_JOYSTICK_HIDAPI_SWITCH",
        "SDL_JOYSTICK_HIDAPI_XBOX",
        "SDL_OPENGLES_LIBRARY",
        "SDL_OPENGL_ES_DRIVER",
        "SDL_OPENGL_LIBRARY",
        "SDL_PATH_DSP",
        "SDL_RENDER_DRIVER",
        "SDL_RENDER_LOGICAL_SIZE_MODE",
        "SDL_RENDER_OPENGL_SHADERS",
        "SDL_RENDER_SCALE_QUALITY",
        "SDL_RENDER_VSYNC",
        "SDL_VIDEODRIVER",
        "SDL_VIDEO_ALLOW_SCREENSAVER",
        "SDL_VIDEO_DOUBLE_BUFFER",
        "SDL_VIDEO_EGL_DRIVER",
        "SDL_VIDEO_EXTERNAL_CONTEXT",
        "SDL_VIDEO_GL_DRIVER",
        "SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS",
        "SDL_VIDEO_WAYLAND_WMCLASS",
        "SDL_VIDEO_X11_FORCE_EGL",
        "SDL_VIDEO_X11_LEGACY_FULLSCREEN",
        "SDL_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR",
        "SDL_VIDEO_X11_NET_WM_PING",
        "SDL_VIDEO_X11_VISUALID",
        "SDL_VIDEO_X11_WINDOW_VISUALID",
        "SDL_VIDEO_X11_WMCLASS",
        "SDL_VIDEO_X11_XINERAMA",
        "SDL_VIDEO_X11_XRANDR",
        "SDL_VIDEO_X11_XVIDMODE",
        "SDL_VULKAN_DISPLAY",
        "SDL_VULKAN_LIBRARY",
        "SDL_X11_XCB_LIBRARY",
        "SDL_XINPUT_ENABLED",
        "SSL_CERT_DIR",
        "STEAM_RUNTIME_PREFER_HOST_LIBRARIES",
        "VDPAU_DRIVER",
        "VK_ICD_FILENAMES",
        "VK_LAYER_PATH",
        "VULKAN_DEVICE_INDEX",
        "WAYLAND_DISPLAY",
        "WINEESYNC",
        "WINEFSYNC",
        "WINE_FULLSCREEN_INTEGER_SCALING",
        "WINE_HIDE_NVIDIA_GPU",
        "XDG_RUNTIME_DIR",
        "__EGL_VENDOR_LIBRARY_DIRS",
        "__EGL_VENDOR_LIBRARY_FILENAMES",
        "__GLX_VENDOR_LIBRARY_NAME",
        "__NV_PRIME_RENDER_OFFLOAD",
        NULL
      };

      builder = g_ptr_array_new_with_free_func (g_free);

      for (guint i = 0; drivers_env[i] != NULL; i++)
        {
          const gchar *value = g_environ_getenv (env_list, drivers_env[i]);
          if (value != NULL)
            {
              gchar *key_value = g_strjoin ("=", drivers_env[i], value, NULL);
              g_ptr_array_add (builder, key_value);
            }
        }

      regex = g_regex_new ("^__GLX_FORCE_VENDOR_LIBRARY_[0-9]+=", 0, 0, NULL);
      g_assert (regex != NULL);    /* known to be valid at compile-time */

      for (gsize i = 0; env_list != NULL && env_list[i] != NULL; i++)
        {
          if (!g_regex_match (regex, env_list[i], 0, NULL))
            continue;

          g_ptr_array_add (builder, g_strdup (env_list[i]));
        }

      g_ptr_array_sort (builder, _srt_indirect_strcmp0);
      g_ptr_array_add (builder, NULL);
      g_regex_unref (regex);

      self->cached_driver_environment = (gchar **) g_ptr_array_free (builder, FALSE);
    }
}

/**
 * srt_system_info_list_driver_environment:
 * @self: The #SrtSystemInfo object
 *
 * List of the driver-selection environment variables.
 *
 * Some drivers have an environment variable that overrides the automatic
 * detection of which driver should be used.
 * For example Mesa has `MESA_LOADER_DRIVER_OVERRIDE`, VA-API has
 * `LIBVA_DRIVER_NAME` and so on.
 *
 * The output will contain a list, in the form "NAME=VALUE", of the well-known
 * driver environment variables that are currently being set.
 *
 * The drivers will be in lexicographic order, for example
 * `LIBVA_DRIVER_NAME=radeonsi`, `VDPAU_DRIVER=radeonsi`,
 * `__GLX_FORCE_VENDOR_LIBRARY_0=i965`, in that order.
 *
 * Returns: (array zero-terminated=1) (transfer full) (element-type utf8) (nullable):
 *  An array of strings, or %NULL if we were unable to find driver-selection
 *  environment variables. Free with g_strfreev().
 */
gchar **
srt_system_info_list_driver_environment (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  ensure_driver_environment (self);

  if (self->cached_driver_environment == NULL || self->cached_driver_environment[0] == NULL)
    return NULL;
  else
    return g_strdupv (self->cached_driver_environment);
}

/**
 * _srt_system_info_driver_environment_from_report:
 * @json_obj: (not nullable): A JSON Object used to search for "driver_environment"
 *  property
 *
 * Returns: (array zero-terminated=1) (element-type utf8) (nullable): An array
 *  of strings, or %NULL if the provided @json_obj doesn't have a
 *  "driver_environment" member. Free with g_strfreev().
 */
static gchar **
_srt_system_info_driver_environment_from_report (JsonObject *json_obj)
{
  return _srt_json_object_dup_strv_member (json_obj,
                                           "driver_environment",
                                           "<invalid>");
}

typedef struct
{
  SrtContainerType type;
  const char *name;
} ContainerTypeName;

static const ContainerTypeName container_types[] =
{
  { SRT_CONTAINER_TYPE_DOCKER, "docker" },
  { SRT_CONTAINER_TYPE_FLATPAK, "flatpak" },
  { SRT_CONTAINER_TYPE_PODMAN, "podman" },
  { SRT_CONTAINER_TYPE_PRESSURE_VESSEL, "pressure-vessel" },
};

static SrtContainerType
container_type_from_name (const char *name)
{
  gsize i;

  for (i = 0; i < G_N_ELEMENTS (container_types); i++)
    {
      const ContainerTypeName *entry = &container_types[i];

      if (strcmp (entry->name, name) == 0)
        return entry->type;
    }

  return SRT_CONTAINER_TYPE_UNKNOWN;
}

static void
ensure_container_info (SrtSystemInfo *self)
{
  g_autofree gchar *contents = NULL;
  g_autofree gchar *run_host_path = NULL;
  glnx_autofd int run_host_fd = -1;

  if (self->container.have_data || self->immutable_values)
    return;

  g_assert (self->container.host_directory == NULL);
  g_assert (self->container.type == SRT_CONTAINER_TYPE_UNKNOWN);
  g_assert (self->sysroot != NULL);

  if (self->sysroot_fd < 0)
    {
      g_debug ("Cannot find container info: previously failed to open "
               "sysroot %s", self->sysroot);
      goto out;
    }

  g_debug ("Finding container info in sysroot %s...", self->sysroot);

  run_host_fd = _srt_resolve_in_sysroot (self->sysroot_fd, "/run/host",
                                         SRT_RESOLVE_FLAGS_DIRECTORY,
                                         &run_host_path, NULL);

  if (run_host_path != NULL)
    self->container.host_directory = g_build_filename (self->sysroot,
                                                       run_host_path,
                                                       NULL);

  if (_srt_file_get_contents_in_sysroot (self->sysroot_fd,
                                         "/run/host/container-manager",
                                         &contents, NULL, NULL))
    {
      g_strchomp (contents);
      self->container.type = container_type_from_name (contents);
      g_debug ("Type %d based on /run/host/container-manager",
               self->container.type);
      goto out;
    }

  if (_srt_file_get_contents_in_sysroot (self->sysroot_fd,
                                         "/run/systemd/container",
                                         &contents, NULL, NULL))
    {
      g_strchomp (contents);
      self->container.type = container_type_from_name (contents);
      g_debug ("Type %d based on /run/systemd/container",
               self->container.type);
      goto out;
    }

  if (_srt_file_test_in_sysroot (self->sysroot, self->sysroot_fd,
                                 "/.flatpak-info", G_FILE_TEST_IS_REGULAR))
    {
      self->container.type = SRT_CONTAINER_TYPE_FLATPAK;
      g_debug ("Flatpak based on /.flatpak-info");
      goto out;
    }

  if (_srt_file_test_in_sysroot (self->sysroot, self->sysroot_fd,
                                 "/run/pressure-vessel", G_FILE_TEST_IS_DIR))
    {
      self->container.type = SRT_CONTAINER_TYPE_PRESSURE_VESSEL;
      g_debug ("pressure-vessel based on /run/pressure-vessel");
      goto out;
    }

  if (_srt_file_test_in_sysroot (self->sysroot, self->sysroot_fd,
                                 "/.dockerenv", G_FILE_TEST_EXISTS))
    {
      self->container.type = SRT_CONTAINER_TYPE_DOCKER;
      g_debug ("Docker based on /.dockerenv");
      goto out;
    }

  if (_srt_file_test_in_sysroot (self->sysroot, self->sysroot_fd,
                                 "/run/.containerenv", G_FILE_TEST_EXISTS))
    {
      self->container.type = SRT_CONTAINER_TYPE_PODMAN;
      g_debug ("Podman based on /run/.containerenv");
      goto out;
    }

  if (_srt_file_get_contents_in_sysroot (self->sysroot_fd,
                                         "/proc/1/cgroup",
                                         &contents, NULL, NULL))
    {
      if (strstr (contents, "/docker/") != NULL)
        self->container.type = SRT_CONTAINER_TYPE_DOCKER;

      if (self->container.type != SRT_CONTAINER_TYPE_UNKNOWN)
        {
          g_debug ("Type %d based on /proc/1/cgroup", self->container.type);
          goto out;
        }

      g_clear_pointer (&contents, g_free);
    }

  if (run_host_fd >= 0)
    {
      g_debug ("Unknown container technology based on /run/host");
      self->container.type = SRT_CONTAINER_TYPE_UNKNOWN;
      goto out;
    }

  /* We haven't found any particular evidence of being in a container */
  g_debug ("Probably not a container");
  self->container.type = SRT_CONTAINER_TYPE_NONE;

out:
  self->container.have_data = TRUE;
}

/**
 * srt_system_info_get_container_type:
 * @self: The #SrtSystemInfo object
 *
 * If the program appears to be running in a container, return what sort
 * of container it is.
 *
 * Returns: A recognised container type, or %SRT_CONTAINER_TYPE_NONE
 *  if a container cannot be detected, or %SRT_CONTAINER_TYPE_UNKNOWN
 *  if unsure.
 */
SrtContainerType
srt_system_info_get_container_type (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), SRT_CONTAINER_TYPE_UNKNOWN);

  ensure_container_info (self);
  return self->container.type;
}

/**
 * srt_system_info_dup_container_host_directory:
 * @self: The #SrtSystemInfo object
 *
 * If the program appears to be running in a container, return the
 * directory where host files can be found. For example, if this function
 * returns `/run/host`, it might be possible to load the host system's
 * `/usr/lib/os-release` by reading `/run/host/usr/lib/os-release`.
 *
 * The returned directory is usually not complete. For example,
 * in a Flatpak app, `/run/host` will sometimes contain the host system's
 * `/etc` and `/usr`, but only if suitable permissions flags are set.
 *
 * Returns: A path from which at least some host-system files can be
 *  loaded, typically `/run/host`, or %NULL if unknown or unavailable
 */
gchar *
srt_system_info_dup_container_host_directory (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  ensure_container_info (self);
  return g_strdup (self->container.host_directory);
}

/**
 * _srt_system_info_get_container_info_from_report:
 * @json_obj: (not nullable): A JSON Object used to search for "container"
 *  property
 * @host_path: (not nullable): Used to return the host path
 *
 * If the provided @json_obj doesn't have a "container" member,
 * %SRT_CONTAINER_TYPE_UNKNOWN will be returned.
 * If @json_obj has some elements that we can't parse, the returned
 * #SrtContainerType will be set to %SRT_CONTAINER_TYPE_UNKNOWN.
 *
 * Returns: The found container type
 */
static SrtContainerType
_srt_system_info_get_container_info_from_report (JsonObject *json_obj,
                                                 gchar **host_path)
{
  JsonObject *json_sub_obj;
  JsonObject *json_host_obj = NULL;
  const gchar *type_string = NULL;
  SrtContainerType ret = SRT_CONTAINER_TYPE_UNKNOWN;

  g_return_val_if_fail (host_path != NULL && *host_path == NULL, SRT_CONTAINER_TYPE_UNKNOWN);

  if (json_object_has_member (json_obj, "container"))
    {
      json_sub_obj = json_object_get_object_member (json_obj, "container");

      if (json_sub_obj == NULL)
        goto out;

      if (json_object_has_member (json_sub_obj, "type"))
        {
          type_string = json_object_get_string_member (json_sub_obj, "type");
          if (!srt_enum_from_nick (SRT_TYPE_CONTAINER_TYPE, type_string, &ret, NULL))
            {
              g_debug ("The parsed container type '%s' is not a known element", type_string);
              ret = SRT_CONTAINER_TYPE_UNKNOWN;
            }
        }

      if (json_object_has_member (json_sub_obj, "host"))
        {
          json_host_obj = json_object_get_object_member (json_sub_obj, "host");
          *host_path = g_strdup (json_object_get_string_member_with_default (json_host_obj, "path",
                                                                             NULL));
        }
    }

out:
  return ret;
}

static void
ensure_desktop_entries (SrtSystemInfo *self)
{
  g_return_if_fail (!self->immutable_values);

  if (self->desktop_entry.have_data)
    return;

  g_assert (self->desktop_entry.values == NULL);

  self->desktop_entry.values = _srt_list_steam_desktop_entries ();
  self->desktop_entry.have_data = TRUE;
}

/**
 * srt_system_info_list_desktop_entries:
 * @self: The #SrtSystemInfo object
 *
 * List all the available desktop applications that are able to handle the
 * type "x-scheme-handler/steam".
 *
 * This function will also search for well known desktop applications ID like
 * the Flathub `com.valvesoftware.Steam.desktop` and they'll be included even
 * if they are not able to handle the `steam:` URI.
 * Using `srt_desktop_entry_is_steam_handler()` it is possible to filter them out.
 *
 * The returned list is in no particular order.
 *
 * Please note that the environment variables of the current process will be used.
 * Any possible custom environ set with `srt_system_info_set_environ()` will be
 * ignored.
 *
 * Returns: (transfer full) (element-type SrtDesktopEntry) (nullable): A list of
 *  opaque #SrtDesktopEntry objects or %NULL if nothing was found. Free with
 *  `g_list_free_full (entries, g_object_unref)`.
 */
GList *
srt_system_info_list_desktop_entries (SrtSystemInfo *self)
{
  GList *ret = NULL;
  const GList *iter;

  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  if (!self->immutable_values)
    ensure_desktop_entries (self);

  for (iter = self->desktop_entry.values; iter != NULL; iter = iter->next)
    ret = g_list_prepend (ret, g_object_ref (iter->data));

  return g_list_reverse (ret);
}

static void
ensure_x86_features_cached (SrtSystemInfo *self)
{
  g_return_if_fail (!self->immutable_values);

  if (self->cpu_features.x86_known != SRT_X86_FEATURE_NONE)
    return;

  self->cpu_features.x86_features = _srt_feature_get_x86_flags (&self->cpu_features.x86_known);
}

/**
 * srt_system_info_get_x86_features:
 * @self: The #SrtSystemInfo object
 *
 * Detect and return a list of x86 features that the CPU supports.
 *
 * Returns: x86 CPU supported features, or %SRT_X86_FEATURE_NONE
 *  if none of the checked features are supported.
 */
SrtX86FeatureFlags
srt_system_info_get_x86_features (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), SRT_X86_FEATURE_NONE);

  if (!self->immutable_values)
    ensure_x86_features_cached (self);

  return self->cpu_features.x86_features;
}

/**
 * srt_system_info_get_known_x86_features:
 * @self: The #SrtSystemInfo object
 *
 * Return a list of x86 CPU features that has been checked.
 *
 * Returns: x86 CPU checked features, or %SRT_X86_FEATURE_NONE
 *  if no features were checked, e.g. when the CPU is not x86 based.
 */
SrtX86FeatureFlags
srt_system_info_get_known_x86_features (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), SRT_X86_FEATURE_NONE);

  if (!self->immutable_values)
    ensure_x86_features_cached (self);

  return self->cpu_features.x86_known;
}

/**
 * srt_system_info_dup_steamscript_path:
 * @self: The #SrtSystemInfo object
 *
 * Return the absolute path to the script used to launch Steam, if known.
 * If the application using this library was not run as a child process
 * of the Steam client, then this will usually be %NULL.
 *
 * This will usually be `/usr/bin/steam` for the packaged Steam launcher
 * released by Valve, `/app/bin/steam` for the Flatpak app, or either
 * `/usr/bin/steam` or `/usr/games/steam` for third-party packaged versions
 * of the Steam client.
 *
 * Returns: (transfer full) (type filename) (nullable): A filename, or %NULL.
 */
gchar *
srt_system_info_dup_steamscript_path (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  ensure_steam_cached (self);
  return g_strdup (srt_steam_get_steamscript_path (self->steam_data));
}

/**
 * srt_system_info_dup_steamscript_version:
 * @self: The #SrtSystemInfo object
 *
 * Return the version of the script used to launch Steam, if known.
 * If the application using this library was not run as a child process
 * of the Steam client, then this will usually be %NULL.
 *
 * Typical values look like `1.0.0.66` for the packaged Steam launcher
 * released by Valve, `1.0.0.66-2/Debian` for recent Debian packages, or
 * %NULL for older Debian/Ubuntu packages. Future Ubuntu packages might
 * produce a string like `1.0.0.66-2ubuntu1/Ubuntu`.
 *
 * Returns: (transfer full) (type filename) (nullable): A filename, or %NULL.
 */
gchar *
srt_system_info_dup_steamscript_version (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  ensure_steam_cached (self);
  return g_strdup (srt_steam_get_steamscript_version (self->steam_data));
}

static void
ensure_xdg_portals_cached (SrtSystemInfo *self)
{
  g_return_if_fail (SRT_IS_SYSTEM_INFO (self));

  if (self->xdg_portal_data == NULL)
    {
      ensure_container_info (self);
      _srt_check_xdg_portals (self->env,
                              self->helpers_path,
                              self->test_flags,
                              self->container.type,
                              srt_system_info_get_primary_multiarch_tuple (self),
                              &self->xdg_portal_data);
    }
}

/**
 * srt_system_info_list_xdg_portal_backends:
 * @self: The #SrtSystemInfo object
 *
 * List the XDG portal backends that have been checked, with information about
 * their eventual availability.
 *
 * Examples of XDG portal backends are "org.freedesktop.impl.portal.desktop.gtk"
 * and "org.freedesktop.impl.portal.desktop.kde".
 *
 * The returned list is in the same arbitrary order as the check-xdg-portal
 * helper used to return them.
 *
 * Returns: (transfer full) (element-type SrtXdgPortalBackend) (nullable):
 *  A list of opaque #SrtXdgPortalBackend objects or %NULL if an error
 *  occurred trying to check the backends availability or if we were unable to
 *  check them, e.g. if we are in a Flatpak environment.
 *  Free with `g_list_free_full (backends, g_object_unref)`.
 */
GList *
srt_system_info_list_xdg_portal_backends (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  if (!self->immutable_values)
    ensure_xdg_portals_cached (self);

  return srt_xdg_portal_get_backends (self->xdg_portal_data);
}

/**
 * srt_system_info_list_xdg_portal_interfaces:
 * @self: The #SrtSystemInfo object
 *
 * List the XDG portal interfaces that have been checked, with information
 * about their eventual availability and version property.
 *
 * Examples of XDG portal interfaces are "org.freedesktop.portal.OpenURI"
 * and "org.freedesktop.portal.Email".
 *
 * The returned list is in the same arbitrary order as the check-xdg-portal
 * helper used to return them.
 *
 * Returns: (transfer full) (element-type SrtXdgPortalInterface) (nullable):
 *  A list of opaque #SrtXdgPortalInterface objects or %NULL if an error
 *  occurred trying to check the interfaces availability. Free with
 *  `g_list_free_full (interfaces, g_object_unref)`.
 */
GList *
srt_system_info_list_xdg_portal_interfaces (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  if (!self->immutable_values)
    ensure_xdg_portals_cached (self);

  return srt_xdg_portal_get_interfaces (self->xdg_portal_data);
}

/**
 * srt_system_info_get_xdg_portal_issues:
 * @self: The #SrtSystemInfo object
 * @messages: (optional) (out): If not %NULL, used to return the diagnostic
 *  messages. Free with g_free().
 *
 * Check if the current system supports the XDG portals.
 *
 * Returns: A bitfield containing problems, or %SRT_XDG_PORTAL_ISSUES_NONE
 *  if no problems were found.
 */
SrtXdgPortalIssues
srt_system_info_get_xdg_portal_issues (SrtSystemInfo *self,
                                       gchar **messages)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), SRT_XDG_PORTAL_ISSUES_UNKNOWN);
  g_return_val_if_fail (messages == NULL || *messages == NULL, SRT_XDG_PORTAL_ISSUES_UNKNOWN);

  if (!self->immutable_values)
    ensure_xdg_portals_cached (self);

  if (messages != NULL)
    *messages = g_strdup (srt_xdg_portal_get_messages (self->xdg_portal_data));

  return srt_xdg_portal_get_issues (self->xdg_portal_data);
}

static void
ensure_runtime_linker (SrtSystemInfo *self,
                       Abi *abi)
{
  g_autofree gchar *tmp = NULL;
  int fd;

  if (abi->runtime_linker_resolved != NULL)
    return;

  if (abi->runtime_linker_error != NULL)
    return;

  if (abi->known_architecture == NULL)
    return;

  if (abi->known_architecture->interoperable_runtime_linker == NULL)
    {
      g_set_error (&abi->runtime_linker_error, SRT_ARCHITECTURE_ERROR,
                   SRT_ARCHITECTURE_ERROR_NO_INFORMATION,
                   "Interoperable runtime_linker for \"%s\" not known",
                   abi->known_architecture->multiarch_tuple);
      return;
    }

  if (self->sysroot_fd < 0)
    {
      g_set_error (&abi->runtime_linker_error, SRT_ARCHITECTURE_ERROR,
                   SRT_ARCHITECTURE_ERROR_INTERNAL_ERROR,
                   "Unable to open sysroot");
      return;
    }

  fd = _srt_resolve_in_sysroot (self->sysroot_fd,
                                abi->known_architecture->interoperable_runtime_linker,
                                SRT_RESOLVE_FLAGS_READABLE,
                                &tmp,
                                &abi->runtime_linker_error);

  if (fd >= 0)
    {
      abi->runtime_linker_resolved = g_strconcat ("/", tmp, NULL);
      close (fd);
    }
}

/**
 * srt_system_info_check_runtime_linker:
 * @self: The #SrtSystemInfo object
 * @multiarch_tuple: A multiarch tuple defining an ABI, as printed
 *  by `gcc -print-multiarch` in the Steam Runtime
 * @resolved: (out) (type filename) (transfer full) (optional): Used
 *  to return the path to the runtime linker after resolving all
 *  symbolic links. Free with g_free(); may be %NULL to ignore.
 * @error: Used to raise an error on failure.
 *
 * Check whether the runtime linker `ld.so(8)` for the ABI described
 * by @multiarch_tuple, as returned by
 * srt_architecture_get_expected_runtime_linker(), is available.
 *
 * If `ld.so` is unavailable, return %FALSE with @error set.
 *
 * If not enough information is available to determine whether `ld.so`
 * is available, raise %SRT_ARCHITECTURE_ERROR_NO_INFORMATION.
 *
 * Returns: %TRUE if `ld.so(8)` is available at the interoperable path
 *  for the given architecture
 */
gboolean
srt_system_info_check_runtime_linker (SrtSystemInfo *self,
                                      const char *multiarch_tuple,
                                      gchar **resolved,
                                      GError **error)
{
  Abi *abi;

  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), FALSE);
  g_return_val_if_fail (multiarch_tuple != NULL, FALSE);

  abi = ensure_abi_unless_immutable (self, multiarch_tuple);

  if (abi == NULL)
    {
      g_set_error (error, SRT_ARCHITECTURE_ERROR,
                   SRT_ARCHITECTURE_ERROR_NO_INFORMATION,
                   "ABI \"%s\" not included in report",
                   multiarch_tuple);
      return FALSE;
    }

  if (!self->immutable_values)
    ensure_runtime_linker (self, abi);

  if (abi->known_architecture == NULL)
    {
      g_set_error (error, SRT_ARCHITECTURE_ERROR,
                   SRT_ARCHITECTURE_ERROR_NO_INFORMATION,
                   "Interoperable runtime linker for \"%s\" not known",
                   multiarch_tuple);
      return FALSE;
    }
  else if (abi->runtime_linker_resolved != NULL)
    {
      if (resolved != NULL)
        *resolved = g_strdup (abi->runtime_linker_resolved);

      return TRUE;
    }
  else if (abi->runtime_linker_error != NULL)
    {
      if (error != NULL)
        *error = g_error_copy (abi->runtime_linker_error);

      return FALSE;
    }
  else if (self->immutable_values)
    {
      g_set_error (error, SRT_ARCHITECTURE_ERROR,
                   SRT_ARCHITECTURE_ERROR_NO_INFORMATION,
                   "Runtime linker for \"%s\" not included in report",
                   multiarch_tuple);
      return FALSE;
    }
  else
    {
      /* We shouldn't be able to get here */
      g_set_error (error, SRT_ARCHITECTURE_ERROR,
                   SRT_ARCHITECTURE_ERROR_INTERNAL_ERROR,
                   "Runtime linker for \"%s\" not checked",
                   multiarch_tuple);
      g_return_val_if_reached (FALSE);
    }
}
