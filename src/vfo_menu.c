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
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <locale.h>

#include <wdsp.h>

#include "new_menu.h"
#include "band.h"
#include "filter.h"
#include "mode.h"
#include "radio.h"
#include "receiver.h"
#include "transmitter.h"
#include "vfo.h"
#include "ext.h"
#include "radio_menu.h"

static int myvfo;  //  VFO the menu is referring to
static GtkWidget *dialog = NULL;
static GtkWidget *label;

//
// Note that the decimal point is hard-wired to a point,
// which may be incompatible with the LOCALE setting
// such that atof() and friends do not understand it.
// This is taken care of below
//
static char *btn_labels[] = {"1", "2", "3",
                             "4", "5", "6",
                             "7", "8", "9",
                             ".", "0", "BS",
                             "Hz", "kHz", "MHz"
                             , "Clear"
                            };

//
// The parameters for num_pad corresponding to the button labels
//
static int btn_actions[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, -5, 0, -6, -2, -3, -4, -1};

static GtkWidget *btn[16];

static void cleanup() {
  if (dialog != NULL) {
    GtkWidget *tmp = dialog;
    dialog = NULL;
    gtk_window_destroy(GTK_WINDOW(tmp));
    sub_menu = NULL;
    active_menu  = NO_MENU;
    num_pad(-1, myvfo);
  }
}

static gboolean close_cb () {
  cleanup();
  return TRUE;
}

static gboolean num_pad_cb(GtkWidget *widget, gpointer data) {
  int val = GPOINTER_TO_INT(data);
  char output[64];
  num_pad(btn_actions[val], myvfo);

  if (vfo[myvfo].entered_frequency[0]) {
    snprintf(output, 64, "<big><b>%s</b></big>", vfo[myvfo].entered_frequency);
  } else {
    snprintf(output, 64, "<big><b>0</b></big>");
  }

  gtk_label_set_markup (GTK_LABEL (label), output);
  return FALSE;
}

static void rit_cb(GtkComboBox *widget, gpointer data) {
  switch (gtk_combo_box_get_active(widget)) {
  case 0:
    rit_increment = 1;
    break;

  case 1:
    rit_increment = 10;
    break;

  case 2:
    rit_increment = 100;
    break;
  }

  g_idle_add(ext_vfo_update, NULL);
}

static void vfo_cb(GtkComboBox *widget, gpointer data) {
  vfo_set_step_from_index(myvfo, gtk_combo_box_get_active(widget));
  g_idle_add(ext_vfo_update, NULL);
}

static void duplex_cb(GtkCheckButton *widget, gpointer data) {
  if (isTransmitting()) {
    gtk_check_button_set_active (GTK_CHECK_BUTTON (widget), duplex);
    return;
  }

  duplex = gtk_check_button_get_active(GTK_CHECK_BUTTON(widget));
  setDuplex();
}

static void ctun_cb(GtkWidget *widget, gpointer data) {
  int state = gtk_check_button_get_active (GTK_CHECK_BUTTON (widget));
  vfo_ctun_update(myvfo, state);
  g_idle_add(ext_vfo_update, NULL);
}

static void split_cb(GtkCheckButton *widget, gpointer data) {
  int state = gtk_check_button_get_active (GTK_CHECK_BUTTON (widget));
  radio_set_split(state);
  g_idle_add(ext_vfo_update, NULL);
}

static void set_btn_state() {
  int i;

  for (i = 0; i < 16; i++) {
    gtk_widget_set_sensitive(btn[i], locked == 0);
  }
}

static void lock_cb(GtkCheckButton *widget, gpointer data) {
  TOGGLE(locked);
  set_btn_state();
  g_idle_add(ext_vfo_update, NULL);
}

