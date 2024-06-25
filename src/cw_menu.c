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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "new_menu.h"
#include "pa_menu.h"
#include "band.h"
#include "bandstack.h"
#include "filter.h"
#include "radio.h"
#include "receiver.h"
#include "new_protocol.h"
#include "old_protocol.h"
#include "iambic.h"
#include "ext.h"
#include "message.h"

static GtkWidget *dialog = NULL;

void cw_changed() {
  // inform the local keyer about CW parameter changes
  // NewProtocol: rely on periodically sent HighPrio packets
  keyer_update();
  schedule_transmit_specific();
  //
  // speed and side tone frequency are displayed in the VFO bar
  //
  g_idle_add(ext_vfo_update, NULL);
}

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

static void cw_keyer_internal_cb(GtkCheckButton *widget, gpointer data) {
  cw_keyer_internal = gtk_check_button_get_active(GTK_CHECK_BUTTON(widget));
  cw_changed();
}

static void cw_keyer_spacing_cb(GtkCheckButton *widget, gpointer data) {
  cw_keyer_spacing = gtk_check_button_get_active(GTK_CHECK_BUTTON(widget));
  cw_changed();
}

static void cw_keyer_speed_value_changed_cb(GtkWidget *widget, gpointer data) {
  cw_keyer_speed = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
  cw_changed();
}

static void cw_breakin_cb(GtkCheckButton *widget, gpointer data) {
  cw_breakin = gtk_check_button_get_active(GTK_CHECK_BUTTON(widget));
  cw_changed();
}

static void cw_keyer_hang_time_value_changed_cb(GtkWidget *widget, gpointer data) {
  cw_keyer_hang_time = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
  cw_changed();
}

static void cw_keyer_weight_value_changed_cb(GtkWidget *widget, gpointer data) {
  cw_keyer_weight = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
  cw_changed();
}

static void cw_keys_reversed_cb(GtkCheckButton *widget, gpointer data) {
  cw_keys_reversed = gtk_check_button_get_active(GTK_CHECK_BUTTON(widget));
  cw_changed();
}

static void cw_keyer_mode_cb(GtkToggleButton *widget, gpointer data) {
  int val = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));
  cw_keyer_mode = val;
  cw_changed();
}

static void cw_keyer_sidetone_level_value_changed_cb(GtkWidget *widget, gpointer data) {
  cw_keyer_sidetone_volume = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
  cw_changed();
}

static void cw_keyer_sidetone_frequency_value_changed_cb(GtkWidget *widget, gpointer data) {
  cw_keyer_sidetone_frequency = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
  cw_changed();
  receiver_filter_changed(active_receiver);
  // changing the side tone frequency affects BFO frequency offsets
  schedule_high_priority();
}

static void cw_ramp_width_changed_cb(GtkWidget *widget, gpointer data) {
  cw_ramp_width = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
  tx_set_ramps(transmitter);
  schedule_transmit_specific();
}

