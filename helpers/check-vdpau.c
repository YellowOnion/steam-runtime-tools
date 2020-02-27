/*
 * Functional test for VDPAU, loosely based on test code from libvdpau-va-git
 *
 * Copyright © 2013 Rinat Ibragimov
 * Copyright © 2020 Collabora Ltd.
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

#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

#include <vdpau/vdpau.h>
#include <vdpau/vdpau_x11.h>

static VdpGetProcAddress *vdp_get_proc_address;
static VdpDeviceDestroy *vdp_device_destroy;
static VdpGetErrorString *vdp_get_error_string;
static VdpOutputSurfaceCreate *vdp_output_surface_create;
static VdpOutputSurfaceGetBitsNative *vdp_output_surface_get_bits_native;
static VdpOutputSurfacePutBitsNative *vdp_output_surface_put_bits_native;
static VdpOutputSurfaceRenderOutputSurface *vdp_output_surface_render_output_surface;

enum
{
  OPTION_HELP = 1,
  OPTION_VERSION,
};

struct option long_options[] =
{
  { "help", no_argument, NULL, OPTION_HELP },
  { "version", no_argument, NULL, OPTION_VERSION },
  { NULL, 0, NULL, 0 }
};

static void usage (int code) __attribute__((__noreturn__));

/*
 * Print usage information and exit with status @code.
 */
static void
usage (int code)
{
  FILE *fp;

  if (code == 0)
    fp = stdout;
  else
    fp = stderr;

  fprintf (fp, "Usage: %s [OPTIONS]\n",
           program_invocation_short_name);
  exit (code);
}

static void
_do_vdpau_or_exit (const char *description,
                   VdpStatus status)
{
  if (status != VDP_STATUS_OK)
    {
      if (vdp_get_error_string != NULL)
        fprintf (stderr, "%s failed: %s (%d)\n", description, vdp_get_error_string (status), status);
      else
        fprintf (stderr, "%s failed: %d\n", description, status);

      if (getenv ("SRT_CHECK_VDPAU_CRASH") != NULL)
        abort ();
      else
        exit (1);
    }
}

#define do_vdpau_or_exit(expr) _do_vdpau_or_exit (#expr, expr)

static void
load_vdpau (VdpDevice dev)
{
#define GET_POINTER(fid, fn) do_vdpau_or_exit (vdp_get_proc_address (dev, fid, (void**)&fn))

   GET_POINTER(VDP_FUNC_ID_DEVICE_DESTROY,                       vdp_device_destroy);
   GET_POINTER(VDP_FUNC_ID_GET_ERROR_STRING,                     vdp_get_error_string);
   GET_POINTER(VDP_FUNC_ID_OUTPUT_SURFACE_CREATE,                vdp_output_surface_create);
   GET_POINTER(VDP_FUNC_ID_OUTPUT_SURFACE_GET_BITS_NATIVE,       vdp_output_surface_get_bits_native);
   GET_POINTER(VDP_FUNC_ID_OUTPUT_SURFACE_PUT_BITS_NATIVE,       vdp_output_surface_put_bits_native);
   GET_POINTER(VDP_FUNC_ID_OUTPUT_SURFACE_RENDER_OUTPUT_SURFACE, vdp_output_surface_render_output_surface);
}

