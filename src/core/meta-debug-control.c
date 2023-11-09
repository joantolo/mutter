/*
 * Copyright (C) 2023 Red Hat
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include "core/meta-debug-control.h"

#include "core/util-private.h"
#include "meta/meta-backend.h"
#include "meta/meta-context.h"

enum
{
  PROP_0,

  PROP_CONTEXT,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

#define META_DEBUG_CONTROL_DBUS_SERVICE "org.gnome.Mutter.DebugControl"
#define META_DEBUG_CONTROL_DBUS_PATH "/org/gnome/Mutter/DebugControl"

struct _MetaDebugControl
{
  MetaDBusDebugControlSkeleton parent;

  MetaContext *context;

  guint dbus_name_id;
};

static void meta_dbus_debug_control_iface_init (MetaDBusDebugControlIface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaDebugControl,
                         meta_debug_control,
                         META_DBUS_TYPE_DEBUG_CONTROL_SKELETON,
                         G_IMPLEMENT_INTERFACE (META_DBUS_TYPE_DEBUG_CONTROL,
                                                meta_dbus_debug_control_iface_init))

static void
meta_dbus_debug_control_iface_init (MetaDBusDebugControlIface *iface)
{
}

static void
on_bus_acquired (GDBusConnection *connection,
                 const char      *name,
                 gpointer         user_data)
{
  MetaDebugControl *debug_control = META_DEBUG_CONTROL (user_data);
  g_autoptr (GError) error = NULL;

  meta_topic (META_DEBUG_BACKEND,
              "Acquired D-Bus name '%s', exporting service on '%s'",
              META_DEBUG_CONTROL_DBUS_SERVICE, META_DEBUG_CONTROL_DBUS_PATH);

  if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (debug_control),
                                         connection,
                                         META_DEBUG_CONTROL_DBUS_PATH,
                                         &error))
    {
      g_warning ("Failed to export '%s' object on '%s': %s",
                 META_DEBUG_CONTROL_DBUS_SERVICE,
                 META_DEBUG_CONTROL_DBUS_PATH,
                 error->message);
    }
}

static void
on_enable_hdr_changed (MetaDebugControl *debug_control,
                       GParamSpec       *pspec)
{
  MetaDBusDebugControl *dbus_debug_control =
    META_DBUS_DEBUG_CONTROL (debug_control);
  MetaBackend *backend = meta_context_get_backend (debug_control->context);
  MetaMonitorManager *monitor_manager = meta_backend_get_monitor_manager (backend);
  gboolean enable;

  enable = meta_dbus_debug_control_get_enable_hdr (dbus_debug_control);
  g_object_set (G_OBJECT (monitor_manager),
                "experimental-hdr", enable ? "on" : "off",
                NULL);
}

static ClutterColorEncoding
validate_color_encoding (ClutterColorEncoding color_encoding)
{
  switch (color_encoding)
    {
    case CLUTTER_COLOR_ENCODING_OPTICAL:
    case CLUTTER_COLOR_ENCODING_ELECTRICAL:
      return color_encoding;
    }

  if (color_encoding == -1)
    return -1;

  g_warning ("Invalid color encoding %d", color_encoding);
  return -1;
}

static void
on_force_color_encoding_changed (MetaDebugControl *debug_control,
                                 GParamSpec       *pspec)
{
  MetaDBusDebugControl *dbus_debug_control =
    META_DBUS_DEBUG_CONTROL (debug_control);
  ClutterColorEncoding color_encoding;
  ClutterColorEncoding validated_color_encoding;

  color_encoding =
    meta_dbus_debug_control_get_force_color_encoding (dbus_debug_control);
  validated_color_encoding = validate_color_encoding (color_encoding);

  if (color_encoding != validated_color_encoding)
    {
      meta_dbus_debug_control_set_force_color_encoding (dbus_debug_control,
                                                        validated_color_encoding);
    }
}

static void
on_experimental_hdr_changed (MetaMonitorManager *monitor_manager,
                             GParamSpec         *pspec,
                             MetaDebugControl   *debug_control)
{
  MetaDBusDebugControl *dbus_debug_control =
    META_DBUS_DEBUG_CONTROL (debug_control);
  g_autofree char *experimental_hdr = NULL;
  gboolean enable;

  g_object_get (G_OBJECT (monitor_manager),
                "experimental-hdr", &experimental_hdr,
                NULL);
  
  enable = g_strcmp0 (experimental_hdr, "on") == 0;
  if (enable == meta_dbus_debug_control_get_enable_hdr (dbus_debug_control))
    return;

  meta_dbus_debug_control_set_enable_hdr (META_DBUS_DEBUG_CONTROL (debug_control),
                                          g_strcmp0 (experimental_hdr, "on") == 0);
}

static void
on_context_started (MetaContext      *context,
                    MetaDebugControl *debug_control)
{
  MetaBackend *backend = meta_context_get_backend (context);
  MetaMonitorManager *monitor_manager = meta_backend_get_monitor_manager (backend);

  g_signal_connect (monitor_manager, "notify::experimental-hdr",
                    G_CALLBACK (on_experimental_hdr_changed),
                    debug_control);
}

static void
meta_debug_control_constructed (GObject *object)
{
  MetaDebugControl *debug_control = META_DEBUG_CONTROL (object);

  g_signal_connect_object (debug_control->context, "started",
                           G_CALLBACK (on_context_started), debug_control,
                           G_CONNECT_DEFAULT);

  g_signal_connect_object (debug_control, "notify::enable-hdr",
                           G_CALLBACK (on_enable_hdr_changed), debug_control,
                           G_CONNECT_DEFAULT);

  g_signal_connect_object (debug_control, "notify::force-color-encoding",
                           G_CALLBACK (on_force_color_encoding_changed), debug_control,
                           G_CONNECT_DEFAULT);

  G_OBJECT_CLASS (meta_debug_control_parent_class)->constructed (object);
}

static void
meta_debug_control_dispose (GObject *object)
{
  MetaDebugControl *debug_control = META_DEBUG_CONTROL (object);

  g_clear_handle_id (&debug_control->dbus_name_id, g_bus_unown_name);

  G_OBJECT_CLASS (meta_debug_control_parent_class)->dispose (object);
}

static void
meta_debug_control_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  MetaDebugControl *debug_control = META_DEBUG_CONTROL (object);

  switch (prop_id)
    {
    case PROP_CONTEXT:
      debug_control->context = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_debug_control_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  MetaDebugControl *debug_control = META_DEBUG_CONTROL (object);

  switch (prop_id)
    {
    case PROP_CONTEXT:
      g_value_set_object (value, debug_control->context);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_debug_control_class_init (MetaDebugControlClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = meta_debug_control_constructed;
  object_class->dispose = meta_debug_control_dispose;
  object_class->set_property = meta_debug_control_set_property;
  object_class->get_property = meta_debug_control_get_property;

  obj_props[PROP_CONTEXT] = g_param_spec_object ("context", NULL, NULL,
                                                 META_TYPE_CONTEXT,
                                                 G_PARAM_CONSTRUCT_ONLY |
                                                 G_PARAM_READWRITE |
                                                 G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);
}

static void
meta_debug_control_init (MetaDebugControl *debug_control)
{
  MetaDBusDebugControl *dbus_debug_control =
    META_DBUS_DEBUG_CONTROL (debug_control);

  meta_dbus_debug_control_set_force_color_encoding (dbus_debug_control, -1);
}

gboolean
meta_debug_control_get_forced_color_encoding (MetaDebugControl     *debug_control,
                                              ClutterColorEncoding *forced_color_encoding)
{
  MetaDBusDebugControl *dbus_debug_control =
    META_DBUS_DEBUG_CONTROL (debug_control);
  const char *forced_color_encoding_env;
  ClutterColorEncoding force_color_encoding;

  forced_color_encoding_env = getenv ("MUTTER_DEBUG_FORCE_COLOR_ENCODING");
  if (g_strcmp0 (forced_color_encoding_env, "optical") == 0)
    return CLUTTER_COLOR_ENCODING_OPTICAL;
  else if (g_strcmp0 (forced_color_encoding_env, "electrical") == 0)
    return CLUTTER_COLOR_ENCODING_ELECTRICAL;

  force_color_encoding =
    meta_dbus_debug_control_get_force_color_encoding (dbus_debug_control);
  if (force_color_encoding != -1)
    {
      *forced_color_encoding = force_color_encoding;
      return TRUE;
    }
  else
    {
      return FALSE;
    }
}

void
meta_debug_control_export (MetaDebugControl *debug_control)
{
  debug_control->dbus_name_id =
    g_bus_own_name (G_BUS_TYPE_SESSION,
                    META_DEBUG_CONTROL_DBUS_SERVICE,
                    G_BUS_NAME_OWNER_FLAGS_NONE,
                    on_bus_acquired,
                    NULL,
                    NULL,
                    debug_control,
                    NULL);
}
