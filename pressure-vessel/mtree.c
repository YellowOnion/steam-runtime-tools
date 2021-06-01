/*
 * Copyright © 2021 Collabora Ltd.
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

#include "mtree.h"

#include "steam-runtime-tools/profiling-internal.h"
#include "steam-runtime-tools/resolve-in-sysroot-internal.h"
#include "steam-runtime-tools/utils-internal.h"

#include <gio/gunixinputstream.h>

#include "enumtypes.h"

/* Enabling debug logging for this is rather too verbose, so only
 * enable it when actively debugging this module */
#if 0
#define trace(...) g_debug (__VA_ARGS__)
#else
#define trace(...) do { } while (0)
#endif

static gboolean
is_token (const char *token,
          const char *equals,
          const char *expected)
{
  if (equals == NULL)
    {
      return strcmp (token, expected) == 0;
    }
  else
    {
      g_assert (equals >= token);
      g_assert (*equals == '=');
      return strncmp (token, expected, equals - token) == 0;
    }
}

static gboolean
require_value (const char *token,
               const char *equals,
               GError **error)
{
  if (equals == NULL)
    return glnx_throw (error, "%s requires a value", token);

  return TRUE;
}

static gboolean
forbid_value (const char *token,
              const char *equals,
              GError **error)
{
  if (equals != NULL)
    return glnx_throw (error, "%s does not take a value", token);

  return TRUE;
}

