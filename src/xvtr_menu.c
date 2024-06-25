/* Copyright (C)
* 2015 - John Melton, G0ORX/N6LYT
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
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "new_menu.h"
#include "band.h"
#include "filter.h"
#include "mode.h"
#include "xvtr_menu.h"
#include "radio.h"
#include "vfo.h"
#include "message.h"
#include "mystring.h"

static GtkWidget *dialog = NULL;
static GtkWidget *title[BANDS + XVTRS];
static GtkWidget *min_frequency[BANDS + XVTRS];
static GtkWidget *max_frequency[BANDS + XVTRS];
static GtkWidget *lo_frequency[BANDS + XVTRS];
static GtkWidget *lo_error[BANDS + XVTRS];
static GtkWidget *gain[BANDS + XVTRS];
static GtkWidget *disable_pa[BANDS + XVTRS];

static void save_xvtr () {
  for (int i = BANDS; i < BANDS + XVTRS; i++) {
    const char *txt;
    char f[16];
    BAND *xvtr = band_get_band(i);
    BANDSTACK *bandstack = xvtr->bandstack;
    txt = gtk_editable_get_text(GTK_EDITABLE(title[i]));
    STRLCPY(xvtr->title, txt, sizeof(xvtr->title));

    if (strlen(txt) != 0) {
      txt = gtk_editable_get_text(GTK_EDITABLE(min_frequency[i]));
      xvtr->frequencyMin = (long long)(atof(txt) * 1000000.0);
      txt = gtk_editable_get_text(GTK_EDITABLE(max_frequency[i]));
      xvtr->frequencyMax = (long long)(atof(txt) * 1000000.0);
      txt = gtk_editable_get_text(GTK_EDITABLE(lo_frequency[i]));
      xvtr->frequencyLO = (long long)(atof(txt) * 1000000.0);
      txt = gtk_editable_get_text(GTK_EDITABLE(lo_error[i]));
      xvtr->errorLO = atoll(txt);
      txt = gtk_editable_get_text(GTK_EDITABLE(gain[i]));
      xvtr->gain = atoi(txt);

      if (protocol == ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL) {
        xvtr->disablePA = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(disable_pa[i]));
      }

      //
      // Patch up the frequencies:
      // frequencyMax must be at most frequencyLO + max radio frequency
      // frequencyMin must be at least frequencyLO + min radio frequency
      //
      if ((xvtr->frequencyMin < xvtr->frequencyLO + radio->frequency_min) ||
          (xvtr->frequencyMin > xvtr->frequencyLO + radio->frequency_max)) {
        xvtr->frequencyMin = xvtr->frequencyLO + radio->frequency_min;
        //t_print("XVTR band %s MinFrequency changed to %lld\n", t, xvtr->frequencyMin);
      }

      if (xvtr->frequencyMax < xvtr->frequencyMin) {
        xvtr->frequencyMax = xvtr->frequencyMin + 1000000LL;
        //t_print("XVTR band %s MaxFrequency changed to %lld\n", t, xvtr->frequencyMax);
      }

      if (xvtr->frequencyMax > xvtr->frequencyLO + radio->frequency_max) {
        xvtr->frequencyMax = xvtr->frequencyLO + radio->frequency_max;
        //t_print("XVTR band %s MaxFrequency changed to %lld\n", t, xvtr->frequencyMax);
      }

      //
      // Initialize all bandstack entries where the frequency is not inside the
      // transverter band
      //
      for (int b = 0; b < bandstack->entries; b++) {
        BANDSTACK_ENTRY *entry = &bandstack->entry[b];

        if (entry->frequency < xvtr->frequencyMin || entry->frequency > xvtr->frequencyMax) {
          entry->frequency = xvtr->frequencyMin + ((xvtr->frequencyMax - xvtr->frequencyMin) / 2);
          entry->mode = modeUSB;
          entry->filter = filterF6;
        }
      }
    } else {
      xvtr->frequencyMin = 0;
      xvtr->frequencyMax = 0;
      xvtr->frequencyLO = 0;
      xvtr->errorLO = 0;
      xvtr->disablePA = 1;
    }

    //
    // Update all the text fields
    //
    gtk_editable_set_text(GTK_EDITABLE(title[i]), xvtr->title);
    snprintf(f, 16, "%5.3f", (double)xvtr->frequencyMin / 1000000.0);
    gtk_editable_set_text(GTK_EDITABLE(min_frequency[i]), f);
    snprintf(f, 16, "%5.3f", (double)xvtr->frequencyMax / 1000000.0);
    gtk_editable_set_text(GTK_EDITABLE(max_frequency[i]), f);
    snprintf(f, 16, "%5.3f", (double)xvtr->frequencyLO / 1000000.0);
    gtk_editable_set_text(GTK_EDITABLE(lo_frequency[i]), f);
    snprintf(f, 16, "%lld", xvtr->errorLO);
    gtk_editable_set_text(GTK_EDITABLE(lo_error[i]), f);
    snprintf(f, 16, "%d", xvtr->gain);
    gtk_editable_set_text(GTK_EDITABLE(gain[i]), f);

    if (protocol == ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL) {
      gtk_check_button_set_active(GTK_CHECK_BUTTON(disable_pa[i]), xvtr->disablePA);
    }
  }

  vfo_xvtr_changed();
}

void pa_disable_cb(GtkCheckButton *widget, gpointer data) {
  int i = GPOINTER_TO_INT(data);
  BAND *xvtr = band_get_band(i);
  xvtr->disablePA = gtk_check_button_get_active(GTK_CHECK_BUTTON(widget));
}

static void cleanup() {
  if (dialog != NULL) {
    GtkWidget *tmp = dialog;
    dialog = NULL;
    save_xvtr();
    gtk_window_destroy(GTK_WINDOW(tmp));
    sub_menu = NULL;
    active_menu  = NO_MENU;
  }
}

static gboolean close_cb() {
  cleanup();
  return TRUE;
}

static gboolean update_cb() {
  save_xvtr();
  return TRUE;
}

void xvtr_menu(GtkWidget *parent) {
  int i;
  char f[16];
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  gtk_window_set_title(GTK_WINDOW(dialog), "piHPSDR reimagined - XVTR");
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_spacing (GTK_GRID(grid), 10);
  gtk_grid_set_row_homogeneous(GTK_GRID(grid), FALSE);
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), FALSE);

  gtk_widget_set_margin_start(grid, 10);
  gtk_widget_set_margin_end(grid, 10);
  gtk_widget_set_margin_top(grid, 10);
  gtk_widget_set_margin_bottom(grid, 10);

  GtkWidget *update_b = gtk_button_new_with_label("Update");
  g_signal_connect (update_b, "clicked", G_CALLBACK(update_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), update_b, 1, 0, 1, 1);

  GtkWidget *label = gtk_label_new("Title");
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), label, 0, 1, 1, 1);
  label = gtk_label_new("Min Frq(MHz)");
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), label, 1, 1, 1, 1);
  label = gtk_label_new("Max Frq(MHz)");
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), label, 2, 1, 1, 1);
  label = gtk_label_new("LO Frq(MHz)");
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), label, 3, 1, 1, 1);
  label = gtk_label_new("LO Err(Hz)");
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), label, 4, 1, 1, 1);
  label = gtk_label_new("Gain (dB)");
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), label, 5, 1, 1, 1);

  if (protocol == ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL) {
    label = gtk_label_new("Disable PA");
    gtk_widget_set_name(label, "boldlabel");
    gtk_grid_attach(GTK_GRID(grid), label, 6, 1, 1, 1);
  }

  //
  // Note  no signal connect for the text fields:
  // this will lead to intermediate frequency values that are unreasonable.
  // Set all parameters only when leaving the menu, except for disablePA
  // which can be applied immediately
  //
  for (i = BANDS; i < BANDS + XVTRS; i++) {
    const BAND *xvtr = band_get_band(i);
    
    title[i] = gtk_entry_new();
    gtk_entry_buffer_set_max_length(gtk_entry_get_buffer(GTK_ENTRY(title[i])), 7);
    gtk_editable_set_text(GTK_EDITABLE(title[i]), xvtr->title);
    gtk_grid_attach(GTK_GRID(grid), title[i], 0, i + 2, 1, 1);

    min_frequency[i] = gtk_entry_new();
    gtk_entry_buffer_set_max_length(gtk_entry_get_buffer(GTK_ENTRY(min_frequency[i])), 7);
    snprintf(f, 16, "%5.3f", (double)xvtr->frequencyMin / 1000000.0);
    gtk_editable_set_text(GTK_EDITABLE(min_frequency[i]), f);
    gtk_grid_attach(GTK_GRID(grid), min_frequency[i], 1, i + 2, 1, 1);

    max_frequency[i] = gtk_entry_new();
    gtk_entry_buffer_set_max_length(gtk_entry_get_buffer(GTK_ENTRY(max_frequency[i])), 7);
    snprintf(f, 16, "%5.3f", (double)xvtr->frequencyMax / 1000000.0);
    gtk_editable_set_text(GTK_EDITABLE(max_frequency[i]), f);
    gtk_grid_attach(GTK_GRID(grid), max_frequency[i], 2, i + 2, 1, 1);

    lo_frequency[i] = gtk_entry_new();
    gtk_entry_buffer_set_max_length(gtk_entry_get_buffer(GTK_ENTRY(lo_frequency[i])), 7);
    snprintf(f, 16, "%5.3f", (double)xvtr->frequencyLO / 1000000.0);
    gtk_editable_set_text(GTK_EDITABLE(lo_frequency[i]), f);
    gtk_grid_attach(GTK_GRID(grid), lo_frequency[i], 3, i + 2, 1, 1);

    lo_error[i] = gtk_entry_new();
    gtk_entry_buffer_set_max_length(gtk_entry_get_buffer(GTK_ENTRY(lo_error[i])), 9);
    snprintf(f, 16, "%lld", xvtr->errorLO);
    gtk_editable_set_text(GTK_EDITABLE(lo_error[i]), f);
    gtk_grid_attach(GTK_GRID(grid), lo_error[i], 4, i + 2, 1, 1);

    gain[i] = gtk_entry_new();
    gtk_entry_buffer_set_max_length(gtk_entry_get_buffer(GTK_ENTRY(gain[i])), 3);
    snprintf(f, 16, "%d", xvtr->gain);
    gtk_editable_set_text(GTK_EDITABLE(gain[i]), f);
    gtk_grid_attach(GTK_GRID(grid), gain[i], 5, i + 2, 1, 1);

    if (protocol == ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL) {
      disable_pa[i] = gtk_check_button_new();
      gtk_check_button_set_active(GTK_CHECK_BUTTON(disable_pa[i]), xvtr->disablePA);
      gtk_grid_attach(GTK_GRID(grid), disable_pa[i], 6, i + 2, 1, 1);
      gtk_widget_set_halign(disable_pa[i], GTK_ALIGN_CENTER);
      g_signal_connect(disable_pa[i], "toggled", G_CALLBACK(pa_disable_cb), GINT_TO_POINTER(i));
    }
  }

  gtk_box_append(GTK_BOX(content), grid);
  sub_menu = dialog;
  gtk_widget_show(dialog);
}
