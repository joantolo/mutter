/*
 * Wayland Transaction Support
 *
 * Copyright (C) 2021 Red Hat, Inc.
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include "config.h"

#include "wayland/meta-wayland-transaction.h"

#include "wayland/meta-wayland.h"
#include "wayland/meta-wayland-subsurface.h"

#define META_WAYLAND_TRANSACTION_NONE ((void *)(uintptr_t) G_MAXSIZE)

struct _MetaWaylandTransaction
{
  GList node;
  MetaWaylandCompositor *compositor;
  MetaWaylandTransaction *next_candidate;
  uint64_t committed_sequence;

  /*
   * Keys:   All surfaces referenced in the transaction
   * Values: Pointer to MetaWaylandTransactionEntry for the surface
   */
  GHashTable *entries;
};

typedef struct _MetaWaylandTransactionEntry
{
  MetaWaylandSurfaceState *state;

  /* Sub-surface position */
  gboolean has_sub_pos;
  int x;
  int y;
} MetaWaylandTransactionEntry;

static MetaWaylandTransactionEntry *
meta_wayland_transaction_get_entry (MetaWaylandTransaction *transaction,
                                    MetaWaylandSurface     *surface)
{
  return g_hash_table_lookup (transaction->entries, surface);
}

static void
meta_wayland_transaction_sync_child_states (MetaWaylandSurface *surface)
{
  MetaWaylandSurface *subsurface_surface;

  META_WAYLAND_SURFACE_FOREACH_SUBSURFACE (surface, subsurface_surface)
    {
      MetaWaylandSubsurface *subsurface;
      MetaWaylandActorSurface *actor_surface;

      subsurface = META_WAYLAND_SUBSURFACE (subsurface_surface->role);
      actor_surface = META_WAYLAND_ACTOR_SURFACE (subsurface);
      meta_wayland_actor_surface_sync_actor_state (actor_surface);
    }
}

static void
meta_wayland_transaction_apply_subsurface_position (MetaWaylandSurface          *surface,
                                                    MetaWaylandTransactionEntry *entry)
{
  if (!entry->has_sub_pos)
    return;

  surface->sub.x = entry->x;
  surface->sub.y = entry->y;
}

static gboolean
is_ancestor (MetaWaylandSurface *candidate,
             MetaWaylandSurface *reference)
{
  MetaWaylandSurface *ancestor;

  for (ancestor = reference->sub.parent; ancestor; ancestor = ancestor->sub.parent)
    {
      if (ancestor == candidate)
        return TRUE;
    }

  return FALSE;
}

static int
meta_wayland_transaction_compare (const void *key1,
                                  const void *key2)
{
  MetaWaylandSurface *surface1 = *(MetaWaylandSurface **) key1;
  MetaWaylandSurface *surface2 = *(MetaWaylandSurface **) key2;

  /* Order of siblings doesn't matter */
  if (surface1->sub.parent == surface2->sub.parent)
    return 0;

  /* Ancestor surfaces come before descendant surfaces */
  if (is_ancestor (surface1, surface2))
    return 1;

  if (is_ancestor (surface2, surface1))
    return -1;

  /*
   * Order unrelated surfaces by their toplevel surface pointer values, to
   * prevent unrelated surfaces from getting mixed between siblings
   */
  return (meta_wayland_surface_get_toplevel (surface1) <
          meta_wayland_surface_get_toplevel (surface2)) ? -1 : 1;
}

static MetaWaylandTransaction *
find_next_transaction_for_surface (MetaWaylandTransaction *transaction,
                                   MetaWaylandSurface     *surface)
{
  GList *node;

  for (node = transaction->node.next; node; node = node->next)
    {
      MetaWaylandTransaction *next = node->data;

      if (surface->transaction.last_committed == next ||
          g_hash_table_contains (next->entries, surface))
        return next;
    }

  return NULL;
}

static void
ensure_next_candidate (MetaWaylandTransaction  *transaction,
                       MetaWaylandTransaction **first_candidate)
{
  MetaWaylandTransaction **candidate;

  if (transaction->next_candidate)
    return;

  candidate = first_candidate;
  while (*candidate != META_WAYLAND_TRANSACTION_NONE &&
         (*candidate)->committed_sequence <
         transaction->committed_sequence)
    candidate = &(*candidate)->next_candidate;

  transaction->next_candidate = *candidate;
  *candidate = transaction;
}

