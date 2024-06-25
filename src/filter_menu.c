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
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "new_menu.h"
#include "filter_menu.h"
#include "band.h"
#include "bandstack.h"
#include "filter.h"
#include "mode.h"
#include "radio.h"
#include "receiver.h"
#include "vfo.h"
#include "ext.h"
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

static GtkWidget *var1_spin_low;
static GtkWidget *var1_spin_high;
static GtkWidget *var2_spin_low;
static GtkWidget *var2_spin_high;

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
    active_menu = NO_MENU;
  }
}

static void cw_peak_cb(GtkCheckButton *widget, gpointer data) {
  vfo[active_receiver->id].cwAudioPeakFilter = gtk_check_button_get_active(GTK_CHECK_BUTTON(widget));
  receiver_filter_changed(active_receiver);
  g_idle_add(ext_vfo_update, NULL);
}

static gboolean default_cb (GtkWidget *widget, gpointer data) {
  int mode = vfo[active_receiver->id].mode;
  int f = GPOINTER_TO_INT(data);
  int low, high;
  GtkWidget *spinlow, *spinhigh;

  switch (f) {
  case filterVar1:
    spinlow = var1_spin_low;
    spinhigh = var1_spin_high;
    low = var1_default_low[mode];
    high = var1_default_high[mode];
    break;

  case filterVar2:
    spinlow = var2_spin_low;
    spinhigh = var2_spin_high;
    low = var2_default_low[mode];
    high = var2_default_high[mode];
    break;

  default:
    t_print("%s: illegal data = %p (%d)\n", __FUNCTION__, data, f);
    return FALSE;
    break;
  }

  switch (mode) {
  case modeCWL:
  case modeCWU:
  case modeAM:
  case modeDSB:
  case modeSAM:
  case modeSPEC:
  case modeDRM:
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spinlow), (double)(high - low));
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spinhigh), 0.5 * (double)(high + low));
    break;

  case modeLSB:
  case modeDIGL:
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spinlow), (double)(-high));
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spinhigh), (double)(-low));
    break;

  case modeUSB:
  case modeDIGU:
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spinlow), (double)(low));
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spinhigh), (double)(high));
    break;
  }

  return FALSE;
}

static gboolean close_cb () {
  cleanup();
  return TRUE;
}

static gboolean filter_select_cb (GtkWidget *widget, gpointer data) {
  CHOICE *choice = (CHOICE *) data;

  if (current) {
    g_signal_handler_block(G_OBJECT(current->button), current->signal);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(current->button), current == choice);
    g_signal_handler_unblock(G_OBJECT(current->button), current->signal);
  }

  if (current != choice) {
    current = choice;
    vfo_filter_changed(current->info);
  }

  return FALSE;
}

static gboolean deviation_select_cb (GtkWidget *widget, gpointer data) {
  CHOICE *choice = (CHOICE *) data;

  if (current) {
    g_signal_handler_block(G_OBJECT(current->button), current->signal);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(current->button), current == choice);
    g_signal_handler_unblock(G_OBJECT(current->button), current->signal);
  }

  if (current != choice) {
    int id = active_receiver->id;
    current = choice;
    vfo[id].deviation = choice->info;
    set_filter(active_receiver);

    if (can_transmit) {
      tx_set_filter(transmitter);
    }

    g_idle_add(ext_vfo_update, NULL);
  }

  return FALSE;
}

//
// var_spin_low controls the width for modes that ajust width/shift
//
static void var_spin_low_cb (GtkWidget *widget, gpointer data) {
  int f = GPOINTER_TO_UINT(data);
  int id = active_receiver->id;
  int m = vfo[id].mode;
  FILTER *mode_filters = filters[m];
  FILTER *filter = &mode_filters[f];
  int val = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
  int shift;

  switch (m) {
  case modeCWL:
  case modeCWU:
  case modeDSB:
  case modeAM:
  case modeSAM:
  case modeSPEC:
  case modeDRM:
    //
    // Set new width but keep the current shift
    //
    shift = (filter->low + filter->high) / 2;
    filter->low = shift - val / 2;
    filter->high = shift + val / 2;
    break;

  case modeLSB:
  case modeDIGL:
    //
    // The value corresponds to an audio frequency
    //
    filter->high = -val;
    break;

  case modeUSB:
  case modeDIGU:
    filter->low = val;
    break;
  }

  //t_print("%s: new values=(%d:%d)\n", __FUNCTION__, filter->low, filter->high);

  //
  // Change all receivers that use *this* variable filter
  //
  for (int i = 0; i < receivers; i++) {
    if (vfo[i].filter == f) {
      receiver_filter_changed(receiver[i]);
    }
  }

  g_idle_add(ext_vfo_update, NULL);
}

