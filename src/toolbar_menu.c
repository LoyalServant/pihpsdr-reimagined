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
#include <string.h>

#include "radio.h"
#include "new_menu.h"
#include "actions.h"
#include "action_dialog.h"
#include "gpio.h"
#include "toolbar.h"

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

static void update_switch_button_label(GtkWidget *button, int new_function) {
    const gchar *new_label = ActionTable[new_function].button_str;
    gtk_button_set_label(GTK_BUTTON(button), new_label);
}

static void switch_cb(GtkWidget *widget, gpointer data) {
    SWITCH *sw = (SWITCH *)data;
    select_action_dialog(dialog, CONTROLLER_SWITCH, sw, NULL, -1, G_CALLBACK(update_switch_button_label), widget);
}

void toolbar_menu(GtkWidget *parent) {
  GtkWidget *widget;
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  gtk_window_set_title(GTK_WINDOW(dialog), "piHPSDR reimagined - Toolbar configuration");
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
  gtk_grid_set_row_homogeneous(GTK_GRID(grid), FALSE);
  gtk_grid_set_column_spacing (GTK_GRID(grid), 15);
  gtk_grid_set_row_spacing (GTK_GRID(grid), 15);
  gtk_widget_set_margin_start(grid, 15);
  gtk_widget_set_margin_end(grid, 15);
  gtk_widget_set_margin_top(grid, 15);
  gtk_widget_set_margin_bottom(grid, 15);
  int lfunction = 0;
  const int max_switches = 8;

  for (lfunction = 0; lfunction < MAX_FUNCTIONS; lfunction++) {
    SWITCH *sw = switches_controller1[lfunction];

    for (int i = 0; i < max_switches; i++) {
      if (i == max_switches - 1) {
        // Rightmost switch is hardwired to FUNCTION
        sw[i].switch_function = FUNCTION;
        gchar text[16];
        snprintf(text, 16, "FNC(%d)", lfunction);
        widget = gtk_button_new_with_label(text);
        gtk_grid_attach(GTK_GRID(grid), widget, i, lfunction + 1, 1, 1);
      } else {
        widget = gtk_button_new_with_label(ActionTable[sw[i].switch_function].button_str);
        gtk_grid_attach(GTK_GRID(grid), widget, i, lfunction + 1, 1, 1);
        g_signal_connect(widget, "clicked", G_CALLBACK(switch_cb), (gpointer) &sw[i]);
      }
    }
  }

  gtk_box_append(GTK_BOX(content), grid);
  sub_menu = dialog;
  gtk_widget_show(dialog);
}
