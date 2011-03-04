/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2005-2008  Marcel Holtmann <marcel@holtmann.org>
 *  Copyright (C) 2006-2010  Bastien Nocera <hadess@hadess.net>
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n-lib.h>
#include <libgnome-control-center/cc-shell.h>

#include "cc-bluetooth-panel.h"

#include <bluetooth-client.h>
#include <bluetooth-client-private.h>
#include <bluetooth-killswitch.h>
#include <bluetooth-chooser.h>
#include <bluetooth-chooser-private.h>
#include <bluetooth-plugin-manager.h>

G_DEFINE_DYNAMIC_TYPE (CcBluetoothPanel, cc_bluetooth_panel, CC_TYPE_PANEL)

#define BLUETOOTH_PANEL_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CC_TYPE_BLUETOOTH_PANEL, CcBluetoothPanelPrivate))

#define WID(s) GTK_WIDGET (gtk_builder_get_object (self->priv->builder, s))

#define GNOMECC			"gnome-control-center"
#define KEYBOARD_PREFS		GNOMECC " keyboard"
#define MOUSE_PREFS		GNOMECC " mouse"
#define SOUND_PREFS		GNOMECC " sound"
#define WIZARD			"bluetooth-wizard"

struct CcBluetoothPanelPrivate {
	GtkBuilder          *builder;
	GtkWidget           *chooser;
	BluetoothClient     *client;
	BluetoothKillswitch *killswitch;
	gboolean             debug;
};

static void cc_bluetooth_panel_finalize (GObject *object);

static void
launch_command (const char *command)
{
	GError *error = NULL;

	if (!g_spawn_command_line_async(command, &error)) {
		g_warning ("Couldn't execute command '%s': %s\n", command, error->message);
		g_error_free (error);
	}
}

static void
cc_bluetooth_panel_class_init (CcBluetoothPanelClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = cc_bluetooth_panel_finalize;

	g_type_class_add_private (klass, sizeof (CcBluetoothPanelPrivate));
}

static void
cc_bluetooth_panel_class_finalize (CcBluetoothPanelClass *klass)
{
}

static void
cc_bluetooth_panel_finalize (GObject *object)
{
	CcBluetoothPanel *self;

	bluetooth_plugin_manager_cleanup ();

	self = CC_BLUETOOTH_PANEL (object);
	if (self->priv->builder) {
		g_object_unref (self->priv->builder);
		self->priv->builder = NULL;
	}
	if (self->priv->killswitch) {
		g_object_unref (self->priv->killswitch);
		self->priv->killswitch = NULL;
	}
	if (self->priv->client) {
		g_object_unref (self->priv->client);
		self->priv->client = NULL;
	}

	G_OBJECT_CLASS (cc_bluetooth_panel_parent_class)->finalize (object);
}

typedef struct {
	char             *bdaddr;
	CcBluetoothPanel *self;
} ConnectData;

static void
connect_done (BluetoothClient  *client,
	      gboolean          success,
	      ConnectData      *data)
{
	CcBluetoothPanel *self;
	char *bdaddr;

	self = data->self;

	/* Check whether the same device is now selected */
	bdaddr = bluetooth_chooser_get_selected_device (BLUETOOTH_CHOOSER (self->priv->chooser));
	if (g_strcmp0 (bdaddr, data->bdaddr) == 0) {
		GtkSwitch *button;

		button = GTK_SWITCH (WID ("switch_connection"));
		/* Reset the switch if it failed */
		if (success == FALSE)
			gtk_switch_set_active (button, !gtk_switch_get_active (button));
		gtk_widget_set_sensitive (GTK_WIDGET (button), TRUE);
	}

	g_free (bdaddr);
	g_object_unref (data->self);
	g_free (data->bdaddr);
	g_free (data);
}

