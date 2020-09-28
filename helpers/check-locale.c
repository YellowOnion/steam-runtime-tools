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

#include <errno.h>
#include <locale.h>

#include <glib.h>
#include <json-glib/json-glib.h>

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/utils-internal.h"

#ifdef MOCK_CHECK_LOCALE
typedef enum
{
  MOCK_LOCALE_C,
#ifdef MOCK_CHECK_LOCALE_HAVE_C_UTF8
  MOCK_LOCALE_C_UTF8,
#endif
#ifdef MOCK_CHECK_LOCALE_HAVE_EN_US
  MOCK_LOCALE_EN_US,
  MOCK_LOCALE_EN_US_UTF8,
#endif
  MOCK_LOCALE_EN_GB_UTF8,
  MOCK_LOCALE_COMPLICATED
} MockLocale;

static struct
{
  const char *name;
  const char *alias;
} mock_locales[] = {
  { "C", "POSIX" },
#ifdef MOCK_CHECK_LOCALE_HAVE_C_UTF8
  { "C.UTF-8", NULL },
#endif
#ifdef MOCK_CHECK_LOCALE_HAVE_EN_US
  { "en_US", NULL },
  { "en_US.UTF-8", NULL },
#endif
  { "en_GB.UTF-8", NULL },
  /* This is what smcv's en_GB installation of Debian returns from
   * setlocale(LC_ALL, ""). Why are some of them utf8 and some UTF-8?
   * We just don't know. */
  { "LC_CTYPE=en_GB.UTF-8;"
    "LC_NUMERIC=en_GB.utf8;"
    "LC_TIME=en_GB.utf8;"
    "LC_COLLATE=en_GB.UTF-8;"
    "LC_MONETARY=en_GB.utf8;"
    "LC_MESSAGES=en_GB.UTF-8;"
    "LC_PAPER=en_GB.utf8;"
    "LC_NAME=en_GB.UTF-8;"
    "LC_ADDRESS=en_GB.UTF-8;"
    "LC_TELEPHONE=en_GB.UTF-8;"
    "LC_MEASUREMENT=en_GB.utf8;"
    "LC_IDENTIFICATION=en_GB.UTF-8",
    NULL },
};
G_STATIC_ASSERT (G_N_ELEMENTS (mock_locales) == MOCK_LOCALE_COMPLICATED + 1);

static MockLocale mock_locale = MOCK_LOCALE_C;

static const char *
mock_setlocale (const char *locale_name)
{
  gsize i;

  if (locale_name == NULL)
    return mock_locales[mock_locale].name;

  if (g_strcmp0 (locale_name, "") == 0)
    {
#ifdef MOCK_CHECK_LOCALE_LEGACY
      mock_locale = MOCK_LOCALE_EN_US;
#else
      mock_locale = MOCK_LOCALE_COMPLICATED;
#endif
      return mock_locales[mock_locale].name;
    }

  for (i = 0; i < G_N_ELEMENTS (mock_locales); i++)
    {
      if (g_strcmp0 (locale_name, mock_locales[i].name) == 0
          || g_strcmp0 (locale_name, mock_locales[i].alias) == 0)
        {
          mock_locale = i;
          return mock_locales[mock_locale].name;
        }
    }

  errno = ENOENT;
  return NULL;
}

static gboolean
mock_get_charset (const char **charset)
{
  switch (mock_locale)
    {
      case MOCK_LOCALE_C:
        if (charset != NULL)
          *charset = "ANSI_X3.4-1968";

        return FALSE;

#ifdef MOCK_CHECK_LOCALE_HAVE_EN_US
      case MOCK_LOCALE_EN_US:
        if (charset != NULL)
          *charset = "ISO-8859-1";

        return FALSE;
#endif

#ifdef MOCK_CHECK_LOCALE_HAVE_C_UTF8
      case MOCK_LOCALE_C_UTF8:
#endif
#ifdef MOCK_CHECK_LOCALE_HAVE_EN_US
      case MOCK_LOCALE_EN_US_UTF8:
#endif
      case MOCK_LOCALE_EN_GB_UTF8:
      case MOCK_LOCALE_COMPLICATED:
      default:
        if (charset != NULL)
          *charset = "UTF-8";

        return TRUE;
    }
}
#endif