static gboolean
pv_mtree_entry_parse_internal (const char *line,
                               PvMtreeEntry *entry,
                               const char *filename,
                               guint line_number,
                               GError **error)
{
  PvMtreeEntry blank = PV_MTREE_ENTRY_BLANK;
  g_auto(GStrv) tokens = NULL;
  gsize i;

  *entry = blank;

  if (line[0] == '\0' || line[0] == '#')
    return TRUE;

  if (line[0] == '/')
    return glnx_throw (error, "Special commands not supported");

  if (line[0] != '.' || (line[1] != ' ' && line[1] != '/' && line[1] != '\0'))
    return glnx_throw (error,
                       "Filenames not relative to top level not supported");

  if (g_str_has_suffix (line, "\\"))
    return glnx_throw (error, "Continuation lines not supported");

  for (i = 0; line[i] != '\0'; i++)
    {
      if (line[i] == '\\')
        {
          if (line[i + 1] >= '0' && line[i + 1] <= '9')
            continue;

          switch (line[i + 1])
            {
              /* g_strcompress() documents these to work */
              case 'b':
              case 'f':
              case 'n':
              case 'r':
              case 't':
              case 'v':
              case '"':
              case '\\':
                i += 1;
                continue;

              /* \M, \^, \a, \s, \E, \x, \$, escaped whitespace and escaped
               * newline are not supported here */
              default:
                return glnx_throw (error,
                                   "Unsupported backslash escape: \"\\%c\"",
                                   line[i + 1]);
            }
        }
    }

  tokens = g_strsplit_set (line, " \t", -1);

  if (tokens == NULL)
    return glnx_throw (error, "Line is empty");

  entry->name = g_strcompress (tokens[0]);

  for (i = 1; tokens[i] != NULL; i++)
    {
      static const char * const ignored[] =
      {
        "cksum",
        "device",
        "flags",
        "gid",
        "gname",
        "inode",
        "md5",
        "md5digest",
        "nlink",
        "resdevice",
        "ripemd160digest",
        "rmd160",
        "rmd160digest",
        "sha1",
        "sha1digest",
        "sha384",
        "sha384digest",
        "sha512",
        "sha512digest",
        "uid",
        "uname",
      };
      const char *equals;
      char *endptr;
      gsize j;

      equals = strchr (tokens[i], '=');

      for (j = 0; j < G_N_ELEMENTS (ignored); j++)
        {
          if (is_token (tokens[i], equals, ignored[j]))
            break;
        }

      if (j < G_N_ELEMENTS (ignored))
        continue;

#define REQUIRE_VALUE \
      G_STMT_START \
        { \
          if (!require_value (tokens[i], equals, error)) \
            return FALSE; \
        } \
      G_STMT_END

#define FORBID_VALUE(token) \
      G_STMT_START \
        { \
          if (!forbid_value ((token), equals, error)) \
            return FALSE; \
        } \
      G_STMT_END

      if (is_token (tokens[i], equals, "link"))
        {
          REQUIRE_VALUE;
          entry->link = g_strcompress (equals + 1);
          continue;
        }

      if (is_token (tokens[i], equals, "contents")
          || is_token (tokens[i], equals, "content"))
        {
          REQUIRE_VALUE;
          entry->contents = g_strcompress (equals + 1);
          continue;
        }

      if (is_token (tokens[i], equals, "sha256")
          || is_token (tokens[i], equals, "sha256digest"))
        {
          REQUIRE_VALUE;

          if (entry->sha256 == NULL)
            entry->sha256 = g_strdup (equals + 1);
          else if (strcmp (entry->sha256, equals + 1) != 0)
            return glnx_throw (error,
                               "sha256 and sha256digest not consistent");

          continue;
        }

      if (is_token (tokens[i], equals, "mode"))
        {
          gint64 value;

          REQUIRE_VALUE;
          value = g_ascii_strtoll (equals + 1, &endptr, 8);

          if (equals[1] == '\0' || *endptr != '\0')
            return glnx_throw (error, "Invalid mode %s", equals + 1);

          entry->mode = value & 07777;
          continue;
        }

      if (is_token (tokens[i], equals, "size"))
        {
          gint64 value;

          REQUIRE_VALUE;
          value = g_ascii_strtoll (equals + 1, &endptr, 10);

          if (equals[1] == '\0' || *endptr != '\0')
            return glnx_throw (error, "Invalid size %s", equals + 1);

          entry->size = value;
          continue;
        }

      if (is_token (tokens[i], equals, "time"))
        {
          guint64 value;
          guint64 ns = 0;

          REQUIRE_VALUE;
          value = g_ascii_strtoull (equals + 1, &endptr, 10);

          if (equals[1] == '\0' || (*endptr != '\0' && *endptr != '.'))
            return glnx_throw (error, "Invalid time %s", equals + 1);

          /* This is silly, but time=1.234 has historically meant
           * 1 second + 234 nanoseconds, or what normal people would
           * write as 1.000000234, so parsing it as a float is incorrect
           * (for example mtree-netbsd in Debian still prints it
           * like that).
           *
           * time=1.0 is unambiguous, and so is time=1.123456789
           * with exactly 9 digits. */
          if (*endptr == '.' && strcmp (endptr, ".0") != 0)
            {
              const char *dot = endptr;

              ns = g_ascii_strtoull (dot + 1, &endptr, 10);

              if (dot[1] == '\0' || *endptr != '\0' || ns > 999999999)
                return glnx_throw (error, "Invalid nanoseconds count %s",
                                   dot + 1);

              /* If necessary this could become just a warning, but for
               * now require it to be unambiguous - libarchive and
               * FreeBSD mtree show this unambiguous format. */
              if (endptr != dot + 10)
                return glnx_throw (error,
                                   "Ambiguous nanoseconds count %s, "
                                   "should have exactly 9 digits",
                                   dot + 1);
            }

          /* We store it as a GTimeSpan which is "only" microsecond
           * precision. */
          entry->mtime_usec = (value * G_TIME_SPAN_SECOND) + (ns / 1000);
          continue;
        }

      if (is_token (tokens[i], equals, "type"))
        {
          int value;

          REQUIRE_VALUE;

          if (srt_enum_from_nick (PV_TYPE_MTREE_ENTRY_KIND, equals + 1,
                                  &value, NULL))
            entry->kind = value;
          else
            entry->kind = PV_MTREE_ENTRY_KIND_UNKNOWN;

          continue;
        }

      if (is_token (tokens[i], equals, "ignore"))
        {
          FORBID_VALUE ("ignore");
          entry->entry_flags |= PV_MTREE_ENTRY_FLAGS_IGNORE_BELOW;
          continue;
        }

      if (is_token (tokens[i], equals, "nochange"))
        {
          FORBID_VALUE ("nochange");
          entry->entry_flags |= PV_MTREE_ENTRY_FLAGS_NO_CHANGE;
          continue;
        }

      if (is_token (tokens[i], equals, "optional"))
        {
          FORBID_VALUE ("optional");
          entry->entry_flags |= PV_MTREE_ENTRY_FLAGS_OPTIONAL;
          continue;
        }

      g_warning ("%s:%u: Unknown mtree keyword %s",
                 filename, line_number, tokens[i]);
    }

  if (entry->kind == PV_MTREE_ENTRY_KIND_UNKNOWN)
    return glnx_throw (error, "Unknown mtree entry type");

  if (entry->link != NULL && entry->kind != PV_MTREE_ENTRY_KIND_LINK)
    return glnx_throw (error, "Non-symlink cannot have a symlink target");

  if (entry->link == NULL && entry->kind == PV_MTREE_ENTRY_KIND_LINK)
    return glnx_throw (error, "Symlink must have a symlink target");

  return TRUE;
}

