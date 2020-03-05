/*
 * Functional test for VA-API
 *
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
#include <stdbool.h>
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

#include <va/va.h>
#include <va/va_x11.h>

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

static bool
_do_vaapi (const char *description,
           VAStatus va_status)
{
  if (va_status != VA_STATUS_SUCCESS)
    {
      fprintf (stderr, "%s failed: %s (%d)\n", description, vaErrorStr (va_status), va_status);
      return False;
    }
  return True;
}

int
main (int argc,
      char **argv)
{

#define do_vaapi_or_exit(expr) if (! _do_vaapi (#expr, expr)) goto out;

  int opt;
  int surfaces_count = 2;
  int ret = 1;
  int max_profiles;
  int num_profiles;
  int major_version;
  int minor_version;
  unsigned int width = 1280;
  unsigned int height = 720;
  Display *display = NULL;
  VADisplay va_display = NULL;
  VASurfaceAttrib attr;
  VAImage img;
  VASurfaceID *surfaces = NULL;
  VAImageFormat image_format;
  VAProfile *profiles = NULL;
  VAConfigID config;
  VAContextID context = VA_INVALID_ID;
  VABufferID misc_buf_id = VA_INVALID_ID;
  VABufferID pipeline_param_buf_id = VA_INVALID_ID;
  VAEncMiscParameterBuffer *misc_param_buffer;
  VAEncMiscParameterBufferQualityLevel *buffer_quality_level;
  VAProcPipelineParameterBuffer pipeline_param_buf;
  VARectangle input_region;
  VARectangle output_region;

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

  img.image_id = VA_INVALID_ID;

  attr.type = VASurfaceAttribPixelFormat;
  attr.flags = VA_SURFACE_ATTRIB_SETTABLE;
  attr.value.type = VAGenericValueTypeInteger;
  /* Arbitrarily use the 8-bit RGBA, assuming to be widely supported.
   * In case the current system doesn't support it, vaCreateSurfaces will fail
   * and this test will exit returning 1 */
  attr.value.value.i = VA_FOURCC_RGBA;
  image_format.fourcc = VA_FOURCC_RGBA;
  image_format.byte_order = VA_LSB_FIRST;
  image_format.bits_per_pixel = 32;

  /* Use the whole input surface */
  input_region.x = 0;
  input_region.y = 0;
  input_region.width = width;
  input_region.height = height;
  /* Crop the output a few pixels from every corner */
  output_region.x = 10;
  output_region.y = 20;
  output_region.width = width - 30;
  output_region.height = height - 30;

  display = XOpenDisplay (NULL);
  if (!display)
    {
      fprintf (stderr, "An error occurred trying to open a connection to the X server\n");
      return 1;
    }

  va_display = vaGetDisplay (display);
  if (!va_display)
    {
      fprintf (stderr, "An error occurred trying to get a suitable VADisplay for VA-API\n");
      goto out;
    }

  do_vaapi_or_exit (vaInitialize (va_display, &major_version, &minor_version));

  /* Test the ability to get the supported profiles and that they are not more than
   * the maximum number from the implementation */
  max_profiles = vaMaxNumProfiles (va_display);
  if (max_profiles < 1)
    {
      fprintf (stderr,
               "vaMaxNumProfiles failed: unexpected number of maximum profiles (%i)\n", max_profiles);
      goto out;
    }
  profiles = calloc (max_profiles, sizeof (VAProfile));

  if (profiles == NULL)
    {
      fprintf (stderr, "Out of memory\n");
      goto out;
    }

  do_vaapi_or_exit (vaQueryConfigProfiles (va_display, profiles, &num_profiles));
  if (num_profiles > max_profiles)
    {
      fprintf (stderr,
               "vaQueryConfigProfiles failed: the number of profiles (%i) exceed the maximum (%i)\n",
               num_profiles, max_profiles);
      goto out;
    }

  surfaces = calloc (surfaces_count, sizeof (VASurfaceID));

  if (surfaces == NULL)
    {
      fprintf (stderr, "Out of memory\n");
      goto out;
    }

  /* Test the creation of two surfaces and an image */
  do_vaapi_or_exit (vaCreateSurfaces (va_display, VA_RT_FORMAT_RGB32, width, height,
                                      surfaces, surfaces_count, &attr, 1));
  do_vaapi_or_exit (vaCreateImage (va_display, &image_format, width, height, &img));

  /* Get an image from the first surface */
  do_vaapi_or_exit (vaGetImage (va_display, surfaces[0], 0, 0, width, height, img.image_id));

  /* Render the image back to the second surface */
  do_vaapi_or_exit (vaPutImage (va_display, surfaces[1], img.image_id,
                                0, 0, width, height,
                                0, 0, width, height));

  /* Wait for all operations to complete */
  do_vaapi_or_exit (vaSyncSurface (va_display, surfaces[1]));


  /* Assume VAProfileNone to be available. If it isn't vaCreateConfig will not return
   * "VA_STATUS_SUCCESS" and this test will exit returning 1 */
  do_vaapi_or_exit (vaCreateConfig (va_display, VAProfileNone, VAEntrypointVideoProc,
                                    NULL, 0, &config));

  do_vaapi_or_exit (vaCreateContext (va_display, config, width, height, 0, surfaces,
                                     surfaces_count, &context));


  /* Try to render a picture, tuning its encode quality */
  do_vaapi_or_exit (vaCreateBuffer (va_display, context, VAEncMiscParameterBufferType,
                                    sizeof (VAEncMiscParameterBuffer) + sizeof (VAEncMiscParameterBufferQualityLevel),
                                    1, NULL, &misc_buf_id));
  do_vaapi_or_exit (vaMapBuffer (va_display, misc_buf_id, (void **)&misc_param_buffer));
  misc_param_buffer->type = VAEncMiscParameterTypeQualityLevel;
  buffer_quality_level = (VAEncMiscParameterBufferQualityLevel *)misc_param_buffer->data;
  /* 1 is always the highest possible quality level, we don't need to check VAConfigAttribEncQualityRange */
  buffer_quality_level->quality_level = 1;
  do_vaapi_or_exit (vaUnmapBuffer (va_display, misc_buf_id));
  do_vaapi_or_exit (vaBeginPicture (va_display, context, surfaces[1]));
  do_vaapi_or_exit (vaRenderPicture (va_display, context, &misc_buf_id, 1));
  do_vaapi_or_exit (vaSyncSurface (va_display, surfaces[1]));


  /* Try to render a picture from the first surface to the second, applying a crop to it */
  memset (&pipeline_param_buf, 0, sizeof (pipeline_param_buf));
  pipeline_param_buf.surface = surfaces[0];
  pipeline_param_buf.surface_region = &input_region;
  pipeline_param_buf.output_region = &output_region;
  /* Set a green background */
  pipeline_param_buf.output_background_color = 0xff00ff00;
  pipeline_param_buf.output_color_standard = VAProcColorStandardNone;
  do_vaapi_or_exit (vaCreateBuffer (va_display, context, VAProcPipelineParameterBufferType,
                                    sizeof (pipeline_param_buf), 1, &pipeline_param_buf, &pipeline_param_buf_id));
  do_vaapi_or_exit (vaBeginPicture (va_display, context, surfaces[1]));
  do_vaapi_or_exit (vaRenderPicture (va_display, context, &pipeline_param_buf_id, 1));
  do_vaapi_or_exit (vaEndPicture (va_display, context));
  do_vaapi_or_exit (vaSyncSurface (va_display, surfaces[1]));

  ret = 0;

out:
  if (va_display)
    {
      if (pipeline_param_buf_id != VA_INVALID_ID)
        vaDestroyBuffer (va_display, pipeline_param_buf_id);
      if (misc_buf_id != VA_INVALID_ID)
        vaDestroyBuffer (va_display, misc_buf_id);
      if (context != VA_INVALID_ID)
        vaDestroyContext (va_display, context);
      if (img.image_id != VA_INVALID_ID)
        vaDestroyImage (va_display, img.image_id);
      if (surfaces)
        vaDestroySurfaces (va_display, surfaces, surfaces_count);

      vaTerminate (va_display);
    }
  if (display)
    XCloseDisplay (display);

  free (profiles);
  free (surfaces);

  return ret;
}