static void
switch_connected_active_changed (GtkSwitch        *button,
				 GParamSpec       *spec,
				 CcBluetoothPanel *self)
{
	char *proxy;
	GValue value = { 0, };
	ConnectData *data;

	if (bluetooth_chooser_get_selected_device_info (BLUETOOTH_CHOOSER (self->priv->chooser),
							"proxy", &value) == FALSE) {
		g_warning ("Could not get D-Bus proxy for selected device");
		return;
	}
	proxy = g_strdup (dbus_g_proxy_get_path (g_value_get_object (&value)));
	g_value_unset (&value);

	if (proxy == NULL)
		return;

	data = g_new0 (ConnectData, 1);
	data->bdaddr = bluetooth_chooser_get_selected_device (BLUETOOTH_CHOOSER (self->priv->chooser));
	data->self = g_object_ref (self);

	if (gtk_switch_get_active (button)) {
		bluetooth_client_connect_service (self->priv->client, proxy,
						  (BluetoothClientConnectFunc) connect_done, data);
	} else {
		bluetooth_client_disconnect_service (self->priv->client, proxy,
						     (BluetoothClientConnectFunc) connect_done, data);
	}

	/* FIXME: make a note somewhere that the button for that
	 * device should be disabled */
	gtk_widget_set_sensitive (GTK_WIDGET (button), FALSE);
	g_free (proxy);
}

static void
dump_current_device (CcBluetoothPanel *self)
{
	GtkWidget *tree;
	GtkTreeModel *model;
	GtkTreeSelection *selection;
	GtkTreeIter iter;

	tree = bluetooth_chooser_get_treeview (BLUETOOTH_CHOOSER (self->priv->chooser));
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (tree));
	gtk_tree_selection_get_selected (selection, &model, &iter);

	bluetooth_client_dump_device (model, &iter, FALSE);
}

static void
cc_bluetooth_panel_update_properties (CcBluetoothPanel *self)
{
	char *bdaddr;
	GtkSwitch *button;

	button = GTK_SWITCH (WID ("switch_connection"));
	g_signal_handlers_block_by_func (button, switch_connected_active_changed, self);

	/* Hide all the buttons now, and show them again if we need to */
	gtk_widget_hide (WID ("keyboard_button"));
	gtk_widget_hide (WID ("sound_button"));
	gtk_widget_hide (WID ("mouse_button"));

	bdaddr = bluetooth_chooser_get_selected_device (BLUETOOTH_CHOOSER (self->priv->chooser));
	if (bdaddr == NULL) {
		gtk_widget_set_sensitive (WID ("properties_vbox"), FALSE);
		gtk_switch_set_active (button, FALSE);
		gtk_label_set_text (GTK_LABEL (WID ("paired_label")), "");
		gtk_label_set_text (GTK_LABEL (WID ("type_label")), "");
		gtk_label_set_text (GTK_LABEL (WID ("address_label")), "");
		gtk_widget_set_sensitive (WID ("button_delete"), FALSE);
	} else {
		BluetoothType type;
		gboolean connected;
		GValue value = { 0 };
		GHashTable *services;

		if (self->priv->debug)
			dump_current_device (self);

		gtk_widget_set_sensitive (WID ("properties_vbox"), TRUE);

		connected = bluetooth_chooser_get_selected_device_is_connected (BLUETOOTH_CHOOSER (self->priv->chooser));
		gtk_switch_set_active (button, connected);

		/* Paired */
		bluetooth_chooser_get_selected_device_info (BLUETOOTH_CHOOSER (self->priv->chooser),
							    "paired", &value);
		gtk_label_set_text (GTK_LABEL (WID ("paired_label")),
				    g_value_get_boolean (&value) ? _("Yes") : _("No"));
		g_value_unset (&value);

		/* Connection */
		bluetooth_chooser_get_selected_device_info (BLUETOOTH_CHOOSER (self->priv->chooser),
							    "services", &value);
		services = g_value_get_boxed (&value);
		gtk_widget_set_sensitive (GTK_WIDGET (button), (services != NULL));
		g_value_unset (&value);

		/* Type */
		type = bluetooth_chooser_get_selected_device_type (BLUETOOTH_CHOOSER (self->priv->chooser));
		gtk_label_set_text (GTK_LABEL (WID ("type_label")), bluetooth_type_to_string (type));
		switch (type) {
		case BLUETOOTH_TYPE_KEYBOARD:
			gtk_widget_show (WID ("keyboard_button"));
			break;
		case BLUETOOTH_TYPE_MOUSE:
			/* FIXME what about Bluetooth touchpads */
			gtk_widget_show (WID ("mouse_button"));
			break;
		case BLUETOOTH_TYPE_HEADSET:
		case BLUETOOTH_TYPE_HEADPHONES:
		case BLUETOOTH_TYPE_OTHER_AUDIO:
			gtk_widget_show (WID ("sound_button"));
		default:
			/* FIXME others? */
			;
		}

		gtk_label_set_text (GTK_LABEL (WID ("address_label")), bdaddr);
		g_free (bdaddr);

		gtk_widget_set_sensitive (WID ("button_delete"), TRUE);
	}

	g_signal_handlers_unblock_by_func (button, switch_connected_active_changed, self);
}

