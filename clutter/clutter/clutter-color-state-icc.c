/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2024 SUSE Software Solutions Germany GmbH
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Author:
 *   Joan Torres <joan.torres@suse.com>
 */

#include "config.h"

#include <glib/gstdio.h>
#include <lcms2.h>
#include <sys/mman.h>

#include "clutter/clutter-color-state-icc.h"

#include "clutter/clutter-color-state-params.h"
#include "clutter/clutter-color-state-private.h"
#include "clutter/clutter-main.h"

#define UNIFORM_NAME_3D_LUT_VALUES "lut_3D_values"
#define UNIFORM_NAME_3D_LUT_SIZE "lut_3D_size"

typedef enum
{
  TONE_CURVE_SRGB,
  TONE_CURVE_PQ,
  TONE_CURVE_BT709,
} ToneCurve;

typedef struct _Clutter3DLut
{
  uint8_t *data;
  uint32_t size;
  CoglPixelFormat format;
} Clutter3DLut;

typedef struct _ClutterColorStateIcc
{
  ClutterColorState parent;

  int fd;
  uint32_t length;

  cmsHPROFILE *icc_profile;
  cmsHPROFILE *eotf_profile;
  cmsHPROFILE *inv_eotf_profile;

  uint8_t checksum[16];

  gboolean is_linear;
} ClutterColorStateIcc;

G_DEFINE_TYPE (ClutterColorStateIcc,
               clutter_color_state_icc,
               CLUTTER_TYPE_COLOR_STATE)

G_DEFINE_AUTOPTR_CLEANUP_FUNC (cmsHPROFILE, cmsCloseProfile);

int
clutter_color_state_icc_get_fd (ClutterColorStateIcc *color_state_icc)
{
  return color_state_icc->fd;
}

uint32_t
clutter_color_state_icc_get_length (ClutterColorStateIcc *color_state_icc)
{
  return color_state_icc->length;
}

static void
clutter_color_state_icc_finalize (GObject *object)
{
  ClutterColorStateIcc *color_state_icc = CLUTTER_COLOR_STATE_ICC (object);

  g_clear_pointer (&color_state_icc->icc_profile, cmsCloseProfile);
  g_clear_pointer (&color_state_icc->eotf_profile, cmsCloseProfile);
  g_clear_pointer (&color_state_icc->inv_eotf_profile, cmsCloseProfile);
  g_clear_fd (&color_state_icc->fd, NULL);

  G_OBJECT_CLASS (clutter_color_state_icc_parent_class)->finalize (object);
}

void
clutter_color_state_icc_init_color_transform_key (ClutterColorState        *color_state,
                                                  ClutterColorState        *target_color_state,
                                                  ClutterColorTransformKey *key)
{
  key->source_eotf_bits = 0;
  key->target_eotf_bits = 0;
  key->luminance_bit = 0;
  key->color_trans_bit = 0;
  key->icc_bit = 1;
}

/**
 * clutter_color_state_icc_create_transform_snippet:
 * @color_state: a #ClutterColorState
 * @target_color_state: target #ClutterColorState
 *
 * Generates a snippet wich performs a color transformation
 * using a tetrahedral interpolation from a 3D lut.
 *
 * Tetrahedral interpolation implementation based on:
 * https://docs.acescentral.com/specifications/clf#appendix-interpolation
 *
 * Returns: (transfer full): The #CoglSnippet with the interpolation
 */