gboolean
pv_mtree_entry_parse (const char *line,
                      PvMtreeEntry *entry,
                      const char *filename,
                      guint line_number,
                      GError **error)
{
  if (!pv_mtree_entry_parse_internal (line, entry,
                                      filename, line_number, error))
    {
      g_prefix_error (error, "%s: %u: ", filename, line_number);
      return FALSE;
    }

  return TRUE;
}

/*
 * pv_mtree_apply:
 * @mtree: (type filename): Path to a mtree(5) manifest
 * @sysroot: (type filename): A directory
 * @sysroot_fd: A fd opened on @sysroot
 * @source_files: (optional): A directory from which files will be
 *  hard-linked or copied when populating @sysroot. The `content`
 *  or filename in @mtree is taken to be relative to @source_files.
 * @flags: Flags affecting how this is done
 *
 * Make the container root filesystem @sysroot conform to @mtree.
 *
 * @mtree must contain a subset of BSD mtree(5) syntax:
 *
 * - one entry per line
 * - no device nodes, fifos, sockets or other special devices
 * - strings are escaped using octal (for example \040 for space)
 * - filenames other than "." start with "./"
 *
 * For regular files, we assert that the file exists, set its mtime,
 * and set its permissions to either 0644 or 0755.
 *
 * For directories, we create the directory with 0755 permissions.
 *
 * For symbolic links, we create the symbolic link if it does not
 * already exist.
 *
 * A suitable mtree file can be created from a tarball or the filesystem
 * with `bsdtar(1)` from the `libarchive-tools` Debian package:
 *
 * |[
 * bsdtar -cf - \
 *     --format=mtree \
 *     --options "!all,type,link,mode,size,time" \
 *     @- < foo.tar.gz
 * bsdtar -cf - \
 *     --format=mtree \
 *     --options "!all,type,link,mode,size,time" \
 *     -C files/ .
 * ]|
 *
 * A suitable mtree file can also be created by `mtree(8)` from the
 * `netbsd-mtree` Debian package if the filenames happen to be ASCII
 * (although this implementation does not support all escaped non-ASCII
 * filenames produced by `netbsd-mtree`):
 *
 * |[
 * mtree -p files -c | mtree -C
 * ]|
 *
 * Because hard links are used whenever possible, the permissions or
 * modification time of a source file in @source_files might be modified
 * to conform to the @mtree.
 *
 * Returns: %TRUE on success
 */
