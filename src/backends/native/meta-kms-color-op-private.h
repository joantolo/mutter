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

#include "backends/native/meta-kms-color-op.h"

#include "backends/native/meta-kms-impl-device.h"

/*
 * Define DRM structs from color pipeline branch, until upstreamed.
 * More info:
 * https://gitlab.freedesktop.org/hwentland/linux/-/tree/amd-color-pipeline-v6
 * https://lore.kernel.org/all/20241003200129.1732122-1-harry.wentland@amd.com/
 */
struct drm_color_ctm_3x4 {
	/*
	 * Conversion matrix with 3x4 dimensions in S31.32 sign-magnitude
	 * (not two's complement!) format.
	 *
	 * out   matrix          in
	 * |R|   |0  1  2  3 |   | R |
	 * |G| = |4  5  6  7 | x | G |
	 * |B|   |8  9  10 11|   | B |
	 *                       |1.0|
	 */
	__u64 matrix[12];
};

struct drm_mode_3dlut_mode {
	/**
	 * @lut_size: 3D LUT size - can be 9, 17 or 33
	 */
	__u16 lut_size;
	/**
	 * @lut_stride: dimensions of 3D LUT. Must be larger than lut_size
	 */
	__u16 lut_stride[3];
	/**
	 * @interpolation: interpolation algorithm for 3D LUT. See drm_colorop_lut3d_interpolation_type
	 */
	__u16 interpolation;
	/**
	 * @color_depth: color depth - can be 8, 10 or 12
	 */
	__u16 color_depth;
	/**
	 * @color_format: color format specified by fourcc values
	 * ex. DRM_FORMAT_XRGB16161616 - color in order of RGB, each is 16bit.
	 */
	__u32 color_format;
	/**
	 * @traversal_order:
	 *
	 * Traversal order when parsing/writing the 3D LUT. See enum drm_colorop_lut3d_traversal_order
	 */
	 __u16 traversal_order;
};

typedef enum _MetaKmsColorOpPropp
{
  META_KMS_COLOR_OP_PROP_TYPE = 0,            /* used by ALL */
  META_KMS_COLOR_OP_PROP_BYPASS,              /* used by ALL */
  META_KMS_COLOR_OP_PROP_NEXT,                /* used by ALL */
  META_KMS_COLOR_OP_PROP_1D_CURVE_TYPE,       /* used by 1D Curve */
  META_KMS_COLOR_OP_PROP_1D_LUT_SIZE,         /* used by 1D LUT */
  META_KMS_COLOR_OP_PROP_DATA,                /* used by 1D LUT, and 3x4 CTM, and 3D LUT */
  META_KMS_COLOR_OP_PROP_MULTIPLIER,          /* used by Multiplier */
  META_KMS_COLOR_OP_PROP_3D_LUT_MODES,        /* used by 3D LUT */
  META_KMS_COLOR_OP_PROP_3D_LUT_MODE_INDEX,   /* used by 3D LUT */
  META_KMS_COLOR_OP_N_PROPS,
} MetaKmsColorOpProp;

typedef enum _MetaKmsColorOpType
{
  META_KMS_COLOR_OP_TYPE_1D_CURVE = 0,
  META_KMS_COLOR_OP_TYPE_1D_LUT,
  META_KMS_COLOR_OP_TYPE_CTM_3X4,
  META_KMS_COLOR_OP_TYPE_MULTIPLIER,
  META_KMS_COLOR_OP_TYPE_3D_LUT,
  META_KMS_COLOR_OP_TYPE_N_PROPS,
  META_KMS_COLOR_OP_TYPE_UNKNOWN,
} MetaKmsColorOpType;

typedef enum _MetaKmsColorOpCurve1DType
{
  META_KMS_COLOR_OP_CURVE_1D_TYPE_SRGB = 0,
  META_KMS_COLOR_OP_CURVE_1D_TYPE_INV_SRGB,
  META_KMS_COLOR_OP_CURVE_1D_TYPE_PQ,
  META_KMS_COLOR_OP_CURVE_1D_TYPE_INV_PQ,
  META_KMS_COLOR_OP_CURVE_1D_TYPE_N_PROPS,
  META_KMS_COLOR_OP_CURVE_1D_TYPE_UNKNOWN,
} MetaKmsColorOpCurve1DType;

MetaKmsColorOp * meta_kms_color_op_new (MetaKmsImplDevice  *impl_device,
                                        uint64_t            id,
                                        GError            **error);

uint64_t meta_kms_color_op_get_next (MetaKmsColorOp *color_op);