void vfo_menu(GtkWidget *parent, int id) {
  int i, row;
  myvfo = id; // store this for cleanup()
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  char title[64];
  snprintf(title, 64, "piHPSDR reimagined - VFO %s", myvfo == 0 ? "A" : "B");
  gtk_window_set_title(GTK_WINDOW(dialog),title);
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), FALSE);
  gtk_grid_set_row_homogeneous(GTK_GRID(grid), TRUE);
  gtk_grid_set_column_spacing (GTK_GRID(grid), 10);
  gtk_grid_set_row_spacing (GTK_GRID(grid), 10);
  gtk_widget_set_margin_start(grid, 10);
  gtk_widget_set_margin_end(grid, 10);
  gtk_widget_set_margin_top(grid, 10);
  gtk_widget_set_margin_bottom(grid, 10);

  label = gtk_label_new (NULL);
  gtk_label_set_markup(GTK_LABEL(label), "<big><b>0</b></big>");
  gtk_widget_set_size_request(label, 150, 0);
  gtk_widget_set_halign(label, GTK_ALIGN_END); 
  gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
  gtk_grid_attach(GTK_GRID(grid), label, 0, 1, 3, 1);

  for (i = 0; i < 16; i++) {
    btn[i] = gtk_button_new_with_label(btn_labels[i]);
    gtk_widget_set_name(btn[i], "close_button");
    gtk_widget_show(btn[i]);

    if (i == 15) {
      gtk_grid_attach(GTK_GRID(grid), btn[i], i % 3, 2 + (i / 3), 3, 1);
    } else {
      gtk_grid_attach(GTK_GRID(grid), btn[i], i % 3, 2 + (i / 3), 1, 1);
    }

    g_signal_connect(btn[i], "clicked", G_CALLBACK(num_pad_cb), GINT_TO_POINTER(i));
  }

  set_btn_state();
  GtkWidget *rit_label = gtk_label_new("RIT step: ");
  gtk_widget_set_name(rit_label, "boldlabel");
  gtk_widget_set_halign(rit_label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), rit_label, 3, 2, 1, 1);
  GtkWidget *rit_b = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(rit_b), NULL, "1 Hz");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(rit_b), NULL, "10 Hz");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(rit_b), NULL, "100 Hz");

  switch (rit_increment) {
  case 1:
    gtk_combo_box_set_active(GTK_COMBO_BOX(rit_b), 0);
    break;

  case 10:
    gtk_combo_box_set_active(GTK_COMBO_BOX(rit_b), 1);
    break;

  case 100:
    gtk_combo_box_set_active(GTK_COMBO_BOX(rit_b), 2);
    break;
  }

  g_signal_connect(rit_b, "changed", G_CALLBACK(rit_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), rit_b, 4, 2, 1, 1);
  GtkWidget *vfo_label = gtk_label_new("VFO step: ");
  gtk_widget_set_name(vfo_label, "boldlabel");
  gtk_widget_set_halign(vfo_label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), vfo_label, 3, 3, 1, 1);
  GtkWidget *vfo_b = gtk_combo_box_text_new();
  int ind = vfo_get_stepindex(myvfo);

  for (i = 0; i < STEPS; i++) {
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(vfo_b), NULL, step_labels[i]);

    if (i == ind) {
      gtk_combo_box_set_active (GTK_COMBO_BOX(vfo_b), i);
    }
  }

  g_signal_connect(vfo_b, "changed", G_CALLBACK(vfo_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), vfo_b, 4, 3, 1, 1);
  row = 4;

  GtkWidget *lock_b = gtk_check_button_new_with_label("Lock VFOs");
  gtk_widget_set_name(lock_b, "boldlabel");
  gtk_check_button_set_active (GTK_CHECK_BUTTON (lock_b), locked);
  gtk_grid_attach(GTK_GRID(grid), lock_b, 3, row, 2, 1);
  g_signal_connect(lock_b, "toggled", G_CALLBACK(lock_cb), NULL);
  row++;

  GtkWidget *duplex_b = gtk_check_button_new_with_label("Duplex");
  gtk_widget_set_name(duplex_b, "boldlabel");
  gtk_check_button_set_active (GTK_CHECK_BUTTON (duplex_b), duplex);
  gtk_grid_attach(GTK_GRID(grid), duplex_b, 3, row, 2, 1);
  g_signal_connect(duplex_b, "toggled", G_CALLBACK(duplex_cb), NULL);
  row++;

  GtkWidget *ctun_b = gtk_check_button_new_with_label("CTUN");
  gtk_widget_set_name(ctun_b, "boldlabel");
  gtk_check_button_set_active (GTK_CHECK_BUTTON (ctun_b), vfo[myvfo].ctun);
  gtk_grid_attach(GTK_GRID(grid), ctun_b, 3, row, 2, 1);
  g_signal_connect(ctun_b, "toggled", G_CALLBACK(ctun_cb), NULL);
  row++;

  GtkWidget *split_b = gtk_check_button_new_with_label("Split");
  gtk_widget_set_name(split_b, "boldlabel");
  gtk_check_button_set_active (GTK_CHECK_BUTTON (split_b), split);
  gtk_grid_attach(GTK_GRID(grid), split_b, 3, row, 2, 1);
  g_signal_connect(split_b, "toggled", G_CALLBACK(split_cb), NULL);
  
  gtk_box_append(GTK_BOX(content), grid);
  sub_menu = dialog;
  gtk_widget_show(dialog);
}

//
// This is also called when hitting buttons on the
// numpad of the MIDI device or the controller
//
void num_pad(int action, int id) {
  double mult = 1.0;
  double fd;
  long long fl;
  struct lconv *locale = localeconv();
  char *buffer = vfo[id].entered_frequency;
  size_t len = strlen(buffer);

  switch (action) {
  default:  // digit 0...9
    if (len <= sizeof(vfo[id].entered_frequency) - 2) {
      buffer[len++] = '0' + action;
      buffer[len] = 0;
    }

    break;

  case -5:  // Decimal point

    // if there is already a decimal point in the string,
    // do not add another one
    if (strchr(buffer, *(locale->decimal_point)) == NULL &&
      len <= sizeof(vfo[id].entered_frequency) - 2) {
      buffer[len++] = *(locale->decimal_point);
      buffer[len] = 0;
    }

    break;

  case -1:  // Clear
    *buffer = 0;
    break;

  case -6:  // Backspace
    if (len > 0) { buffer[len - 1] = 0; }

    break;

  case -4:  // Enter as MHz
    mult *= 1000.0;

  //FALLTHROUGH
  case -3:  // Enter as kHz
    mult *= 1000.0;

  //FALLTHROUGH
  case -2:  // Enter
    fd = atof(buffer) * mult;
    fl = (long long) (fd + 0.5);
    *buffer = 0;

    //
    // If the frequency is less than 10 kHz, this
    // is most likely not intended by the user,
    // so we do not set the frequency. This guards
    // against setting the frequency to zero just
    // by hitting the "NUMPAD enter" button.
    //
    if (fl >= 10000ll) {
#ifdef CLIENT_SERVER

      if (radio_is_remote) {
        send_vfo_frequency(client_socket, id, fl);
      } else {
#endif
        vfo_set_frequency(id, fl);
#ifdef CLIENT_SERVER
      }

#endif
    }

    break;
  }

  g_idle_add(ext_vfo_update, NULL);
}