CoglSnippet *
clutter_color_state_icc_create_transform_snippet (ClutterColorState *color_state,
                                                  ClutterColorState *target_color_state)
{
  CoglSnippet *snippet;
  g_autoptr (GString) snippet_globals = NULL;
  g_autoptr (GString) snippet_source = NULL;

  snippet_globals = g_string_new (NULL);
  snippet_source = g_string_new (NULL);

  g_string_append (
    snippet_globals,
    "uniform sampler2D " UNIFORM_NAME_3D_LUT_VALUES ";\n"
    "uniform float " UNIFORM_NAME_3D_LUT_SIZE ";\n"
    "// sample_3d_lut:\n"
    "// Tetrahedral inerpolation\n"
    "// @color: Normalized ([0,1]) electrical signal value\n"
    "// Returns: tristimulus values ([0,1])\n"
    "vec3 sample_3d_lut (vec3 color)\n"
    "{\n"
    "  vec3 scaled_color = color * (" UNIFORM_NAME_3D_LUT_SIZE " - 1.0);\n"
    "  vec3 index_low = floor (scaled_color);\n"
    "  vec3 index_high = min (index_low + 1.0, " UNIFORM_NAME_3D_LUT_SIZE " - 1.0);\n"
    "  vec3 t = scaled_color - index_low;\n"
    "\n"
    "  // For accessing the y, z coordinates on texture v coord:\n"
    "  // y + (z * size) and normalize it after that\n"
    "  index_low.z *= " UNIFORM_NAME_3D_LUT_SIZE ";\n"
    "  index_high.z *= " UNIFORM_NAME_3D_LUT_SIZE ";\n"
    "  float normalize_v = 1.0 / "
         "((" UNIFORM_NAME_3D_LUT_SIZE " * " UNIFORM_NAME_3D_LUT_SIZE ") - 1.0);\n"
    "  // x can be normalized now\n"
    "  index_low.x /= " UNIFORM_NAME_3D_LUT_SIZE " - 1.0;\n"
    "  index_high.x /= " UNIFORM_NAME_3D_LUT_SIZE " - 1.0;\n"
    "\n"
    "  vec2 coord000 = vec2 (index_low.x, (index_low.y + index_low.z) * normalize_v);\n"
    "  vec2 coord111 = vec2 (index_high.x, (index_high.y + index_high.z) * normalize_v);\n"
    "  vec3 v000 = texture (" UNIFORM_NAME_3D_LUT_VALUES ", coord000).rgb;\n"
    "  vec3 v111 = texture (" UNIFORM_NAME_3D_LUT_VALUES ", coord111).rgb;\n"
    "\n"
    "  if (t.x > t.y)\n"
    "    {\n"
    "      if (t.y > t.z)\n"
    "        {\n"
    "          vec2 coord100 = vec2 (index_high.x, (index_low.y + index_low.z) * normalize_v);\n"
    "          vec2 coord110 = vec2 (index_high.x, (index_high.y + index_low.z) * normalize_v);\n"
    "\n"
    "          vec3 v100 = texture (" UNIFORM_NAME_3D_LUT_VALUES ", coord100).rgb;\n"
    "          vec3 v110 = texture (" UNIFORM_NAME_3D_LUT_VALUES ", coord110).rgb;\n"
    "\n"
    "          return v000 + t.x * (v100 - v000) + t.y * (v110 - v100) + t.z * (v111 - v110);\n"
    "        }\n"
    "      else if (t.x > t.z)\n"
    "        {\n"
    "          vec2 coord100 = vec2 (index_high.x, (index_low.y + index_low.z) * normalize_v);\n"
    "          vec2 coord101 = vec2 (index_high.x, (index_low.y + index_high.z) * normalize_v);\n"
    "\n"
    "          vec3 v100 = texture (" UNIFORM_NAME_3D_LUT_VALUES ", coord100).rgb;\n"
    "          vec3 v101 = texture (" UNIFORM_NAME_3D_LUT_VALUES ", coord101).rgb;\n"
    "\n"
    "          return v000 + t.x * (v100 - v000) + t.y * (v111 - v101) + t.z * (v101 - v100);\n"
    "        }\n"
    "      else\n"
    "        {\n"
    "          vec2 coord001 = vec2 (index_low.x, (index_low.y + index_high.z) * normalize_v);\n"
    "          vec2 coord101 = vec2 (index_high.x, (index_low.y + index_high.z) * normalize_v);\n"
    "\n"
    "          vec3 v001 = texture (" UNIFORM_NAME_3D_LUT_VALUES ", coord001).rgb;\n"
    "          vec3 v101 = texture (" UNIFORM_NAME_3D_LUT_VALUES ", coord101).rgb;\n"
    "\n"
    "          return v000 + t.x * (v101 - v001) + t.y * (v111 - v101) + t.z * (v001 - v000);\n"
    "        }\n"
    "    }\n"
    "  else\n"
    "    {\n"
    "      if (t.z > t.y)\n"
    "        {\n"
    "          vec2 coord001 = vec2 (index_low.x, (index_low.y + index_high.z) * normalize_v);\n"
    "          vec2 coord011 = vec2 (index_low.x, (index_high.y + index_high.z) * normalize_v);\n"
    "\n"
    "          vec3 v001 = texture (" UNIFORM_NAME_3D_LUT_VALUES ", coord001).rgb;\n"
    "          vec3 v011 = texture (" UNIFORM_NAME_3D_LUT_VALUES ", coord011).rgb;\n"
    "\n"
    "          return v000 + t.x * (v111 - v011) + t.y * (v011 - v001) + t.z * (v001 - v000);\n"
    "        }\n"
    "      else if (t.z > t.x)\n"
    "        {\n"
    "          vec2 coord010 = vec2 (index_low.x, (index_high.y + index_low.z) * normalize_v);\n"
    "          vec2 coord011 = vec2 (index_low.x, (index_high.y + index_high.z) * normalize_v);\n"
    "\n"
    "          vec3 v010 = texture (" UNIFORM_NAME_3D_LUT_VALUES ", coord010).rgb;\n"
    "          vec3 v011 = texture (" UNIFORM_NAME_3D_LUT_VALUES ", coord011).rgb;\n"
    "\n"
    "          return v000 + t.x * (v111 - v011) + t.y * (v010 - v000) + t.z * (v011 - v010);\n"
    "        }\n"
    "      else\n"
    "        {\n"
    "          vec2 coord010 = vec2 (index_low.x, (index_high.y + index_low.z) * normalize_v);\n"
    "          vec2 coord110 = vec2 (index_high.x, (index_high.y + index_low.z) * normalize_v);\n"
    "\n"
    "          vec3 v010 = texture (" UNIFORM_NAME_3D_LUT_VALUES ", coord010).rgb;\n"
    "          vec3 v110 = texture (" UNIFORM_NAME_3D_LUT_VALUES ", coord110).rgb;\n"
    "\n"
    "          return v000 + t.x * (v110 - v010) + t.y * (v010 - v000) + t.z * (v111 - v110);\n"
    "        }\n"
    "    }\n"
    "}\n"
    "\n"
    "vec4 sample_3d_lut (vec4 color)\n"
    "{\n"
    "  return vec4 (sample_3d_lut (color.rgb), color.a);\n"
    "}\n"
    "\n");

  g_string_append (snippet_source,
                   "  vec3 color_state_color = cogl_color_out.rgb;\n"
                   "  color_state_color = sample_3d_lut (color_state_color);\n"
                   "  cogl_color_out = vec4 (color_state_color, cogl_color_out.a);\n");

  snippet = cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT,
                              snippet_globals->str,
                              snippet_source->str);
  cogl_snippet_set_capability (snippet,
                               CLUTTER_PIPELINE_CAPABILITY,
                               CLUTTER_PIPELINE_CAPABILITY_COLOR_STATE);
  return snippet;
}

