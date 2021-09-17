/*
 * Copyright © 2019 Collabora Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <assert.h>
#include <err.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/Xlib.h>
#include <GL/gl.h>
#include <GL/glx.h>

#include <getopt.h>

static const int WIDTH = 200;
static const int HEIGHT = 200;

#define N_ELEMENTS(arr) (sizeof (arr) / sizeof (arr[0]))

static const char *argv0;

typedef struct
{
  bool visible;
  Display *display;
  Window window;
  GLXContext context;
} App;

static void *
xcalloc (size_t n,
         size_t len)
{
  void *self = calloc (n, len);

  if (self == NULL)
    err (EXIT_FAILURE, "calloc");

  return self;
}

#define new0(type) ((type *) xcalloc (1, sizeof (type)))

static App *
app_new (bool visible)
{
  App *self = new0 (App);

  self->display = NULL;
  self->visible = visible;
  return self;
}

static void app_free (App *self);

static void app_draw_frame (App *self);
static void app_draw_triangle (void);
static void app_init_gl (App *self);
static void app_main_loop (App *self);
static void app_make_window (App *self);

static void
app_run (App *self)
{
  app_init_gl (self);
  app_main_loop (self);
}

static void
app_init_gl (App *self)
{
  self->display = XOpenDisplay (NULL);

  if (self->display == NULL)
    errx (EXIT_FAILURE, "Unable to open display");

  app_make_window (self);

  if (self->visible)
    XMapWindow (self->display, self->window);

  glXMakeCurrent (self->display, self->window, self->context);
}

static void
app_draw_triangle (void)
{
  //clear color and depth buffer
  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glLoadIdentity ();

  // Make it red
  glColor3f (1.0f, 0.0f, 0.0f);

  glBegin (GL_TRIANGLES);
  glVertex3f (-0.8f, -0.8f, 0.0f);
  glVertex3f (0.8f, -0.8f, 0.0f);
  glVertex3f (0.0f, 0.6f, 0.0f);
  glEnd ();

  glEndList ();

  glEnable (GL_NORMALIZE);
}

static void
app_make_window (App *self)
{
  int attribs[64];
  int i = 0;
  int scrnum;
  XSetWindowAttributes attr;
  unsigned long mask;
  Window root;
  XVisualInfo *visinfo;

  /* Singleton attributes. */
  attribs[i++] = GLX_RGBA;
  attribs[i++] = GLX_DOUBLEBUFFER;

  /* Key/value attributes. */
  attribs[i++] = GLX_RED_SIZE;
  attribs[i++] = 1;
  attribs[i++] = GLX_GREEN_SIZE;
  attribs[i++] = 1;
  attribs[i++] = GLX_BLUE_SIZE;
  attribs[i++] = 1;
  attribs[i++] = GLX_DEPTH_SIZE;
  attribs[i++] = 1;

  attribs[i++] = None;
  assert (i < N_ELEMENTS (attribs));

  scrnum = DefaultScreen (self->display);
  root = RootWindow (self->display, scrnum);

  visinfo = glXChooseVisual (self->display, scrnum, attribs);

  if (visinfo == NULL)
    errx (EXIT_FAILURE, "Error: couldn't get an RGB, Double-buffered visual");

  /* window attributes */
  attr.background_pixel = 0;
  attr.border_pixel = 0;
  attr.colormap = XCreateColormap (self->display, root, visinfo->visual, AllocNone);
  attr.event_mask = StructureNotifyMask | ExposureMask | KeyPressMask;
  mask = CWBackPixel | CWBorderPixel | CWColormap | CWEventMask;

  self->window = XCreateWindow (self->display, root, 0, 0, WIDTH, HEIGHT,
                               0, visinfo->depth, InputOutput,
                               visinfo->visual, mask, &attr);

  /* set hints and properties */
  {
    XSizeHints sizehints;
    sizehints.x = 0;
    sizehints.y = 0;
    sizehints.width  = WIDTH;
    sizehints.height = HEIGHT;
    sizehints.flags = USSize | USPosition;
    XSetNormalHints (self->display, self->window, &sizehints);
    XSetStandardProperties (self->display, self->window, "check-gl", "check-gl",
                            None, (char **) NULL, 0, &sizehints);
  }

  self->context = glXCreateContext (self->display, visinfo, NULL, True);

  if (!self->context)
    errx (EXIT_FAILURE, "Error: glXCreateContext failed");

  XFree (visinfo);
}

static void
app_main_loop (App *self)
{
  int i;

  for (i = 0; i < (self->visible ? 10000 : 10); ++i)
    app_draw_frame (self);
}

static void
app_free (App *self)
{
  if (self->display)
    glXMakeCurrent (self->display, None, NULL);

  if (self->context)
    glXDestroyContext (self->display, self->context);

  if (self->window)
    XDestroyWindow (self->display, self->window);

  XCloseDisplay (self->display);
  free (self);
}

static void
app_draw_frame (App *self)
{
  app_draw_triangle ();
  glXSwapBuffers (self->display, self->window);
}

enum
{
  OPTION_HELP = 1,
  OPTION_VERSION,
  OPTION_VISIBLE,
};

static struct option long_options[] =
{
  { "help", no_argument, NULL, OPTION_HELP },
  { "version", no_argument, NULL, OPTION_VERSION },
  { "visible", no_argument, NULL, OPTION_VISIBLE },
  { NULL, 0, NULL, 0 }
};

static void usage (int code) __attribute__((__noreturn__));

static void usage (int code)
{
    FILE *stream = (code == EXIT_SUCCESS ? stdout : stderr);

    fprintf (stream, "Usage: %s [OPTIONS]\n", argv0);
    fprintf (stream, "Options:\n");
    fprintf (stream, "--help\t\tShow this help and exit\n");
    fprintf (stream, "--visible\tMake test window visible\n");
    fprintf (stream, "--version\tShow version and exit\n");
    exit (code);
}

int
main (int argc, char **argv)
{
  int opt;
  bool visible = false;
  App *app = NULL;

  argv0 = argv[0];

  while ((opt = getopt_long (argc, argv, "", long_options, NULL)) != -1)
    {
      switch (opt)
        {
          case OPTION_HELP:
            usage(0);
            break;  // not reached

          case OPTION_VERSION:
            /* Output version number as YAML for machine-readability,
             * inspired by `ostree --version` and `docker version` */
            printf ("%s:\n", argv[0]);
            printf (" Package: steam-runtime-tools\n");
            printf (" Version: %s\n", VERSION);
            return EXIT_SUCCESS;

          case OPTION_VISIBLE:
            visible = true;
            break;

          case '?':
          default:
            usage(2);
            break;  // not reached
        }
    }

  app = app_new (visible);
  app_run (app);
  app_free (app);

  return EXIT_SUCCESS;
}
