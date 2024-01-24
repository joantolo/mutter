/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2008,2009 Intel Corporation.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *
 */

#include "config.h"

#include <string.h>

#include <pango/pango.h>

#include "cogl/cogl-util.h"
#include "cogl/cogl-color.h"
#include "cogl/cogl-color-private.h"

G_DEFINE_BOXED_TYPE (CoglColor,
                     cogl_color,
                     cogl_color_copy,
                     cogl_color_free)

CoglColor *
cogl_color_copy (const CoglColor *color)
{
  if (G_LIKELY (color))
    return g_memdup2 (color, sizeof (CoglColor));

  return NULL;
}

void
cogl_color_free (CoglColor *color)
{
  if (G_LIKELY (color))
    g_free (color);
}

void
cogl_color_init_from_4f (CoglColor *color,
                         float red,
                         float green,
                         float blue,
                         float alpha)
{
  g_return_if_fail (color != NULL);

  color->red   =  (red * 255);
  color->green =  (green * 255);
  color->blue  =  (blue * 255);
  color->alpha =  (alpha * 255);
}

static inline void
skip_whitespace (gchar **str)
{
  while (g_ascii_isspace (**str))
    *str += 1;
}

static inline void
parse_rgb_value (gchar   *str,
                 guint8  *color,
                 gchar  **endp)
{
  gdouble number;
  gchar *p;

  skip_whitespace (&str);

  number = g_ascii_strtod (str, endp);

  p = *endp;

  skip_whitespace (&p);

  if (*p == '%')
    {
      *endp = (gchar *) (p + 1);

      *color = CLAMP (number / 100.0, 0.0, 1.0) * 255;
    }
  else
    *color = CLAMP (number, 0, 255);
}

static gboolean
parse_rgba (CoglColor *color,
            gchar     *str,
            gboolean   has_alpha)
{
  skip_whitespace (&str);

  if (*str != '(')
    return FALSE;

  str += 1;

  /* red */
  parse_rgb_value (str, &color->red, &str);
  skip_whitespace (&str);
  if (*str != ',')
    return FALSE;

  str += 1;

  /* green */
  parse_rgb_value (str, &color->green, &str);
  skip_whitespace (&str);
  if (*str != ',')
    return FALSE;

  str += 1;

  /* blue */
  parse_rgb_value (str, &color->blue, &str);
  skip_whitespace (&str);

  /* alpha (optional); since the alpha channel value can only
   * be between 0 and 1 we don't use the parse_rgb_value()
   * function
   */
  if (has_alpha)
    {
      gdouble number;

      if (*str != ',')
        return FALSE;

      str += 1;

      skip_whitespace (&str);
      number = g_ascii_strtod (str, &str);

      color->alpha = CLAMP (number * 255.0, 0, 255);
    }
  else
    color->alpha = 255;

  skip_whitespace (&str);
  if (*str != ')')
    return FALSE;

  return TRUE;
}

static gboolean
parse_hsla (CoglColor *color,
            gchar     *str,
            gboolean   has_alpha)
{
  gdouble number;
  gdouble h, l, s;

  skip_whitespace (&str);

  if (*str != '(')
    return FALSE;

  str += 1;

  /* hue */
  skip_whitespace (&str);
  /* we don't do any angle normalization here because
   * cogl_color_from_hls() will do it for us
   */
  number = g_ascii_strtod (str, &str);
  skip_whitespace (&str);
  if (*str != ',')
    return FALSE;

  h = number;

  str += 1;

  /* saturation */
  skip_whitespace (&str);
  number = g_ascii_strtod (str, &str);
  skip_whitespace (&str);
  if (*str != '%')
    return FALSE;

  str += 1;

  s = CLAMP (number / 100.0, 0.0, 1.0);
  skip_whitespace (&str);
  if (*str != ',')
    return FALSE;

  str += 1;

  /* luminance */
  skip_whitespace (&str);
  number = g_ascii_strtod (str, &str);
  skip_whitespace (&str);
  if (*str != '%')
    return FALSE;

  str += 1;

  l = CLAMP (number / 100.0, 0.0, 1.0);
  skip_whitespace (&str);

  /* alpha (optional); since the alpha channel value can only
   * be between 0 and 1 we don't use the parse_rgb_value()
   * function
   */
  if (has_alpha)
    {
      if (*str != ',')
        return FALSE;

      str += 1;

      skip_whitespace (&str);
      number = g_ascii_strtod (str, &str);

      color->alpha = CLAMP (number * 255.0, 0, 255);
    }
  else
    color->alpha = 255;

  skip_whitespace (&str);
  if (*str != ')')
    return FALSE;

  cogl_color_init_from_hsl (color, h, s, l);

  return TRUE;
}

