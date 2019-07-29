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

#include "steam-runtime-tools/architecture.h"
#include "steam-runtime-tools/architecture-internal.h"
#include "steam-runtime-tools/glib-compat.h"
#include "steam-runtime-tools/utils-internal.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

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
  gchar *expectations;
  Tristate can_write_uinput;
  /* (element-type Abi) */
  GPtrArray *abis;
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
  GQuark multiarch_tuple;
  Tristate can_run;
} Abi;

static Abi *
ensure_abi (SrtSystemInfo *self,
            const char *multiarch_tuple)
{
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

  abi = g_slice_new0 (Abi);
  abi->multiarch_tuple = quark;
  abi->can_run = TRI_MAYBE;
  /* transfer ownership to self->abis */
  g_ptr_array_add (self->abis, abi);
  return abi;
}

static void
abi_free (gpointer self)
{
  g_slice_free (Abi, self);
}

static void
srt_system_info_init (SrtSystemInfo *self)
{
  self->can_write_uinput = TRI_MAYBE;

  /* Assume that in practice we will usually add two ABIs: amd64 and i386 */
  self->abis = g_ptr_array_new_full (2, abi_free);
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
srt_system_info_finalize (GObject *object)
{
  SrtSystemInfo *self = SRT_SYSTEM_INFO (object);

  g_clear_pointer (&self->abis, g_ptr_array_unref);
  g_free (self->expectations);

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

SrtSystemInfo *
srt_system_info_new (const char *expectations)
{
  g_return_val_if_fail ((expectations == NULL ||
                         g_file_test (expectations, G_FILE_TEST_IS_DIR)),
                        NULL);
  return g_object_new (SRT_TYPE_SYSTEM_INFO,
                       "expectations", expectations,
                       NULL);
}

gboolean
srt_system_info_can_run (SrtSystemInfo *self,
                         const char *multiarch_tuple)
{
  Abi *abi = NULL;

  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), FALSE);
  g_return_val_if_fail (multiarch_tuple != NULL, FALSE);

  abi = ensure_abi (self, multiarch_tuple);

  if (abi->can_run == TRI_MAYBE)
    {
      if (_srt_architecture_can_run (multiarch_tuple))
        abi->can_run = TRI_YES;
      else
        abi->can_run = TRI_NO;
    }

  return (abi->can_run == TRI_YES);
}

gboolean
srt_system_info_can_write_to_uinput (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), FALSE);

  if (self->can_write_uinput == TRI_MAYBE)
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
