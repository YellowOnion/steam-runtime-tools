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
  OPTION_VERBOSE,
  OPTION_VERSION,
};

struct option long_options[] =
{
  { "help", no_argument, NULL, OPTION_HELP },
  { "verbose", no_argument, NULL, OPTION_VERBOSE },
  { "version", no_argument, NULL, OPTION_VERSION },
  { NULL, 0, NULL, 0 }
};

/* Pseudo-randomly generated MPEG2 video clip with one I-frame */
static unsigned char clip_mpeg2[] =
{
  0x28, 0x00, 0x2f, 0x64, 0x36, 0x00, 0x2d, 0xd0, 0x40, 0x00, 0x2d, 0x60,
  0x28, 0x00, 0x2f, 0x64, 0x36, 0x00, 0x2d, 0xd0, 0x40, 0x00, 0x2d, 0x60,
  0x28, 0x00, 0x2f, 0x64, 0x36, 0x00, 0x2d, 0xd0, 0x40, 0x00, 0x2d, 0x60,
  0x28, 0x00, 0x2f, 0x64, 0x36, 0x00, 0x2d, 0xd0, 0x40, 0x00, 0x2d, 0x60,
  0x28, 0x00, 0x2f, 0x64, 0x36, 0x00, 0x2d, 0xd0, 0x40, 0x00, 0x2d, 0x60,
  0x28, 0x00, 0x2f, 0x64, 0x36, 0x00, 0x2d, 0xd0, 0x40, 0x00, 0x2d, 0x60,
  0x28, 0x00, 0x2f, 0x64, 0x36, 0x00, 0x2d, 0xd0, 0x40, 0x00, 0x2d, 0x60,
  0x28, 0x00, 0x2f, 0x64, 0x36, 0x00, 0x2d, 0xd0, 0x40, 0x00, 0x2d, 0x60,
  0x28, 0x00, 0x2f, 0x64, 0x36, 0x00, 0x2d, 0xd0, 0x40, 0x00, 0x2d, 0x60,
  0x28, 0x00, 0x2f, 0x64, 0x36, 0x00, 0x2d, 0xd0, 0x40, 0x00, 0x2d, 0x60,
  0x12, 0x00, 0x2a, 0x61, 0x20, 0x00, 0x3e, 0x65,
};

#define CLIP_SIZE_MPEG2 sizeof(clip_mpeg2) / sizeof(clip_mpeg2[0])

static VAPictureParameterBufferMPEG2 pic_param_mpeg2 =
{
  /* Limit the picture buffer to 16x16 */
  .horizontal_size = 16,
  .vertical_size = 16,
  .picture_coding_type = 1, /* I-frame */
};

static VAIQMatrixBufferMPEG2 iq_matrix_mpeg2 =
{
  /* Do not do anything particular here */
  .load_intra_quantiser_matrix = 1,
  .load_non_intra_quantiser_matrix = 1,
  .load_chroma_intra_quantiser_matrix = 0,
  .load_chroma_non_intra_quantiser_matrix = 0,
  .intra_quantiser_matrix = {0},
  .non_intra_quantiser_matrix = {0},
  .chroma_intra_quantiser_matrix = {0},
  .chroma_non_intra_quantiser_matrix = {0},
};

static VASliceParameterBufferMPEG2 slice_param_mpeg2 =
{
  .slice_data_size = CLIP_SIZE_MPEG2,
  .slice_data_offset = 0,
  .slice_data_flag = 0,
  /* Assume a slice with a 64bit header */
  .macroblock_offset = 64,
};

/* Pseudo-randomly generated H264 video clip with one I-frame */
static unsigned int clip_h264[] =
{
  0xca123456, 0xe2255446, 0x9a61c747, 0xe10133c7, 0x71ccf20f, 0xfd2e5af3,
  0xca123456, 0xe2255446, 0x9a61c747, 0xe10133c7, 0x71ccf20f, 0xfd2e5af3,
  0xca123456, 0xe2255446, 0x9a61c747, 0xe10133c7, 0x71ccf20f, 0xfd2e5af3,
  0xca123456, 0xe2255446, 0x9a61c747, 0xe10133c7, 0x71ccf20f, 0xfd2e5af3,
  0xca123456, 0xe2255446, 0x9a61c747, 0xe10133c7, 0x71ccf20f, 0xfd2e5af3,
  0xca123456, 0xe2255446, 0x9a61c747, 0xe10133c7, 0x71ccf20f, 0xfd2e5af3,
  0xca123456, 0xe2255446, 0x9a61c747, 0xe10133c7, 0x71ccf20f, 0xfd2e5af3,
  0xca123456, 0xe2255446, 0x9a61c747, 0xe10133c7, 0x71ccf20f, 0xfd2e5af3,
  0xca123456, 0xe2255446, 0x9a61c747, 0xe10133c7, 0x71ccf20f, 0xfd2e5af3,
  0xca123456, 0xe2255446, 0x9a61c747, 0xe10133c7, 0x71ccf20f, 0xfd2e5af3,
  0xca123456, 0xe2255446, 0x9a61c747, 0xe10133c7, 0x71ccf20f, 0xfd2e5af3,
};

