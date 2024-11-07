/*
 * Copyright (C) 2024 SUSE Software Solutions Germany GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Written by:
 *     Joan Torres <joan.torres@suse.com>
 */

#include "config.h"

#include <xf86drmMode.h>

#include "backends/native/meta-kms-color-op-private.h"

typedef struct _MetaKmsColorOpPropTable
{
  MetaKmsProp props[META_KMS_COLOR_OP_N_PROPS];
  MetaKmsEnum types[META_KMS_COLOR_OP_TYPE_N_PROPS];
  MetaKmsEnum curve_1d_types[META_KMS_COLOR_OP_CURVE_1D_TYPE_N_PROPS];
} MetaKmsColorOpPropTable;

typedef struct _MetaKmsColorOp1DCurve
{
  MetaKmsColorOpCurve1DType type;
} MetaKmsColorOp1DCurve;

typedef struct _MetaKmsColorOp1DLut
{
  uint32_t size;
  GBytes *data;
} MetaKmsColorOp1DLut;

typedef struct _MetaKmsColorOpCtm3x4
{
  double matrix[12];
} MetaKmsColorOpCtm3x4;

typedef struct _MetaKmsColorOpMultiplier
{
  double value;
} MetaKmsColorOpMultiplier;

typedef struct _MetaKmsColorOp3DLut
{
  struct drm_mode_3dlut_mode *modes;
  uint32_t n_modes;
  uint32_t mode_index;
  GBytes *data;
} MetaKmsColorOp3DLut;

struct _MetaKmsColorOp
{
  GObject parent;

  MetaKmsImplDevice *impl_device;

  uint32_t id;
  uint32_t next;
  gboolean bypass;

  MetaKmsColorOpType type;
  union
  {
    MetaKmsColorOp1DCurve curve_1d;
    MetaKmsColorOp1DLut lut_1d;
    MetaKmsColorOpCtm3x4 ctm;
    MetaKmsColorOpMultiplier mult;
    MetaKmsColorOp3DLut lut_3d;
  };

  MetaKmsColorOpPropTable prop_table;
};

G_DEFINE_TYPE (MetaKmsColorOp, meta_kms_color_op, G_TYPE_OBJECT)

#define INTEGER_MASK  0xffffffff00000000UL
#define FRACTION_MASK 0x00000000ffffffffUL
#define SIGN_MASK     0x8000000000000000UL

static const char *
color_op_type_to_string (MetaKmsColorOpType type)
{
  switch (type)
    {
    case META_KMS_COLOR_OP_TYPE_1D_CURVE:
      return "1D curve";
    case META_KMS_COLOR_OP_TYPE_1D_LUT:
      return "1D LUT";
    case META_KMS_COLOR_OP_TYPE_CTM_3X4:
      return "3x4 CTM";
    case META_KMS_COLOR_OP_TYPE_MULTIPLIER:
      return "Multiplier";
    case META_KMS_COLOR_OP_TYPE_3D_LUT:
      return "3D LUT";
    default:
      g_warning ("ColorOp unsupported");
      return "Unknown ColorOp";
    }
}

static const char *
color_op_curve_1d_type_to_string (MetaKmsColorOpCurve1DType type)
{
  switch (type)
    {
    case META_KMS_COLOR_OP_CURVE_1D_TYPE_SRGB:
      return "srgb";
    case META_KMS_COLOR_OP_CURVE_1D_TYPE_INV_SRGB:
      return "inv srgb";
    case META_KMS_COLOR_OP_CURVE_1D_TYPE_PQ:
      return "pq";
    case META_KMS_COLOR_OP_CURVE_1D_TYPE_INV_PQ:
      return "inv pq";
    default:
      g_warning ("Curve 1D unsupported");
      return "Unknown Curve";
    }
}

uint64_t
meta_kms_color_op_get_next (MetaKmsColorOp *color_op)
{
  return color_op->next;
}

static void
set_color_op_data (MetaKmsColorOp  *color_op,
                   GBytes         **data,
                   uint32_t         blob_id)
{
  int fd;
  drmModePropertyBlobPtr data_blob;
  GBytes *color_op_data;

  fd = meta_kms_impl_device_get_fd (color_op->impl_device);
  data_blob = drmModeGetPropertyBlob (fd, blob_id);
  if (!data_blob)
    {
      g_warning ("Failed to read ColorOp %s data: %s",
                 color_op_type_to_string (color_op->type),
                 g_strerror (errno));
      return;
    }

   color_op_data = g_bytes_new (data_blob->data, data_blob->length);
   drmModeFreePropertyBlob (data_blob);

   *data = color_op_data;
}