static void
clutter_3d_lut_free (Clutter3DLut *lut_3d)
{
  g_clear_pointer (&lut_3d->data, g_free);
  g_free (lut_3d);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (Clutter3DLut, clutter_3d_lut_free);

static void
sample_3d_lut_input (float **in,
                     int     lut_size)
{
  int index;
  int i, j, k;
  float x, y, z;
  float step;
  float *sample;

  sample = *in;
  step = 1.0f / (lut_size - 1);

  /* Store the 3D LUT as a 2D texture (lut_size x (lut_size x lut_size))
   * Access the data as tex(x, y + z * lut_size) */
  index = 0;
  for (k = 0, z = 0.0f; k < lut_size; k++, z += step)
    for (j = 0, y = 0.0f; j < lut_size; j++, y += step)
      for (i = 0, x = 0.0f; i < lut_size; i++, x += step)
        {
          sample[index++] = x;
          sample[index++] = y;
          sample[index++] = z;
        }
}

static gboolean
get_3d_lut (ClutterColorStateIcc  *color_state_icc,
            ClutterColorStateIcc  *target_color_state_icc,
            Clutter3DLut         **out_lut_3d)
{
  cmsHTRANSFORM transform;
  cmsHPROFILE profiles[4];
  CoglPixelFormat lut_format;
  int n_profiles;
  int output_format;
  int lut_size;
  int n_samples;
  size_t bpp;
  g_autofree float *lut_input = NULL;
  g_autofree uint8_t *lut_output = NULL;
  Clutter3DLut *lut_3d;
  CoglContext *context =
    clutter_backend_get_cogl_context (clutter_get_default_backend ());

  n_profiles = 0;
  if (color_state_icc->is_linear)
    profiles[n_profiles++] = color_state_icc->inv_eotf_profile;

  profiles[n_profiles++] = color_state_icc->icc_profile;
  profiles[n_profiles++] = target_color_state_icc->icc_profile;

  if (target_color_state_icc->is_linear)
    profiles[n_profiles++] = target_color_state_icc->eotf_profile;

  if (cogl_context_has_feature (context, COGL_FEATURE_ID_TEXTURE_HALF_FLOAT))
    {
      output_format = TYPE_RGBA_HALF_FLT;
      lut_format = COGL_PIXEL_FORMAT_RGBX_FP_16161616;
      bpp = 4 * sizeof (uint16_t);
    }
  else
    {
      output_format = TYPE_RGBA_8;
      lut_format = COGL_PIXEL_FORMAT_RGBX_8888;
      bpp = 4 * sizeof (uint8_t);
    }

  transform = cmsCreateMultiprofileTransform (profiles,
                                              n_profiles,
                                              TYPE_RGB_FLT,
                                              output_format,
                                              INTENT_PERCEPTUAL,
                                              0);
  if (!transform)
    {
      g_warning ("Failed generating ICC transform");
      return FALSE;
    }

  lut_size = 33;
  n_samples = lut_size * lut_size * lut_size;

  lut_input = g_malloc (n_samples * 3 * sizeof (float));
  lut_output = g_malloc (n_samples * bpp);

  sample_3d_lut_input (&lut_input, lut_size);

  cmsDoTransform (transform, lut_input, lut_output, n_samples);
  cmsDeleteTransform (transform);

  lut_3d = g_new (Clutter3DLut, 1);
  lut_3d->data = g_steal_pointer (&lut_output);
  lut_3d->size = lut_size;
  lut_3d->format = lut_format;

  *out_lut_3d = lut_3d;

  return TRUE;
}

static gboolean
upload_3d_lut_as_2d_texture (CoglPipeline *pipeline,
                             Clutter3DLut *lut_3d,
                             int           texture_unit)
{
  int rowstride;
  g_autoptr (CoglTexture) lut_texture = NULL;
  g_autoptr (GError) error = NULL;
  CoglContext *context =
    clutter_backend_get_cogl_context (clutter_get_default_backend ());

  if (lut_3d->format == COGL_PIXEL_FORMAT_RGBX_FP_16161616)
    {
      rowstride = lut_3d->size * 4 * sizeof (uint16_t);
    }
  else if (lut_3d->format == COGL_PIXEL_FORMAT_RGBX_8888)
    {
      rowstride = lut_3d->size * 4 * sizeof (uint8_t);
    }
  else
    {
      g_warning ("Unhandled pixel format uploading 1D lut");
      rowstride = lut_3d->size * 4 * sizeof (uint8_t);
    }

  lut_texture = cogl_texture_2d_new_from_data (context,
                                               lut_3d->size,
                                               lut_3d->size * lut_3d->size,
                                               lut_3d->format,
                                               rowstride,
                                               lut_3d->data,
                                               &error);
  if (!lut_texture)
    {
      g_warning ("Failed creating 1D lut as a texture: %s", error->message);
      return FALSE;
    }

  cogl_pipeline_set_layer_texture (pipeline, texture_unit, lut_texture);

  /* Textures are only added as layers, use this combine mode to avoid
   * this layer to modify the result, and use it as a standard texture */
  cogl_pipeline_set_layer_combine (pipeline, texture_unit,
                                   "RGBA = REPLACE(PREVIOUS)", NULL);

  cogl_pipeline_set_layer_wrap_mode_s (pipeline, texture_unit,
                                       COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE);
  cogl_pipeline_set_layer_wrap_mode_t (pipeline, texture_unit,
                                       COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE);
  /* Interpolation explicitly done at shader so use nearest filter */
  cogl_pipeline_set_layer_filters (pipeline, texture_unit,
                                   COGL_PIPELINE_FILTER_NEAREST,
                                   COGL_PIPELINE_FILTER_NEAREST);

  return TRUE;
}

void
clutter_color_state_icc_update_uniforms (ClutterColorState *color_state,
                                         ClutterColorState *target_color_state,
                                         CoglPipeline      *pipeline)
{
  ClutterColorStateIcc *color_state_icc =
    CLUTTER_COLOR_STATE_ICC (color_state);
  ClutterColorStateIcc *target_color_state_icc =
    CLUTTER_COLOR_STATE_ICC (target_color_state);
  g_autoptr (Clutter3DLut) lut_3d = NULL;
  int uniform_location_values;
  int uniform_location_size;

  if (!get_3d_lut (color_state_icc, target_color_state_icc, &lut_3d))
    return;

  /* FIXME: Probably something is missing and texture index shouldn't be 0 */
  if (!upload_3d_lut_as_2d_texture (pipeline, lut_3d, 0))
    return;

  uniform_location_values =
    cogl_pipeline_get_uniform_location (pipeline,
                                        UNIFORM_NAME_3D_LUT_VALUES);
  cogl_pipeline_set_uniform_1i (pipeline,
                                uniform_location_values,
                                0);

  uniform_location_size =
    cogl_pipeline_get_uniform_location (pipeline,
                                        UNIFORM_NAME_3D_LUT_SIZE);
  cogl_pipeline_set_uniform_1f (pipeline,
                                uniform_location_size,
                                lut_3d->size);
}

static gboolean
clutter_color_state_icc_equals (ClutterColorState *color_state,
                                ClutterColorState *other_color_state)
{
  ClutterColorStateIcc *color_state_icc =
    CLUTTER_COLOR_STATE_ICC (color_state);
  ClutterColorStateIcc *other_color_state_icc =
    CLUTTER_COLOR_STATE_ICC (other_color_state);

  return memcmp (color_state_icc->checksum,
                 other_color_state_icc->checksum,
                 16) == 0;
}

static char *
clutter_color_state_icc_to_string (ClutterColorState *color_state)
{
  ClutterColorStateIcc *color_state_icc =
    CLUTTER_COLOR_STATE_ICC (color_state);
  uint8_t *checksum = color_state_icc->checksum;
  g_autoptr (GString) hex_checksum = g_string_new (NULL);
  int i;

  for (i = 0; i < 16; i++)
    g_string_append_printf (hex_checksum, "%02x", checksum[i]);

  return g_strdup_printf ("ClutterColorState ICC (%s)", hex_checksum->str);
}

static ClutterEncodingRequiredFormat
clutter_color_state_icc_required_format (ClutterColorState *color_state)
{
  ClutterColorStateIcc *color_state_icc = CLUTTER_COLOR_STATE_ICC (color_state);

  return color_state_icc->is_linear ? CLUTTER_ENCODING_REQUIRED_FORMAT_FP16 :
                                      CLUTTER_ENCODING_REQUIRED_FORMAT_UINT8;
}

/*
 * On ICC color_states the blending is done in linear.
 */
static ClutterColorState *
clutter_color_state_icc_get_blending (ClutterColorState *color_state,
                                      gboolean           force)
{
  ClutterColorStateIcc *color_state_icc = CLUTTER_COLOR_STATE_ICC (color_state);
  ClutterColorState *blending_color_state;
  ClutterColorStateIcc *blending_color_state_icc;
  ClutterContext *context;
  g_autofd int icc_fd = -1;

  if (color_state_icc->is_linear)
    return g_object_ref (color_state);

  g_object_get (G_OBJECT (color_state), "context", &context, NULL);

  blending_color_state = clutter_color_state_icc_new (context,
                                                      color_state_icc->fd,
                                                      color_state_icc->length);
  blending_color_state_icc = CLUTTER_COLOR_STATE_ICC (blending_color_state);
  blending_color_state_icc->is_linear = TRUE;

  return blending_color_state;
}

static void
clutter_color_state_icc_class_init (ClutterColorStateIccClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterColorStateClass *color_state_class = CLUTTER_COLOR_STATE_CLASS (klass);

  object_class->finalize = clutter_color_state_icc_finalize;

  color_state_class->init_color_transform_key = clutter_color_state_icc_init_color_transform_key;
  color_state_class->create_transform_snippet = clutter_color_state_icc_create_transform_snippet;
  color_state_class->update_uniforms = clutter_color_state_icc_update_uniforms;
  color_state_class->equals = clutter_color_state_icc_equals;
  color_state_class->to_string = clutter_color_state_icc_to_string;
  color_state_class->required_format = clutter_color_state_icc_required_format;
  color_state_class->get_blending = clutter_color_state_icc_get_blending;
}

static void
clutter_color_state_icc_init (ClutterColorStateIcc *color_state_icc)
{
}

static gboolean
get_icc_profile (int           icc_fd,
                 uint32_t      icc_length,
                 cmsHPROFILE **icc_profile,
                 GError      **error)
{
  void *icc_mem;
  cmsColorSpaceSignature pcs;
  cmsColorSpaceSignature color_space;
  g_autoptr (cmsHPROFILE) profile = NULL;

  icc_mem = mmap (NULL, icc_length, PROT_READ, MAP_PRIVATE, icc_fd, 0);
  if (icc_mem == MAP_FAILED)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Couldn't mmap ICC fd (%s)", g_strerror (errno));
      return FALSE;
    }

  profile = cmsOpenProfileFromMem (icc_mem, icc_length);
  munmap (icc_mem, icc_length);

  if (!profile)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Couldn't parse ICC profile");
      return FALSE;
    }

  color_space = cmsGetColorSpace (profile);
  pcs = cmsGetPCS (profile);
  if (color_space != cmsSigRgbData || pcs != cmsSigXYZData)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "ICC profile unsupported");
      return FALSE;
    }

  *icc_profile = g_steal_pointer (&profile);

  return TRUE;
}