/**
 * cogl_color_from_string:
 * @color: (out caller-allocates): return location for a #CoglColor
 * @str: a string specifying a color
 *
 * Parses a string definition of a color, filling the #CoglColor.red,
 * #CoglColor.green, #CoglColor.blue and #CoglColor.alpha fields
 * of @color.
 *
 * The @color is not allocated.
 *
 * The format of @str can be either one of:
 *
 *   - a standard name (as taken from the X11 rgb.txt file)
 *   - an hexadecimal value in the form: `#rgb`, `#rrggbb`, `#rgba`, or `#rrggbbaa`
 *   - a RGB color in the form: `rgb(r, g, b)`
 *   - a RGB color in the form: `rgba(r, g, b, a)`
 *   - a HSL color in the form: `hsl(h, s, l)`
 *    -a HSL color in the form: `hsla(h, s, l, a)`
 *
 * where 'r', 'g', 'b' and 'a' are (respectively) the red, green, blue color
 * intensities and the opacity. The 'h', 's' and 'l' are (respectively) the
 * hue, saturation and luminance values.
 *
 * In the rgb() and rgba() formats, the 'r', 'g', and 'b' values are either
 * integers between 0 and 255, or percentage values in the range between 0%
 * and 100%; the percentages require the '%' character. The 'a' value, if
 * specified, can only be a floating point value between 0.0 and 1.0.
 *
 * In the hls() and hlsa() formats, the 'h' value (hue) is an angle between
 * 0 and 360.0 degrees; the 'l' and 's' values (luminance and saturation) are
 * percentage values in the range between 0% and 100%. The 'a' value, if specified,
 * can only be a floating point value between 0.0 and 1.0.
 *
 * Whitespace inside the definitions is ignored; no leading whitespace
 * is allowed.
 *
 * If the alpha component is not specified then it is assumed to be set to
 * be fully opaque.
 *
 * Return value: %TRUE if parsing succeeded, and %FALSE otherwise
 */
