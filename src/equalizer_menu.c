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
#include <semaphore.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <wdsp.h>

#include "new_menu.h"
#include "equalizer_menu.h"
#include "radio.h"
#include "ext.h"
#include "vfo.h"
#include "transmitter.h"
#include "message.h"

static GtkWidget *dialog = NULL;
static int dialog_width = 1;

static GtkWidget *scale[7];
static GtkWidget *freqspin[7];
static GtkWidget *enable_b;
static GtkWidget *sixband_b;

static int eqid;  // 0: RX1, 1: RX2, 2: TX

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

void update_eq() {
#ifdef CLIENT_SERVER

  if (radio_is_remote) {
    //
    // insert here any function to inform the client of equalizer setting
    // changes, if this becomes part of the protocol
    //
  } else {
#endif

    if (can_transmit) {
      tx_set_equalizer(transmitter);
    }

    for (int id = 0; id < receivers; id++) {
      receiver_set_equalizer(receiver[id]);
    }

#ifdef CLIENT_SERVER
  }

#endif
}

static void sixband_cb (GtkCheckButton *widget, gpointer data) {
  int val = gtk_check_button_get_active(GTK_CHECK_BUTTON(widget));
  int m;

  if (val) {
    gtk_widget_show(freqspin[5]);
    gtk_widget_show(freqspin[6]);
    gtk_widget_show(scale   [5]);
    gtk_widget_show(scale   [6]);
  } else {
    gtk_widget_hide(freqspin[5]);
    gtk_widget_hide(freqspin[6]);
    gtk_widget_hide(scale   [5]);
    gtk_widget_hide(scale   [6]);
  }
  gtk_window_set_default_size(GTK_WINDOW(dialog), dialog_width, 1);


  switch (eqid) {
  case 0:
  case 1:
    if (eqid < receivers) {
      receiver[eqid]->eq_sixband = val;
    }
    if (eqid == 0) {
      m = vfo[eqid].mode;
      mode_settings[m].rx_eq_sixband = val;
    }

    break;

  case 2:
    if (can_transmit) {
      transmitter->eq_sixband = val;
      m = vfo[get_tx_vfo()].mode;
      mode_settings[m].tx_eq_sixband = val;
    }

    break;
  }

  update_eq();
  g_idle_add(ext_vfo_update, NULL);
}

static void enable_cb (GtkCheckButton *widget, gpointer data) {
  int val = gtk_check_button_get_active(GTK_CHECK_BUTTON(widget));
  int m;

  switch (eqid) {
  case 0:
  case 1:
    if (eqid < receivers) {
      receiver[eqid]->eq_enable = val;
    }
    if (eqid == 0) {
      m = vfo[eqid].mode;
      mode_settings[m].en_rxeq = val;
    }

    break;

  case 2:
    if (can_transmit) {
      transmitter->eq_enable = val;
      m = vfo[get_tx_vfo()].mode;
      mode_settings[m].en_txeq = val;
    }

    break;
  }

  update_eq();
  g_idle_add(ext_vfo_update, NULL);
}

static void freq_changed_cb (GtkWidget *widget, gpointer data) {
  int i = GPOINTER_TO_INT(data);
  double val = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  int m;

  //
  // take care that we do not cross the frequencies of the
  // neighbouring spin buttons
  //
  if (i > 1) {
    double valmin = gtk_spin_button_get_value(GTK_SPIN_BUTTON(freqspin[i-1])) + 10.0;
    if (val < valmin) { val = valmin; }
  }
  if (i < 6) {
    double valmax = gtk_spin_button_get_value(GTK_SPIN_BUTTON(freqspin[i+1])) - 10.0;
    if (val > valmax) { val = valmax; }
  }
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(widget), val);
    
  switch (eqid) {
  case 0:
  case 1:
    if (eqid < receivers) {
      receiver[eqid]->eq_freq[i] = val;
    }
    if (eqid == 0) {
      m = vfo[eqid].mode;
      mode_settings[m].rx_eq_freq[i] = val;
    }

    break;

  case 2:
    if (can_transmit) {
      transmitter->eq_freq[i] = val;
      m = vfo[get_tx_vfo()].mode;
      mode_settings[m].tx_eq_freq[i] = val;
    }

    break;
  }

  update_eq();
}


