/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2022  Intel Corporation.
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
 *   Naveen Kumar <naveen1.kumar@intel.com>
 */

/**
 * ClutterColorState:
 *
 * Color state of each ClutterActor
 *
 * The #ClutterColorState class contains the colorspace of each color
 * states (e.g. sRGB colorspace).
 *
 * Each [class@Actor] would own such an object.
 *
 * A single #ClutterColorState object can be shared by multiple [class@Actor]
 * or maybe a separate color state for each [class@Actor] (depending on whether
 * #ClutterColorState would be statefull or stateless).
 *
 * #ClutterColorState, if not set during construction, it will default to sRGB
 * color state
 *
 * The #ClutterColorState would have API to get the colorspace, whether the
 * actor content is in pq or not, and things like that
 */

#include "config.h"

#include "clutter/clutter-color-state.h"

#include "clutter/clutter-debug.h"
#include "clutter/clutter-enum-types.h"
#include "clutter/clutter-private.h"

enum
{
  PROP_0,

  PROP_COLORSPACE,
  PROP_TRANSFER_FUNCTION,
  PROP_COLOR_ENCODING,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

typedef struct _ClutterColorStatePrivate ClutterColorStatePrivate;

struct _ClutterColorState
{
  GObject parent_instance;
};

struct _ClutterColorStatePrivate
{
  ClutterColorspace colorspace;
  ClutterTransferFunction transfer_function;
  ClutterColorEncoding color_encoding;
};

/* Luminance gain default value retrieved from
 * https://github.com/w3c/ColorWeb-CG/blob/feature/add-mastering-display-info/hdr_html_canvas_element.md#srgb-to-rec2100-pq */
#define SRGB_TO_PQ_LUMINANCE_GAIN 203

G_DEFINE_TYPE_WITH_PRIVATE (ClutterColorState,
                            clutter_color_state,
                            G_TYPE_OBJECT)

static const char *
clutter_colorspace_to_string (ClutterColorspace colorspace)
{
  switch (colorspace)
    {
    case CLUTTER_COLORSPACE_UNKNOWN:
      return "unknown";
    case CLUTTER_COLORSPACE_SRGB:
      return "sRGB";
    case CLUTTER_COLORSPACE_BT2020:
      return "BT.2020";
    }

  g_assert_not_reached ();
}

static const char *
clutter_transfer_function_to_string (ClutterTransferFunction transfer_function)
{
  switch (transfer_function)
    {
    case CLUTTER_TRANSFER_FUNCTION_UNKNOWN:
      return "unknown";
    case CLUTTER_TRANSFER_FUNCTION_SRGB:
      return "sRGB";
    case CLUTTER_TRANSFER_FUNCTION_PQ:
      return "PQ";
    }

  g_assert_not_reached ();
}

static const char *
clutter_color_encoding_to_string (ClutterColorEncoding color_encoding)
{
  switch (color_encoding)
    {
    case CLUTTER_COLOR_ENCODING_ELECTRICAL:
      return "electrical";
    case CLUTTER_COLOR_ENCODING_OPTICAL:
      return "optical";
    }

  g_assert_not_reached ();
}

ClutterColorspace
clutter_color_state_get_colorspace (ClutterColorState *color_state)
{
  ClutterColorStatePrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_COLOR_STATE (color_state),
                        CLUTTER_COLORSPACE_UNKNOWN);

  priv = clutter_color_state_get_instance_private (color_state);

  return priv->colorspace;
}

ClutterTransferFunction
clutter_color_state_get_transfer_function (ClutterColorState *color_state)
{
  ClutterColorStatePrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_COLOR_STATE (color_state),
                        CLUTTER_TRANSFER_FUNCTION_UNKNOWN);

  priv = clutter_color_state_get_instance_private (color_state);

  return priv->transfer_function;
}

ClutterColorEncoding
clutter_color_state_get_color_encoding (ClutterColorState *color_state)
{
  ClutterColorStatePrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_COLOR_STATE (color_state),
                        CLUTTER_COLOR_ENCODING_ELECTRICAL);

  priv = clutter_color_state_get_instance_private (color_state);

  return priv->color_encoding;
}

