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

#include <glib/gstdio.h>
#include <sys/mman.h>

#include "wayland/meta-wayland-icc-profile.h"

#include "core/meta-anonymous-file.h"

typedef struct _IccProfileContext
{
  int icc_fd;
  uint32_t offset;
  uint32_t length;

  int out_icc_fd;
} IccProfileContext;

typedef struct _SigbusListener
{
  struct {
    uintptr_t addr;
    off64_t offset;
    size_t size;
  } mem;

  gboolean error_found;
} SigbusListener;

typedef struct _SigbusContext
{
  GList *listeners;
  struct sigaction old_act;
} SigbusContext;

static SigbusContext sigbus_context = { 0 };

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MetaAnonymousFile, meta_anonymous_file_free)

static void
icc_profile_context_free (IccProfileContext *icc_profile_context)
{
  g_clear_fd (&icc_profile_context->icc_fd, NULL);
  g_clear_fd (&icc_profile_context->out_icc_fd, NULL);
  g_free (icc_profile_context);
}

static void
on_sigbus_raised (int        sig,
                  siginfo_t *info,
                  void      *context)
{
  uintptr_t corrupted_addr = (uintptr_t) info->si_addr;
  struct sigaction old_act = sigbus_context.old_act;
  SigbusListener *listener;
  GList *l;

  for (l = sigbus_context.listeners; l != NULL; l = l->next)
    {
      listener = l->data;

      if (corrupted_addr >= listener->mem.addr &&
          corrupted_addr < listener->mem.addr + listener->mem.size)
        {
          listener->error_found = TRUE;
          break;
        }
    }

  if (l != NULL)
    {
      /* Remmap with MAP_ANONYMOUS with all bytes to 0 to avoid corruption */
      if (mmap ((void *) listener->mem.addr, listener->mem.size,
                PROT_READ, MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS,
                -1, listener->mem.offset) == MAP_FAILED)
        {
          goto old_action;
        }

      return;
    }

old_action:
  if (old_act.sa_flags & SA_SIGINFO)
    old_act.sa_sigaction (sig, info, context);
  else
    old_act.sa_handler (sig);
}

static void
register_sigbus_listener (SigbusListener *sigbus_listener)
{
  if (!sigbus_context.listeners)
    {
      struct sigaction old_act;
      struct sigaction act = { 0 };

      act.sa_sigaction = on_sigbus_raised;
      act.sa_flags = SA_SIGINFO;

      if (sigaction (SIGBUS, &act, &old_act) == -1)
        {
          g_warning ("Failed installing SIGBUS handler");
          return;
        }

      sigbus_context.old_act = old_act;
    }

  sigbus_context.listeners =  g_list_prepend (sigbus_context.listeners,
                                              sigbus_listener);
}

static void
unregister_sigbus_listener (SigbusListener *sigbus_listener)
{
  sigbus_context.listeners = g_list_remove (sigbus_context.listeners,
                                            sigbus_listener);

  if (!sigbus_context.listeners)
    {
      if (sigaction (SIGBUS, &sigbus_context.old_act, NULL) == -1)
        g_warning ("Failed uninstalling SIGBUS handler");
    }
}

static int
copy_and_protect_mem (int      fd,
                      uint32_t offset,
                      uint32_t size)
{
  uint8_t *data;
  int anon_fd, out_fd;
  SigbusListener sigbus_listener = { 0 };
  g_autoptr (MetaAnonymousFile) anonymous_file = NULL;

  data = mmap (NULL, size, PROT_READ, MAP_PRIVATE, fd, offset);
  if (data == MAP_FAILED)
    return -1;

  sigbus_listener.mem.addr = (uintptr_t) data;
  sigbus_listener.mem.offset = offset;
  sigbus_listener.mem.size = size;
  register_sigbus_listener (&sigbus_listener);

  anonymous_file = meta_anonymous_file_new (size, data);

  unregister_sigbus_listener (&sigbus_listener);

  munmap (data, size);

  if (sigbus_listener.error_found)
    {
      g_warning ("Reading ICC profile failed, SIGBUS raised");
      return -1;
    }

  if (!anonymous_file)
    return -1;

  anon_fd = meta_anonymous_file_open_fd (anonymous_file,
                                         META_ANONYMOUS_FILE_MAPMODE_PRIVATE);
  if (anon_fd == -1)
    return -1;

  out_fd = dup (anon_fd);

  meta_anonymous_file_close_fd (anon_fd);

  return out_fd;
}

static void
prepare_icc_profile_mem_in_thread (GTask        *task,
                                   gpointer      source_object,
                                   gpointer      task_data,
                                   GCancellable *cancellable)
{
  IccProfileContext *icc_profile_context = task_data;
  g_autofd int out_icc_fd = -1;

  out_icc_fd = copy_and_protect_mem (icc_profile_context->icc_fd,
                                     icc_profile_context->offset,
                                     icc_profile_context->length);
  if (out_icc_fd == -1)
    {
      g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_FAILED,
                               "Failed copying and sealing ICC fd");
      return;
    }

  icc_profile_context->out_icc_fd = g_steal_fd (&out_icc_fd);

  g_task_return_boolean (task, TRUE);
}

void
meta_wayland_icc_profile_prepare_mem_async (int                 icc_fd,
                                            uint32_t            offset,
                                            uint32_t            length,
                                            GAsyncReadyCallback callback,
                                            gpointer            user_data)
{
  g_autoptr (GTask) task = NULL;
  IccProfileContext *icc_profile_context;

  task = g_task_new (NULL, NULL, callback, user_data);

  icc_profile_context = g_new (IccProfileContext, 1);
  icc_profile_context->icc_fd = dup (icc_fd);
  icc_profile_context->offset = offset;
  icc_profile_context->length = length;
  icc_profile_context->out_icc_fd = -1;

  g_task_set_task_data (task, icc_profile_context,
                        (GDestroyNotify) icc_profile_context_free);
  g_task_run_in_thread (task, prepare_icc_profile_mem_in_thread);
}

gboolean
meta_wayland_icc_profile_prepare_mem_finish (GAsyncResult  *result,
                                             int           *icc_fd,
                                             uint32_t      *length,
                                             GError       **error)
{
  IccProfileContext *icc_profile_context =
    g_task_get_task_data (G_TASK (result));

  *icc_fd = g_steal_fd (&icc_profile_context->out_icc_fd);
  *length = icc_profile_context->length;

  return g_task_propagate_boolean (G_TASK (result), error);
}