static float
dot_product (float a[3],
             float b[3])
{
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/*
 * Estimation of eotf based on sketch:
 * https://lists.freedesktop.org/archives/wayland-devel/2019-March/040171.html
 */
static void
estimate_eotf_curves (cmsHPROFILE  *icc_profile,
                      cmsToneCurve *curves[3])
{
  cmsHTRANSFORM transform;
  int ch, i;
  int n_points;
  float t, step;
  float squared_max_xyz_norm;
  float xyz[3], max_xyz[3];
  float rgb[3] = { 0.0f, 0.0f, 0.0f };
  g_autofree float *values = NULL;
  g_autoptr (cmsHPROFILE) xyz_profile = NULL;

  xyz_profile = cmsCreateXYZProfile ();
  transform = cmsCreateTransform (icc_profile,
                                  TYPE_RGB_FLT,
                                  xyz_profile,
                                  TYPE_XYZ_FLT,
                                  INTENT_PERCEPTUAL,
                                  0);
  if (!transform)
    return;

  n_points = 1024;
  step = 1.0f / (n_points - 1);
  values = g_malloc (n_points * sizeof (float));

  for (ch = 0; ch < 3; ch++)
    {
      rgb[ch] = 1.0f;
      cmsDoTransform (transform, rgb, max_xyz, 1);
      squared_max_xyz_norm = dot_product (max_xyz, max_xyz);

      for (i = 0, t = 0.0f; i < n_points; i++, t += step)
        {
          rgb[ch] = t;
          cmsDoTransform (transform, rgb, xyz, 1);
          values[i] = dot_product (xyz, max_xyz) / squared_max_xyz_norm;
        }

      rgb[ch] = 0.0f;

      curves[ch] = cmsBuildTabulatedToneCurveFloat (NULL, n_points, values);

      if (!cmsIsToneCurveMonotonic (curves[ch]))
        g_warning ("Estimated curve is not monotonic, something is "
                   "probably wrong");
    }

  cmsDeleteTransform (transform);
}

static gboolean
get_eotf_profiles (cmsHPROFILE  *icc_profile,
                   cmsHPROFILE **eotf_profile,
                   cmsHPROFILE **inv_eotf_profile,
                   GError      **error)
{
  g_autoptr (cmsHPROFILE) eotf_prof = NULL;
  g_autoptr (cmsHPROFILE) inv_eotf_prof = NULL;
  cmsToneCurve *eotfs[3] = { 0 };
  cmsToneCurve *inv_eotfs[3] = { 0 };

  if (cmsIsMatrixShaper (icc_profile))
    {
      eotfs[0] = cmsDupToneCurve (cmsReadTag (icc_profile, cmsSigRedTRCTag));
      eotfs[1] = cmsDupToneCurve (cmsReadTag (icc_profile, cmsSigGreenTRCTag));
      eotfs[2] = cmsDupToneCurve (cmsReadTag (icc_profile, cmsSigBlueTRCTag));
    }
  else
    {
      estimate_eotf_curves (icc_profile, eotfs);
    }

  if (!eotfs[0] || !eotfs[1] || !eotfs[2])
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Couldn't find required tags or estimate "
                   "to get EOTF of ICC profile");
      cmsFreeToneCurveTriple (eotfs);
      return FALSE;
    }

  inv_eotfs[0] = cmsReverseToneCurve (eotfs[0]);
  inv_eotfs[1] = cmsReverseToneCurve (eotfs[1]);
  inv_eotfs[2] = cmsReverseToneCurve (eotfs[2]);
  if (!inv_eotfs[0] || !inv_eotfs[1] || !inv_eotfs[2])
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Couldn't inverse EOTFs of ICC profile");
      cmsFreeToneCurveTriple (eotfs);
      cmsFreeToneCurveTriple (inv_eotfs);
      return FALSE;
    }

  eotf_prof = cmsCreateLinearizationDeviceLink (cmsSigRgbData, eotfs);
  inv_eotf_prof = cmsCreateLinearizationDeviceLink (cmsSigRgbData, inv_eotfs);
  if (!eotf_prof || !inv_eotf_prof)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Couldn't create EOTFs profiles from ICC profile");
      cmsFreeToneCurveTriple (eotfs);
      cmsFreeToneCurveTriple (inv_eotfs);
      return FALSE;
    }

  cmsFreeToneCurveTriple (eotfs);
  cmsFreeToneCurveTriple (inv_eotfs);

  *eotf_profile = g_steal_pointer (&eotf_prof);
  *inv_eotf_profile = g_steal_pointer (&inv_eotf_prof);

  return TRUE;
}