static void
set_1d_curve_properties (MetaKmsColorOp *color_op)
{
  MetaKmsProp *prop;

  prop = &color_op->prop_table.props[META_KMS_COLOR_OP_PROP_1D_CURVE_TYPE];
  if (prop->prop_id)
    color_op->curve_1d.type = prop->value;

  g_message ("JOAN: 1d curve: %s",
             color_op_curve_1d_type_to_string(color_op->curve_1d.type));
}

static void
set_1d_lut_properties (MetaKmsColorOp *color_op)
{
  MetaKmsProp *prop;

  prop = &color_op->prop_table.props[META_KMS_COLOR_OP_PROP_1D_LUT_SIZE];
  if (prop->prop_id)
    color_op->lut_1d.size = prop->value;

  prop = &color_op->prop_table.props[META_KMS_COLOR_OP_PROP_DATA];
  if (prop->prop_id && prop->value)
    set_color_op_data (color_op,
                       &color_op->lut_1d.data,
                       prop->value);

  g_message ("JOAN: 1d lut: %u, %p",
             color_op->lut_1d.size,
             color_op->lut_1d.data);
}

static double
fixed_point_to_double (uint64_t fixed)
{
  double ret = 0.0;

  ret += (INTEGER_MASK & fixed) >> 32;
  ret += (FRACTION_MASK & fixed) / 0xffffffffUL;
  ret *= (SIGN_MASK & fixed) ? -1.0 : 1.0;

  return ret;
}

static void
set_ctm_3x4_properties (MetaKmsColorOp *color_op)
{
  int i;
  int fd;
  MetaKmsProp *prop;
  drmModePropertyBlobPtr blob;
  struct drm_color_ctm_3x4 *drm_ctm_3x4;

  fd = meta_kms_impl_device_get_fd (color_op->impl_device);

  prop = &color_op->prop_table.props[META_KMS_COLOR_OP_PROP_DATA];
  if (prop->prop_id && prop->value)
    {
      blob = drmModeGetPropertyBlob (fd, prop->value);
      if (!blob)
        {
          g_warning ("Failed to read ColorOp %s data: %s",
                     color_op_type_to_string (color_op->type),
                     g_strerror (errno));
          return;
        }

      if (blob->length != sizeof (struct drm_color_ctm_3x4))
        {
          g_warning ("ColorOp %s size unexpected: %i, expected: %lu",
                     color_op_type_to_string (color_op->type),
                     blob->length,
                     sizeof (struct drm_color_ctm_3x4));
          return;
        }

      drm_ctm_3x4 = blob->data;
      for (i = 0; i < 12; i++)
        color_op->ctm.matrix[i] = fixed_point_to_double (drm_ctm_3x4->matrix[i]);

      g_message ("JOAN: ctm: %p", color_op->ctm.matrix);

      drmModeFreePropertyBlob (blob);
    }
}

static void
set_multiplier_properties (MetaKmsColorOp *color_op)
{
  MetaKmsProp *prop;

  prop = &color_op->prop_table.props[META_KMS_COLOR_OP_PROP_MULTIPLIER];
  if (prop->prop_id)
    color_op->mult.value = fixed_point_to_double (prop->value);

  g_message ("JOAN: multiplier: %f", color_op->mult.value);
}

static void
set_3d_lut_properties (MetaKmsColorOp *color_op)
{
  MetaKmsProp *prop;
  int fd;
  drmModePropertyBlobPtr blob;

  fd = meta_kms_impl_device_get_fd (color_op->impl_device);

  prop = &color_op->prop_table.props[META_KMS_COLOR_OP_PROP_3D_LUT_MODE_INDEX];
  if (prop->prop_id)
    color_op->lut_3d.mode_index = prop->value;

  prop = &color_op->prop_table.props[META_KMS_COLOR_OP_PROP_3D_LUT_MODES];
  if (prop->prop_id && prop->value)
    {
      blob = drmModeGetPropertyBlob (fd, prop->value);
      if (!blob)
        {
          g_warning ("Failed to read ColorOp %s data: %s",
                     color_op_type_to_string (color_op->type),
                     g_strerror (errno));
          return;
        }

      if (blob->length % sizeof (struct drm_mode_3dlut_mode) != 0)
        {
          g_warning ("ColorOp %s size unexpected: %i, it should be a multiple "
                     "of 3dlut_mode size",
                     color_op_type_to_string (color_op->type),
                     blob->length);
          drmModeFreePropertyBlob (blob);
          return;
        }

      color_op->lut_3d.modes = g_memdup2 (blob->data, blob->length);
      color_op->lut_3d.n_modes = blob->length /
                                 sizeof (struct drm_mode_3dlut_mode);

      g_message ("JOAN: lut 3d: n_modes: %i, index %i",
                 color_op->lut_3d.n_modes,
                 color_op->lut_3d.mode_index);

      for (int i = 0; i < color_op->lut_3d.n_modes; i++)
        g_message ("JOAN: modes[%i]: %p", i, color_op->lut_3d.modes);

      drmModeFreePropertyBlob (blob);
    }

  prop = &color_op->prop_table.props[META_KMS_COLOR_OP_PROP_DATA];
  if (prop->prop_id && prop->value)
    set_color_op_data (color_op,
                       &color_op->lut_3d.data,
                       prop->value);

  g_message ("JOAN: 3d lut: %p", color_op->lut_3d.data);
}