//
// var_spin_low controls the shift for modes that ajust width/shift
//
static void var_spin_high_cb (GtkWidget *widget, gpointer data) {
  int f = GPOINTER_TO_UINT(data);
  int id = active_receiver->id;
  FILTER *mode_filters = filters[vfo[id].mode];
  FILTER *filter = &mode_filters[f];
  int m = vfo[id].mode;
  int val = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
  int width;

  switch (m) {
  case modeCWL:
    //
    // Set new shift but keep the current width.
    // (The shift is in the audio domain)
    //
    width = (filter->high - filter->low);
    filter->low = -val - width / 2;
    filter->high = -val + width / 2;
    break;

  case modeCWU:
  case modeDSB:
  case modeAM:
  case modeSAM:
  case modeSPEC:
  case modeDRM:
    //
    // Set new shift but keep the current width
    //
    width = (filter->high - filter->low);
    filter->low = val - width / 2;
    filter->high = val + width / 2;
    break;

  case modeLSB:
  case modeDIGL:
    //
    // The value corresponds to an audio frequency
    //
    filter->low = -val;
    break;

  case modeUSB:
  case modeDIGU:
    filter->high = val;
    break;
  }

  //t_print("%s: new values=(%d:%d)\n", __FUNCTION__, filter->low, filter->high);

  //
  // Change all receivers that use *this* variable filter
  //
  for (int i = 0; i < receivers; i++) {
    if (vfo[i].filter == f) {
      receiver_filter_changed(receiver[i]);
    }
  }

  g_idle_add(ext_vfo_update, NULL);
}