static gboolean
get_checksum (cmsHPROFILE *icc_profile,
              uint8_t      checksum[16])
{
  uint8_t checksum_zeros[16] = { 0 };

  cmsGetHeaderProfileID (icc_profile, checksum);
  if (memcmp (checksum, checksum_zeros, 16) == 0)
    {
      cmsMD5computeID (icc_profile);
      cmsGetHeaderProfileID (icc_profile, checksum);
    }

  return TRUE;
}

static void
get_white_point (ClutterColorStateParams *color_state_params,
                 cmsCIEXYZ               *white_point_XYZ)
{
  const ClutterColorimetry *colorimetry;
  const ClutterPrimaries *primaries;
  cmsCIExyY white_point_xyY;

  colorimetry = clutter_color_state_params_get_colorimetry (color_state_params);
  switch (colorimetry->type)
    {
    case CLUTTER_COLORIMETRY_TYPE_COLORSPACE:
      primaries = clutter_colorspace_to_primaries (colorimetry->colorspace);
      break;
    case CLUTTER_COLORIMETRY_TYPE_PRIMARIES:
      primaries = colorimetry->primaries;
      break;
    }

  white_point_xyY = (cmsCIExyY) { primaries->w_x, primaries->w_y, 1.0f };
  cmsxyY2XYZ (white_point_XYZ, &white_point_xyY);
}