static void
power_callback (GObject          *object,
		GParamSpec       *spec,
		CcBluetoothPanel *self)
{
	gboolean state;

	state = gtk_switch_get_active (GTK_SWITCH (WID ("switch_bluetooth")));
	g_debug ("Power switched to %s", state ? "off" : "on");
	bluetooth_killswitch_set_state (self->priv->killswitch,
					state ? KILLSWITCH_STATE_UNBLOCKED : KILLSWITCH_STATE_SOFT_BLOCKED);
}

static void
cc_bluetooth_panel_update_treeview_message (CcBluetoothPanel *self,
					    const char       *message)
{
	if (message != NULL) {
		gtk_widget_hide (self->priv->chooser);
		gtk_widget_show (WID ("message_scrolledwindow"));

		gtk_label_set_text (GTK_LABEL (WID ("message_label")),
				    message);
	} else {
		gtk_widget_hide (WID ("message_scrolledwindow"));
		gtk_widget_show (self->priv->chooser);
	}
}

static void
cc_bluetooth_panel_update_power (CcBluetoothPanel *self)
{
	KillswitchState state;
	char *bdaddr;
	gboolean powered, sensitive;
	GtkSwitch *button;

	g_object_get (G_OBJECT (self->priv->client),
		      "default-adapter", &bdaddr,
		      "default-adapter-powered", &powered,
		      NULL);
	state = bluetooth_killswitch_get_state (self->priv->killswitch);

	g_debug ("Updating power, default adapter: %s (powered: %s), killswitch: %s",
		 bdaddr ? bdaddr : "(none)",
		 powered ? "on" : "off",
		 bluetooth_killswitch_state_to_string (state));

	if (bdaddr == NULL &&
	    bluetooth_killswitch_has_killswitches (self->priv->killswitch) &&
	    state != KILLSWITCH_STATE_HARD_BLOCKED) {
		g_debug ("Default adapter is unpowered, but should be available");
		sensitive = TRUE;
		cc_bluetooth_panel_update_treeview_message (self, _("Bluetooth is disabled"));
	} else if (bdaddr == NULL &&
		   state == KILLSWITCH_STATE_HARD_BLOCKED) {
		g_debug ("Bluetooth is Hard blocked");
		sensitive = FALSE;
		cc_bluetooth_panel_update_treeview_message (self, _("Bluetooth is disabled by hardware switch"));
	} else if (bdaddr == NULL) {
		sensitive = FALSE;
		g_debug ("No Bluetooth available");
		cc_bluetooth_panel_update_treeview_message (self, _("No Bluetooth adapters found"));
	} else {
		sensitive = TRUE;
		g_debug ("Bluetooth is available and powered");
		cc_bluetooth_panel_update_treeview_message (self, NULL);
	}

	g_free (bdaddr);
	gtk_widget_set_sensitive (WID ("hbox2") , sensitive);

	button = GTK_SWITCH (WID ("switch_bluetooth"));

	g_signal_handlers_block_by_func (button, power_callback, self);
	gtk_switch_set_active (button, powered);
	g_signal_handlers_unblock_by_func (button, power_callback, self);
}

static void
keyboard_callback (GtkButton        *button,
		   CcBluetoothPanel *self)
{
	launch_command(KEYBOARD_PREFS);
}

static void
mouse_callback (GtkButton        *button,
		CcBluetoothPanel *self)
{
	launch_command(MOUSE_PREFS);
}

static void
sound_callback (GtkButton        *button,
		CcBluetoothPanel *self)
{
	launch_command(SOUND_PREFS);
}

/* Visibility/Discoverable */
static void
switch_discoverable_active_changed (GtkSwitch        *button,
				    GParamSpec       *spec,
				    CcBluetoothPanel *self)
{
	bluetooth_client_set_discoverable (self->priv->client,
					   gtk_switch_get_active (button),
					   0);
}