gboolean
pv_mtree_apply (const char *mtree,
                const char *sysroot,
                int sysroot_fd,
                const char *source_files,
                PvMtreeApplyFlags flags,
                GError **error)
{
  glnx_autofd int mtree_fd = -1;
  g_autoptr(GInputStream) istream = NULL;
  g_autoptr(GDataInputStream) reader = NULL;
  g_autoptr(SrtProfilingTimer) timer = NULL;
  glnx_autofd int source_files_fd = -1;
  guint line_number = 0;

  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  g_return_val_if_fail (mtree != NULL, FALSE);
  g_return_val_if_fail (sysroot != NULL, FALSE);
  g_return_val_if_fail (sysroot_fd >= 0, FALSE);

  timer = _srt_profiling_start ("Apply %s to %s", mtree, sysroot);

  if (!glnx_openat_rdonly (AT_FDCWD, mtree, TRUE, &mtree_fd, error))
    return FALSE;

  istream = g_unix_input_stream_new (glnx_steal_fd (&mtree_fd), TRUE);

  if (flags & PV_MTREE_APPLY_FLAGS_GZIP)
    {
      g_autoptr(GInputStream) filter = NULL;
      g_autoptr(GZlibDecompressor) decompressor = NULL;

      decompressor = g_zlib_decompressor_new (G_ZLIB_COMPRESSOR_FORMAT_GZIP);
      filter = g_converter_input_stream_new (istream, G_CONVERTER (decompressor));
      g_clear_object (&istream);
      istream = g_object_ref (filter);
    }

  reader = g_data_input_stream_new (istream);
  g_data_input_stream_set_newline_type (reader, G_DATA_STREAM_NEWLINE_TYPE_LF);

  if (source_files != NULL)
    {
      if (!glnx_opendirat (AT_FDCWD, source_files, FALSE, &source_files_fd,
                           error))
        return FALSE;
    }

  g_info ("Applying \"%s\" to \"%s\"...", mtree, sysroot);

  while (TRUE)
    {
      g_autofree gchar *line = NULL;
      g_autofree gchar *parent = NULL;
      const char *base;
      g_autoptr(GError) local_error = NULL;
      g_auto(PvMtreeEntry) entry = PV_MTREE_ENTRY_BLANK;
      glnx_autofd int parent_fd = -1;
      glnx_autofd int fd = -1;
      int adjusted_mode;

      line = g_data_input_stream_read_line (reader, NULL, NULL, &local_error);

      if (line == NULL)
        {
          if (local_error != NULL)
            {
              g_propagate_prefixed_error (error, g_steal_pointer (&local_error),
                                          "While reading a line from %s: ",
                                          mtree);
              return FALSE;
            }
          else
            {
              /* End of file, not an error */
              break;
            }
        }

      g_strstrip (line);
      line_number++;

      trace ("line %u: %s", line_number, line);

      if (!pv_mtree_entry_parse (line, &entry, mtree, line_number, error))
        return FALSE;

      if (entry.name == NULL || strcmp (entry.name, ".") == 0)
        continue;

      trace ("mtree entry: %s", entry.name);

      parent = g_path_get_dirname (entry.name);
      base = glnx_basename (entry.name);
      trace ("Creating %s in %s", parent, sysroot);
      parent_fd = _srt_resolve_in_sysroot (sysroot_fd, parent,
                                           SRT_RESOLVE_FLAGS_MKDIR_P,
                                           NULL, error);

      if (parent_fd < 0)
        return glnx_prefix_error (error,
                                  "Unable to create parent directory for \"%s\" in \"%s\"",
                                  entry.name, sysroot);

      switch (entry.kind)
        {
          case PV_MTREE_ENTRY_KIND_FILE:
            if (entry.size == 0)
              {
                /* For empty files, we can create it from nothing. */
                fd = TEMP_FAILURE_RETRY (openat (parent_fd, base,
                                                 (O_RDWR | O_CLOEXEC | O_NOCTTY
                                                  | O_NOFOLLOW | O_CREAT
                                                  | O_TRUNC),
                                                 0644));

                if (fd < 0)
                  return glnx_throw_errno_prefix (error,
                                                  "Unable to open \"%s\" in \"%s\"",
                                                  entry.name, sysroot);
              }
            else if (source_files_fd >= 0)
              {
                const char *source = entry.contents;

                if (source == NULL)
                  source = entry.name;

                /* If it already exists, assume it's correct */
                if (glnx_openat_rdonly (parent_fd, base, FALSE, &fd, NULL))
                  {
                    trace ("\"%s\" already exists in \"%s\"",
                           entry.name, sysroot);
                  }
                /* If we can create a hard link, that's also fine */
                else if (TEMP_FAILURE_RETRY (linkat (source_files_fd, source,
                                                     parent_fd, base, 0)) == 0)
                  {
                    trace ("Created hard link \"%s\" in \"%s\"",
                           entry.name, sysroot);
                  }
                /* Or if we can copy it, that's fine too */
                else
                  {
                    g_debug ("Could not create hard link \"%s\" from \"%s/%s\" into \"%s\": %s",
                             entry.name, source_files, source, sysroot,
                             g_strerror (errno));

                    if (!glnx_file_copy_at (source_files_fd, source, NULL,
                                            parent_fd, base,
                                            GLNX_FILE_COPY_OVERWRITE | GLNX_FILE_COPY_NOCHOWN,
                                            NULL, error))
                      return glnx_prefix_error (error,
                                                "Could not create copy \"%s\" from \"%s/%s\" into \"%s\"",
                                                entry.name, source_files,
                                                source, sysroot);

                  }
              }

            /* For other regular files we just assert that it already exists
             * (and is not a symlink). */
            if (fd < 0
                && !(entry.entry_flags & PV_MTREE_ENTRY_FLAGS_OPTIONAL)
                && !glnx_openat_rdonly (parent_fd, base, FALSE, &fd, error))
              return glnx_prefix_error (error,
                                        "Unable to open \"%s\" in \"%s\"",
                                        entry.name, sysroot);

            break;

          case PV_MTREE_ENTRY_KIND_DIR:
            /* Create directories on-demand */
            if (!glnx_ensure_dir (parent_fd, base, 0755, error))
              return glnx_prefix_error (error,
                                        "Unable to create directory \"%s\" in \"%s\"",
                                        entry.name, sysroot);

            /* Assert that it is in fact a directory */
            if (!glnx_opendirat (parent_fd, base, FALSE, &fd, error))
              return glnx_prefix_error (error,
                                        "Unable to open directory \"%s\" in \"%s\"",
                                        entry.name, sysroot);

            break;

          case PV_MTREE_ENTRY_KIND_LINK:
              {
                g_autofree char *target = NULL;
                /* Create symlinks on-demand. To be idempotent, don't delete
                 * an existing symlink. */
                target = glnx_readlinkat_malloc (parent_fd, base,
                                                 NULL, NULL);

                if (target == NULL && symlinkat (entry.link, parent_fd, base) != 0)
                  return glnx_throw_errno_prefix (error,
                                                  "Unable to create symlink \"%s\" in \"%s\"",
                                                  entry.name, sysroot);
              }
            break;

          case PV_MTREE_ENTRY_KIND_BLOCK:
          case PV_MTREE_ENTRY_KIND_CHAR:
          case PV_MTREE_ENTRY_KIND_FIFO:
          case PV_MTREE_ENTRY_KIND_SOCKET:
          case PV_MTREE_ENTRY_KIND_UNKNOWN:
          default:
            return glnx_throw (error,
                               "%s:%u: Special file not supported",
                               mtree, line_number);
        }

      if (entry.kind == PV_MTREE_ENTRY_KIND_DIR
          || (entry.mode >= 0 && entry.mode & 0111))
        adjusted_mode = 0755;
      else
        adjusted_mode = 0644;

      if (fd >= 0
          && !(entry.entry_flags & PV_MTREE_ENTRY_FLAGS_NO_CHANGE)
          && !glnx_fchmod (fd, adjusted_mode, error))
        {
          g_prefix_error (error,
                          "Unable to set mode of \"%s\" in \"%s\": ",
                          entry.name, sysroot);
          return FALSE;
        }

      if (entry.mtime_usec >= 0
          && fd >= 0
          && !(entry.entry_flags & PV_MTREE_ENTRY_FLAGS_NO_CHANGE)
          && entry.kind == PV_MTREE_ENTRY_KIND_FILE)
        {
          struct timespec times[2] =
          {
            { .tv_sec = 0, .tv_nsec = UTIME_OMIT },   /* atime */
            {
              .tv_sec = entry.mtime_usec / G_TIME_SPAN_SECOND,
              .tv_nsec = (entry.mtime_usec % G_TIME_SPAN_SECOND) * 1000
            }   /* mtime */
          };

          if (futimens (fd, times) != 0)
            g_warning ("Unable to set mtime of \"%s\" in \"%s\": %s",
                       entry.name, sysroot, g_strerror (errno));
        }
    }

  return TRUE;
}

/*
 * Free the contents of @entry, but not @entry itself.
 */
void
pv_mtree_entry_clear (PvMtreeEntry *entry)
{
  g_clear_pointer (&entry->name, g_free);
  g_clear_pointer (&entry->contents, g_free);
  g_clear_pointer (&entry->link, g_free);
  g_clear_pointer (&entry->sha256, g_free);
}