static void
meta_wayland_transaction_apply (MetaWaylandTransaction  *transaction,
                                MetaWaylandTransaction **first_candidate)
{
  g_autofree MetaWaylandSurface **surfaces = NULL;
  g_autofree MetaWaylandSurfaceState **states = NULL;
  unsigned int num_surfaces;
  int i;

  surfaces = (MetaWaylandSurface **)
    g_hash_table_get_keys_as_array (transaction->entries, &num_surfaces);
  states = g_new (MetaWaylandSurfaceState *, num_surfaces);

  /* Sort surfaces from ancestors to descendants */
  qsort (surfaces, num_surfaces, sizeof (MetaWaylandSurface *),
         meta_wayland_transaction_compare);

  /* Apply states from ancestors to descendants */
  for (i = 0; i < num_surfaces; i++)
    {
      MetaWaylandSurface *surface = surfaces[i];
      MetaWaylandTransactionEntry *entry;

      entry = meta_wayland_transaction_get_entry (transaction, surface);
      states[i] = entry->state;
      meta_wayland_transaction_apply_subsurface_position (surface, entry);
      if (entry->state)
        meta_wayland_surface_apply_state (surface, entry->state);

      if (surface->transaction.last_committed == transaction)
        {
          surface->transaction.first_committed = NULL;
          surface->transaction.last_committed = NULL;
        }
      else
        {
          MetaWaylandTransaction *next_transaction;

          next_transaction = find_next_transaction_for_surface (transaction, surface);
          if (next_transaction)
            {
              surface->transaction.first_committed = next_transaction;
              ensure_next_candidate (next_transaction, first_candidate);
            }
        }
    }

  /* Synchronize child states from descendants to ancestors */
  for (i = num_surfaces - 1; i >= 0; i--)
    {
      if (states[i])
        meta_wayland_transaction_sync_child_states (surfaces[i]);
    }

  meta_wayland_transaction_free (transaction);
}

static gboolean
has_unapplied_dependencies (MetaWaylandTransaction *transaction)
{
  GHashTableIter iter;
  MetaWaylandSurface *surface;

  g_hash_table_iter_init (&iter, transaction->entries);
  while (g_hash_table_iter_next (&iter, (gpointer *) &surface, NULL))
    {
      if (surface->transaction.first_committed != transaction)
        return TRUE;
    }

  return FALSE;
}

static void
meta_wayland_transaction_maybe_apply_one (MetaWaylandTransaction  *transaction,
                                          MetaWaylandTransaction **first_candidate)
{
  if (has_unapplied_dependencies (transaction))
    return;

  meta_wayland_transaction_apply (transaction, first_candidate);
}

static void
meta_wayland_transaction_maybe_apply (MetaWaylandTransaction *transaction)
{
  MetaWaylandTransaction *first_candidate = META_WAYLAND_TRANSACTION_NONE;

  while (TRUE)
    {
      meta_wayland_transaction_maybe_apply_one (transaction, &first_candidate);

      if (first_candidate == META_WAYLAND_TRANSACTION_NONE)
        return;

      transaction = first_candidate;
      first_candidate = transaction->next_candidate;
      transaction->next_candidate = NULL;
    }
}

void
meta_wayland_transaction_commit (MetaWaylandTransaction *transaction)
{
  static uint64_t committed_sequence;
  GQueue *committed_queue;
  gboolean maybe_apply = TRUE;
  GHashTableIter iter;
  MetaWaylandSurface *surface;

  transaction->committed_sequence = ++committed_sequence;
  transaction->node.data = transaction;

  committed_queue =
    meta_wayland_compositor_get_committed_transactions (transaction->compositor);
  g_queue_push_tail_link (committed_queue, &transaction->node);

  g_hash_table_iter_init (&iter, transaction->entries);
  while (g_hash_table_iter_next (&iter, (gpointer *) &surface, NULL))
    {
      surface->transaction.last_committed = transaction;

      if (!surface->transaction.first_committed)
        surface->transaction.first_committed = transaction;
      else
        maybe_apply = FALSE;
    }

  if (maybe_apply)
    meta_wayland_transaction_maybe_apply (transaction);
}

static MetaWaylandTransactionEntry *
meta_wayland_transaction_ensure_entry (MetaWaylandTransaction *transaction,
                                       MetaWaylandSurface     *surface)
{
  MetaWaylandTransactionEntry *entry;

  entry = meta_wayland_transaction_get_entry (transaction, surface);
  if (entry)
    return entry;

  entry = g_new0 (MetaWaylandTransactionEntry, 1);
  g_hash_table_insert (transaction->entries, g_object_ref (surface), entry);

  return entry;
}

static void
meta_wayland_transaction_entry_free (MetaWaylandTransactionEntry *entry)
{
  g_clear_object (&entry->state);
  g_free (entry);
}

