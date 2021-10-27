/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2005-2008  Marcel Holtmann <marcel@holtmann.org>
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

#include <gtk/gtk.h>

#include "bluetooth-client.h"
#include "bluetooth-client-private.h"
#include "gnome-bluetooth-enum-types.h"
#include "bluetooth-utils.h"

static BluetoothClient *client;
static GtkTreeSelection *selection;

static void scan_callback(GtkWidget *button, gpointer user_data)
{
	g_object_set (G_OBJECT (client), "default-adapter-discovering", TRUE, NULL);
}

static void select_callback(GtkTreeSelection *selection, gpointer user_data)
{
	GtkTreeModel *model;
	GtkTreeIter iter;

	if (gtk_tree_selection_get_selected(selection, &model, &iter) == FALSE) {
		g_print ("No devices selected");
		return;
	}

	bluetooth_client_dump_device (model, &iter);
}

static void row_inserted(GtkTreeModel *model, GtkTreePath *path,
				GtkTreeIter *iter, gpointer user_data)
{
	GtkTreeView *tree = user_data;

	gtk_tree_view_expand_all(tree);
}

static void proxy_to_text(GtkTreeViewColumn *column, GtkCellRenderer *cell,
		GtkTreeModel *model, GtkTreeIter *iter, gpointer user_data)
{
	GDBusProxy *proxy;
	gchar *path;

	gtk_tree_model_get(model, iter, BLUETOOTH_COLUMN_PROXY, &proxy, -1);

	if (proxy == NULL) {
		g_object_set(cell, "text", "", NULL);
		return;
	}

	path = g_path_get_basename(g_dbus_proxy_get_object_path(proxy));

	g_object_set(cell, "text", path, NULL);

	g_free(path);

	g_object_unref(proxy);
}

static void type_to_text(GtkTreeViewColumn *column, GtkCellRenderer *cell,
		GtkTreeModel *model, GtkTreeIter *iter, gpointer user_data)
{
	guint type;

	gtk_tree_model_get(model, iter, BLUETOOTH_COLUMN_TYPE, &type, -1);

	g_object_set(cell, "text", bluetooth_type_to_string(type), NULL);
}

static void
legacypairing_to_text(GtkTreeViewColumn *column, GtkCellRenderer *cell,
		      GtkTreeModel *model, GtkTreeIter *iter, gpointer user_data)
{
	gint legacypairing;

	gtk_tree_model_get(model, iter, BLUETOOTH_COLUMN_LEGACYPAIRING, &legacypairing, -1);

	switch (legacypairing) {
	case -1:
		g_object_set(cell, "text", "UNSET", NULL);
		break;
	case 0:
		g_object_set(cell, "text", "FALSE", NULL);
		break;
	default:
		g_object_set(cell, "text", "TRUE", NULL);
	}
}

static void uuids_to_text(GtkTreeViewColumn *column, GtkCellRenderer *cell,
			  GtkTreeModel *model, GtkTreeIter *iter, gpointer user_data)
{
	char **uuids;
	char *str;

	gtk_tree_model_get(model, iter, BLUETOOTH_COLUMN_UUIDS, &uuids, -1);
	if (uuids == NULL)
		str = NULL;
	else
		str = g_strjoinv (", ", uuids);
	g_strfreev (uuids);

	g_object_set(cell, "text", str, NULL);
	g_free (str);
}

