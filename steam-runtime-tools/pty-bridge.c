/*
 * Bridge between a pair of file descriptors (usually stdin and stdout)
 * and a pseudo-terminal.
 * Partially adapted from ptyfwd, in the systemd codebase.
 *
 * Copyright 2010-2020 Lennart Poettering
 * Copyright 2022 Collabora Ltd.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "steam-runtime-tools/pty-bridge-internal.h"

#include "steam-runtime-tools/glib-backports-internal.h"

#include <glib-unix.h>

#include <stdint.h>
#include <sys/ioctl.h>
#include <termios.h>

#ifndef TIOCGPTPEER
/* It varies between architectures, but is the same on the two where we
 * really care about backporting to old libc */
# if defined(__i386__) || defined(__x86_64__)
#   define TIOCGPTPEER 0x5441
# endif
#endif

#if 0
#define trace(...) g_debug (__VA_ARGS__)
#else
#define trace(...) do { } while (0)
#endif

typedef enum
{
  /* Set if this fd is a duplicate of another fd that shares the same
   * open file description. */
  BRIDGE_FD_SHARED = (1 << 0),
  /* Set if this is the write side. */
  BRIDGE_FD_WRITE_SIDE = (1 << 1),
  /* Set if we have saved terminal attributes for this fd. */
  BRIDGE_FD_HAVE_ATTRS = (1 << 2),
  /* Set if the fd was originally blocking. */
  BRIDGE_FD_WAS_BLOCKING = (1 << 3),
  /* Set if the fd reached a hangup or error condition. */
  BRIDGE_FD_HANGUP = (1 << 4),
  BRIDGE_FD_DEFAULT = 0
} BridgeFdFlags;

typedef struct
{
  /* Buffer for communication between fd and ptmx_fd */
  char buffer[LINE_MAX];

  /* Watches for ability to read input on fd or ptmx_fd */
  GSource *read_source;
  /* Watches for ability to write output on ptmx_fd or fd */
  GSource *write_source;
  /* Bytes used at the beginning of buffer */
  size_t buffer_used;

  /* Attributes that might need to be restored when we are finished.
   * Only valid if flags & HAVE_ATTRS. */
  struct termios saved_attrs;

  /* The actual file descriptor, other than ptmx_fd */
  int fd;

  /* Flags */
  BridgeFdFlags flags;
} BridgeFd;

static void
bridge_fd_init (BridgeFd *self)
{
  self->fd = -1;
  self->flags = BRIDGE_FD_DEFAULT;
}

static int
fd_reopen (int fd,
           int flags)
{
  g_autofree gchar *path = g_strdup_printf ("/proc/self/fd/%d", fd);
  int new_fd;

  /* We don't provide a mode */
  g_return_val_if_fail ((flags & (O_CREAT|O_TMPFILE)) == 0, -1);

  new_fd = open (path, flags);

  if (new_fd >= 0)
    return new_fd;

  if (errno == ENOENT)
    errno = EBADF;

  return -1;
}

static gboolean
fd_set_blocking (int fd,
                 gboolean want_blocking,
                 gboolean *originally_blocking,
                 GError **error)
{
  gboolean is_blocking;
  int flags;

  flags = fcntl (fd, F_GETFL, 0);

  if (flags < 0)
    return glnx_throw_errno_prefix (error, "Unable to get fd flags");

  is_blocking = ((flags & O_NONBLOCK) == 0);

  if (originally_blocking != NULL)
    *originally_blocking = is_blocking;

  if (want_blocking == is_blocking)
    return TRUE;

  if (want_blocking)
    flags &= ~O_NONBLOCK;
  else
    flags |= O_NONBLOCK;

  if (fcntl (fd, F_SETFL, flags) < 0)
    return glnx_throw_errno_prefix (error, "Unable to set non-blocking state of fd");

  return TRUE;
}

static gboolean
bridge_fd_set (BridgeFd *self,
               const char *side,
               int mode,
               int fd,
               GError **error)
{
  g_return_val_if_fail (self->fd < 0, FALSE);
  g_return_val_if_fail (fd >= 0, FALSE);
  g_return_val_if_fail (mode == O_RDONLY || mode == O_WRONLY, FALSE);

  if (mode == O_WRONLY)
    self->flags |= BRIDGE_FD_WRITE_SIDE;

  /* Try to reopen the same underlying terminals as the
   * input source and output destination in non-blocking mode. This
   * avoids messing with the non-blocking state of the fds we were
   * given, e.g. by a parent process which might still be using them. */
  self->fd = fd_reopen (fd, mode | O_CLOEXEC | O_NOCTTY | O_NONBLOCK);

  if (self->fd < 0)
    {
      /* We might not be able to do that, for example if fd is a socket. */
      g_debug ("Failed to reopen %s fd, duplicating original fd: %s",
               side, g_strerror (errno));
      self->fd = TEMP_FAILURE_RETRY (fcntl (fd, F_DUPFD_CLOEXEC, 0));
      self->flags |= BRIDGE_FD_SHARED;
    }

  if (self->fd < 0)
    return glnx_throw_errno_prefix (error, "Unable to duplicate %s fd %d",
                                    side, fd);

  return TRUE;
}