static void
cc_bluetooth_panel_update_visibility (CcBluetoothPanel *self)
{
	gboolean discoverable;
	GtkSwitch *button;
	char *name;

	button = GTK_SWITCH (WID ("switch_discoverable"));
	discoverable = bluetooth_client_get_discoverable (self->priv->client);
	g_signal_handlers_block_by_func (button, switch_discoverable_active_changed, self);
	gtk_switch_set_active (button, discoverable);
	g_signal_handlers_unblock_by_func (button, switch_discoverable_active_changed, self);

	name = bluetooth_client_get_name (self->priv->client);
	if (name == NULL) {
		gtk_widget_set_sensitive (WID ("switch_discoverable"), FALSE);
		gtk_widget_set_sensitive (WID ("visible_label"), FALSE);
		gtk_label_set_text (GTK_LABEL (WID ("visible_label")), _("Visibility"));
	} else {
		char *label;

		label = g_strdup_printf (_("Visibility of “%s”"), name);
		g_free (name);
		gtk_label_set_text (GTK_LABEL (WID ("visible_label")), label);
		g_free (label);

		gtk_widget_set_sensitive (WID ("switch_discoverable"), TRUE);
		gtk_widget_set_sensitive (WID ("visible_label"), TRUE);
	}
}

static void
discoverable_changed (BluetoothClient *client,
		      GParamSpec       *spec,
		      CcBluetoothPanel *self)
{
	cc_bluetooth_panel_update_visibility (self);
}

static void
device_selected_changed (BluetoothChooser *chooser,
			 GParamSpec       *spec,
			 CcBluetoothPanel *self)
{
	cc_bluetooth_panel_update_properties (self);
}

/* Treeview buttons */
static void
delete_clicked (GtkToolButton    *button,
		CcBluetoothPanel *self)
{
	char *address;

	address = bluetooth_chooser_get_selected_device (BLUETOOTH_CHOOSER (self->priv->chooser));
	g_assert (address);

	if (bluetooth_chooser_remove_selected_device (BLUETOOTH_CHOOSER (self->priv->chooser)))
		bluetooth_plugin_manager_device_deleted (address);

	g_free (address);
}

static void
setup_clicked (GtkToolButton    *button,
	       CcBluetoothPanel *self)
{
	launch_command (WIZARD);
}

/* Overall device state */
static void
cc_bluetooth_panel_update_state (CcBluetoothPanel *self)
{
	char *bdaddr;
	gboolean powered;

	g_object_get (G_OBJECT (self->priv->client),
		      "default-adapter", &bdaddr,
		      "default-adapter-powered", &powered,
		      NULL);
	gtk_widget_set_sensitive (WID ("toolbar"), (bdaddr != NULL));
	g_free (bdaddr);
}

static void
default_adapter_changed (BluetoothClient  *client,
			 GParamSpec       *spec,
			 CcBluetoothPanel *self)
{
	cc_bluetooth_panel_update_state (self);
	cc_bluetooth_panel_update_power (self);
}

static void
killswitch_changed (BluetoothKillswitch *killswitch,
		    KillswitchState      state,
		    CcBluetoothPanel    *self)
{
	cc_bluetooth_panel_update_state (self);
	cc_bluetooth_panel_update_power (self);
}

