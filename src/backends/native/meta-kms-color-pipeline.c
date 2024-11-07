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

#include "backends/native/meta-kms-color-pipeline.h"

#include "backends/native/meta-kms-color-op-private.h"

struct _MetaKmsColorPipeline
{
  GObject parent;

  uint64_t id;

  GSList *color_ops;
};

G_DEFINE_TYPE (MetaKmsColorPipeline, meta_kms_color_pipeline, G_TYPE_OBJECT)

MetaKmsColorPipeline *
meta_kms_color_pipeline_new (MetaKmsImplDevice  *impl_device,
                             uint64_t            id,
                             GError            **error)
{
  MetaKmsColorPipeline *color_pipeline;
  MetaKmsColorOp *color_op;
  GSList *color_ops = NULL;
  uint64_t color_op_id = id;

  while (color_op_id != 0)
    {
      color_op = meta_kms_color_op_new (impl_device, color_op_id, error);
      if (!color_op)
        {
          g_slist_free_full (color_ops, g_object_unref);
          return NULL;
        }

      color_ops = g_slist_prepend (color_ops, color_op);

      color_op_id = meta_kms_color_op_get_next (color_op);
    }

  color_pipeline = g_object_new (META_TYPE_KMS_COLOR_PIPELINE, NULL);
  color_pipeline->id = id;
  color_pipeline->color_ops = g_slist_reverse (color_ops);

  return color_pipeline;
}

static void
meta_kms_color_pipeline_finalize (GObject *object)
{
  MetaKmsColorPipeline *color_pipeline = META_KMS_COLOR_PIPELINE (object);

  g_slist_free_full (color_pipeline->color_ops, g_object_unref);

  G_OBJECT_CLASS (meta_kms_color_pipeline_parent_class)->finalize (object);
}

static void
meta_kms_color_pipeline_class_init (MetaKmsColorPipelineClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_kms_color_pipeline_finalize;
}

static void
meta_kms_color_pipeline_init (MetaKmsColorPipeline *lease)
{
}
