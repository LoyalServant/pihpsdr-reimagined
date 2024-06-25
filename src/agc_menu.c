/* Copyright (C)
* 2016 - John Melton, G0ORX/N6LYT
*
*   This program is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program.  If not, see <https://www.gnu.org/licenses/>.
*
*/

#include <gtk/gtk.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "new_menu.h"
#include "agc_menu.h"
#include "agc.h"
#include "band.h"
#include "radio.h"
#include "receiver.h"
#include "vfo.h"
#include "ext.h"

static GtkWidget *dialog = NULL;

static void cleanup() {
  if (dialog != NULL) {
    GtkWidget *tmp = dialog;
    dialog = NULL;
    gtk_window_destroy(GTK_WINDOW(tmp));
    sub_menu = NULL;
    active_menu  = NO_MENU;
  }
}

static gboolean close_cb () {
  cleanup();
  return TRUE;
}

static void agc_hang_threshold_value_changed_cb(GtkWidget *widget, gpointer data) {
  active_receiver->agc_hang_threshold = (int)gtk_range_get_value(GTK_RANGE(widget));
#ifdef CLIENT_SERVER
  if (radio_is_remote) {
    send_agc_gain(client_socket, active_receiver->id, active_receiver->agc_gain, active_receiver->agc_hang,
                  active_receiver->agc_thresh, active_receiver->agc_hang_threshold);
  } else {
#endif
    set_agc(active_receiver, active_receiver->agc);
#ifdef CLIENT_SERVER
  }

#endif
}

static void agc_cb (GtkToggleButton *widget, gpointer data) {
  int val = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));
  active_receiver->agc = val;
#ifdef CLIENT_SERVER

  if (radio_is_remote) {
    send_agc(client_socket, active_receiver->id, active_receiver->agc);
  } else {
#endif
    set_agc(active_receiver, active_receiver->agc);
#ifdef CLIENT_SERVER
  }

#endif
  g_idle_add(ext_vfo_update, NULL);
}

void agc_menu(GtkWidget *parent) {
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  char title[64];
  snprintf(title, 64, "piHPSDR - AGC (RX%d VFO-%s)", active_receiver->id+1, active_receiver->id == 0 ? "A" : "B");
  gtk_window_set_title(GTK_WINDOW(dialog), title);
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);

  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
  gtk_grid_set_row_homogeneous(GTK_GRID(grid), FALSE);
  gtk_grid_set_column_spacing (GTK_GRID(grid), 5);
  gtk_grid_set_row_spacing (GTK_GRID(grid), 5);


  GtkWidget *agc_title = gtk_label_new("AGC");
  gtk_widget_set_name(agc_title, "boldlabel");
  gtk_widget_set_halign(agc_title, GTK_ALIGN_END);
  gtk_widget_show(agc_title);
  gtk_grid_attach(GTK_GRID(grid), agc_title, 0, 1, 1, 1);

  const char *items[] = {"Off", "Long", "Slow", "Medium", "Fast", NULL};
  GtkStringList *string_list = gtk_string_list_new(items);
  GtkWidget *agc_combo = gtk_drop_down_new(G_LIST_MODEL(string_list), NULL);
  gtk_drop_down_set_selected(GTK_DROP_DOWN(agc_combo), active_receiver->agc);
  g_signal_connect(agc_combo, "notify::selected", G_CALLBACK(agc_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), agc_combo, 1, 1, 1, 1);

  GtkWidget *agc_hang_threshold_label = gtk_label_new("Hang Threshold");
  gtk_widget_set_name(agc_hang_threshold_label, "boldlabel");
  gtk_widget_set_halign(agc_hang_threshold_label, GTK_ALIGN_END);
  gtk_widget_show(agc_hang_threshold_label);
  gtk_grid_attach(GTK_GRID(grid), agc_hang_threshold_label, 0, 2, 1, 1);

  GtkWidget *agc_hang_threshold_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 100.0, 1.0);
  gtk_range_set_increments (GTK_RANGE(agc_hang_threshold_scale), 1.0, 1.0);
  gtk_range_set_value (GTK_RANGE(agc_hang_threshold_scale), active_receiver->agc_hang_threshold);
  gtk_widget_show(agc_hang_threshold_scale);
  gtk_grid_attach(GTK_GRID(grid), agc_hang_threshold_scale, 1, 2, 2, 1);
  g_signal_connect(G_OBJECT(agc_hang_threshold_scale), "value_changed", G_CALLBACK(agc_hang_threshold_value_changed_cb), NULL);

  gtk_box_append(GTK_BOX(content), grid);
  sub_menu = dialog;
  gtk_widget_set_visible(dialog, TRUE);
}