static void
meta_wayland_transaction_add_placement_surfaces (MetaWaylandTransaction  *transaction,
                                                 MetaWaylandSurface      *surface,
                                                 MetaWaylandSurfaceState *state)
{
  GSList *l;

  for (l = state->subsurface_placement_ops; l; l = l->next)
    {
      MetaWaylandSubsurfacePlacementOp *op = l->data;

      if (op->surface)
        meta_wayland_transaction_ensure_entry (transaction, op->surface);

      if (op->sibling)
        meta_wayland_transaction_ensure_entry (transaction, op->sibling);
    }
}

static void
meta_wayland_transaction_add_entry (MetaWaylandTransaction      *transaction,
                                    MetaWaylandSurface          *surface,
                                    MetaWaylandTransactionEntry *entry)
{
  g_hash_table_insert (transaction->entries, g_object_ref (surface), entry);

  if (entry->state)
    {
      meta_wayland_transaction_add_placement_surfaces (transaction, surface,
                                                       entry->state);
    }
}

void
meta_wayland_transaction_add_subsurface_position (MetaWaylandTransaction *transaction,
                                                  MetaWaylandSurface     *surface,
                                                  int                     x,
                                                  int                     y)
{
  MetaWaylandTransactionEntry *entry;

  entry = meta_wayland_transaction_ensure_entry (transaction, surface);
  entry->x = x;
  entry->y = y;
  entry->has_sub_pos = TRUE;
}

static void
meta_wayland_transaction_entry_merge_into (MetaWaylandTransactionEntry *from,
                                           MetaWaylandTransactionEntry *to)
{
  if (from->has_sub_pos)
    {
      to->x = from->x;
      to->y = from->y;
      to->has_sub_pos = TRUE;
    }

  if (to->state)
    {
      meta_wayland_surface_state_merge_into (from->state, to->state);
      g_clear_object (&from->state);
      return;
    }

  to->state = g_steal_pointer (&from->state);
}

void
meta_wayland_transaction_merge_into (MetaWaylandTransaction *from,
                                     MetaWaylandTransaction *to)
{
  GHashTableIter iter;
  MetaWaylandSurface *surface;
  MetaWaylandTransactionEntry *from_entry, *to_entry;

  g_hash_table_iter_init (&iter, from->entries);
  while (g_hash_table_iter_next (&iter, (gpointer *) &surface,
                                 (gpointer *) &from_entry))
    {
      to_entry = meta_wayland_transaction_get_entry (to, surface);
      if (!to_entry)
        {
          g_hash_table_iter_steal (&iter);
          meta_wayland_transaction_add_entry (to, surface, from_entry);
          g_object_unref (surface);
          continue;
        }

      if (from_entry->state)
        {
          meta_wayland_transaction_add_placement_surfaces (to, surface,
                                                           from_entry->state);
        }

      meta_wayland_transaction_entry_merge_into (from_entry, to_entry);
      g_hash_table_iter_remove (&iter);
    }

  meta_wayland_transaction_free (from);
}

void
meta_wayland_transaction_merge_pending_state (MetaWaylandTransaction *transaction,
                                              MetaWaylandSurface     *surface)
{
  MetaWaylandSurfaceState *pending = surface->pending_state;
  MetaWaylandTransactionEntry *entry;

  entry = meta_wayland_transaction_ensure_entry (transaction, surface);

  if (!entry->state)
    {
      entry->state = pending;
      surface->pending_state = meta_wayland_surface_state_new ();
      return;
    }

  meta_wayland_surface_state_merge_into (pending, entry->state);
  meta_wayland_surface_state_reset (pending);
}

MetaWaylandTransaction *
meta_wayland_transaction_new (MetaWaylandCompositor *compositor)
{
  MetaWaylandTransaction *transaction;

  transaction = g_new0 (MetaWaylandTransaction, 1);

  transaction->compositor = compositor;
  transaction->entries = g_hash_table_new_full (NULL, NULL, g_object_unref,
                                                (GDestroyNotify) meta_wayland_transaction_entry_free);

  return transaction;
}

void
meta_wayland_transaction_free (MetaWaylandTransaction *transaction)
{
  if (transaction->node.data)
    {
      GQueue *committed_queue =
        meta_wayland_compositor_get_committed_transactions (transaction->compositor);

      g_queue_unlink (committed_queue, &transaction->node);
    }

  g_hash_table_destroy (transaction->entries);
  g_free (transaction);
}

void
meta_wayland_transaction_finalize (MetaWaylandCompositor *compositor)
{
  GQueue *transactions;
  MetaWaylandTransaction *transaction;

  transactions = meta_wayland_compositor_get_committed_transactions (compositor);

  while ((transaction = g_queue_pop_head (transactions)))
    meta_wayland_transaction_free (transaction);
}

void
meta_wayland_transaction_init (MetaWaylandCompositor *compositor)
{
  GQueue *transactions;

  transactions = meta_wayland_compositor_get_committed_transactions (compositor);
  g_queue_init (transactions);
}