gboolean
cogl_color_from_string (CoglColor   *color,
                        const gchar *str)
{
  PangoColor pango_color = { 0, };

  g_return_val_if_fail (color != NULL, FALSE);
  g_return_val_if_fail (str != NULL, FALSE);

  if (strncmp (str, "rgb", 3) == 0)
    {
      gchar *s = (gchar *) str;
      gboolean res;

      if (strncmp (str, "rgba", 4) == 0)
        res = parse_rgba (color, s + 4, TRUE);
      else
        res = parse_rgba (color, s + 3, FALSE);

      return res;
    }

  if (strncmp (str, "hsl", 3) == 0)
    {
      gchar *s = (gchar *) str;
      gboolean res;

      if (strncmp (str, "hsla", 4) == 0)
        res = parse_hsla (color, s + 4, TRUE);
      else
        res = parse_hsla (color, s + 3, FALSE);

      return res;
    }

  /* if the string contains a color encoded using the hexadecimal
   * notations (#rrggbbaa or #rgba) we attempt a rough pass at
   * parsing the color ourselves, as we need the alpha channel that
   * Pango can't retrieve.
   */
  if (str[0] == '#' && str[1] != '\0')
    {
      gsize length = strlen (str + 1);
      gint32 result;

      if (sscanf (str + 1, "%x", &result) == 1)
        {
          switch (length)
            {
            case 8: /* rrggbbaa */
              color->red = (result >> 24) & 0xff;
              color->green = (result >> 16) & 0xff;
              color->blue = (result >> 8) & 0xff;

              color->alpha = result & 0xff;

              return TRUE;

            case 6: /* #rrggbb */
              color->red = (result >> 16) & 0xff;
              color->green = (result >> 8) & 0xff;
              color->blue = result & 0xff;

              color->alpha = 0xff;

              return TRUE;

            case 4: /* #rgba */
              color->red = ((result >> 12) & 0xf);
              color->green = ((result >> 8) & 0xf);
              color->blue = ((result >> 4) & 0xf);
              color->alpha = result & 0xf;

              color->red = (color->red << 4) | color->red;
              color->green = (color->green << 4) | color->green;
              color->blue = (color->blue << 4) | color->blue;
              color->alpha = (color->alpha << 4) | color->alpha;

              return TRUE;

            case 3: /* #rgb */
              color->red = ((result >> 8) & 0xf);
              color->green = ((result >> 4) & 0xf);
              color->blue = result & 0xf;

              color->red = (color->red << 4) | color->red;
              color->green = (color->green << 4) | color->green;
              color->blue = (color->blue << 4) | color->blue;

              color->alpha = 0xff;

              return TRUE;

            default:
              return FALSE;
            }
        }
    }

  /* fall back to pango for X11-style named colors; see:
   *
   *   http://en.wikipedia.org/wiki/X11_color_names
   *
   * for a list. at some point we might even ship with our own list generated
   * from X11/rgb.txt, like we generate the key symbols.
   */
  if (pango_color_parse (&pango_color, str))
    {
      color->red = pango_color.red;
      color->green = pango_color.green;
      color->blue = pango_color.blue;

      color->alpha = 0xff;

      return TRUE;
    }

  return FALSE;
}

/**
 * cogl_color_to_string:
 * @color: a #CoglColor
 *
 * Returns a textual specification of @color in the hexadecimal form
 * `&num;rrggbbaa`, where `r`, `g`, `b` and `a` are
 * hexadecimal digits representing the red, green, blue and alpha components
 * respectively.
 *
 * Return value: (transfer full): a newly-allocated text string
 */
gchar *
cogl_color_to_string (const CoglColor *color)
{
  g_return_val_if_fail (color != NULL, NULL);

  return g_strdup_printf ("#%02x%02x%02x%02x",
                          color->red,
                          color->green,
                          color->blue,
                          color->alpha);
}

/**
 * cogl_color_to_pixel:
 * @color: a #CoglColor
 *
 * Converts @color into a packed 32 bit integer, containing
 * all the four 8 bit channels used by #CoglColor.
 *
 * Return value: a packed color
 */
guint32
cogl_color_to_pixel (const CoglColor *color)
{
  g_return_val_if_fail (color != NULL, 0);

  return (color->alpha |
          color->blue << 8 |
          color->green << 16 |
          color->red << 24);
}

/**
 * cogl_color_from_pixel:
 * @color: (out caller-allocates): return location for a #CoglColor
 * @pixel: a 32 bit packed integer containing a color
 *
 * Converts @pixel from the packed representation of a four 8 bit channel
 * color to a #CoglColor.
 */
void
cogl_color_from_pixel (CoglColor *color,
                       guint32    pixel)
{
  g_return_if_fail (color != NULL);

  color->red = pixel >> 24;
  color->green = (pixel >> 16) & 0xff;
  color->blue = (pixel >> 8) & 0xff;
  color->alpha = pixel & 0xff;
}

float
cogl_color_get_red (const CoglColor *color)
{
  return  ((float) color->red / 255.0);
}

float
cogl_color_get_green (const CoglColor *color)
{
  return  ((float) color->green / 255.0);
}