static void
color_op_set_type_properties (MetaKmsColorOp *color_op)
{
  switch (color_op->type)
    {
    case META_KMS_COLOR_OP_TYPE_1D_CURVE:
      set_1d_curve_properties (color_op);
      return;
    case META_KMS_COLOR_OP_TYPE_1D_LUT:
      set_1d_lut_properties (color_op);
      return;
    case META_KMS_COLOR_OP_TYPE_CTM_3X4:
      set_ctm_3x4_properties (color_op);
      return;
    case META_KMS_COLOR_OP_TYPE_MULTIPLIER:
      set_multiplier_properties (color_op);
      return;
    case META_KMS_COLOR_OP_TYPE_3D_LUT:
      set_3d_lut_properties (color_op);
      return;
    default:
      g_warning ("Unknown ColorOp type");
    }
  g_message ("\n");
}

static MetaKmsResourceChanges
meta_kms_color_op_read_state (MetaKmsColorOp          *color_op,
                              MetaKmsImplDevice       *impl_device,
                              drmModeObjectProperties *drm_color_op_props)
{
  MetaKmsResourceChanges changes = META_KMS_RESOURCE_CHANGE_NONE;
  MetaKmsProp *prop;

  g_message ("JOAN: read state of colorop: %u", color_op->id);

  meta_kms_impl_device_update_prop_table (impl_device,
                                          drm_color_op_props->props,
                                          drm_color_op_props->prop_values,
                                          drm_color_op_props->count_props,
                                          color_op->prop_table.props,
                                          META_KMS_COLOR_OP_N_PROPS);

  prop = &color_op->prop_table.props[META_KMS_COLOR_OP_PROP_TYPE];
  if (prop->prop_id)
    {
      color_op->type = prop->value;
      g_message ("JOAN: type: %s", color_op_type_to_string (color_op->type));
    }

  prop = &color_op->prop_table.props[META_KMS_COLOR_OP_PROP_BYPASS];
  if (prop->prop_id)
    {
      color_op->bypass = prop->value;
      g_message ("JOAN: bypass: %i", color_op->bypass);
    }

  prop = &color_op->prop_table.props[META_KMS_COLOR_OP_PROP_NEXT];
  if (prop->prop_id)
    {
      color_op->next = prop->value;
      g_message ("JOAN: next: %i", color_op->next);
    }

  color_op_set_type_properties (color_op);

  g_message ("\n");

  return changes;
}