static void gain_changed_cb (GtkWidget *widget, gpointer data) {
  int i = GPOINTER_TO_INT(data);
  double val = gtk_range_get_value(GTK_RANGE(widget));
  int m;

  switch (eqid) {
  case 0:
  case 1:
    if (eqid < receivers) {
      receiver[eqid]->eq_gain[i] = val;
    }
    if (eqid == 0) {
      m = vfo[eqid].mode;
      mode_settings[m].rx_eq_gain[i] = val;
    }

    break;

  case 2:
    if (can_transmit) {
      transmitter->eq_gain[i] = val;
      m = vfo[get_tx_vfo()].mode;
      mode_settings[m].tx_eq_gain[i] = val;
    }

    break;
  }

  update_eq();
}

//
// If RX2 is selected while only one RX is running, or if TX
// is selected but there is no transmitter, silently force
// the combo box back to RX1
// At the very end, update the "Enable" button
//
static void eqid_changed_cb(GtkWidget *widget, gpointer data) {
  eqid = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
  int six;

  switch (eqid) {
  case 0:
  case 1:
    if (eqid < receivers) {
      for (int i = 0; i < 7; i++) {
        gtk_range_set_value(GTK_RANGE(scale[i]), receiver[eqid]->eq_gain[i]);
      }

      for (int i = 1; i < 7; i++) {
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(freqspin[i]), receiver[eqid]->eq_freq[i]);
      }
    } else {
      gtk_combo_box_set_active(GTK_COMBO_BOX(widget), 0);
      eqid = 0;
    }

    break;

  case 2:
    if (can_transmit) {
      for (int i = 0; i < 7; i++) {
        gtk_range_set_value(GTK_RANGE(scale[i]), transmitter->eq_gain[i]);
      }

      for (int i = 1; i < 7; i++) {
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(freqspin[i]), transmitter->eq_freq[i]);
      }
    } else {
      gtk_combo_box_set_active(GTK_COMBO_BOX(widget), 0);
      eqid = 0;
    }
  }

  six = 0;  // silence "maybe uninitialized" compiler warnings
  switch (eqid) {
  case 0:
    six = receiver[0]->eq_sixband;
    gtk_check_button_set_active (GTK_CHECK_BUTTON (enable_b), receiver[0]->eq_enable);
    break;
  case 1:
    six = receiver[1]->eq_sixband;
    gtk_check_button_set_active (GTK_CHECK_BUTTON (enable_b), receiver[1]->eq_enable);
    break;
  case 2:
    six = transmitter->eq_sixband;
    gtk_check_button_set_active (GTK_CHECK_BUTTON (enable_b), transmitter->eq_enable);
    break;
  }
  gtk_check_button_set_active (GTK_CHECK_BUTTON (sixband_b), six);

  if (six) {
    gtk_widget_show(freqspin[5]);
    gtk_widget_show(freqspin[6]);
    gtk_widget_show(scale   [5]);
    gtk_widget_show(scale   [6]);
  } else {
    gtk_widget_hide(freqspin[5]);
    gtk_widget_hide(freqspin[6]);
    gtk_widget_hide(scale   [5]);
    gtk_widget_hide(scale   [6]);
  }
  gtk_window_set_default_size(GTK_WINDOW(dialog), dialog_width, 1);


}