static void create_window(void)
{
	GtkWidget *window;
	GtkWidget *vbox;
	GtkWidget *toolbar;
	GtkWidget *refresh_button;
	GtkWidget *scrolled;
	GtkWidget *tree;
	GtkTreeModel *model;
	GtkTreeModel *sorted;
	GtkWidget *statusbar;

	window = gtk_window_new();
	gtk_window_set_title(GTK_WINDOW(window), "Test client");
	gtk_window_set_icon_name(GTK_WINDOW(window), "bluetooth");
	gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);

	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_window_set_child(GTK_WINDOW(window), vbox);

	toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_append(GTK_BOX(vbox), toolbar);

	refresh_button = gtk_button_new_from_icon_name ("view-refresh");
	gtk_box_append(GTK_BOX(toolbar), refresh_button);
	g_signal_connect(refresh_button, "clicked", G_CALLBACK(scan_callback), NULL);

	scrolled = gtk_scrolled_window_new(NULL, NULL);
	gtk_widget_set_vexpand(scrolled, TRUE);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_box_append (GTK_BOX (vbox), scrolled);

	tree = gtk_tree_view_new();
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tree), TRUE);
	gtk_widget_grab_focus(GTK_WIDGET(tree));
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), tree);

	gtk_tree_view_insert_column_with_data_func(GTK_TREE_VIEW(tree), -1,
					"Proxy", gtk_cell_renderer_text_new(),
						proxy_to_text, NULL, NULL);

	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1,
					"Address", gtk_cell_renderer_text_new(),
					"text", BLUETOOTH_COLUMN_ADDRESS, NULL);

	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1,
					"Alias", gtk_cell_renderer_text_new(),
					"text", BLUETOOTH_COLUMN_ALIAS, NULL);

	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1,
					"Name", gtk_cell_renderer_text_new(),
					"text", BLUETOOTH_COLUMN_NAME, NULL);

	gtk_tree_view_insert_column_with_data_func(GTK_TREE_VIEW(tree), -1,
					"Type", gtk_cell_renderer_text_new(),
						type_to_text, NULL, NULL);

	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1,
					"Icon", gtk_cell_renderer_text_new(),
					"text", BLUETOOTH_COLUMN_ICON, NULL);

	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1,
					"Default", gtk_cell_renderer_text_new(),
					"text", BLUETOOTH_COLUMN_DEFAULT, NULL);

	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1,
					"Paired", gtk_cell_renderer_text_new(),
					"text", BLUETOOTH_COLUMN_PAIRED, NULL);

	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1,
					"Trusted", gtk_cell_renderer_text_new(),
					"text", BLUETOOTH_COLUMN_TRUSTED, NULL);

	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1,
					"Connected", gtk_cell_renderer_text_new(),
					"text", BLUETOOTH_COLUMN_CONNECTED, NULL);

	gtk_tree_view_insert_column_with_data_func(GTK_TREE_VIEW(tree), -1,
					"Legacy Pairing", gtk_cell_renderer_text_new(),
						legacypairing_to_text, NULL, NULL);

	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1,
					"Discoverable", gtk_cell_renderer_text_new(),
					"text", BLUETOOTH_COLUMN_DISCOVERABLE, NULL);

	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1,
					"Discovering", gtk_cell_renderer_text_new(),
					"text", BLUETOOTH_COLUMN_DISCOVERING, NULL);

	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1,
					"Powered", gtk_cell_renderer_text_new(),
					"text", BLUETOOTH_COLUMN_POWERED, NULL);

	gtk_tree_view_insert_column_with_data_func(GTK_TREE_VIEW(tree), -1,
					"UUIDs", gtk_cell_renderer_text_new(),
						uuids_to_text, NULL, NULL);

	model = bluetooth_client_get_model(client);
	sorted = gtk_tree_model_sort_new_with_model(model);
	gtk_tree_view_set_model(GTK_TREE_VIEW(tree), sorted);
	g_signal_connect(G_OBJECT(model), "row-inserted",
					G_CALLBACK(row_inserted), tree);
	g_object_unref(sorted);
	g_object_unref(model);

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
	gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
	g_signal_connect(G_OBJECT(selection), "changed",
				G_CALLBACK(select_callback), NULL);

	gtk_tree_view_expand_all(GTK_TREE_VIEW(tree));

	statusbar = gtk_statusbar_new();
	gtk_box_append(GTK_BOX(vbox), statusbar);

	gtk_window_present(GTK_WINDOW(window));
}

static void
default_adapter_changed (GObject    *gobject,
			 GParamSpec *pspec,
			 gpointer    user_data)
{
	char *adapter;

	g_object_get (G_OBJECT (gobject), "default-adapter", &adapter, NULL);
	g_message ("Default adapter changed: %s", adapter ? adapter : "(none)");
	g_free (adapter);
}

static void
default_adapter_powered_changed (GObject    *gobject,
				 GParamSpec *pspec,
				 gpointer    user_data)
{
	gboolean powered;

	g_object_get (G_OBJECT (gobject), "default-adapter-powered", &powered, NULL);
	g_message ("Default adapter is %s", powered ? "powered" : "switched off");
}

static void
default_adapter_discovering_changed (GObject    *gobject,
				     GParamSpec *pspec,
				     gpointer    user_data)
{
	gboolean discovering;

	g_object_get (G_OBJECT (gobject), "default-adapter-discovering", &discovering, NULL);
	g_message ("Default adapter is %s", discovering ? "discovering" : "not discovering");
}

int main(int argc, char *argv[])
{
	GLogLevelFlags fatal_mask;

	gtk_init();

	fatal_mask = g_log_set_always_fatal (G_LOG_FATAL_MASK);
	fatal_mask |= G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL;
	g_log_set_always_fatal (fatal_mask);

	client = bluetooth_client_new();
	g_signal_connect (G_OBJECT (client), "notify::default-adapter",
			  G_CALLBACK (default_adapter_changed), NULL);
	g_signal_connect (G_OBJECT (client), "notify::default-adapter-powered",
			  G_CALLBACK (default_adapter_powered_changed), NULL);
	g_signal_connect (G_OBJECT (client), "notify::default-adapter-discovering",
			  G_CALLBACK (default_adapter_discovering_changed), NULL);

	default_adapter_changed (G_OBJECT (client), NULL, NULL);
	default_adapter_powered_changed (G_OBJECT (client), NULL, NULL);

	create_window();

	while (g_list_model_get_n_items (gtk_window_get_toplevels()) > 0)
		g_main_context_iteration (NULL, TRUE);

	g_object_unref(client);

	return 0;
}