float
cogl_color_get_blue (const CoglColor *color)
{
  return  ((float) color->blue / 255.0);
}

float
cogl_color_get_alpha (const CoglColor *color)
{
  return  ((float) color->alpha / 255.0);
}

void
cogl_color_premultiply (CoglColor *color)
{
  color->red = (color->red * color->alpha + 128) / 255;
  color->green = (color->green * color->alpha + 128) / 255;
  color->blue = (color->blue * color->alpha + 128) / 255;
}

gboolean
cogl_color_equal (const void *v1, const void *v2)
{
  const uint32_t *c1 = v1, *c2 = v2;

  g_return_val_if_fail (v1 != NULL, FALSE);
  g_return_val_if_fail (v2 != NULL, FALSE);

  /* XXX: We don't compare the padding */
  return *c1 == *c2 ? TRUE : FALSE;
}

/**
 * cogl_color_hash:
 * @v: (type Cogl.Color): a #CoglColor
 *
 * Converts a #CoglColor to a hash value.
 *
 * This function can be passed to g_hash_table_new() as the @hash_func
 * parameter, when using `CoglColor`s as keys in a #GHashTable.
 *
 * Return value: a hash value corresponding to the color
 */
guint
cogl_color_hash (gconstpointer v)
{
  return cogl_color_to_pixel ((const CoglColor *) v);
}

void
_cogl_color_get_rgba_4ubv (const CoglColor *color,
                           uint8_t *dest)
{
  memcpy (dest, color, 4);
}

void
cogl_color_to_hsl (const CoglColor *color,
                   float           *hue,
                   float           *saturation,
                   float           *luminance)
{
  float red, green, blue;
  float min, max, delta;
  float h, l, s;

  red   = color->red / 255.0;
  green = color->green / 255.0;
  blue  = color->blue / 255.0;

  if (red > green)
    {
      if (red > blue)
	max = red;
      else
	max = blue;

      if (green < blue)
	min = green;
      else
	min = blue;
    }
  else
    {
      if (green > blue)
	max = green;
      else
	max = blue;

      if (red < blue)
	min = red;
      else
	min = blue;
    }

  l = (max + min) / 2;
  s = 0;
  h = 0;

  if (max != min)
    {
      if (l <= 0.5)
	s = (max - min) / (max + min);
      else
	s = (max - min) / (2.0 - max - min);

      delta = max - min;

      if (red == max)
	h = (green - blue) / delta;
      else if (green == max)
	h = 2.0 + (blue - red) / delta;
      else if (blue == max)
	h = 4.0 + (red - green) / delta;

      h *= 60;

      if (h < 0)
	h += 360.0;
    }

  if (hue)
    *hue = h;

  if (luminance)
    *luminance = l;

  if (saturation)
    *saturation = s;
}

void
cogl_color_init_from_hsl (CoglColor *color,
                          float      hue,
                          float      saturation,
                          float      luminance)
{
  float tmp1, tmp2;
  float tmp3[3];
  float clr[3];
  int   i;

  hue /= 360.0;

  if (saturation == 0)
    {
      cogl_color_init_from_4f (color, luminance, luminance, luminance, 1.0f);
      return;
    }

  if (luminance <= 0.5)
    tmp2 = luminance * (1.0 + saturation);
  else
    tmp2 = luminance + saturation - (luminance * saturation);

  tmp1 = 2.0 * luminance - tmp2;

  tmp3[0] = hue + 1.0 / 3.0;
  tmp3[1] = hue;
  tmp3[2] = hue - 1.0 / 3.0;

  for (i = 0; i < 3; i++)
    {
      if (tmp3[i] < 0)
        tmp3[i] += 1.0;

      if (tmp3[i] > 1)
        tmp3[i] -= 1.0;

      if (6.0 * tmp3[i] < 1.0)
        clr[i] = tmp1 + (tmp2 - tmp1) * tmp3[i] * 6.0;
      else if (2.0 * tmp3[i] < 1.0)
        clr[i] = tmp2;
      else if (3.0 * tmp3[i] < 2.0)
        clr[i] = (tmp1 + (tmp2 - tmp1) * ((2.0 / 3.0) - tmp3[i]) * 6.0);
      else
        clr[i] = tmp1;
    }

  cogl_color_init_from_4f (color, clr[0], clr[1], clr[2], 1.0f);
}