static void
get_luminance_mapping_matrices (ClutterColorStateParams *color_state_params,
                                graphene_matrix_t       *to_pcs,
                                graphene_matrix_t       *to_rgb)
{
  const ClutterLuminance *lum;
  float inv_scale, scale;

  lum = clutter_color_state_params_get_luminance (color_state_params);

  inv_scale = lum->max / lum->ref;
  graphene_matrix_init_from_float (
    to_pcs,
    (float [16]) {
    inv_scale, 0, 0, 0,
    0, inv_scale, 0, 0,
    0, 0, inv_scale, 0,
    0, 0, 0, 1,
  });

  scale = lum->ref / lum->max;
  graphene_matrix_init_from_float (
    to_rgb,
    (float [16]) {
    scale, 0, 0, 0,
    0, scale, 0, 0,
    0, 0, scale, 0,
    0, 0, 0, 1,
  });
}

static void
get_transform_matrices (ClutterColorStateParams *color_state_params,
                        cmsFloat64Number         to_pcs_perceptual[9],
                        cmsFloat64Number         to_rgb_perceptual[9])
{
  graphene_matrix_t rgb_to_xyz, xyz_to_rgb;
  graphene_matrix_t to_d50, from_d50;
  graphene_matrix_t lum_to_pcs, lum_to_rgb;
  graphene_matrix_t to_pcs_merged, to_rgb_merged;

  if (!clutter_color_state_params_get_color_space_trans_matrices (
        color_state_params,
        &rgb_to_xyz,
        &xyz_to_rgb))
    {
      g_warning ("Failed getting color transformation matrices");
      graphene_matrix_init_identity (&rgb_to_xyz);
      graphene_matrix_init_identity (&xyz_to_rgb);
    }

  if (!clutter_color_state_params_get_d50_chromatic_adaptation (
        color_state_params,
        &to_d50,
        &from_d50))
    {
      g_warning ("Failed getting chromatic adaptation matrices");
      graphene_matrix_init_identity (&to_d50);
      graphene_matrix_init_identity (&from_d50);
    }

  get_luminance_mapping_matrices (color_state_params,
                                  &lum_to_pcs,
                                  &lum_to_rgb);

  /* Res = lum * to_d50 * rgb_to_xyz */
  graphene_matrix_multiply (&rgb_to_xyz, &to_d50, &to_pcs_merged);
  graphene_matrix_multiply (&to_pcs_merged, &lum_to_pcs, &to_pcs_merged);
  to_pcs_perceptual[0] = graphene_matrix_get_value (&to_pcs_merged, 0, 0);
  to_pcs_perceptual[1] = graphene_matrix_get_value (&to_pcs_merged, 0, 1);
  to_pcs_perceptual[2] = graphene_matrix_get_value (&to_pcs_merged, 0, 2);
  to_pcs_perceptual[3] = graphene_matrix_get_value (&to_pcs_merged, 1, 0);
  to_pcs_perceptual[4] = graphene_matrix_get_value (&to_pcs_merged, 1, 1);
  to_pcs_perceptual[5] = graphene_matrix_get_value (&to_pcs_merged, 1, 2);
  to_pcs_perceptual[6] = graphene_matrix_get_value (&to_pcs_merged, 2, 0);
  to_pcs_perceptual[7] = graphene_matrix_get_value (&to_pcs_merged, 2, 1);
  to_pcs_perceptual[8] = graphene_matrix_get_value (&to_pcs_merged, 2, 2);

  /* Res = xyz_to_rgb * from_d50 * lum */
  graphene_matrix_multiply (&lum_to_rgb, &from_d50, &to_rgb_merged);
  graphene_matrix_multiply (&to_rgb_merged, &xyz_to_rgb, &to_rgb_merged);
  to_rgb_perceptual[0] = graphene_matrix_get_value (&to_rgb_merged, 0, 0);
  to_rgb_perceptual[1] = graphene_matrix_get_value (&to_rgb_merged, 0, 1);
  to_rgb_perceptual[2] = graphene_matrix_get_value (&to_rgb_merged, 0, 2);
  to_rgb_perceptual[3] = graphene_matrix_get_value (&to_rgb_merged, 1, 0);
  to_rgb_perceptual[4] = graphene_matrix_get_value (&to_rgb_merged, 1, 1);
  to_rgb_perceptual[5] = graphene_matrix_get_value (&to_rgb_merged, 1, 2);
  to_rgb_perceptual[6] = graphene_matrix_get_value (&to_rgb_merged, 2, 0);
  to_rgb_perceptual[7] = graphene_matrix_get_value (&to_rgb_merged, 2, 1);
  to_rgb_perceptual[8] = graphene_matrix_get_value (&to_rgb_merged, 2, 2);
}

