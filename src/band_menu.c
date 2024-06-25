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
#include "band_menu.h"
#include "band.h"
#include "bandstack.h"
#include "filter.h"
#include "radio.h"
#include "receiver.h"
#include "vfo.h"
#ifdef CLIENT_SERVER
  #include "client_server.h"
#endif
#include "message.h"

static GtkWidget *dialog = NULL;

struct _CHOICE {
  int info;
  GtkWidget      *button;
  gulong          signal;
  struct _CHOICE *next;
};

typedef struct _CHOICE CHOICE;

static struct _CHOICE *first = NULL;
static struct _CHOICE *current = NULL;

static void cleanup() {
  if (dialog != NULL) {
    GtkWidget *tmp = dialog;
    dialog = NULL;

    while (first != NULL) {
      CHOICE *choice = first;
      first = first->next;
      g_free(choice);
    }

    current = NULL;
    gtk_window_destroy(GTK_WINDOW(tmp));
    sub_menu = NULL;
    active_menu  = NO_MENU;
  }
}

static gboolean close_cb () {
  cleanup();
  return TRUE;
}

gboolean band_select_cb (GtkWidget *widget, gpointer data) {
  CHOICE *choice = (CHOICE *) data;
  int id = active_receiver->id;

  //
  // If the current band has been clicked, this will cycle through the
  // band stack
  //
  if (radio_is_remote) {
#ifdef CLIENT_SERVER
    send_band(client_socket, id, choice->info);
#endif
  } else {
    vfo_band_changed(id, choice->info);
  }

  if (vfo[id].band != choice->info) {
    //
    // Note that a band change may fail. This can happen if the local oscillator
    // of a transverter band has a grossly wrong frequency such that the effective
    // radio frequency falls outside the hardware frequency range of the radio.
    // In this case, the band button to the band that is effective must be highlighted
    // and all others turned off.
    //
    //
    choice = first;
    current = NULL;

    while (choice) {
      g_signal_handler_block(G_OBJECT(choice->button), choice->signal);

      if (choice->info == vfo[id].band) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(choice->button), TRUE);
        current = choice;
      } else {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(choice->button), FALSE);
      }

      g_signal_handler_unblock(G_OBJECT(choice->button), choice->signal);
      choice = choice->next;
    }

    return FALSE;
  }

  if (current) {
    g_signal_handler_block(G_OBJECT(current->button), current->signal);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(current->button), current == choice);
    g_signal_handler_unblock(G_OBJECT(current->button), current->signal);
  }

  current = choice;
  return FALSE;
}

void band_menu(GtkWidget *parent) {
  int i, j;
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  char title[64];
  snprintf(title, 64, "piHPSDR reimagined - Band (VFO-%s)", active_receiver->id == 0 ? "A" : "B");
  gtk_window_set_title(GTK_WINDOW(dialog), title);
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);

  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
  gtk_grid_set_row_homogeneous(GTK_GRID(grid), TRUE);
  gtk_grid_set_column_spacing (GTK_GRID(grid), 5);
  gtk_grid_set_row_spacing (GTK_GRID(grid), 5);

  gtk_widget_set_margin_start(grid, 10);
  gtk_widget_set_margin_end(grid, 10);
  gtk_widget_set_margin_top(grid, 10);
  gtk_widget_set_margin_bottom(grid, 10);

  long long frequency_min = radio->frequency_min;
  long long frequency_max = radio->frequency_max;
  //t_print("band_menu: min=%lld max=%lld\n",frequency_min,frequency_max);
  j = 0;

  for (i = 0; i < BANDS + XVTRS; i++) {
    const BAND *band;
    band = (BAND*)band_get_band(i);

    if (strlen(band->title) > 0) {
      if (i < BANDS) {
        if (!(band->frequencyMin == 0.0 && band->frequencyMax == 0.0)) {
          if (band->frequencyMin < frequency_min || band->frequencyMax > frequency_max) {
            continue;
          }
        }
      }

      GtkWidget *w = gtk_toggle_button_new_with_label(band->title);
      gtk_widget_set_name(w, "small_toggle_button");
      gtk_widget_show(w);
      gtk_grid_attach(GTK_GRID(grid), w, j % 5, 1 + (j / 5), 1, 1);
      CHOICE *choice = g_new(CHOICE, 1);
      choice->next = first;
      first = choice;
      choice->info = i;
      choice->button = w;

      if (i == vfo[active_receiver->id].band) {
        current = choice;
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      }

      choice->signal = g_signal_connect(w, "toggled", G_CALLBACK(band_select_cb), choice);
      j++;
    }
  }

  gtk_box_append(GTK_BOX(content), grid);
  sub_menu = dialog;
  gtk_widget_show(dialog);
}