static void
clutter_color_state_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  ClutterColorState *color_state = CLUTTER_COLOR_STATE (object);
  ClutterColorStatePrivate *priv;

  priv = clutter_color_state_get_instance_private (color_state);

  switch (prop_id)
    {
    case PROP_COLORSPACE:
      priv->colorspace = g_value_get_enum (value);
      break;

    case PROP_TRANSFER_FUNCTION:
      priv->transfer_function = g_value_get_enum (value);
      break;

    case PROP_COLOR_ENCODING:
      priv->color_encoding = g_value_get_enum (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
clutter_color_state_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  ClutterColorState *color_state = CLUTTER_COLOR_STATE (object);

  switch (prop_id)
    {
    case PROP_COLORSPACE:
      g_value_set_enum (value,
                        clutter_color_state_get_colorspace (color_state));
      break;

    case PROP_TRANSFER_FUNCTION:
      g_value_set_enum (value,
                        clutter_color_state_get_transfer_function (color_state));
      break;

    case PROP_COLOR_ENCODING:
      g_value_set_enum (value,
                        clutter_color_state_get_color_encoding (color_state));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
clutter_color_state_class_init (ClutterColorStateClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property = clutter_color_state_set_property;
  gobject_class->get_property = clutter_color_state_get_property;

  /**
   * ClutterColorState:colorspace:
   *
   * Colorspace information of the each color state,
   * defaults to sRGB colorspace
   */
  obj_props[PROP_COLORSPACE] =
    g_param_spec_enum ("colorspace", NULL, NULL,
                       CLUTTER_TYPE_COLORSPACE,
                       CLUTTER_COLORSPACE_SRGB,
                       G_PARAM_READWRITE |
                       G_PARAM_STATIC_STRINGS |
                       G_PARAM_CONSTRUCT_ONLY);

  /**
   * ClutterColorState:transfer-function:
   *
   * Transfer function.
   */
  obj_props[PROP_TRANSFER_FUNCTION] =
    g_param_spec_enum ("transfer-function", NULL, NULL,
                       CLUTTER_TYPE_TRANSFER_FUNCTION,
                       CLUTTER_TRANSFER_FUNCTION_SRGB,
                       G_PARAM_READWRITE |
                       G_PARAM_STATIC_STRINGS |
                       G_PARAM_CONSTRUCT_ONLY);

  /**
   * ClutterColorState:color-encoding:
   *
   * Color encoding.
   */
  obj_props[PROP_COLOR_ENCODING] =
    g_param_spec_enum ("color-encoding", NULL, NULL,
                       CLUTTER_TYPE_COLOR_ENCODING,
                       CLUTTER_COLOR_ENCODING_ELECTRICAL,
                       G_PARAM_READWRITE |
                       G_PARAM_STATIC_STRINGS |
                       G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (gobject_class, N_PROPS, obj_props);
}

static void
clutter_color_state_init (ClutterColorState *color_state)
{
}

/**
 * clutter_color_state_new:
 *
 * Create a new ClutterColorState object.
 *
 * Return value: A new ClutterColorState object.
 **/
ClutterColorState *
clutter_color_state_new (ClutterColorspace       colorspace,
                         ClutterTransferFunction transfer_function,
                         ClutterColorEncoding    color_encoding)
{
  return g_object_new (CLUTTER_TYPE_COLOR_STATE,
                       "colorspace", colorspace,
                       "transfer-function", transfer_function,
                       "color-encoding", color_encoding,
                       NULL);
}

static const char pq_eotf_source[] =
  "vec3 pq_eotf (vec3 pq)\n"
  "{\n"
  "  const float c1 = 0.8359375;\n"
  "  const float c2 = 18.8515625;\n"
  "  const float c3 = 18.6875;\n"
  "\n"
  "  const float oo_m1 = 1.0 / 0.1593017578125;\n"
  "  const float oo_m2 = 1.0 / 78.84375;\n"
  "\n"
  "  vec3 num = max (pow (pq, vec3 (oo_m2)) - c1, vec3 (0.0));\n"
  "  vec3 den = c2 - c3 * pow (pq, vec3 (oo_m2));\n"
  "\n"
  "  return pow (num / den, vec3 (oo_m1));\n"
  "}\n"
  "\n"
  "vec4 pq_eotf (vec4 pq)\n"
  "{\n"
  "  return vec4 (pq_eotf (pq.rgb), pq.a);\n"
  "}\n";

static const char pq_inv_eotf_source[] =
  "vec3 pq_inv_eotf (vec3 nits)\n"
  "{\n"
  "  vec3 normalized = clamp (nits / 10000.0, vec3 (0), vec3 (1));\n"
  "  float m1 = 0.1593017578125;\n"
  "  float m2 = 78.84375;\n"
  "  float c1 = 0.8359375;\n"
  "  float c2 = 18.8515625;\n"
  "  float c3 = 18.6875;\n"
  "  vec3 normalized_pow_m1 = pow (normalized, vec3 (m1));\n"
  "  vec3 num = vec3 (c1) + c2 * normalized_pow_m1;\n"
  "  vec3 denum = vec3 (1.0) + c3 * normalized_pow_m1;\n"
  "  return pow (num / denum, vec3 (m2));\n"
  "}\n"
  "\n"
  "vec4 pq_inv_eotf (vec4 nits)\n"
  "{\n"
  "  return vec4 (pq_inv_eotf (nits.rgb), nits.a);\n"
  "}\n";

static const char srgb_eotf_source[] =
  "vec3 srgb_eotf (vec3 color)\n"
  "{\n"
  "  bvec3 is_low = lessThanEqual (color, vec3 (0.04045));\n"
  "  vec3 lo_part = color / 12.92;\n"
  "  vec3 hi_part = pow ((color + 0.055) / 1.055, vec3 (12.0 / 5.0));\n"
  "  return mix (hi_part, lo_part, is_low);\n"
  "}\n"
  "\n"
  "vec4 srgb_eotf (vec4 color)\n"
  "{\n"
  "  return vec4 (srgb_eotf (color.rgb), color.a);\n"
  "}\n";

static const char srgb_inv_eotf_source[] =
  "vec3 srgb_inv_eotf (vec3 color)\n"
  "{\n"
  "  bvec3 is_lo = lessThanEqual (color, vec3 (0.0031308));\n"
  "\n"
  "  vec3 lo_part = color * 12.92;\n"
  "  vec3 hi_part = pow (color, vec3 (5.0 / 12.0)) * 1.055 - 0.055;\n"
  "  return mix (hi_part, lo_part, is_lo);\n"
  "}\n"
  "\n"
  "vec4 srgb_inv_eotf (vec4 color)\n"
  "{\n"
  "  return vec4 (srgb_inv_eotf (color.rgb), color.a);\n"
  "}\n";

/* Calculated using:
 *   numpy.dot(colour.models.RGB_COLOURSPACE_BT2020.matrix_XYZ_to_RGB,
 *             colour.models.RGB_COLOURSPACE_BT709.matrix_RGB_to_XYZ)
 */
static const char bt709_to_bt2020_matrix_source[] =
  "mat3 bt709_to_bt2020 =\n"
  "  mat3 (vec3 (0.6274039,  0.06909729, 0.01639144),\n"
  "        vec3 (0.32928304, 0.9195404,  0.08801331),\n"
  "        vec3 (0.04331307, 0.01136232, 0.89559525));\n";

/*
 * Calculated using:
 *  numpy.dot(colour.models.RGB_COLOURSPACE_BT709.matrix_XYZ_to_RGB,
 *            colour.models.RGB_COLOURSPACE_BT2020.matrix_RGB_to_XYZ)
 */
static const char bt2020_to_bt709_matrix_source[] =
  "mat3 bt2020_to_bt709 =\n"
  "  mat3 (vec3 (1.660491,    -0.12455047, -0.01815076),\n"
  "        vec3 (-0.58764114,  1.1328999,  -0.1005789),\n"
  "        vec3 (-0.07284986, -0.00834942,  1.11872966));\n";

typedef struct _TransferFunction
{
  const char *source;
  const char *name;
} TransferFunction;

typedef struct _MatrixMultiplication
{
  const char *source;
  const char *name;
} MatrixMultiplication;

static const TransferFunction pq_eotf = {
  .source = pq_eotf_source,
  .name = "pq_eotf",
};

static const TransferFunction pq_inv_eotf = {
  .source = pq_inv_eotf_source,
  .name = "pq_inv_eotf",
};

static const TransferFunction srgb_eotf = {
  .source = srgb_eotf_source,
  .name = "srgb_eotf",
};

static const TransferFunction srgb_inv_eotf = {
  .source = srgb_inv_eotf_source,
  .name = "srgb_inv_eotf",
};

static const MatrixMultiplication bt709_to_bt2020 = {
  .source = bt709_to_bt2020_matrix_source,
  .name = "bt709_to_bt2020",
};

static const MatrixMultiplication bt2020_to_bt709 = {
  .source = bt2020_to_bt709_matrix_source,
  .name = "bt2020_to_bt709",
};

static int
calculate_luminance_gain (ClutterColorState *color_state,
                          ClutterColorState *target_color_state)
{
  ClutterColorStatePrivate *priv =
    clutter_color_state_get_instance_private (color_state);
  ClutterColorStatePrivate *target_priv =
    clutter_color_state_get_instance_private (target_color_state);

  if (priv->transfer_function == CLUTTER_TRANSFER_FUNCTION_SRGB &&
      target_priv->transfer_function == CLUTTER_TRANSFER_FUNCTION_PQ)
    {
      return SRGB_TO_PQ_LUMINANCE_GAIN;
    }
  else if (priv->transfer_function == CLUTTER_TRANSFER_FUNCTION_PQ &&
           target_priv->transfer_function == CLUTTER_TRANSFER_FUNCTION_SRGB)
    {
      g_warning_once ("Mapping PQ content brightness to sRGB not yet implemented.");
      return 1;
    }
  else
    {
      return 1;
    }
}

static void
append_shader_description (GString           *snippet_source,
                           ClutterColorState *color_state,
                           ClutterColorState *target_color_state)
{
  ClutterColorStatePrivate *priv =
    clutter_color_state_get_instance_private (color_state);
  ClutterColorStatePrivate *target_priv =
    clutter_color_state_get_instance_private (target_color_state);
  const char *colorspace =
    clutter_colorspace_to_string (priv->colorspace);
  const char *transfer_function =
    clutter_transfer_function_to_string (priv->transfer_function);
  const char *color_encoding =
    clutter_color_encoding_to_string (priv->color_encoding);
  const char *target_colorspace =
    clutter_colorspace_to_string (target_priv->colorspace);
  const char *target_transfer_function =
    clutter_transfer_function_to_string (target_priv->transfer_function);
  const char *target_color_encoding =
    clutter_color_encoding_to_string (target_priv->color_encoding);

  g_string_append_printf (snippet_source,
                          "  // %s (%s, %s) to %s (%s, %s)\n",
                          colorspace,
                          transfer_function,
                          color_encoding,
                          target_colorspace,
                          target_transfer_function,
                          target_color_encoding);
}

/**
 * clutter_color_state_get_transform_snippet: (skip)
 */
CoglSnippet *
clutter_color_state_get_transform_snippet (ClutterColorState *color_state,
                                           ClutterColorState *target_color_state)
{
  ClutterColorStatePrivate *priv;
  ClutterColorStatePrivate *target_priv;
  gboolean needs_transfer_function = FALSE;
  const MatrixMultiplication *color_space_mapping = NULL;
  const TransferFunction *transfer_function = NULL;
  g_autoptr (GString) globals_source = NULL;
  g_autoptr (GString) snippet_source = NULL;

  g_return_val_if_fail (CLUTTER_IS_COLOR_STATE (target_color_state), NULL);

  priv = clutter_color_state_get_instance_private (color_state);
  target_priv = clutter_color_state_get_instance_private (target_color_state);

  if (target_priv->color_encoding != priv->color_encoding)
    needs_transfer_function = TRUE;

  if (priv->colorspace == CLUTTER_COLORSPACE_SRGB &&
      target_priv->colorspace == CLUTTER_COLORSPACE_BT2020)
    {
      color_space_mapping = &bt709_to_bt2020;
    }
  else if (priv->colorspace == CLUTTER_COLORSPACE_BT2020 &&
           target_priv->colorspace == CLUTTER_COLORSPACE_SRGB)
    {
      color_space_mapping = &bt2020_to_bt709;
    }
  else if (priv->colorspace == target_priv->colorspace)
    {
      color_space_mapping = NULL;
    }
  else
    {
      g_warning ("Unhandled color space mapping (%s to %s)",
                 clutter_colorspace_to_string (priv->colorspace),
                 clutter_colorspace_to_string (target_priv->colorspace));
      return NULL;
    }

  if (needs_transfer_function)
    {
      switch (priv->color_encoding)
        {
        case CLUTTER_COLOR_ENCODING_ELECTRICAL:
          switch (priv->transfer_function)
            {
            case CLUTTER_TRANSFER_FUNCTION_PQ:
              transfer_function = &pq_eotf;
              break;
            case CLUTTER_TRANSFER_FUNCTION_SRGB:
              transfer_function = &srgb_eotf;
              break;
            default:
              g_warning ("Unhandled tranfer function %s",
                         clutter_transfer_function_to_string (priv->transfer_function));
              return NULL;
            }
          break;
        case CLUTTER_COLOR_ENCODING_OPTICAL:
          switch (target_priv->transfer_function)
            {
            case CLUTTER_TRANSFER_FUNCTION_PQ:
              transfer_function = &pq_inv_eotf;
              break;
            case CLUTTER_TRANSFER_FUNCTION_SRGB:
              transfer_function = &srgb_inv_eotf;
              break;
            default:
              g_warning ("Unhandled tranfer function %s",
                         clutter_transfer_function_to_string (priv->transfer_function));
              return NULL;
            }
          break;
        }
    }

  globals_source = g_string_new (NULL);
  if (transfer_function)
    g_string_append_printf (globals_source, "%s\n", transfer_function->source);
  if (color_space_mapping)
    g_string_append_printf (globals_source, "%s\n", color_space_mapping->source);

  /*
   * The following statements generate a shader snippet that transforms colors
   * from one color state (transfer function, color space, color encoding) into
   * another. When the target color state is optically encoded, we always draw
   * into an intermediate 64 bit half float typed pixel.
   *
   * The value stored in this pixel is roughly the luminance expected by the
   * target color state's transfer function.
   *
   * For sRGB that means luminance relative the reference display as defined by
   * the sRGB specification, i.e. a value typically between 0.0 and 1.0. For PQ
   * this means absolute luminance in cd/m² (nits).
   *
   * The snippet contains a pipeline that roughly looks like this:
   *
   *     color = source_transfer_function (color)
   *     color *= luminance_gain
   *     color = color_space_mapping_matrix * color
   *
   */

  snippet_source = g_string_new (NULL);
  append_shader_description (snippet_source, color_state, target_color_state);

  g_string_append (snippet_source,
                   "  vec3 color_state_color = cogl_color_out.rgb;\n");
  if (transfer_function)
    {
      g_string_append_printf (snippet_source,
                              "  color_state_color = %s (color_state_color);\n",
                              transfer_function->name);
    }

  g_string_append_printf (snippet_source,
                          "  color_state_color = %d.0 * color_state_color;\n",
                          calculate_luminance_gain (color_state,
                                                    target_color_state));
  if (color_space_mapping)
    {
      g_string_append_printf (snippet_source,
                              "  color_state_color = %s * color_state_color;\n",
                              color_space_mapping->name);
    }

  g_string_append (snippet_source,
                   "  cogl_color_out = vec4 (color_state_color, cogl_color_out.a);\n");

  return cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT,
                           globals_source->str,
                           snippet_source->str);
}