int
main (int argc,
      char **argv)
{
  Display *display = NULL;
  VdpDevice device;
  VdpOutputSurface out_surface_1;
  VdpOutputSurface out_surface_2;
  int screen;
  int opt;

  /* Arbitrarily declare two 4x4 source images */
  uint32_t black_box[] =
  {
    0xff000000, 0xff000000, 0xff000000, 0xff000000,
    0xff000000, 0xff000000, 0xff000000, 0xff000000,
    0xff000000, 0xff000000, 0xff000000, 0xff000000,
    0xff000000, 0xff000000, 0xff000000, 0xff000000
  };

  uint32_t two_red_dots[] =
  {
    0xff000000, 0xff000000, 0xff000000, 0xff000000,
    0xff000000, 0xffff0000, 0xff000000, 0xff000000,
    0xff000000, 0xff000000, 0xff000000, 0xff000000,
    0xff000000, 0xff000000, 0xff000000, 0xffff0000
  };

  const void * const source_data_1[] = {black_box};
  const void * const source_data_2[] = {two_red_dots};
  uint32_t source_pitches[] = { 4 * 4 };

  while ((opt = getopt_long (argc, argv, "", long_options, NULL)) != -1)
    {
      switch (opt)
        {
          case OPTION_HELP:
            usage (0);
            break;

          case OPTION_VERSION:
            /* Output version number as YAML for machine-readability,
             * inspired by `ostree --version` and `docker version` */
            printf (
                "%s:\n"
                " Package: steam-runtime-tools\n"
                " Version: %s\n",
                argv[0], VERSION);
            return 0;

          case '?':
          default:
            usage (1);
            break;  /* not reached */
        }
    }

  display = XOpenDisplay (NULL);
  if (display == NULL)
    {
      fprintf (stderr, "An error occurred trying to open a connection to the X server\n");
      return 1;
    }
  screen = DefaultScreen (display);

  do_vdpau_or_exit (vdp_device_create_x11 (display, screen, &device, &vdp_get_proc_address));

  load_vdpau (device);

  do_vdpau_or_exit (vdp_output_surface_create (device, VDP_RGBA_FORMAT_B8G8R8A8, 4, 4, &out_surface_1));
  do_vdpau_or_exit (vdp_output_surface_create (device, VDP_RGBA_FORMAT_B8G8R8A8, 4, 4, &out_surface_2));

  /* Upload the first black_box surface and then the two_red_dots one */
  do_vdpau_or_exit (vdp_output_surface_put_bits_native (out_surface_1, source_data_1, source_pitches, NULL));
  do_vdpau_or_exit (vdp_output_surface_put_bits_native (out_surface_2, source_data_2, source_pitches, NULL));

  VdpOutputSurfaceRenderBlendState blend_state = {
    .struct_version =                 VDP_OUTPUT_SURFACE_RENDER_BLEND_STATE_VERSION,
    .blend_factor_source_color =      VDP_OUTPUT_SURFACE_RENDER_BLEND_FACTOR_ONE,
    .blend_factor_source_alpha =      VDP_OUTPUT_SURFACE_RENDER_BLEND_FACTOR_ONE,
    .blend_factor_destination_color = VDP_OUTPUT_SURFACE_RENDER_BLEND_FACTOR_ZERO,
    .blend_factor_destination_alpha = VDP_OUTPUT_SURFACE_RENDER_BLEND_FACTOR_ZERO,
    .blend_equation_color =           VDP_OUTPUT_SURFACE_RENDER_BLEND_EQUATION_ADD,
    .blend_equation_alpha =           VDP_OUTPUT_SURFACE_RENDER_BLEND_EQUATION_ADD,
    .blend_constant =                 {0, 0, 0, 0}
  };

  /* Do the rendering of the two surfaces */
  do_vdpau_or_exit (vdp_output_surface_render_output_surface (out_surface_1, NULL, out_surface_2, NULL,
                                                              NULL, &blend_state,
                                                              VDP_OUTPUT_SURFACE_RENDER_ROTATE_0));

  /* Retrieve the data back from the surface */
  uint32_t receive_buf[16];
  void * const dest_data[] = {receive_buf};
  do_vdpau_or_exit (vdp_output_surface_get_bits_native (out_surface_1, NULL, dest_data, source_pitches));

  /* We expect that the retrieved buffer is equal to two_red_dots because previously
   * we blended the two surfaces together */
  if (memcmp (receive_buf, two_red_dots, 4*4*4) != 0)
    {
      fprintf (stderr, "The rendered surface is not what we expected!\n");
      return 1;
    }

  vdp_device_destroy (device);

  return 0;
}
