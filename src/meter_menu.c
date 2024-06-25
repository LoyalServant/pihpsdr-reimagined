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
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include <wdsp.h>

#include "new_menu.h"
#include "receiver.h"
#include "meter_menu.h"
#include "meter.h"
#include "radio.h"

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

static void smeter_select_cb (GtkToggleButton *widget, gpointer data) {
  int val = gtk_drop_down_get_selected(GTK_DROP_DOWN(widget));

  switch (val) {
  case 0:
    smeter = RXA_S_PK;
    break;

  case 1:
    smeter = RXA_S_AV;
    break;
  }
}

static void analog_cb (GtkToggleButton *widget, gpointer data) {
  analog_meter = gtk_drop_down_get_selected(GTK_DROP_DOWN(widget));
}

static void alc_select_cb(GtkToggleButton *widget, gpointer data) {
  int val = gtk_drop_down_get_selected(GTK_DROP_DOWN(widget));

  switch (val) {
  case 0:
    alc = TXA_ALC_PK;
    break;

  case 1:
    alc = TXA_ALC_AV;
    break;

  case 2:
    alc = TXA_ALC_GAIN;
    break;
  }
}

void meter_menu (GtkWidget *parent) {
  GtkWidget *w;
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  gtk_window_set_title(GTK_WINDOW(dialog), "piHPSDR reimagined - Meter");
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);

  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_widget_set_margin_start(grid, 10);
  gtk_widget_set_margin_end(grid, 10);
  gtk_widget_set_margin_top(grid, 10);
  gtk_widget_set_margin_bottom(grid, 10); 

  // Set spacing between rows and columns in a grid
  gtk_grid_set_row_spacing(GTK_GRID(grid), 10);  // 10 pixels between rows  
  gtk_grid_set_column_spacing(GTK_GRID(grid), 10);  // 10 pixels between columns


  gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
  gtk_grid_set_row_homogeneous(GTK_GRID(grid), TRUE);

  w = gtk_label_new("Meter Type:");
  gtk_widget_set_name(w, "boldlabel");
  gtk_widget_set_halign(w, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), w, 0, 1, 1, 1);

  const char *meter_type[] = {"Digital", "Analog", NULL};
  GtkStringList *meter_list = gtk_string_list_new(meter_type);
  w = gtk_drop_down_new(G_LIST_MODEL(meter_list), NULL);
  gtk_drop_down_set_selected(GTK_DROP_DOWN(w), analog_meter ? 1 : 0);
  g_signal_connect(w, "notify::selected", G_CALLBACK(analog_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), w, 1, 1, 1, 1);


  w = gtk_label_new("S-Meter Reading:");
  gtk_widget_set_name(w, "boldlabel");
  gtk_widget_set_halign(w, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), w, 0, 2, 1, 1);

  const char *meter_setting[] = {"Peak", "Average", NULL};
  GtkStringList *setting_list = gtk_string_list_new(meter_setting);
  w = gtk_drop_down_new(G_LIST_MODEL(setting_list), NULL);  

  switch (smeter) {
  case RXA_S_PK:
    gtk_drop_down_set_selected(GTK_DROP_DOWN(w), 0);
    break;

  case RXA_S_AV:
    gtk_drop_down_set_selected(GTK_DROP_DOWN(w), 1);
    break;
  }
  g_signal_connect(w, "notify::selected", G_CALLBACK(smeter_select_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), w, 1, 2, 1, 1);



  if (can_transmit) {
    w = gtk_label_new("TX ALC Reading:");
    gtk_widget_set_name(w, "boldlabel");
    gtk_widget_set_halign(w, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), w, 0, 3, 1, 1);


    const char *alc_meter_setting[] = {"Peak", "Average", "Gain", NULL};
    GtkStringList *setting_list = gtk_string_list_new(alc_meter_setting);
    w = gtk_drop_down_new(G_LIST_MODEL(setting_list), NULL);  

    switch (alc) {
    case TXA_ALC_PK:
      gtk_drop_down_set_selected(GTK_DROP_DOWN(w), 0);
      break;

    case TXA_ALC_AV:
      gtk_drop_down_set_selected(GTK_DROP_DOWN(w), 1);
      break;

    case TXA_ALC_GAIN:
      gtk_drop_down_set_selected(GTK_DROP_DOWN(w), 2);
      break;
    }
    gtk_grid_attach(GTK_GRID(grid), w, 1, 3, 1, 1);
    g_signal_connect(w, "notify::selected", G_CALLBACK(alc_select_cb), NULL);

  }

  gtk_box_append(GTK_BOX(content), grid);
  sub_menu = dialog;
  gtk_widget_show(dialog);
}
