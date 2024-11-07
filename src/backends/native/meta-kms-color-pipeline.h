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

#include "backends/native/meta-kms-impl-device.h"

#define META_TYPE_KMS_COLOR_PIPELINE (meta_kms_color_pipeline_get_type ())
G_DECLARE_FINAL_TYPE (MetaKmsColorPipeline,
                      meta_kms_color_pipeline,
                      META, KMS_COLOR_PIPELINE,
                      GObject)

MetaKmsColorPipeline * meta_kms_color_pipeline_new (MetaKmsImplDevice  *impl_device,
                                                    uint64_t            id,
                                                    GError            **error);