static gboolean
bridge_fd_set_attributes (BridgeFd *self,
                          GError **error)
{
  gboolean was_blocking = FALSE;
  const char *side = "input source";

  if (self->flags & BRIDGE_FD_WRITE_SIDE)
    side = "output destination";

  if ((self->flags & BRIDGE_FD_SHARED)
      && !fd_set_blocking (self->fd, FALSE, &was_blocking, error))
    return FALSE;

  g_debug ("Saving and setting attributes for %s fd %d",
           side, self->fd);

  if (was_blocking)
    self->flags |= BRIDGE_FD_WAS_BLOCKING;

  if (tcgetattr (self->fd, &self->saved_attrs) >= 0)
    {
      struct termios raw_attrs;

      self->flags |= BRIDGE_FD_HAVE_ATTRS;

      /* Mostly put the fd in raw mode, but don't alter the input flags
       * for the output fd or the output flags for the input fd, if they
       * are pointing to different terminals. */
      raw_attrs = self->saved_attrs;
      cfmakeraw (&raw_attrs);

      if (self->flags & BRIDGE_FD_WRITE_SIDE)
        {
          raw_attrs.c_iflag = self->saved_attrs.c_iflag;
          raw_attrs.c_lflag = self->saved_attrs.c_lflag;
        }
      else
        {
          raw_attrs.c_oflag = self->saved_attrs.c_oflag;
        }

      if (tcsetattr (self->fd, TCSANOW, &raw_attrs) < 0)
        return glnx_throw_errno_prefix (error, "Unable to set terminal flags");
    }
  else if (errno == ENOTTY)
    {
      g_debug ("%s fd is not a terminal", side);
    }
  else
    {
      g_debug ("Cannot get terminal attributes for %s fd, ignoring: %s",
               side, g_strerror (errno));
    }

  return TRUE;
}

static void
bridge_fd_clear_io_sources (BridgeFd *self)
{
  if (self->read_source != NULL)
    g_source_destroy (self->read_source);

  if (self->write_source != NULL)
    g_source_destroy (self->write_source);

  g_clear_pointer (&self->read_source, g_source_unref);
  g_clear_pointer (&self->write_source, g_source_unref);
}

static void
bridge_fd_close (BridgeFd *self)
{
  if (self->fd >= 0)
    {
      g_debug ("Restoring old attributes for %s fd %d",
               (self->flags & BRIDGE_FD_WRITE_SIDE) ? "output" : "input",
               self->fd);

      if (self->flags & BRIDGE_FD_WAS_BLOCKING)
        fd_set_blocking (self->fd, TRUE, NULL, NULL);

      if (self->flags & BRIDGE_FD_HAVE_ATTRS)
        tcsetattr (self->fd, TCSANOW, &self->saved_attrs);
    }

  self->flags = BRIDGE_FD_DEFAULT;
  glnx_close_fd (&self->fd);
}

/*
 * SrtPtyBridge:
 *
 * A pseudo-terminal communicating with a child process elsewhere,
 * similar to what happens in ssh or a terminal emulator.
 */
struct _SrtPtyBridge
{
  GObject parent;

  /* Non-NULL if initialization failed before initable_init. */
  GError *init_error;

  GMainContext *context;

  /* Source of input to be written to the ptmx_fd so that it can be read
   * from the terminal_fd by the other process */
  BridgeFd input_source;
  /* Destination for output read from the ptmx_fd, which was originally
   * written to the terminal_fd by the other process */
  BridgeFd output_dest;

  /* Buffer for out-of-band signalling to the ptmx (Ctrl+C, etc.),
   * with priority over the BridgeFd->buffer. */
  GString *oob_buffer;

  /* The file descriptor obtained by opening /dev/ptmx */
  int ptmx_fd;
  /* The file descriptor controlled by ptmx_fd, to give to the other process */
  int terminal_fd;
  /* The controlling terminal of this process, if available.
   * -1 if lazy allocation has not been done yet.
   * -2 if opening the controlling terminal failed. */
  int controlling_terminal_fd;
};

struct _SrtPtyBridgeClass
{
  GObjectClass parent;
};

enum
{
  PROP_0,
  PROP_INPUT_SOURCE_FD,
  PROP_OUTPUT_DEST_FD,
  N_PROPS
};