void equalizer_menu(GtkWidget *parent) {
  eqid = 0;
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  gtk_window_set_title(GTK_WINDOW(dialog), "piHPSDR reimagined - Equalizer");
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);
  
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_spacing (GTK_GRID(grid), 10);
  gtk_grid_set_row_spacing (GTK_GRID(grid), 5);
  gtk_grid_set_row_homogeneous(GTK_GRID(grid), FALSE);
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);

  gtk_widget_set_margin_start(grid, 10);
  gtk_widget_set_margin_end(grid, 10);
  gtk_widget_set_margin_top(grid, 10);
  gtk_widget_set_margin_bottom(grid, 10);

  GtkWidget *eqid_combo_box = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(eqid_combo_box), NULL, "RX1 Equalizer Settings");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(eqid_combo_box), NULL, "RX2 Equalizer Settings");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(eqid_combo_box), NULL, "TX  Equalizer Settings");
  gtk_combo_box_set_active(GTK_COMBO_BOX(eqid_combo_box), 0);
  gtk_grid_attach(GTK_GRID(grid), eqid_combo_box, 2, 1, 2, 1);

  g_signal_connect(eqid_combo_box, "changed", G_CALLBACK(eqid_changed_cb), NULL);
  enable_b = gtk_check_button_new_with_label("Enable");
  gtk_widget_set_name(enable_b, "boldlabel");
  gtk_check_button_set_active (GTK_CHECK_BUTTON (enable_b), receiver[0]->eq_enable);
  g_signal_connect(enable_b, "toggled", G_CALLBACK(enable_cb), GINT_TO_POINTER(0));
  gtk_grid_attach(GTK_GRID(grid), enable_b, 0, 1, 1, 1);

  sixband_b = gtk_check_button_new_with_label("SixBands");
  gtk_widget_set_name(sixband_b, "boldlabel");
  gtk_check_button_set_active (GTK_CHECK_BUTTON(sixband_b), receiver[0]->eq_sixband);
  g_signal_connect(sixband_b, "toggled", G_CALLBACK(sixband_cb), GINT_TO_POINTER(0));
  gtk_grid_attach(GTK_GRID(grid), sixband_b, 1, 1, 1, 1);

  for (int i = 0; i < 7; i++) {
    if (i == 0) {
      GtkWidget *label = gtk_label_new("Preamp  ");
      gtk_widget_set_name(label, "boldlabel");
      gtk_widget_set_halign(label, GTK_ALIGN_START);
      gtk_grid_attach(GTK_GRID(grid), label, 0, 2, 1, 2);
    } else {
      freqspin[i] = gtk_spin_button_new_with_range(50.0, 16000.0, 10.0);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(freqspin[i]), receiver[0]->eq_freq[i]);
      g_signal_connect(freqspin[i], "value-changed", G_CALLBACK(freq_changed_cb), GINT_TO_POINTER(i));
      gtk_grid_attach(GTK_GRID(grid), freqspin[i], 0, 2 * i + 2, 1, 1);
    }

    scale[i] = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -12.0, 15.0, 1.0);
    gtk_range_set_increments (GTK_RANGE(scale[i]), 1.0, 1.0);
    gtk_range_set_value(GTK_RANGE(scale[i]), receiver[0]->eq_gain[i]);
    g_signal_connect(scale[i], "value-changed", G_CALLBACK(gain_changed_cb), GINT_TO_POINTER(i));
    gtk_grid_attach(GTK_GRID(grid), scale[i], 1, 2 * i + 2, 3, 2);
    gtk_scale_add_mark(GTK_SCALE(scale[i]), -12.0, GTK_POS_LEFT, "-12dB");
    gtk_scale_add_mark(GTK_SCALE(scale[i]), -9.0, GTK_POS_LEFT, NULL);
    gtk_scale_add_mark(GTK_SCALE(scale[i]), -6.0, GTK_POS_LEFT, NULL);
    gtk_scale_add_mark(GTK_SCALE(scale[i]), -3.0, GTK_POS_LEFT, NULL);
    gtk_scale_add_mark(GTK_SCALE(scale[i]), 0.0, GTK_POS_LEFT, "0dB");
    gtk_scale_add_mark(GTK_SCALE(scale[i]), 3.0, GTK_POS_LEFT, NULL);
    gtk_scale_add_mark(GTK_SCALE(scale[i]), 6.0, GTK_POS_LEFT, NULL);
    gtk_scale_add_mark(GTK_SCALE(scale[i]), 9.0, GTK_POS_LEFT, NULL);
    gtk_scale_add_mark(GTK_SCALE(scale[i]), 12.0, GTK_POS_LEFT, NULL);
    gtk_scale_add_mark(GTK_SCALE(scale[i]), 15.0, GTK_POS_LEFT, "15dB");
  }

  gtk_box_append(GTK_BOX(content), grid);
  sub_menu = dialog;
  gtk_widget_show(dialog);

  if (!receiver[0]->eq_sixband) {
    gtk_widget_hide(freqspin[5]);
    gtk_widget_hide(freqspin[6]);
    gtk_widget_hide(scale   [5]);
    gtk_widget_hide(scale   [6]);
  }
  //
  // For some unknown reason, the following gtk_window_resize emits dozens of 
  // "critical warnings" if run on RaspPi with a hsize smaller than the
  // current horizontal size. Therefore,
  // determin the current hsize and using that value in all window resize
  // calls
  //
  GtkAllocation alloc;
  gtk_widget_get_allocation(dialog,&alloc);
  dialog_width = alloc.width;
  gtk_window_set_default_size(GTK_WINDOW(dialog), dialog_width, 1);

}

