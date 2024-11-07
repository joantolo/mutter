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

#pragma once

#include <glib-object.h>
#include <stdint.h>

typedef enum _MetaKmsColorOpType
{
  META_KMS_COLOR_OP_TYPE_1D_CURVE = 0,
  META_KMS_COLOR_OP_TYPE_1D_LUT,
  META_KMS_COLOR_OP_TYPE_CTM_3X4,
  META_KMS_COLOR_OP_TYPE_MULTIPLIER,
  META_KMS_COLOR_OP_TYPE_3D_LUT,
  META_KMS_COLOR_OP_TYPE_N_PROPS,
} MetaKmsColorOpType;

typedef enum _MetaKmsColorOpCurve1DType
{
  META_KMS_COLOR_OP_1D_CURVE_TYPE_SRGB = 0,
  META_KMS_COLOR_OP_1D_CURVE_TYPE_INV_SRGB,
  META_KMS_COLOR_OP_1D_CURVE_TYPE_BT2020,
  META_KMS_COLOR_OP_1D_CURVE_TYPE_INV_BT2020,
  META_KMS_COLOR_OP_1D_CURVE_TYPE_PQ,
  META_KMS_COLOR_OP_1D_CURVE_TYPE_INV_PQ,
  META_KMS_COLOR_OP_1D_CURVE_TYPE_N_PROPS,
} MetaKmsColorOp1DCurveType;

typedef enum _MetaKmsColorOp1DLutInterpolation
{
  META_KMS_COLOR_OP_LUT1D_INTERPOLATION_LINEAR = 0,
  META_KMS_COLOR_OP_LUT1D_INTERPOLATION_N_PROPS,
} MetaKmsColorOp1DLutInterpolation;

typedef enum _MetaKmsColorOp3DLutInterpolation
{
  META_KMS_COLOR_OP_LUT3D_INTERPOLATION_TETRAHEDRAL = 0,
  META_KMS_COLOR_OP_LUT3D_INTERPOLATION_N_PROPS,
} MetaKmsColorOp3DLutInterpolation;

typedef struct _MetaKmsColorOp1DCurve
{
  MetaKmsColorOp1DCurveType type;
} MetaKmsColorOp1DCurve;

typedef struct _MetaKmsColorOp1DLut
{
  uint32_t size;
  MetaKmsColorOp1DLutInterpolation interpolation;
  /* TODO: use MetaGammaLut instead of GBytes */
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
  uint32_t size;
  MetaKmsColorOp3DLutInterpolation interpolation;
  /* TODO: use MetaGammaLut instead of GBytes */
  GBytes *data;
} MetaKmsColorOp3DLut;

#define META_TYPE_KMS_COLOR_OP (meta_kms_color_op_get_type ())
G_DECLARE_FINAL_TYPE (MetaKmsColorOp,
                      meta_kms_color_op,
                      META, KMS_COLOR_OP,
                      GObject)

uint32_t meta_kms_color_op_get_id (MetaKmsColorOp *color_op);