void cw_menu(GtkWidget *parent) {
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  gtk_window_set_title(GTK_WINDOW(dialog), "piHPSDR - CW");
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_spacing (GTK_GRID(grid), 10);
  gtk_grid_set_column_homogeneous (GTK_GRID(grid), FALSE);
  gtk_grid_set_row_homogeneous (GTK_GRID(grid), TRUE);

  GtkWidget *cw_keyer_internal_b = gtk_check_button_new_with_label("CW handled in Radio");
  gtk_widget_set_name(cw_keyer_internal_b, "boldlabel");
  gtk_check_button_set_active (GTK_CHECK_BUTTON (cw_keyer_internal_b), cw_keyer_internal);
  gtk_widget_show(cw_keyer_internal_b);
  gtk_grid_attach(GTK_GRID(grid), cw_keyer_internal_b, 1, 0, 1, 1);
  g_signal_connect(cw_keyer_internal_b, "toggled", G_CALLBACK(cw_keyer_internal_cb), NULL);

  GtkWidget *cw_speed_label = gtk_label_new("CW Speed (WPM)");
  gtk_widget_set_name(cw_speed_label, "boldlabel");
  gtk_widget_set_halign(cw_speed_label, GTK_ALIGN_END);
  gtk_widget_show(cw_speed_label);
  gtk_grid_attach(GTK_GRID(grid), cw_speed_label, 0, 1, 1, 1);

  GtkWidget *cw_keyer_speed_b = gtk_spin_button_new_with_range(1.0, 60.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(cw_keyer_speed_b), (double)cw_keyer_speed);
  gtk_widget_show(cw_keyer_speed_b);
  gtk_grid_attach(GTK_GRID(grid), cw_keyer_speed_b, 1, 1, 1, 1);
  g_signal_connect(cw_keyer_speed_b, "value_changed", G_CALLBACK(cw_keyer_speed_value_changed_cb), NULL);

  GtkWidget *cw_breakin_b = gtk_check_button_new_with_label("CW Break-In, Delay (ms):");
  gtk_widget_set_name(cw_breakin_b, "boldlabel");
  gtk_widget_set_halign(cw_breakin_b, GTK_ALIGN_END);
  gtk_check_button_set_active (GTK_CHECK_BUTTON (cw_breakin_b), cw_breakin);
  gtk_widget_show(cw_breakin_b);
  gtk_grid_attach(GTK_GRID(grid), cw_breakin_b, 0, 2, 1, 1);
  g_signal_connect(cw_breakin_b, "toggled", G_CALLBACK(cw_breakin_cb), NULL);

  GtkWidget *cw_keyer_hang_time_b = gtk_spin_button_new_with_range(0.0, 1000.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(cw_keyer_hang_time_b), (double)cw_keyer_hang_time);
  gtk_widget_show(cw_keyer_hang_time_b);
  gtk_grid_attach(GTK_GRID(grid), cw_keyer_hang_time_b, 1, 2, 1, 1);
  g_signal_connect(cw_keyer_hang_time_b, "value_changed", G_CALLBACK(cw_keyer_hang_time_value_changed_cb), NULL);

  GtkWidget *cw_keyer_sidetone_level_label = gtk_label_new("Sidetone Level:");
  gtk_widget_set_name(cw_keyer_sidetone_level_label, "boldlabel");
  gtk_widget_set_halign(cw_keyer_sidetone_level_label, GTK_ALIGN_END);
  gtk_widget_show(cw_keyer_sidetone_level_label);
  gtk_grid_attach(GTK_GRID(grid), cw_keyer_sidetone_level_label, 0, 3, 1, 1);

  GtkWidget *cw_keyer_sidetone_level_b = gtk_spin_button_new_with_range(0.0, 127.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(cw_keyer_sidetone_level_b), (double)cw_keyer_sidetone_volume);
  gtk_widget_show(cw_keyer_sidetone_level_b);
  gtk_grid_attach(GTK_GRID(grid), cw_keyer_sidetone_level_b, 1, 3, 1, 1);
  g_signal_connect(cw_keyer_sidetone_level_b, "value_changed", G_CALLBACK(cw_keyer_sidetone_level_value_changed_cb), NULL);

  GtkWidget *cw_keyer_sidetone_frequency_label = gtk_label_new("Sidetone Freq:");
  gtk_widget_set_name(cw_keyer_sidetone_frequency_label, "boldlabel");
  gtk_widget_set_halign(cw_keyer_sidetone_frequency_label, GTK_ALIGN_END);
  gtk_widget_show(cw_keyer_sidetone_frequency_label);
  gtk_grid_attach(GTK_GRID(grid), cw_keyer_sidetone_frequency_label, 0, 4, 1, 1);

  GtkWidget *cw_keyer_sidetone_frequency_b = gtk_spin_button_new_with_range(100.0, 1000.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(cw_keyer_sidetone_frequency_b), (double)cw_keyer_sidetone_frequency);
  gtk_widget_show(cw_keyer_sidetone_frequency_b);
  gtk_grid_attach(GTK_GRID(grid), cw_keyer_sidetone_frequency_b, 1, 4, 1, 1);
  g_signal_connect(cw_keyer_sidetone_frequency_b, "value_changed", G_CALLBACK(cw_keyer_sidetone_frequency_value_changed_cb), NULL);

  GtkWidget *cw_keyer_weight_label = gtk_label_new("Weight:");
  gtk_widget_set_name(cw_keyer_weight_label, "boldlabel");
  gtk_widget_set_halign(cw_keyer_weight_label, GTK_ALIGN_END);
  gtk_widget_show(cw_keyer_weight_label);
  gtk_grid_attach(GTK_GRID(grid), cw_keyer_weight_label, 0, 5, 1, 1);
  
  GtkWidget *cw_keyer_weight_b = gtk_spin_button_new_with_range(0.0, 100.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(cw_keyer_weight_b), (double)cw_keyer_weight);
  gtk_widget_show(cw_keyer_weight_b);
  gtk_grid_attach(GTK_GRID(grid), cw_keyer_weight_b, 1, 5, 1, 1);
  g_signal_connect(cw_keyer_weight_b, "value_changed", G_CALLBACK(cw_keyer_weight_value_changed_cb), NULL);

  GtkWidget *cw_ramp_width_label = gtk_label_new("CW ramp width (ms):");
  gtk_widget_set_name(cw_ramp_width_label, "boldlabel");
  gtk_widget_set_halign(cw_ramp_width_label, GTK_ALIGN_END);
  gtk_widget_show(cw_ramp_width_label);
  gtk_grid_attach(GTK_GRID(grid), cw_ramp_width_label, 0, 6, 1, 1);

  GtkWidget *cw_ramp_width_b = gtk_spin_button_new_with_range(5.0, 20.0, 1.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(cw_ramp_width_b), cw_ramp_width);
  gtk_widget_show(cw_ramp_width_b);
  gtk_grid_attach(GTK_GRID(grid), cw_ramp_width_b, 1, 6, 1, 1);
  g_signal_connect(cw_ramp_width_b, "value_changed", G_CALLBACK(cw_ramp_width_changed_cb), NULL);

  GtkWidget *paddle_label = gtk_label_new("Paddle Mode:");
  gtk_widget_set_name(paddle_label, "boldlabel");
  gtk_widget_set_halign(paddle_label, GTK_ALIGN_END);
  gtk_widget_show(paddle_label);
  gtk_grid_attach(GTK_GRID(grid), paddle_label, 0, 7, 1, 1);

  GtkWidget *mode_combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(mode_combo), NULL, "Straight Key");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(mode_combo), NULL, "Iambic Mode A");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(mode_combo), NULL, "Iambic Mode B");
  gtk_combo_box_set_active(GTK_COMBO_BOX(mode_combo), cw_keyer_mode);
  gtk_widget_show(mode_combo);
  gtk_grid_attach(GTK_GRID(grid), mode_combo, 1, 7, 1, 1);
  g_signal_connect(mode_combo, "changed", G_CALLBACK(cw_keyer_mode_cb), NULL);

  GtkWidget *cw_keys_reversed_b = gtk_check_button_new_with_label("Keys reversed");
  gtk_widget_set_name(cw_keys_reversed_b, "boldlabel");
  gtk_check_button_set_active (GTK_CHECK_BUTTON (cw_keys_reversed_b), cw_keys_reversed);
  gtk_widget_show(cw_keys_reversed_b);
  gtk_grid_attach(GTK_GRID(grid), cw_keys_reversed_b, 1, 8, 1, 1);
  g_signal_connect(cw_keys_reversed_b, "toggled", G_CALLBACK(cw_keys_reversed_cb), NULL);

  GtkWidget *cw_keyer_spacing_b = gtk_check_button_new_with_label("Enforce letter spacing");
  gtk_widget_set_name(cw_keyer_spacing_b, "boldlabel");
  gtk_check_button_set_active (GTK_CHECK_BUTTON (cw_keyer_spacing_b), cw_keyer_spacing);
  gtk_widget_show(cw_keyer_spacing_b);
  gtk_grid_attach(GTK_GRID(grid), cw_keyer_spacing_b, 0, 8, 1, 1);
  g_signal_connect(cw_keyer_spacing_b, "toggled", G_CALLBACK(cw_keyer_spacing_cb), NULL);

  gtk_box_append(GTK_BOX(content), grid);
  sub_menu = dialog;
  gtk_widget_show(dialog);
}