static void
build_tone_curves (ToneCurve      tone_curve,
                   cmsToneCurve **eotf_curve,
                   cmsToneCurve **inv_eotf_curve)
{
  int i;
  float t, step;
  float c1, c2, c3, m1, m2, oo_m1, oo_m2, t_pow_m1, num, den;
  int n_points = 1024;
  float values[n_points];
  float inv_values[n_points];

  step = 1.0f / (n_points - 1);

  switch (tone_curve)
    {
    case TONE_CURVE_SRGB:
      for (i = 0, t = 0.0f; i < n_points; i++, t += step)
        {
          if (t <= 0.04045f)
            values[i] = t / 12.92f;
          else
            values[i] = powf ((t + 0.055f) / 1.055f, 12.0f / 5.0f);

          if (t <= 0.0031308f)
            inv_values[i] = t * 12.92f;
          else
            inv_values[i] = powf (t, (5.0f / 12.0f)) * 1.055f - 0.055f;
        }
      break;
    case TONE_CURVE_PQ:
      c1 = 0.8359375f;
      c2 = 18.8515625f;
      c3 = 18.6875f;
      m1 = 0.1593017f;
      m2 = 78.84375f;
      oo_m1 = 1.0f / m1;
      oo_m2 = 1.0f / m2;
      for (i = 0, t = 0.0f; i < n_points; i++, t += step)
        {
          num = MAX (powf (t, oo_m2) - c1, 0.0f);
          den = c2 - c3 * powf (t, oo_m2);
          values[i] = powf (num / den, oo_m1);

          t_pow_m1 = powf (t, m1);
          num = c1 + c2 * t_pow_m1;
          den = 1.0f + c3 * t_pow_m1;
          inv_values[i] = powf (num / den, m2);
        }
      break;
    case TONE_CURVE_BT709:
      for (i = 0, t = 0.0f; i < n_points; i++, t += step)
        {
          if (t < 0.08124f)
            values[i] = t / 4.5f;
          else
            values[i] = powf ((t + 0.099f) / 1.099f, 1.0f / 0.45f);

          if (t < 0.018f)
            inv_values[i] = t * 4.5f;
          else
            inv_values[i] = 1.099f * powf (t, 0.45f) - 0.099f;
        }
      break;
    }

  *eotf_curve = cmsBuildTabulatedToneCurveFloat (NULL, n_points, values);
  *inv_eotf_curve = cmsBuildTabulatedToneCurveFloat (NULL, n_points, inv_values);
}

static void
get_eotf_curves (ClutterColorStateParams  *color_state_params,
                 cmsToneCurve             *eotf_curves[3],
                 cmsToneCurve             *inv_eotf_curves[3])
{
  const ClutterEOTF *eotf;
  cmsToneCurve *eotf_curve = NULL;
  cmsToneCurve *inv_eotf_curve = NULL;

  eotf = clutter_color_state_params_get_eotf (color_state_params);
  switch (eotf->type)
    {
    case CLUTTER_EOTF_TYPE_NAMED:
      switch (eotf->tf_name)
        {
        case CLUTTER_TRANSFER_FUNCTION_SRGB:
          build_tone_curves (TONE_CURVE_SRGB,
                             &eotf_curve,
                             &inv_eotf_curve);
          break;
        case CLUTTER_TRANSFER_FUNCTION_PQ:
          build_tone_curves (TONE_CURVE_PQ,
                             &eotf_curve,
                             &inv_eotf_curve);
          break;
        case CLUTTER_TRANSFER_FUNCTION_BT709:
          build_tone_curves (TONE_CURVE_BT709,
                             &eotf_curve,
                             &inv_eotf_curve);
          break;
        case CLUTTER_TRANSFER_FUNCTION_LINEAR:
          eotf_curve = cmsBuildGamma (NULL, 1.0);
          inv_eotf_curve = cmsBuildGamma (NULL, 1.0);
          break;
        }
      break;
    case CLUTTER_EOTF_TYPE_GAMMA:
      eotf_curve = cmsBuildGamma (NULL, eotf->gamma_exp);
      inv_eotf_curve = cmsBuildGamma (NULL, 1.0f / eotf->gamma_exp);
      break;
    }

  if (!eotf_curve || !inv_eotf_curve)
    {
      g_warning ("Failed generating eotf curves");
      eotf_curve = cmsBuildGamma (NULL, 1.0);
      inv_eotf_curve = cmsBuildGamma (NULL, 1.0);
    }

  eotf_curves[0] = eotf_curve;
  eotf_curves[1] = eotf_curve;
  eotf_curves[2] = eotf_curve;

  inv_eotf_curves[0] = inv_eotf_curve;
  inv_eotf_curves[1] = inv_eotf_curve;
  inv_eotf_curves[2] = inv_eotf_curve;
}

/**
 * clutter_color_state_icc_new_from_params:
 *
 * Create a new ClutterColorStateIcc object generating an ICC profile from
 * a color state params.
 *
 * Return value: A new ClutterColorState object.
 **/
