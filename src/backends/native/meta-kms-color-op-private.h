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
 * https://gitlab.freedesktop.org/alex.hung/linux/-/tree/amd-color-pipeline-v8
 * https://lore.kernel.org/all/20250326234748.2982010-1-alex.hung@amd.com/
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

enum drm_colorop_lut3d_interpolation_type {
	/**
	 * @DRM_COLOROP_LUT3D_INTERPOLATION_TETRAHEDRAL:
	 *
	 * Tetrahedral 3DLUT interpolation
	 */
	DRM_COLOROP_LUT3D_INTERPOLATION_TETRAHEDRAL,
};

/**
 * enum drm_colorop_lut1d_interpolation_type - type of interpolation for 1D LUTs
 */
enum drm_colorop_lut1d_interpolation_type {
	/**
	 * @DRM_COLOROP_LUT1D_INTERPOLATION_LINEAR:
	 *
	 * Linear interpolation. Values between points of the LUT will be
	 * linearly interpolated.
	 */
	DRM_COLOROP_LUT1D_INTERPOLATION_LINEAR,
};

enum drm_colorop_curve_1d_type {
	/**
	 * @DRM_COLOROP_1D_CURVE_SRGB_EOTF:
	 *
	 * enum string "sRGB EOTF"
	 *
	 * sRGB piece-wise electro-optical transfer function. Transfer
	 * characteristics as defined by IEC 61966-2-1 sRGB. Equivalent
	 * to H.273 TransferCharacteristics code point 13 with
	 * MatrixCoefficients set to 0.
	 */
	DRM_COLOROP_1D_CURVE_SRGB_EOTF,

	/**
	 * @DRM_COLOROP_1D_CURVE_SRGB_INV_EOTF:
	 *
	 * enum string "sRGB Inverse EOTF"
	 *
	 * The inverse of &DRM_COLOROP_1D_CURVE_SRGB_EOTF
	 */
	DRM_COLOROP_1D_CURVE_SRGB_INV_EOTF,

	/**
	 * @DRM_COLOROP_1D_CURVE_BT2020_INV_OETF:
	 *
	 * enum string "BT.2020 Inverse OETF"
	 *
	 * The inverse of &DRM_COLOROP_1D_CURVE_BT2020_OETF
	 */
	DRM_COLOROP_1D_CURVE_BT2020_INV_OETF,

	/**
	 * @DRM_COLOROP_1D_CURVE_BT2020_OETF:
	 *
	 * enum string "BT.2020 OETF"
	 *
	 * The BT.2020/BT.709 transfer function. The BT.709 and BT.2020
	 * transfer functions are the same, the only difference is that
	 * BT.2020 is defined with more precision for 10 and 12-bit
	 * encodings.
	 *
	 *
	 */
	DRM_COLOROP_1D_CURVE_BT2020_OETF,

	/**
	 * @DRM_COLOROP_1D_CURVE_PQ_125_EOTF:
	 *
	 * enum string "PQ 125 EOTF"
	 *
	 * The PQ transfer function, scaled by 125.0f, so that 10,000
	 * nits correspond to 125.0f.
	 *
	 * Transfer characteristics of the PQ function as defined by
	 * SMPTE ST 2084 (2014) for 10-, 12-, 14-, and 16-bit systems
	 * and Rec. ITU-R BT.2100-2 perceptual quantization (PQ) system,
	 * represented by H.273 TransferCharacteristics code point 16.
	 */
	DRM_COLOROP_1D_CURVE_PQ_125_EOTF,

	/**
	 * @DRM_COLOROP_1D_CURVE_PQ_125_INV_EOTF:
	 *
	 * enum string "PQ 125 Inverse EOTF"
	 *
	 * The inverse of DRM_COLOROP_1D_CURVE_PQ_125_EOTF.
	 */
	DRM_COLOROP_1D_CURVE_PQ_125_INV_EOTF,

	/**
	 * @DRM_COLOROP_1D_CURVE_COUNT:
	 *
	 * enum value denoting the size of the enum
	 */
	DRM_COLOROP_1D_CURVE_COUNT
};

typedef enum _MetaKmsColorOpProp
{
  META_KMS_COLOR_OP_PROP_TYPE = 0,            /* used by ALL */
  META_KMS_COLOR_OP_PROP_BYPASS,              /* used by ALL */
  META_KMS_COLOR_OP_PROP_NEXT,                /* used by ALL */
  META_KMS_COLOR_OP_PROP_1D_CURVE_TYPE,       /* used by 1D Curve */
  META_KMS_COLOR_OP_PROP_SIZE,                /* used by 1D and 3D LUT */
  META_KMS_COLOR_OP_PROP_DATA,                /* used by 1D LUT, and 3x4 CTM, and 3D LUT */
  META_KMS_COLOR_OP_PROP_MULTIPLIER,          /* used by Multiplier */
  META_KMS_COLOR_OP_PROP_LUT1D_INTERPOLATION, /* used by 1D LUT */
  META_KMS_COLOR_OP_PROP_LUT3D_INTERPOLATION, /* used by 3D LUT */
  META_KMS_COLOR_OP_N_PROPS,
} MetaKmsColorOpProp;

MetaKmsColorOp * meta_kms_color_op_new (MetaKmsImplDevice  *impl_device,
                                        uint64_t            id,
                                        GError            **error);

uint64_t meta_kms_color_op_get_next (MetaKmsColorOp *color_op);

uint32_t meta_kms_color_op_get_prop_id (MetaKmsColorOp     *color_op,
                                        MetaKmsColorOpProp  prop);

const char * meta_kms_color_op_get_prop_name (MetaKmsColorOp     *color_op,
                                              MetaKmsColorOpProp  prop);

uint64_t meta_kms_color_op_get_prop_drm_value (MetaKmsColorOp     *color_op,
                                               MetaKmsColorOpProp  prop,
                                               uint64_t            value);
