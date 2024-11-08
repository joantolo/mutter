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
 * Written by:
 *     Joan Torres <joan.torres@suse.com>
 */

#include "config.h"

#include <gio/gunixinputstream.h>

#include "wayland/meta-wayland-icc-profile.h"

typedef struct _IccProfileContext
{
  int icc_fd;
  uint32_t offset;
  uint32_t length;

  MetaAnonymousFile *file;
} IccProfileContext;

static void
icc_profile_context_free (IccProfileContext *icc_profile_context)
{
  g_clear_pointer (&icc_profile_context->file, meta_anonymous_file_free);
  g_free (icc_profile_context);
}

MetaAnonymousFile *
meta_wayland_icc_profile_get_anonymous_file_sync (int        fd,
                                                  uint32_t   offset,
                                                  uint32_t   length,
                                                  GError   **error)
{
  g_autoptr (GInputStream) input_stream = NULL;
  g_autofree uint8_t *bytes = NULL;
  MetaAnonymousFile *file;
  size_t bytes_read;
  int skipped;

  input_stream = G_INPUT_STREAM (g_unix_input_stream_new (fd, FALSE));

  skipped = g_input_stream_skip (input_stream,
                                 offset,
                                 NULL,
                                 error);
  if (skipped < 0)
    return NULL;

  bytes = g_malloc (length);
  if (!g_input_stream_read_all (input_stream,
                                bytes,
                                length,
                                &bytes_read,
                                NULL,
                                error))
    return NULL;

  file = meta_anonymous_file_new (length, bytes);
  if (!file)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed creating anonymous file");
      return NULL;
    }

  return file;
}

static void
get_anonymous_file_in_thread (GTask        *task,
                              gpointer      source_object,
                              gpointer      task_data,
                              GCancellable *cancellable)
{
  IccProfileContext *context = task_data;
  g_autoptr (GError) error = NULL;
  MetaAnonymousFile *file;

  file = meta_wayland_icc_profile_get_anonymous_file_sync (context->icc_fd,
                                                           context->offset,
                                                           context->length,
                                                           &error);
  if (!file)
    {
      g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_FAILED,
                               "Failed reading and protecting ICC mem (%s)",
                               error->message);
      return;
    }

  context->file = file;

  g_task_return_boolean (task, TRUE);
}

void
meta_wayland_icc_profile_get_anonymous_file_async (int                 icc_fd,
                                                   uint32_t            offset,
                                                   uint32_t            length,
                                                   GAsyncReadyCallback callback,
                                                   gpointer            user_data)
{
  g_autoptr (GTask) task = NULL;
  IccProfileContext *context;

  task = g_task_new (NULL, NULL, callback, user_data);

  context = g_new (IccProfileContext, 1);
  context->icc_fd = icc_fd;
  context->offset = offset;
  context->length = length;

  g_task_set_task_data (task, context,
                        (GDestroyNotify) icc_profile_context_free);
  g_task_run_in_thread (task, get_anonymous_file_in_thread);
}

gboolean
meta_wayland_icc_profile_get_anonymous_file_finish (GAsyncResult       *result,
                                                    MetaAnonymousFile **file,
                                                    GError            **error)
{
  IccProfileContext *icc_profile_context =
    g_task_get_task_data (G_TASK (result));

  *file = g_steal_pointer (&icc_profile_context->file);

  return g_task_propagate_boolean (G_TASK (result), error);
}