ClutterColorState *
clutter_color_state_icc_new_from_params (ClutterColorState *color_state)
{
  ClutterColorStateParams *color_state_params;
  ClutterColorStateIcc *color_state_icc;
  ClutterContext *context;
  g_autoptr (GError) error = NULL;
  g_autoptr (cmsHPROFILE) icc_profile = NULL;
  g_autoptr (cmsHPROFILE) eotf_profile = NULL;
  g_autoptr (cmsHPROFILE) inv_eotf_profile = NULL;
  cmsCIEXYZ white_point;
  cmsStage *stage;
  cmsPipeline *D_to_B_0, *B_to_D_0;
  cmsFloat64Number to_pcs_perc[9], to_rgb_perc[9];
  cmsToneCurve *eotf_curves[3], *inv_eotf_curves[3];

  if (CLUTTER_IS_COLOR_STATE_ICC (color_state))
    return g_object_ref (color_state);

  color_state_params = CLUTTER_COLOR_STATE_PARAMS (color_state);
  get_white_point (color_state_params, &white_point);
  get_transform_matrices (color_state_params,
                          to_pcs_perc,
                          to_rgb_perc);
  get_eotf_curves (color_state_params,
                   eotf_curves,
                   inv_eotf_curves);

  icc_profile = cmsCreateProfilePlaceholder (NULL);
  cmsSetProfileVersion (icc_profile, 4.3);
  cmsSetDeviceClass (icc_profile, cmsSigDisplayClass);
  cmsSetColorSpace (icc_profile, cmsSigRgbData);
  cmsSetPCS (icc_profile, cmsSigXYZData);
  cmsWriteTag (icc_profile, cmsSigMediaWhitePointTag, &white_point);

  /* Perceptual rendering intent (DtoB0/BtoD0) */
  D_to_B_0 = cmsPipelineAlloc (NULL, 3, 3);
  stage = cmsStageAllocToneCurves (NULL, 3, eotf_curves);
  cmsPipelineInsertStage (D_to_B_0, cmsAT_END, stage);
  stage = cmsStageAllocMatrix (NULL, 3, 3, to_pcs_perc, NULL);
  cmsPipelineInsertStage (D_to_B_0, cmsAT_END, stage);
  cmsWriteTag (icc_profile, cmsSigDToB0Tag, D_to_B_0);

  B_to_D_0 = cmsPipelineAlloc (NULL, 3, 3);
  stage = cmsStageAllocMatrix (NULL, 3, 3, to_rgb_perc, NULL);
  cmsPipelineInsertStage (B_to_D_0, cmsAT_END, stage);
  stage = cmsStageAllocToneCurves (NULL, 3, inv_eotf_curves);
  cmsPipelineInsertStage (B_to_D_0, cmsAT_END, stage);
  cmsWriteTag (icc_profile, cmsSigBToD0Tag, B_to_D_0);

  cmsPipelineFree (D_to_B_0);
  cmsPipelineFree (B_to_D_0);
  cmsFreeToneCurve (eotf_curves[0]);
  cmsFreeToneCurve (inv_eotf_curves[0]);

  if (!get_eotf_profiles (icc_profile, &eotf_profile, &inv_eotf_profile, &error))
    {
      g_warning ("Failed getting EOTF profiles from params: %s",
                 error->message);
      return NULL;
    }

  g_object_get (G_OBJECT (color_state), "context", &context, NULL);

  color_state_icc = g_object_new (CLUTTER_TYPE_COLOR_STATE_ICC,
                                  "context", context,
                                  NULL);

  color_state_icc->fd = -1;
  color_state_icc->icc_profile = g_steal_pointer (&icc_profile);
  color_state_icc->eotf_profile = g_steal_pointer (&eotf_profile);
  color_state_icc->inv_eotf_profile = g_steal_pointer (&inv_eotf_profile);

  return CLUTTER_COLOR_STATE (color_state_icc);
}

/**
 * clutter_color_state_icc_new:
 *
 * Create a new ClutterColorStateIcc object from an icc profile.
 *
 * Return value: A new ClutterColorState object.
 **/
ClutterColorState *
clutter_color_state_icc_new (ClutterContext *context,
                             int             icc_fd,
                             uint32_t        icc_length)
{
  ClutterColorStateIcc *color_state_icc;
  g_autofd int icc_fd_dup = -1;
  g_autoptr (GError) error = NULL;
  g_autoptr (cmsHPROFILE) icc_profile = NULL;
  g_autoptr (cmsHPROFILE) eotf_profile = NULL;
  g_autoptr (cmsHPROFILE) inv_eotf_profile = NULL;
  uint8_t checksum[16];

  icc_fd_dup = dup (icc_fd);
  if (icc_fd_dup == -1)
    {
      g_warning ("Failed calling dup to ICC fd: %s", g_strerror (errno));
      return NULL;
    }

  if (!get_icc_profile (icc_fd, icc_length, &icc_profile, &error))
    {
      g_warning ("Failed getting ICC profile: %s", error->message);
      return NULL;
    }

  if (!get_eotf_profiles (icc_profile, &eotf_profile, &inv_eotf_profile, &error))
    {
      g_warning ("Failed getting EOTF from ICC profile: %s", error->message);
      return NULL;
    }

  if (!get_checksum (icc_profile, checksum))
    {
      g_warning ("Failed getting checksum from ICC profile");
      return NULL;
    }

  color_state_icc = g_object_new (CLUTTER_TYPE_COLOR_STATE_ICC,
                                  "context", context,
                                  NULL);

  color_state_icc->fd = g_steal_fd (&icc_fd_dup);
  color_state_icc->length = icc_length;
  color_state_icc->icc_profile = g_steal_pointer (&icc_profile);
  color_state_icc->eotf_profile = g_steal_pointer (&eotf_profile);
  color_state_icc->inv_eotf_profile = g_steal_pointer (&inv_eotf_profile);
  memcpy (color_state_icc->checksum, checksum, G_N_ELEMENTS (checksum));

  return CLUTTER_COLOR_STATE (color_state_icc);
}