#define CLIP_SIZE_H264 sizeof(clip_h264) / sizeof(clip_h264[0])

static VAPictureParameterBufferH264 pic_param_h264 =
{
  /* The size has been arbitrarily chosen */
  .picture_width_in_mbs_minus1 = 10,
  .picture_height_in_mbs_minus1 = 10,
  .num_ref_frames = 1,
};

static VAIQMatrixBufferH264 iq_matrix_h264 =
{
  /* Do not do anything particular here */
  .ScalingList4x4 = {{0}},
  .ScalingList8x8 = {{0}},
};

static VASliceParameterBufferH264 slice_param_h264 =
{
  .slice_data_size = CLIP_SIZE_H264,
  .slice_data_offset = 0,
  .slice_data_flag = 0,
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

  bool verbose = false;
  bool decode_available = false;
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
  VAConfigID config = VA_INVALID_ID;
  VAContextID context = VA_INVALID_ID;
  VABufferID misc_buf_id = VA_INVALID_ID;
  VABufferID pipeline_param_buf_id = VA_INVALID_ID;
  VABufferID pic_param_buf = VA_INVALID_ID;
  VABufferID iq_matrix_buf = VA_INVALID_ID;
  VABufferID slice_param_buf = VA_INVALID_ID;
  VABufferID slice_data_buf = VA_INVALID_ID;
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

          case OPTION_VERBOSE:
            verbose = true;
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
  /* Arbitrarily use the 8-bit YUV 4:2:0, assuming to be widely supported.
   * In case the current system doesn't support it, vaCreateSurfaces will fail
   * and this test will exit returning 1 */
  attr.value.value.i = VA_FOURCC_I420;
  image_format.fourcc = VA_FOURCC_I420;
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

  if (verbose)
    printf ("%s\n", vaQueryVendorString (va_display));

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
  do_vaapi_or_exit (vaCreateSurfaces (va_display, VA_RT_FORMAT_YUV420, width, height,
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


  /* We assume to have at least one of VAProfileH264Main, VAProfileMPEG2Simple
   * or VAProfileNone */
  if (_do_vaapi ("Testing ability to decode VAProfileH264Main",
                 vaCreateConfig (va_display, VAProfileH264Main,
                                 VAEntrypointVLD, NULL, 0, &config)))
    {
      decode_available = true;

      do_vaapi_or_exit (vaCreateBuffer (va_display, context,
                                        VAPictureParameterBufferType,
                                        sizeof (VAPictureParameterBufferH264),
                                        1, &pic_param_h264,
                                        &pic_param_buf));

      do_vaapi_or_exit (vaCreateBuffer (va_display, context,
                                        VAIQMatrixBufferType,
                                        sizeof (VAIQMatrixBufferH264),
                                        1, &iq_matrix_h264,
                                        &iq_matrix_buf));

      do_vaapi_or_exit (vaCreateBuffer (va_display, context,
                                        VASliceParameterBufferType,
                                        sizeof (VASliceParameterBufferH264),
                                        1, &slice_param_h264,
                                        &slice_param_buf));

      do_vaapi_or_exit (vaCreateBuffer (va_display, context,
                                        VASliceDataBufferType,
                                        CLIP_SIZE_H264, 1,
                                        clip_h264, &slice_data_buf));
    }
  else if (_do_vaapi ("Testing ability to decode VAProfileMPEG2Simple",
                      vaCreateConfig (va_display, VAProfileMPEG2Simple,
                                      VAEntrypointVLD, NULL, 0, &config)))
    {
      decode_available = true;

      do_vaapi_or_exit (vaCreateBuffer (va_display, context,
                                        VAPictureParameterBufferType,
                                        sizeof (VAPictureParameterBufferMPEG2),
                                        1, &pic_param_mpeg2,
                                        &pic_param_buf));

      do_vaapi_or_exit (vaCreateBuffer (va_display, context,
                                        VAIQMatrixBufferType,
                                        sizeof (VAIQMatrixBufferMPEG2),
                                        1, &iq_matrix_mpeg2,
                                        &iq_matrix_buf));

      do_vaapi_or_exit (vaCreateBuffer (va_display, context,
                                        VASliceParameterBufferType,
                                        sizeof (VASliceParameterBufferMPEG2),
                                        1, &slice_param_mpeg2,
                                        &slice_param_buf));

      do_vaapi_or_exit (vaCreateBuffer (va_display, context,
                                        VASliceDataBufferType,
                                        CLIP_SIZE_MPEG2, 1,
                                        clip_mpeg2, &slice_data_buf));
    }
  else if (_do_vaapi ("Testing ability to use VAProfileNone video pre/post processing",
                      vaCreateConfig (va_display, VAProfileNone,
                                      VAEntrypointVideoProc, NULL, 0, &config)))
    {
      do_vaapi_or_exit (vaCreateContext (va_display, config, width, height, 0,
                                         surfaces, surfaces_count, &context));

      /* Try to render a picture, tuning its encode quality */
      do_vaapi_or_exit (vaCreateBuffer (va_display, context,
                                        VAEncMiscParameterBufferType,
                                        sizeof (VAEncMiscParameterBuffer) +
                                          sizeof (VAEncMiscParameterBufferQualityLevel),
                                        1, NULL, &misc_buf_id));
      do_vaapi_or_exit (vaMapBuffer (va_display, misc_buf_id, (void **)&misc_param_buffer));
      misc_param_buffer->type = VAEncMiscParameterTypeQualityLevel;
      buffer_quality_level = (VAEncMiscParameterBufferQualityLevel *)misc_param_buffer->data;
      /* 1 is always the highest possible quality level, we don't need to check
       * VAConfigAttribEncQualityRange */
      buffer_quality_level->quality_level = 1;
      do_vaapi_or_exit (vaUnmapBuffer (va_display, misc_buf_id));
      do_vaapi_or_exit (vaBeginPicture (va_display, context, surfaces[1]));
      do_vaapi_or_exit (vaRenderPicture (va_display, context, &misc_buf_id, 1));
      do_vaapi_or_exit (vaSyncSurface (va_display, surfaces[1]));

      /* Try to render a picture from the first surface to the second,
       * applying a crop to it */
      memset (&pipeline_param_buf, 0, sizeof (pipeline_param_buf));
      pipeline_param_buf.surface = surfaces[0];
      pipeline_param_buf.surface_region = &input_region;
      pipeline_param_buf.output_region = &output_region;
      /* Set a green background */
      pipeline_param_buf.output_background_color = 0xff00ff00;
      pipeline_param_buf.output_color_standard = VAProcColorStandardNone;
      do_vaapi_or_exit (vaCreateBuffer (va_display, context,
                                        VAProcPipelineParameterBufferType,
                                        sizeof (pipeline_param_buf), 1,
                                        &pipeline_param_buf, &pipeline_param_buf_id));
      do_vaapi_or_exit (vaBeginPicture (va_display, context, surfaces[1]));
      do_vaapi_or_exit (vaRenderPicture (va_display, context, &pipeline_param_buf_id, 1));
      do_vaapi_or_exit (vaEndPicture (va_display, context));
      do_vaapi_or_exit (vaSyncSurface (va_display, surfaces[1]));
    }
  else
    {
      goto out;
    }

  if (decode_available)
    {
      do_vaapi_or_exit (vaCreateContext (va_display, config, width, height,
                                         VA_PROGRESSIVE, surfaces,
                                         surfaces_count, &context));

      do_vaapi_or_exit (vaBeginPicture (va_display, context, surfaces[1]));
      /* Send the buffers to the server */
      do_vaapi_or_exit (vaRenderPicture (va_display, context, &pic_param_buf, 1));
      do_vaapi_or_exit (vaRenderPicture (va_display, context, &iq_matrix_buf, 1));
      do_vaapi_or_exit (vaRenderPicture (va_display, context, &slice_param_buf, 1));
      do_vaapi_or_exit (vaRenderPicture (va_display, context, &slice_data_buf, 1));
      /* We are done with the sending, now the server will start to process all
      * pending operations */
      do_vaapi_or_exit (vaEndPicture (va_display, context));
      /* Blocks until all pending operations ends */
      do_vaapi_or_exit (vaSyncSurface (va_display, surfaces[1]));
    }

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
      if (config != VA_INVALID_ID)
        vaDestroyConfig(va_display, config);
      if (surfaces)
        vaDestroySurfaces (va_display, surfaces, surfaces_count);
      if (pic_param_buf != VA_INVALID_ID)
        vaDestroyBuffer (va_display, pic_param_buf);
      if (iq_matrix_buf != VA_INVALID_ID)
        vaDestroyBuffer (va_display, iq_matrix_buf);
      if (slice_param_buf != VA_INVALID_ID)
        vaDestroyBuffer (va_display, slice_param_buf);
      if (slice_data_buf != VA_INVALID_ID)
        vaDestroyBuffer (va_display, slice_data_buf);

      vaTerminate (va_display);
    }
  if (display)
    XCloseDisplay (display);

  free (profiles);
  free (surfaces);

  return ret;
}