static gchar *opt_locale = NULL;
static gboolean opt_print_version = FALSE;

static gboolean
opt_locale_cb (const char *name,
               const char *value,
               gpointer data,
               GError **error)
{
  if (opt_locale != NULL)
    {
      g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED,
                   "At most one locale is expected");
      return FALSE;
    }

  opt_locale = g_strdup (value);
  return TRUE;
}

static const GOptionEntry option_entries[] =
{
  { "version", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_print_version,
    "Print version number and exit", NULL },
  { G_OPTION_REMAINING, 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_CALLBACK,
    opt_locale_cb,
    "The locale to test [default: use environment variables]",
    "[LOCALE]" },
  { NULL }
};

int
main (int argc,
      char **argv)
{
  GOptionContext *option_context = NULL;
  GError *local_error = NULL;
  const char *locale_result;
  const char *locale_name;
  const char *charset;
  gchar *json = NULL;
  JsonNode *root = NULL;
  JsonBuilder *builder = NULL;
  JsonGenerator *generator = NULL;
  int ret = 1;
  gboolean is_utf8;

  option_context = g_option_context_new ("");
  g_option_context_add_main_entries (option_context, option_entries, NULL);

  if (!g_option_context_parse (option_context, &argc, &argv, &local_error))
    {
      ret = 2;
      goto out;
    }

  if (opt_print_version)
    {
      /* Output version number as YAML for machine-readability,
       * inspired by `ostree --version` and `docker version` */
      g_print (
          "%s:\n"
          " Package: steam-runtime-tools\n"
          " Version: %s\n",
          argv[0], VERSION);
      ret = 0;
      goto out;
    }

  if (opt_locale == NULL)
    locale_name = "";
  else
    locale_name = opt_locale;

  builder = json_builder_new ();
  json_builder_begin_object (builder);
  json_builder_set_member_name (builder, "requested");
  json_builder_add_string_value (builder, locale_name);

#ifdef MOCK_CHECK_LOCALE
  locale_result = mock_setlocale (locale_name);
#else
  locale_result = setlocale (LC_ALL, locale_name);
#endif

  if (locale_result == NULL)
    {
      int saved_errno = errno;

      json_builder_set_member_name (builder, "error");
      json_builder_add_string_value (builder,
                                     g_strerror (saved_errno));
      ret = 1;
    }
  else
    {
#ifdef MOCK_CHECK_LOCALE
      is_utf8 = mock_get_charset (&charset);
#else
      is_utf8 = g_get_charset (&charset);
#endif

      json_builder_set_member_name (builder, "result");
      json_builder_add_string_value (builder, locale_result);
      json_builder_set_member_name (builder, "charset");
      json_builder_add_string_value (builder, charset);
      json_builder_set_member_name (builder, "is_utf8");
      json_builder_add_boolean_value (builder, is_utf8);
      ret = 0;
    }

  json_builder_end_object (builder);

  root = json_builder_get_root (builder);
  generator = json_generator_new ();
  json_generator_set_pretty (generator, TRUE);
  json_generator_set_root (generator, root);
  json = json_generator_to_data (generator, NULL);
  g_print ("%s\n", json);

out:
  if (local_error != NULL)
    g_printerr ("%s: %s\n", g_get_prgname (), local_error->message);

  g_clear_object (&generator);
  g_clear_object (&builder);
  g_clear_error (&local_error);
  g_clear_pointer (&root, json_node_free);
  g_clear_pointer (&option_context, g_option_context_free);
  g_free (opt_locale);
  g_free (json);
  return ret;
}