/**
 * cogl_value_set_color:
 * @value: a #GValue initialized to #COGL_TYPE_COLOR
 * @color: the color to set
 *
 * Sets @value to @color.
 */
void
cogl_value_set_color (GValue          *value,
                      const CoglColor *color)
{
  g_return_if_fail (COGL_VALUE_HOLDS_COLOR (value));

  g_value_set_boxed (value, color);
}

/**
 * cogl_value_get_color:
 * @value: a #GValue initialized to #COGL_TYPE_COLOR
 *
 * Gets the #CoglColor contained in @value.
 *
 * Return value: (transfer none): the color inside the passed #GValue
 */
const CoglColor *
cogl_value_get_color (const GValue *value)
{
  g_return_val_if_fail (COGL_VALUE_HOLDS_COLOR (value), NULL);

  return g_value_get_boxed (value);
}

static void
param_color_init (GParamSpec *pspec)
{
  CoglParamSpecColor *cspec = COGL_PARAM_SPEC_COLOR (pspec);

  cspec->default_value = NULL;
}

static void
param_color_finalize (GParamSpec *pspec)
{
  CoglParamSpecColor *cspec = COGL_PARAM_SPEC_COLOR (pspec);

  cogl_color_free (cspec->default_value);
}

static void
param_color_set_default (GParamSpec *pspec,
                         GValue     *value)
{
  const CoglColor *default_value =
    COGL_PARAM_SPEC_COLOR (pspec)->default_value;
  cogl_value_set_color (value, default_value);
}

static gint
param_color_values_cmp (GParamSpec   *pspec,
                        const GValue *value1,
                        const GValue *value2)
{
  const CoglColor *color1 = g_value_get_boxed (value1);
  const CoglColor *color2 = g_value_get_boxed (value2);
  int pixel1, pixel2;

  if (color1 == NULL)
    return color2 == NULL ? 0 : -1;

  pixel1 = cogl_color_to_pixel (color1);
  pixel2 = cogl_color_to_pixel (color2);

  if (pixel1 < pixel2)
    return -1;
  else if (pixel1 == pixel2)
    return 0;
  else
    return 1;
}

GType
cogl_param_color_get_type (void)
{
  static GType pspec_type = 0;

  if (G_UNLIKELY (pspec_type == 0))
    {
      const GParamSpecTypeInfo pspec_info = {
        sizeof (CoglParamSpecColor),
        16,
        param_color_init,
        COGL_TYPE_COLOR,
        param_color_finalize,
        param_color_set_default,
        NULL,
        param_color_values_cmp,
      };

      pspec_type = g_param_type_register_static (g_intern_static_string ("CoglParamSpecColor"),
                                                 &pspec_info);
    }

  return pspec_type;
}

/**
 * cogl_param_spec_color: (skip)
 * @name: name of the property
 * @nick: short name
 * @blurb: description (can be translatable)
 * @default_value: default value
 * @flags: flags for the param spec
 *
 * Creates a #GParamSpec for properties using #CoglColor.
 *
 * Returns: (transfer full): the newly created #GParamSpec
 */
GParamSpec *
cogl_param_spec_color (const gchar     *name,
                       const gchar     *nick,
                       const gchar     *blurb,
                       const CoglColor *default_value,
                       GParamFlags      flags)
{
  CoglParamSpecColor *cspec;

  cspec = g_param_spec_internal (COGL_TYPE_PARAM_COLOR,
                                 name, nick, blurb, flags);

  cspec->default_value = cogl_color_copy (default_value);

  return G_PARAM_SPEC (cspec);
}