static void
cc_bluetooth_panel_init (CcBluetoothPanel *self)
{
	GtkWidget *widget;
	GError *error = NULL;
	GtkTreeViewColumn *column;
	GtkStyleContext *context;

	self->priv = BLUETOOTH_PANEL_PRIVATE (self);

	bluetooth_plugin_manager_init ();
	self->priv->killswitch = bluetooth_killswitch_new ();
	self->priv->client = bluetooth_client_new ();
	self->priv->debug = g_getenv ("BLUETOOTH_DEBUG") != NULL;

	self->priv->builder = gtk_builder_new ();
	gtk_builder_add_from_file (self->priv->builder,
				   PKGDATADIR "/bluetooth.ui",
				   &error);
	if (error != NULL) {
		g_warning ("Failed to load '%s': %s", PKGDATADIR "/bluetooth.ui", error->message);
		g_error_free (error);
		return;
	}

	widget = WID ("vbox");
	gtk_widget_reparent (widget, GTK_WIDGET (self));

	/* Overall device state */
	cc_bluetooth_panel_update_state (self);
	g_signal_connect (G_OBJECT (self->priv->client), "notify::default-adapter",
			  G_CALLBACK (default_adapter_changed), self);

	/* The discoverable button */
	cc_bluetooth_panel_update_visibility (self);
	g_signal_connect (G_OBJECT (self->priv->client), "notify::default-adapter-discoverable",
			  G_CALLBACK (discoverable_changed), self);
	g_signal_connect (G_OBJECT (WID ("switch_discoverable")), "notify::active",
			  G_CALLBACK (switch_discoverable_active_changed), self);

	/* The known devices */
	widget = WID ("devices_table");

	context = gtk_widget_get_style_context (WID ("message_scrolledwindow"));
	gtk_style_context_set_junction_sides (context, GTK_JUNCTION_BOTTOM);

	/* Note that this will only ever show the devices on the default
	 * adapter, this is on purpose */
	self->priv->chooser = bluetooth_chooser_new (NULL);
	gtk_box_pack_start (GTK_BOX (WID ("box1")), self->priv->chooser, TRUE, TRUE, 0);
	g_object_set (self->priv->chooser,
		      "show-searching", FALSE,
		      "show-device-type", FALSE,
		      "show-device-category", FALSE,
		      "show-pairing", FALSE,
		      "show-connected", FALSE,
		      "device-category-filter", BLUETOOTH_CATEGORY_PAIRED_OR_TRUSTED,
		      "no-show-all", TRUE,
		      NULL);
	column = bluetooth_chooser_get_type_column (BLUETOOTH_CHOOSER (self->priv->chooser));
	gtk_tree_view_column_set_visible (column, FALSE);
	widget = bluetooth_chooser_get_treeview (BLUETOOTH_CHOOSER (self->priv->chooser));
	g_object_set (G_OBJECT (widget), "headers-visible", FALSE, NULL);

	/* Join treeview and buttons */
	widget = bluetooth_chooser_get_scrolled_window (BLUETOOTH_CHOOSER (self->priv->chooser));
	gtk_scrolled_window_set_min_content_height (GTK_SCROLLED_WINDOW (widget), 250);
	context = gtk_widget_get_style_context (widget);
	gtk_style_context_set_junction_sides (context, GTK_JUNCTION_BOTTOM);
	widget = WID ("toolbar");
	context = gtk_widget_get_style_context (widget);
	gtk_style_context_set_junction_sides (context, GTK_JUNCTION_TOP);

	g_signal_connect (G_OBJECT (self->priv->chooser), "notify::device-selected",
			  G_CALLBACK (device_selected_changed), self);
	g_signal_connect (G_OBJECT (WID ("button_delete")), "clicked",
			  G_CALLBACK (delete_clicked), self);
	g_signal_connect (G_OBJECT (WID ("button_setup")), "clicked",
			  G_CALLBACK (setup_clicked), self);

	/* Set the initial state of the properties */
	cc_bluetooth_panel_update_properties (self);
	g_signal_connect (G_OBJECT (WID ("mouse_button")), "clicked",
			  G_CALLBACK (mouse_callback), self);
	g_signal_connect (G_OBJECT (WID ("keyboard_button")), "clicked",
			  G_CALLBACK (keyboard_callback), self);
	g_signal_connect (G_OBJECT (WID ("sound_button")), "clicked",
			  G_CALLBACK (sound_callback), self);
	g_signal_connect (G_OBJECT (WID ("switch_connection")), "notify::active",
			  G_CALLBACK (switch_connected_active_changed), self);

	/* Set the initial state of power */
	g_signal_connect (G_OBJECT (WID ("switch_bluetooth")), "notify::active",
			  G_CALLBACK (power_callback), self);
	g_signal_connect (G_OBJECT (self->priv->killswitch), "state-changed",
			  G_CALLBACK (killswitch_changed), self);
	cc_bluetooth_panel_update_power (self);

	gtk_widget_show_all (GTK_WIDGET (self));
}

void
cc_bluetooth_panel_register (GIOModule *module)
{
	cc_bluetooth_panel_register_type (G_TYPE_MODULE (module));
	g_io_extension_point_implement (CC_SHELL_PANEL_EXTENSION_POINT,
					CC_TYPE_BLUETOOTH_PANEL,
					"bluetooth", 0);
}

/* GIO extension stuff */
void
g_io_module_load (GIOModule *module)
{
	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

	/* register the panel */
	cc_bluetooth_panel_register (module);
}

void
g_io_module_unload (GIOModule *module)
{
}