static GParamSpec *properties[N_PROPS] = { NULL };
static void initable_iface_init (GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (SrtPtyBridge,
                         _srt_pty_bridge,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                initable_iface_init))

static void
_srt_pty_bridge_init (SrtPtyBridge *self)
{
  self->init_error = NULL;
  self->context = g_main_context_ref_thread_default ();
  bridge_fd_init (&self->input_source);
  bridge_fd_init (&self->output_dest);
  self->oob_buffer = g_string_new ("");
  self->ptmx_fd = -1;
  self->terminal_fd = -1;
  self->controlling_terminal_fd = -1;
}

static void
srt_pty_bridge_get_property (GObject *object,
                             guint prop_id,
                             GValue *value,
                             GParamSpec *pspec)
{
  SrtPtyBridge *self = SRT_PTY_BRIDGE (object);

  switch (prop_id)
    {
      case PROP_INPUT_SOURCE_FD:
        g_value_set_int (value, self->input_source.fd);
        break;

      case PROP_OUTPUT_DEST_FD:
        g_value_set_int (value, self->output_dest.fd);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_pty_bridge_set_property (GObject *object,
                             guint prop_id,
                             const GValue *value,
                             GParamSpec *pspec)
{
  SrtPtyBridge *self = SRT_PTY_BRIDGE (object);
  int fd;

  switch (prop_id)
    {
      case PROP_INPUT_SOURCE_FD:
        fd = g_value_get_int (value);

        if (fd >= 0 && self->init_error == NULL)
          bridge_fd_set (&self->input_source, "input source", O_RDONLY,
                         fd, &self->init_error);

        break;

      case PROP_OUTPUT_DEST_FD:
        fd = g_value_get_int (value);

        if (fd >= 0 && self->init_error == NULL)
          bridge_fd_set (&self->output_dest, "output destination", O_WRONLY,
                         fd, &self->init_error);

        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

/*
 * Lazily open the current process's controlling terminal, if any.
 */
static int
_srt_pty_bridge_get_controlling_terminal (SrtPtyBridge *self)
{
  if (self->controlling_terminal_fd == -1)
    {
      self->controlling_terminal_fd = open ("/dev/tty", O_RDWR | O_CLOEXEC);

      if (self->controlling_terminal_fd < 0)
        {
          g_debug ("Unable to open controlling terminal: %s", g_strerror (errno));
          self->controlling_terminal_fd = -2;
          return FALSE;
        }
    }

  return self->controlling_terminal_fd;
}

static void
srt_pty_bridge_dispose (GObject *object)
{
  SrtPtyBridge *self = SRT_PTY_BRIDGE (object);

  bridge_fd_clear_io_sources (&self->input_source);
  bridge_fd_clear_io_sources (&self->output_dest);

  G_OBJECT_CLASS (_srt_pty_bridge_parent_class)->dispose (object);
}

static void
srt_pty_bridge_finalize (GObject *object)
{
  SrtPtyBridge *self = SRT_PTY_BRIDGE (object);

  /* The order here must be the opposite of what is done in initable_init,
   * so that we progressively go back to the terminals' original state. */
  bridge_fd_close (&self->output_dest);
  bridge_fd_close (&self->input_source);

  g_clear_error (&self->init_error);
  g_clear_pointer (&self->context, g_main_context_unref);
  glnx_close_fd (&self->ptmx_fd);
  glnx_close_fd (&self->terminal_fd);
  glnx_close_fd (&self->controlling_terminal_fd);
  g_string_free (self->oob_buffer, TRUE);

  G_OBJECT_CLASS (_srt_pty_bridge_parent_class)->finalize (object);
}

static void
_srt_pty_bridge_class_init (SrtPtyBridgeClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->get_property = srt_pty_bridge_get_property;
  object_class->set_property = srt_pty_bridge_set_property;
  object_class->dispose = srt_pty_bridge_dispose;
  object_class->finalize = srt_pty_bridge_finalize;

  properties[PROP_INPUT_SOURCE_FD] =
    g_param_spec_int ("input-source-fd", "Input source fd",
                      "Readable fd producing bytes that will come out from "
                      "the terminal",
                      -1, G_MAXINT, -1,
                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  properties[PROP_OUTPUT_DEST_FD] =
    g_param_spec_int ("output-dest-fd", "Output destination fd",
                      "Writable fd which will receive bytes originally "
                      "written to the terminal",
                      -1, G_MAXINT, -1,
                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void _srt_pty_bridge_enable_input (SrtPtyBridge *self);

/*
 * Drain as much as possible of the input buffer into the pseudo-terminal.
 */
static gboolean
_srt_pty_bridge_write_ptmx (SrtPtyBridge *self)
{
  ssize_t len;
  char *buffer;
  size_t available;

  if (self->oob_buffer->len > 0)
    {
      /* Try to write out-of-band interrupts (Ctrl+C, etc.) first */
      buffer = self->oob_buffer->str;
      available = self->oob_buffer->len;
      trace ("Writing up to %zu bytes of out-of-band interrupts",
             available);
    }
  else
    {
      /* Only write out ordinary text if there are no Ctrl+C, etc. pending */
      buffer = self->input_source.buffer;
      available = self->input_source.buffer_used;
      trace ("Writing up to %zu bytes of normal input", available);
    }

  if (G_LIKELY (available > 0))
    {
      len = write (self->ptmx_fd, buffer, available);

      if (len < 0)
        {
          if (G_IN_SET (errno, EAGAIN, EIO))
            {
              return G_SOURCE_CONTINUE;
            }
          else
            {
              g_debug ("Error writing to ptmx: %s", g_strerror (errno));

              /* We can no longer read output from the ptmx */
              if (self->output_dest.read_source != NULL)
                g_source_destroy (self->output_dest.read_source);
              g_clear_pointer (&self->output_dest.read_source, g_source_unref);

              /* We can no longer write input to the ptmx */
              g_clear_pointer (&self->input_source.write_source, g_source_unref);
              glnx_close_fd (&self->ptmx_fd);
              return G_SOURCE_REMOVE;   /* Destroys input_source.write_source */
            }
        }
      else if (len > 0)
        {
          trace ("Wrote %zd bytes of input to ptmx", len);

          if (buffer == self->input_source.buffer)
            {
              g_assert (self->input_source.buffer_used >= (size_t) len);
              memmove (buffer,
                       buffer + len,
                       self->input_source.buffer_used - len);
              self->input_source.buffer_used -= len;
            }
          else
            {
              g_assert (buffer == self->oob_buffer->str);
              g_string_erase (self->oob_buffer, 0, len);
            }
        }
    }

  /* We have freed up some space in the buffer, so we might need to
   * re-enable input */
  _srt_pty_bridge_enable_input (self);

  if (self->oob_buffer->len == 0 && self->input_source.buffer_used == 0)
    {
      trace ("Nothing more to write, disabling ptmx output polling");
      g_clear_pointer (&self->input_source.write_source, g_source_unref);
      return G_SOURCE_REMOVE;   /* Destroys input_source.write_source */
    }

  /* Buffer is non-empty, come back later */
  trace ("Writes will continue, continuing to poll ptmx for output");
  return G_SOURCE_CONTINUE;
}

/* I/O callback for input_source.write_source,
 * called when we are ready to write from input_source.buffer or oob_buffer
 * to ptmx_fd. */
static gboolean
_srt_pty_bridge_ptmx_out_cb (int ptmx_fd,
                             GIOCondition condition,
                             gpointer user_data)
{
  SrtPtyBridge *self = SRT_PTY_BRIDGE (user_data);

  trace ("Ready to write to ptmx");
  g_return_val_if_fail (ptmx_fd == self->ptmx_fd, G_SOURCE_REMOVE);
  g_return_val_if_fail (g_main_current_source () == self->input_source.write_source,
                        G_SOURCE_REMOVE);
  return _srt_pty_bridge_write_ptmx (self);
}

static gboolean
_srt_pty_bridge_try_write_ptmx (SrtPtyBridge *self)
{
  if (self->ptmx_fd < 0)
    return FALSE;

  if (self->input_source.write_source == NULL)
    {
      /* If we don't already have output to the ptmx pending, and we're in
       * the correct thread, try to do one quick write to it.
       * If this drains the buffer, then we won't need to start polling. */
      if (g_main_context_acquire (self->context))
        {
          trace ("Trying to write to ptmx immediately");
          _srt_pty_bridge_write_ptmx (self);
          g_main_context_release (self->context);
        }
      else
        {
          trace ("Called from another context, deferring write to ptmx");
        }

      /* If that didn't drain the buffer, poll the ptmx and write to it
       * when it becomes available. */
      if (self->oob_buffer->len > 0 || self->input_source.buffer_used > 0)
        {
          GSource *source;

          source = g_unix_fd_source_new (self->ptmx_fd, G_IO_OUT);
          g_source_set_callback (source,
                                 (GSourceFunc) G_CALLBACK (_srt_pty_bridge_ptmx_out_cb),
                                 self, NULL);
          g_source_set_priority (source, G_PRIORITY_DEFAULT);
          g_source_attach (source, self->context);
          self->input_source.write_source = source;
        }
    }
  else
    {
      trace ("Write to ptmx already pending");
    }

  return TRUE;
}

/*
 * I/O callback for input_source.read_source, called when data is available
 * to read from input_source.fd
 */
static gboolean
_srt_pty_bridge_input_in_cb (int input_source_fd,
                             GIOCondition condition,
                             gpointer user_data)
{
  SrtPtyBridge *self = SRT_PTY_BRIDGE (user_data);

  g_return_val_if_fail (input_source_fd == self->input_source.fd, G_SOURCE_REMOVE);
  g_return_val_if_fail (g_main_current_source () == self->input_source.read_source,
                        G_SOURCE_REMOVE);

  if (self->ptmx_fd < 0)
    {
      /* We have nowhere to write this, we might as well stop reading */
      g_debug ("ptmx closed, no longer reading input");
      g_clear_pointer (&self->input_source.read_source, g_source_unref);
      bridge_fd_close (&self->input_source);
      return G_SOURCE_REMOVE;   /* Destroys input_source.read_source */
    }

  if (G_LIKELY (self->input_source.buffer_used < sizeof (self->input_source.buffer)))
    {
      ssize_t len;

      len = read (self->input_source.fd,
                  self->input_source.buffer + self->input_source.buffer_used,
                  sizeof (self->input_source.buffer) - self->input_source.buffer_used);

      if (len < 0)
        {
          if (errno == EAGAIN)
            {
              return G_SOURCE_CONTINUE;
            }
          else
            {
              g_debug ("Error reading input source: %s", g_strerror (errno));
              g_clear_pointer (&self->input_source.read_source, g_source_unref);
              bridge_fd_close (&self->input_source);
              return G_SOURCE_REMOVE;   /* Destroys input_source.read_source */
            }
        }
      else if (len == 0)
        {
          g_debug ("EOF on input source");
        }
      else
        {
          trace ("Read %zd bytes of input from input source", len);
          self->input_source.buffer_used += (size_t) len;
        }
    }

  _srt_pty_bridge_try_write_ptmx (self);

  if (self->input_source.buffer_used == sizeof (self->input_source.buffer))
    {
      trace ("Input buffer is full, pausing reading from input source");
      g_clear_pointer (&self->input_source.read_source, g_source_unref);
      return G_SOURCE_REMOVE;   /* Destroys input_source.read_source */
    }

  return G_SOURCE_CONTINUE;
}

/*
 * Drain as much as possible of the output buffer into the destination.
 */
static gboolean
_srt_pty_bridge_write_output (SrtPtyBridge *self)
{
  ssize_t len;

  if (G_LIKELY (self->output_dest.buffer_used > 0))
    {
      len = write (self->output_dest.fd, self->output_dest.buffer,
                   self->output_dest.buffer_used);

      if (len < 0)
        {
          if (errno == EAGAIN)
            {
              return G_SOURCE_CONTINUE;
            }
          else
            {
              g_debug ("Error writing to output destination: %s", g_strerror (errno));
              g_clear_pointer (&self->output_dest.write_source, g_source_unref);
              bridge_fd_close (&self->output_dest);
              return G_SOURCE_REMOVE;   /* Destroys output_dest.write_source */
            }
        }
      else if (len > 0)
        {
          g_assert (self->output_dest.buffer_used >= (size_t) len);
          memmove (self->output_dest.buffer,
                   self->output_dest.buffer + len,
                   self->output_dest.buffer_used - len);
          self->output_dest.buffer_used -= len;
        }
    }

  /* We have freed up some space in the buffer, so we might need to
   * re-enable input */
  _srt_pty_bridge_enable_input (self);

  if (self->output_dest.buffer_used == 0)
    {
      trace ("Nothing more to write, disabling destination output polling");
      g_clear_pointer (&self->output_dest.write_source, g_source_unref);
      return G_SOURCE_REMOVE;   /* Destroys output_dest.write_source */
    }

  trace ("Writes will continue, continuing to poll destination for output");
  return G_SOURCE_CONTINUE;
}

/*
 * I/O callback for output_dest.write_source,
 * called when we are ready to write from output_source.buffer
 * to output_dest.fd
 */
static gboolean
_srt_pty_bridge_output_out_cb (int output_dest_fd,
                               GIOCondition condition,
                               gpointer user_data)
{
  SrtPtyBridge *self = SRT_PTY_BRIDGE (user_data);

  trace ("Ready to write to output destination");
  g_return_val_if_fail (output_dest_fd == self->output_dest.fd, G_SOURCE_REMOVE);
  g_return_val_if_fail (g_main_current_source () == self->output_dest.write_source,
                        G_SOURCE_REMOVE);
  return _srt_pty_bridge_write_output (self);
}

/*
 * Try to drain the output buffer into the output destination, either
 * immediately or when it becomes available for writing.
 */
static gboolean
_srt_pty_bridge_try_write_output (SrtPtyBridge *self)
{
  if (self->output_dest.flags & BRIDGE_FD_HANGUP)
    {
      trace ("No output destination available, discarding output");
      self->output_dest.buffer_used = 0;
      /* We can't just close the ptmx fd, because we are also using it
       * to copy from the input source to the ptmx */
      return FALSE;
    }

  if (self->output_dest.write_source == NULL)
    {
      /* If we don't already have output to the destination pending, and
       * we're in the correct thread, try to do one quick write to it.
       * If this drains the buffer, then we won't need to start polling. */
      if (g_main_context_acquire (self->context))
        {
          trace ("Trying to write to output destination immediately");
          _srt_pty_bridge_write_output (self);
          g_main_context_release (self->context);
        }
      else
        {
          trace ("Called from another context, deferring write to output dest");
        }

      /* If that didn't drain the buffer, poll the ptmx and write to it
       * when it becomes available. */
      if (self->output_dest.buffer_used > 0)
        {
          GSource *source;

          trace ("Scheduling write for when output dest becomes writeable");
          source = g_unix_fd_source_new (self->output_dest.fd, G_IO_OUT);
          g_source_set_callback (source,
                                 (GSourceFunc) G_CALLBACK (_srt_pty_bridge_output_out_cb),
                                 self, NULL);
          g_source_set_priority (source, G_PRIORITY_DEFAULT);
          g_source_attach (source, self->context);
          self->output_dest.write_source = source;
        }
    }
  else
    {
      trace ("Write to output destination already pending");
    }

  return TRUE;
}

/*
 * I/O callback for output_dest.read_source, called when data is available
 * to read from ptmx_fd
 */
static gboolean
_srt_pty_bridge_ptmx_in_cb (int ptmx_fd,
                            GIOCondition condition,
                            gpointer user_data)
{
  SrtPtyBridge *self = SRT_PTY_BRIDGE (user_data);

  trace ("Ready to read from ptmx");
  g_return_val_if_fail (ptmx_fd == self->ptmx_fd, G_SOURCE_REMOVE);
  g_return_val_if_fail (g_main_current_source () == self->output_dest.read_source,
                        G_SOURCE_REMOVE);

  if (G_LIKELY (self->output_dest.buffer_used < sizeof (self->output_dest.buffer)))
    {
      ssize_t len;

      len = read (self->ptmx_fd,
                  self->output_dest.buffer + self->output_dest.buffer_used,
                  sizeof (self->output_dest.buffer) - self->output_dest.buffer_used);

      if (len < 0)
        {
          /* EIO on the ptmx might be caused by vhangup() or by
           * everything on the ptmx side having been closed.
           * We try again next time it reports readable. */
          if (errno == EAGAIN || errno == EIO)
            {
              return G_SOURCE_CONTINUE;
            }
          else
            {
              g_debug ("Error reading ptmx: %s", g_strerror (errno));

              /* We can no longer write input to the ptmx */
              if (self->input_source.write_source != NULL)
                g_source_destroy (self->input_source.write_source);
              g_clear_pointer (&self->input_source.write_source, g_source_unref);

              /* We can no longer read output from the ptmx */
              g_clear_pointer (&self->output_dest.read_source, g_source_unref);
              glnx_close_fd (&self->ptmx_fd);
              return G_SOURCE_REMOVE;   /* Destroys output_dest.read_source */
            }
        }
      else if (len == 0)
        {
          /* Don't destroy the ptmx here: it might stop being readable, but
           * we still want to use it for output */
          g_debug ("Ignoring read EOF on ptmx");
        }
      else
        {
          trace ("Read %zd bytes of output from ptmx", len);
          self->output_dest.buffer_used += (size_t) len;
        }
    }

  _srt_pty_bridge_try_write_output (self);

  if (self->output_dest.buffer_used == sizeof (self->output_dest.buffer))
    {
      trace ("Output buffer is full, pausing reading from ptmx");
      g_clear_pointer (&self->output_dest.read_source, g_source_unref);
      return G_SOURCE_REMOVE;   /* Destroys output_dest.read_source */
    }

  return G_SOURCE_CONTINUE;
}

/*
 * Called during initialization and whenever a full buffer might have
 * been drained.
 */
static void
_srt_pty_bridge_enable_input (SrtPtyBridge *self)
{
  GSource *source;

  trace ("Checking whether to re-enable input sources");

  if (self->input_source.fd >= 0 &&
      self->input_source.buffer_used < sizeof (self->input_source.buffer) &&
      self->input_source.read_source == NULL)
    {
      trace ("Input buffer not full, scheduling read from input source");
      source = g_unix_fd_source_new (self->input_source.fd, G_IO_IN);
      g_source_set_callback (source,
                             (GSourceFunc) G_CALLBACK (_srt_pty_bridge_input_in_cb),
                             self, NULL);
      g_source_set_priority (source, G_PRIORITY_DEFAULT);
      g_source_attach (source, self->context);
      self->input_source.read_source = source;
    }

  if (self->ptmx_fd >= 0 &&
      self->output_dest.buffer_used < sizeof (self->output_dest.buffer) &&
      self->output_dest.read_source == NULL)
    {
      trace ("Output buffer not full, scheduling read from ptmx");
      source = g_unix_fd_source_new (self->ptmx_fd, G_IO_IN);
      g_source_set_callback (source,
                             (GSourceFunc) G_CALLBACK (_srt_pty_bridge_ptmx_in_cb),
                             self, NULL);
      g_source_set_priority (source, G_PRIORITY_DEFAULT);
      g_source_attach (source, self->context);
      self->output_dest.read_source = source;
    }
}

static gboolean
_srt_pty_bridge_initable_init (GInitable *initable,
                               GCancellable *cancellable,
                               GError **error)
{
  SrtPtyBridge *self = SRT_PTY_BRIDGE (initable);

  if (self->init_error != NULL)
    {
      g_set_error_literal (error, self->init_error->domain,
                           self->init_error->code, self->init_error->message);
      return FALSE;
    }

  /* The order here must be the opposite of what is done in finalize,
   * so that we can roll back to the original state later. */
  if (!bridge_fd_set_attributes (&self->input_source, error)
      || !bridge_fd_set_attributes (&self->output_dest, error))
    return FALSE;

  self->ptmx_fd = posix_openpt (O_RDWR | O_NOCTTY | O_CLOEXEC);

  if (self->ptmx_fd < 0)
    return glnx_throw_errno_prefix (error, "posix_openpt");

  if (grantpt (self->ptmx_fd) < 0)
    return glnx_throw_errno_prefix (error, "grantpt");

  if (unlockpt (self->ptmx_fd) < 0)
    return glnx_throw_errno_prefix (error, "unlockpt");

  if (!fd_set_blocking (self->ptmx_fd, FALSE, NULL, error))
    return FALSE;

#ifdef TIOCGPTPEER
  self->terminal_fd = ioctl (self->ptmx_fd, TIOCGPTPEER, O_RDWR | O_NOCTTY | O_CLOEXEC);

  if (self->terminal_fd < 0)
#endif
    {
      char *terminal_path = ptsname (self->ptmx_fd);

      self->terminal_fd = open (terminal_path, O_RDWR | O_NOCTTY | O_CLOEXEC);

      if (self->terminal_fd < 0)
        return glnx_throw_errno_prefix (error, "open %s", terminal_path);
    }

  if (self->output_dest.fd >= 0)
    {
      struct winsize size = {};

      if (ioctl (self->output_dest.fd, TIOCGWINSZ, &size) != 0)
        g_debug ("Unable to get window size of output: %s", g_strerror (errno));
      else if (ioctl (self->ptmx_fd, TIOCSWINSZ, &size) != 0)
        g_debug ("Unable to set window size of terminal: %s", g_strerror (errno));
    }

  _srt_pty_bridge_enable_input (self);
  return TRUE;
}

static void
initable_iface_init (GInitableIface *iface)
{
  iface->init = _srt_pty_bridge_initable_init;
}

/*
 * _srt_pty_bridge_new:
 * @input_source_fd: Source of input to be written to the ptmx side of
 *  the pseudo-terminal, so that it can be read from the terminal side
 *  by processes running on the terminal.
 * @output_dest_fd: Destination for output read from the ptmx side of
 *  the pseudo-terminal, which was originally written to the terminal
 *  side by processes running on the terminal.
 * @error: the usual
 *
 * Create a new pseudo-terminal bridge.
 *
 * Returns: the new object
 */
SrtPtyBridge *
_srt_pty_bridge_new (int input_source_fd,
                     int output_dest_fd,
                     GError **error)
{
  return g_initable_new (SRT_TYPE_PTY_BRIDGE,
                         NULL,
                         error,
                         "input-source-fd", input_source_fd,
                         "output-dest-fd", output_dest_fd,
                         NULL);
}

/*
 * _srt_pty_bridge_handle_signal:
 * @self: the bridge
 * @sig: a signal number such as `SIGINT`
 * @handled: (out) (optional): set to %TRUE if @sig was handled or %FALSE if not
 * @error: the usual
 *
 * Try to handle a signal.
 *
 * Can only be called from a thread that owns the main-context
 * where the bridge was constructed.
 *
 * Returns: %TRUE if no problems, even if the signal was not handled
 */
gboolean
_srt_pty_bridge_handle_signal (SrtPtyBridge *self,
                               int sig,
                               gboolean *handled,
                               GError **error)
{
  int my_terminal = -1;

  if (sig == SIGWINCH)
    {
      struct winsize size = {};

      if (self->ptmx_fd < 0)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "No terminal available to forward SIGWINCH");
          return FALSE;
        }

      my_terminal = _srt_pty_bridge_get_controlling_terminal (self);

      if (my_terminal < 0)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Cannot get controlling terminal");
          return FALSE;
        }

      if (ioctl (my_terminal, TIOCGWINSZ, &size) != 0)
        return glnx_throw_errno_prefix (error, "Unable to get size of controlling terminal");

      if (ioctl (self->ptmx_fd, TIOCSWINSZ, &size) != 0)
        return glnx_throw_errno_prefix (error, "Unable to set size of remote terminal");

      if (handled != NULL)
        *handled = TRUE;

      return TRUE;
    }

  if (G_IN_SET (sig, SIGINT, SIGQUIT, SIGTSTP))
    {
      struct termios settings = {};
      size_t pos;

      if (self->ptmx_fd < 0)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "No terminal available to forward signal %d", sig);
          return FALSE;
        }

      my_terminal = _srt_pty_bridge_get_controlling_terminal (self);

      if (my_terminal < 0)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Cannot get controlling terminal");
          return FALSE;
        }

      if (tcgetattr (my_terminal, &settings) < 0)
        return glnx_throw_errno_prefix (error, "Unable to get size of controlling terminal");

      switch (sig)
        {
          case SIGINT:
            pos = VINTR;
            break;

          case SIGQUIT:
            pos = VQUIT;
            break;

          case SIGTSTP:
            pos = VSUSP;
            break;

          default:
            pos = SIZE_MAX;
        }

      if (pos < G_N_ELEMENTS (settings.c_cc)
          && settings.c_cc[pos] != _POSIX_VDISABLE
          && self->ptmx_fd >= 0)
        {
          g_string_append_c (self->oob_buffer, settings.c_cc[pos]);
          _srt_pty_bridge_try_write_ptmx (self);
          g_debug ("Sent signal %d as \\x%02x", sig, settings.c_cc[pos]);

          if (handled != NULL)
            *handled = TRUE;

          return TRUE;
        }

      g_debug ("Signal %d cannot be forwarded through this pty", sig);

      if (handled != NULL)
        *handled = FALSE;

      return TRUE;
    }

  g_debug ("Signal %d cannot be forwarded through pty", sig);

  if (handled != NULL)
    *handled = FALSE;

  return TRUE;
}

/*
 * _srt_pty_bridge_get_terminal_fd:
 * @self: the bridge
 *
 * Return a file descriptor for the terminal end of the bridge,
 * suitable for forwarding to an only-partially-trusted subprocess.
 *
 * Can only be called from a thread that owns the main-context
 * where the bridge was constructed.
 *
 * Returns: a file descriptor owned by the bridge, or -1 if already closed.
 *  The caller must not close this fd, and will usually need to duplicate it.
 */
int
_srt_pty_bridge_get_terminal_fd (SrtPtyBridge *self)
{
  g_return_val_if_fail (SRT_IS_PTY_BRIDGE (self), -1);
  return self->terminal_fd;
}

/*
 * _srt_pty_bridge_is_active:
 * @self: the bridge
 *
 * Check whether the bridge is still (potentially) active.
 *
 * Can only be called from a thread that owns the main-context
 * where the bridge was constructed.
 *
 * Returns: %TRUE if the bridge is still active.
 */
gboolean
_srt_pty_bridge_is_active (SrtPtyBridge *self)
{
  g_return_val_if_fail (SRT_IS_PTY_BRIDGE (self), FALSE);

  if (self->ptmx_fd >= 0)
    {
      /* Terminal still open, and ^C (or similar) waiting to be sent to it */
      if (self->oob_buffer->len > 0)
        return TRUE;

      /* Terminal still open, and ordinary text waiting to be sent to it
       * (even if the input source has closed) */
      if (self->input_source.buffer_used > 0)
        return TRUE;

      /* Terminal still open, and no ordinary text waiting to be sent to it,
       * but we could read some from the input source */
      if (self->input_source.fd >= 0)
        return TRUE;

      /* Terminal still open, and we might read text from it that we will
       * want to send to the output destination */
      if (self->output_dest.fd >= 0)
        return TRUE;
    }

  /* If we still have an output destination and we want to send buffered
   * text originally from the ptmx to it, then we're still active,
   * even if the ptmx itself has closed */
  if (self->output_dest.buffer_used > 0 && self->output_dest.fd >= 0)
    return TRUE;

  return FALSE;
}

/*
 * _srt_pty_bridge_close_terminal_fd:
 * @self: the bridge
 *
 * Close the fd pointing to the terminal. Subsequent calls to
 * _srt_pty_bridge_get_terminal_fd() will return -1.
 *
 * Can only be called from a thread that owns the main-context
 * where the bridge was constructed.
 */
void
_srt_pty_bridge_close_terminal_fd (SrtPtyBridge *self)
{
  glnx_close_fd (&self->terminal_fd);
}