void filter_menu(GtkWidget *parent) {
  int id = active_receiver->id;
  int f = vfo[id].filter;
  int m = vfo[id].mode;
  GtkWidget *w;
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  char title[64];
  snprintf(title, 64, "piHPSDR reimagined - Filter (RX%d VFO-%s)", id+1, id == 0 ? "A" : "B");
  gtk_window_set_title(GTK_WINDOW(dialog), title);
  g_signal_connect (dialog, "destroy", G_CALLBACK(close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
  gtk_grid_set_row_homogeneous(GTK_GRID(grid), FALSE);
  gtk_grid_set_column_spacing (GTK_GRID(grid), 5);
  gtk_grid_set_row_spacing (GTK_GRID(grid), 5);

  gtk_widget_set_margin_start(grid, 10);
  gtk_widget_set_margin_end(grid, 10);
  gtk_widget_set_margin_top(grid, 10);
  gtk_widget_set_margin_bottom(grid, 10);

  FILTER* band_filters = filters[m];

  if (m == modeFMN) {
    CHOICE *choice;
    w = gtk_label_new("Deviation:");
    gtk_grid_attach(GTK_GRID(grid), w, 0, 1, 4, 1);
    w = gtk_toggle_button_new_with_label("2.5K");
    gtk_widget_set_name(w, "small_toggle_button");
    choice = g_new(CHOICE, 1);
    choice->next = first;
    first = choice;
    choice->info = 2500;
    choice->button = w;

    if (active_receiver->deviation == 2500) {
      current = choice;
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
    }

    choice->signal = g_signal_connect(w, "toggled", G_CALLBACK(deviation_select_cb), choice);
    gtk_grid_attach(GTK_GRID(grid), w, 4, 1, 3, 1);
    w = gtk_toggle_button_new_with_label("5.0K");
    gtk_widget_set_name(w, "small_toggle_button");
    choice = g_new(CHOICE, 1);
    choice->next = first;
    first = choice;
    choice->info = 5000;
    choice->button = w;

    if (active_receiver->deviation == 5000) {
      current = choice;
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
    }

    choice->signal = g_signal_connect(w, "toggled", G_CALLBACK(deviation_select_cb), choice);
    gtk_grid_attach(GTK_GRID(grid), w, 7, 1, 3, 1);
  } else {
    int row = 0;
    int col = 10;
    CHOICE *choice;

    for (int i = 0; i < filterVar1; i++) {
      if (col > 9) {
        col = 0;
        row++;
      }

      w = gtk_toggle_button_new_with_label(band_filters[i].title);
      gtk_widget_set_name(w, "small_toggle_button");
      choice = g_new(CHOICE, 1);
      choice->next = first;
      first = choice;
      choice->info = i;
      choice->button = w;

      if (i == f) {
        current = choice;
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      }

      choice->signal = g_signal_connect(w, "toggled", G_CALLBACK(filter_select_cb), choice);
      gtk_grid_attach(GTK_GRID(grid), w, col, row, 2, 1);
      col += 2;
    }

    row++;
    //
    // Var1 and Var2 separated by a small horizontal line
    //
    GtkWidget *line = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_size_request(line, -1, 3);
    gtk_grid_attach(GTK_GRID(grid), line, 0, row++, 10, 1);
    //
    // Place Var1 and Var2 buttons in row+1, row+2
    //
    const FILTER* filter1 = &band_filters[filterVar1];
    const FILTER* filter2 = &band_filters[filterVar2];
    w = gtk_toggle_button_new_with_label(band_filters[filterVar1].title);
    gtk_widget_set_name(w, "small_toggle_button");
    gtk_grid_attach(GTK_GRID(grid), w, 0, row + 1, 2, 1);
    choice = g_new(CHOICE, 1);
    choice->next = first;
    first = choice;
    choice->info = filterVar1;
    choice->button = w;

    if (f == filterVar1) {
      current = choice;
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
    }

    choice->signal = g_signal_connect(w, "toggled", G_CALLBACK(filter_select_cb), choice);
    w = gtk_toggle_button_new_with_label(band_filters[filterVar2].title);
    gtk_widget_set_name(w, "small_toggle_button");
    gtk_grid_attach(GTK_GRID(grid), w, 0, row + 2, 2, 1);
    choice = g_new(CHOICE, 1);
    choice->next = first;
    first = choice;
    choice->info = filterVar2;
    choice->button = w;

    if (f == filterVar2) {
      current = choice;
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
    }

    choice->signal = g_signal_connect(w, "toggled", G_CALLBACK(filter_select_cb), choice);

    //
    // The spin buttons either control low/high or width/shift
    //
    switch (m) {
    case modeCWL:
    case modeCWU:
    case modeDSB:
    case modeAM:
    case modeSAM:
    case modeSPEC:
    case modeDRM:
      w = gtk_label_new("Filter Width:");
      gtk_widget_set_name(w, "boldlabel");
      gtk_widget_set_halign(w, GTK_ALIGN_START);
      gtk_grid_attach(GTK_GRID(grid), w, 2, row, 3, 1);
      w = gtk_label_new("Filter Shift:");
      gtk_widget_set_name(w, "boldlabel");
      gtk_widget_set_halign(w, GTK_ALIGN_START);
      gtk_grid_attach(GTK_GRID(grid), w, 5, row, 3, 1);
      var1_spin_low = gtk_spin_button_new_with_range(5.0, 16000.0, 5.0);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(var1_spin_low), (double)(filter1->high - filter1->low));
      gtk_entry_set_input_purpose(GTK_ENTRY(var1_spin_low), GTK_INPUT_PURPOSE_NUMBER);

      var2_spin_low = gtk_spin_button_new_with_range(5.0, 15000.0, 5.0);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(var2_spin_low), (double)(filter2->high - filter2->low));
      gtk_entry_set_input_purpose(GTK_ENTRY(var2_spin_low), GTK_INPUT_PURPOSE_NUMBER);

      var1_spin_high = gtk_spin_button_new_with_range(-15000.0, 15000.0, 5.0);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(var1_spin_high), 0.5 * (double)(filter1->high + filter1->low));
      gtk_entry_set_input_purpose(GTK_ENTRY(var1_spin_low), GTK_INPUT_PURPOSE_NUMBER);

      var2_spin_high = gtk_spin_button_new_with_range(-15000.0, 15000.0, 5.0);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(var2_spin_high), 0.5 * (double)(filter2->high + filter2->low));
      gtk_entry_set_input_purpose(GTK_ENTRY(var2_spin_high), GTK_INPUT_PURPOSE_NUMBER);
      break;

    case modeLSB:
    case modeDIGL:
      w = gtk_label_new("Filter Cut Low:");
      gtk_widget_set_name(w, "boldlabel");
      gtk_widget_set_halign(w, GTK_ALIGN_START);
      gtk_grid_attach(GTK_GRID(grid), w, 2, row, 3, 1);
      w = gtk_label_new("Filter Cut High:");
      gtk_widget_set_name(w, "boldlabel");
      gtk_widget_set_halign(w, GTK_ALIGN_START);
      gtk_grid_attach(GTK_GRID(grid), w, 5, row, 3, 1);
      var1_spin_low = gtk_spin_button_new_with_range(0, 15000.0, 5.0);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(var1_spin_low), (double)(-filter1->high));
      var2_spin_low = gtk_spin_button_new_with_range(0, 15000.0, 5.0);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(var2_spin_low), (double)(-filter2->high));
      var1_spin_high = gtk_spin_button_new_with_range(0, 15000.0, 5.0);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(var1_spin_high), (double)(-filter1->low));
      var2_spin_high = gtk_spin_button_new_with_range(0, 15000.0, 5.0);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(var2_spin_high), (double)(-filter2->low));
      break;

    case modeUSB:
    case modeDIGU:
      w = gtk_label_new("Filter Cut Low:");
      gtk_widget_set_name(w, "boldlabel");
      gtk_widget_set_halign(w, GTK_ALIGN_START);
      gtk_grid_attach(GTK_GRID(grid), w, 2, row, 3, 1);
      w = gtk_label_new("Filter Cut High:");
      gtk_widget_set_name(w, "boldlabel");
      gtk_widget_set_halign(w, GTK_ALIGN_START);
      gtk_grid_attach(GTK_GRID(grid), w, 5, row, 3, 1);
      var1_spin_low = gtk_spin_button_new_with_range(-15000, 15000.0, 5.0);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(var1_spin_low), (double)(filter1->low));
      var2_spin_low = gtk_spin_button_new_with_range(-15000, 15000.0, 5.0);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(var2_spin_low), (double)(filter2->low));
      var1_spin_high = gtk_spin_button_new_with_range(-15000, 15000.0, 5.0);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(var1_spin_high), (double)(filter1->high));
      var2_spin_high = gtk_spin_button_new_with_range(-15000, 15000.0, 5.0);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(var2_spin_high), (double)(filter2->high));
      break;
    }

    gtk_grid_attach(GTK_GRID(grid), var1_spin_low, 2, row + 1, 3, 1);
    gtk_grid_attach(GTK_GRID(grid), var2_spin_low, 2, row + 2, 3, 1);
    gtk_grid_attach(GTK_GRID(grid), var1_spin_high, 5, row + 1, 3, 1);
    gtk_grid_attach(GTK_GRID(grid), var2_spin_high, 5, row + 2, 3, 1);
    g_signal_connect(var1_spin_low, "value-changed", G_CALLBACK(var_spin_low_cb), GINT_TO_POINTER(filterVar1));
    g_signal_connect(var2_spin_low, "value-changed", G_CALLBACK(var_spin_low_cb), GINT_TO_POINTER(filterVar2));
    g_signal_connect(var1_spin_high, "value-changed", G_CALLBACK(var_spin_high_cb), GINT_TO_POINTER(filterVar1));
    g_signal_connect(var2_spin_high, "value-changed", G_CALLBACK(var_spin_high_cb), GINT_TO_POINTER(filterVar2));
    GtkWidget *var1_default_b = gtk_button_new_with_label("Default");
    gtk_widget_set_name(var1_default_b, "small_button");
    g_signal_connect (var1_default_b, "clicked", G_CALLBACK(default_cb), GINT_TO_POINTER(filterVar1));
    gtk_grid_attach(GTK_GRID(grid), var1_default_b, 8, row + 1, 2, 1);
    GtkWidget *var2_default_b = gtk_button_new_with_label("Default");
    gtk_widget_set_name(var2_default_b, "small_button");
    g_signal_connect (var2_default_b, "clicked", G_CALLBACK(default_cb), GINT_TO_POINTER(filterVar2));
    gtk_grid_attach(GTK_GRID(grid), var2_default_b, 8, row + 2, 2, 1);

    //
    // Add a checkbox for the CW audio peak filter, if the mode is CWU/CWL
    //
    if (m == modeCWU || m == modeCWL) {
      GtkWidget *cw_peak_b = gtk_check_button_new_with_label("Enable additional CW Audio peak filter");
      gtk_widget_set_name(cw_peak_b, "boldlabel");
      gtk_widget_set_halign(cw_peak_b, GTK_ALIGN_START);
      gtk_check_button_set_active (GTK_CHECK_BUTTON (cw_peak_b), vfo[active_receiver->id].cwAudioPeakFilter);
      gtk_grid_attach(GTK_GRID(grid), cw_peak_b, 4, 0, 6, 1);
      g_signal_connect(cw_peak_b, "toggled", G_CALLBACK(cw_peak_cb), NULL);
    }
  }

  gtk_box_append(GTK_BOX(content), grid);
  sub_menu = dialog;
  gtk_widget_show(dialog);
}