static void
init_properties (MetaKmsColorOp *color_op)
{
  MetaKmsColorOpPropTable *prop_table = &color_op->prop_table;

  *prop_table = (MetaKmsColorOpPropTable) {
    .props = {
      [META_KMS_COLOR_OP_PROP_TYPE] =
        {
          .name = "TYPE",
          .type = DRM_MODE_PROP_ENUM,
          .enum_values = prop_table->types,
          .num_enum_values = META_KMS_COLOR_OP_TYPE_N_PROPS,
          .default_value = META_KMS_COLOR_OP_TYPE_UNKNOWN,
        },
      [META_KMS_COLOR_OP_PROP_BYPASS] =
        {
          .name = "BYPASS",
          .type = DRM_MODE_PROP_RANGE,
        },
      [META_KMS_COLOR_OP_PROP_NEXT] =
        {
          .name = "NEXT",
          .type = DRM_MODE_PROP_OBJECT,
        },
      [META_KMS_COLOR_OP_PROP_1D_CURVE_TYPE] =
        {
          .name = "CURVE_1D_TYPE",
          .type = DRM_MODE_PROP_ENUM,
          .enum_values = prop_table->curve_1d_types,
          .num_enum_values = META_KMS_COLOR_OP_CURVE_1D_TYPE_N_PROPS,
          .default_value = META_KMS_COLOR_OP_CURVE_1D_TYPE_UNKNOWN,
        },
      [META_KMS_COLOR_OP_PROP_1D_LUT_SIZE] =
        {
          .name = "SIZE",
          .type = DRM_MODE_PROP_RANGE,
        },
      [META_KMS_COLOR_OP_PROP_DATA] =
        {
          .name = "DATA",
          .type = DRM_MODE_PROP_BLOB,
        },
      [META_KMS_COLOR_OP_PROP_MULTIPLIER] =
        {
          .name = "MULTIPLIER",
          .type = DRM_MODE_PROP_RANGE,
        },
      [META_KMS_COLOR_OP_PROP_3D_LUT_MODES] =
        {
          .name = "3DLUT_MODES",
          .type = DRM_MODE_PROP_BLOB,
        },
      [META_KMS_COLOR_OP_PROP_3D_LUT_MODE_INDEX] =
        {
          .name = "3DLUT_MODE_INDEX",
          .type = DRM_MODE_PROP_RANGE,
        },
    },
    .types = {
      [META_KMS_COLOR_OP_TYPE_1D_CURVE] =
        {
          .name = "1D Curve",
        },
      [META_KMS_COLOR_OP_TYPE_1D_LUT] =
        {
          .name = "1D Curve Custom LUT",
        },
      [META_KMS_COLOR_OP_TYPE_CTM_3X4] =
        {
          .name = "3x4 Matrix",
        },
      [META_KMS_COLOR_OP_TYPE_MULTIPLIER] =
        {
          .name = "Multiplier",
        },
      [META_KMS_COLOR_OP_TYPE_3D_LUT] =
        {
          .name = "3D LUT",
        },
    },
    .curve_1d_types = {
      [META_KMS_COLOR_OP_CURVE_1D_TYPE_SRGB] =
        {
          .name = "sRGB EOTF",
        },
      [META_KMS_COLOR_OP_CURVE_1D_TYPE_INV_SRGB] =
        {
          .name = "sRGB Inverse EOTF",
        },
      [META_KMS_COLOR_OP_CURVE_1D_TYPE_PQ] =
        {
          .name = "PQ 125 EOTF",
        },
      [META_KMS_COLOR_OP_CURVE_1D_TYPE_INV_PQ] =
        {
          .name = "PQ 125 Inverse EOTF",
        },
    },
  };
}

MetaKmsColorOp *
meta_kms_color_op_new (MetaKmsImplDevice  *impl_device,
                       uint64_t            id,
                       GError            **error)
{
  int fd;
  drmModeObjectProperties *drm_props;
  MetaKmsColorOp *color_op;

  fd = meta_kms_impl_device_get_fd (impl_device);
  drm_props = drmModeObjectGetProperties (fd, id, DRM_MODE_OBJECT_ANY);
  if (!drm_props)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                   "Couldn't get DrmColorOp properties: %s", g_strerror (errno));
      return NULL;
    }

  color_op = g_object_new (META_TYPE_KMS_COLOR_OP, NULL);
  color_op->impl_device = impl_device;
  color_op->id = id;

  init_properties (color_op);

  meta_kms_color_op_read_state (color_op, impl_device, drm_props);

  drmModeFreeObjectProperties (drm_props);

  return color_op;
}

static void
meta_kms_color_op_finalize (GObject *object)
{
  MetaKmsColorOp *color_op = META_KMS_COLOR_OP (object);

  switch (color_op->type)
    {
    case META_KMS_COLOR_OP_TYPE_1D_LUT:
      g_clear_object (&color_op->lut_1d.data);
      break;
    case META_KMS_COLOR_OP_TYPE_3D_LUT:
      g_clear_pointer (&color_op->lut_3d.modes, g_free);
      g_clear_object (&color_op->lut_3d.data);
      break;
    case META_KMS_COLOR_OP_TYPE_MULTIPLIER:
    case META_KMS_COLOR_OP_TYPE_CTM_3X4:
    case META_KMS_COLOR_OP_TYPE_1D_CURVE:
    default:
      break;
  }

  G_OBJECT_CLASS (meta_kms_color_op_parent_class)->finalize (object);
}

static void
meta_kms_color_op_class_init (MetaKmsColorOpClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_kms_color_op_finalize;
}

static void
meta_kms_color_op_init (MetaKmsColorOp *lease)
{
}
