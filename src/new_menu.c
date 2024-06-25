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
#include <string.h>

#include "audio.h"
#include "new_menu.h"
#include "about_menu.h"
#include "exit_menu.h"
#include "radio_menu.h"
#include "rx_menu.h"
#include "ant_menu.h"
#include "display_menu.h"
#include "pa_menu.h"
#include "rigctl_menu.h"
#include "oc_menu.h"
#include "cw_menu.h"
#include "store_menu.h"
#include "xvtr_menu.h"
#include "equalizer_menu.h"
#include "radio.h"
#include "meter_menu.h"
#include "band_menu.h"
#include "bandstack_menu.h"
#include "mode_menu.h"
#include "filter_menu.h"
#include "noise_menu.h"
#include "agc_menu.h"
#include "vox_menu.h"
#include "diversity_menu.h"
#include "tx_menu.h"
#include "ps_menu.h"
#include "encoder_menu.h"
#include "switch_menu.h"
#include "toolbar_menu.h"
#include "vfo_menu.h"
#include "fft_menu.h"
#include "main.h"
#include "actions.h"
#include "gpio.h"
#include "old_protocol.h"
#include "new_protocol.h"
#ifdef CLIENT_SERVER
  #include "server_menu.h"
#endif
#ifdef MIDI
  #include "midi.h"
  #include "midi_menu.h"
#endif
#include "screen_menu.h"
#ifdef SATURN
  #include "saturn_menu.h"
#endif
#include "playcapture.h"
#include "effects.h"

GtkWidget *main_menu = NULL;
GtkWidget *sub_menu = NULL;

int active_menu = NO_MENU;

int menu_active_receiver_changed(void *data) {
  if (sub_menu != NULL) {
    gtk_window_destroy(GTK_WINDOW(sub_menu));
    sub_menu = NULL;
  }

  return FALSE;
}

static void cleanup() {
  if (main_menu != NULL) {
    gtk_window_destroy(GTK_WINDOW(main_menu));
    main_menu = NULL;
  }

  // if (sub_menu != NULL) {
  //   gtk_window_destroy(GTK_WINDOW(sub_menu));
  //   sub_menu = NULL;
  // }

  active_menu = NO_MENU;
}

static gboolean close_cb () {
  cleanup();
  return TRUE;
}

//
// The "Restart" button restarts the protocol
// This may help to recover from certain error conditions
// Hitting this button automatically closes the menu window via cleanup()
//
// cppcheck-suppress constParameterCallback
static gboolean restart_cb (GtkWidget *widget, GdkEvent *event, gpointer data) {
  cleanup();
  protocol_restart();
  return TRUE;
}

//
// This functionality may be useful in full-screen-mode where there is
// no top bar with an "Iconify" button.
//
static gboolean minimize_cb(GtkWidget *widget, GdkEvent *event, gpointer data) {
  cleanup();
  GdkSurface *surface = gtk_native_get_surface(gtk_widget_get_native(GTK_WIDGET(main_window)));
  GdkToplevel *toplevel = GDK_TOPLEVEL(surface);
  gdk_toplevel_minimize(toplevel);
  return TRUE;
}

#ifdef SATURN
// cppcheck-suppress constParameterCallback
static gboolean saturn_cb (GtkWidget *widget, GdkEvent *event, gpointer data) {
  cleanup();
  saturn_menu(main_window);
  return TRUE;
}
#endif

static gboolean about_cb (GtkWidget *widget, gpointer data) {
  cleanup();
  about_menu(main_window);
  return TRUE;
}

static gboolean exit_cb (GtkWidget *widget,  gpointer data) {
  cleanup();
  exit_menu(main_window);
  return TRUE;
}

static gboolean radio_cb (GtkWidget *widget, gpointer data) {
  cleanup();
  radio_menu(main_window);
  return TRUE;
}

// cppcheck-suppress constParameterCallback
static gboolean rx_cb (GtkWidget *widget,  gpointer data) {
  start_rx();
  return TRUE;
}

static gboolean ant_cb (GtkWidget *widget,  gpointer data) {
  cleanup();
  ant_menu(main_window);
  return TRUE;
}

static gboolean display_cb (GtkWidget *widget, GdkEvent *event, gpointer data) {
  cleanup();
  display_menu(main_window, active_receiver->id);
  return TRUE;
}

static gboolean pa_cb (GtkWidget *widget, GdkEvent *event, gpointer data) {
  cleanup();
  pa_menu(main_window);
  return TRUE;
}

static gboolean rigctl_cb (GtkWidget *widget, GdkEvent *event, gpointer data) {
  cleanup();
  rigctl_menu(main_window);
  return TRUE;
}

static gboolean play_cb (GtkWidget *widget, GdkEvent *event, gpointer data) {
  cleanup();
  play_capture_dialog(main_window);
  return TRUE;
}

static gboolean effects_cb (GtkWidget *widget, GdkEvent *event, gpointer data) {
  cleanup();
  show_effects_dialog(main_window);
  return TRUE;
}

//#ifdef GPIO
static gboolean encoder_cb (GtkWidget *widget, GdkEvent *event, gpointer data) {
  cleanup();
  encoder_menu(main_window);
  return TRUE;
}

static gboolean switch_cb (GtkWidget *widget, GdkEvent *event, gpointer data) {
  cleanup();
  switch_menu(main_window);
  return TRUE;
}
//#endif

static gboolean toolbar_cb (GtkWidget *widget, GdkEvent *event, gpointer data) {
  cleanup();
  toolbar_menu(main_window);
  return TRUE;
}

static gboolean cw_cb (GtkWidget *widget, GdkEvent *event, gpointer data) {
  cleanup();
  cw_menu(main_window);
  return TRUE;
}

static gboolean oc_cb (GtkWidget *widget, GdkEvent *event, gpointer data) {
  cleanup();
  oc_menu(main_window);
  return TRUE;
}

static gboolean xvtr_cb (GtkWidget *widget, GdkEvent *event, gpointer data) {
  cleanup();
  xvtr_menu(main_window);
  return TRUE;
}

static gboolean equalizer_cb (GtkWidget *widget, GdkEvent *event, gpointer data) {
  cleanup();
  equalizer_menu(main_window);
  return TRUE;
}

void start_rx() {
  cleanup();
  rx_menu(main_window);
}

void start_meter() {
  cleanup();
  meter_menu(main_window);
}

// cppcheck-suppress constParameterCallback
static gboolean meter_cb (GtkWidget *widget, GdkEvent *event, gpointer data) {
  start_meter();
  return TRUE;
}

void start_band() {
  int old_menu = active_menu;
  cleanup();

  if (old_menu != BAND_MENU) {
    band_menu(main_window);
    active_menu = BAND_MENU;
  }
}

void start_bandstack() {
  int old_menu = active_menu;
  cleanup();

  if (old_menu != BANDSTACK_MENU) {
    bandstack_menu(main_window);
    active_menu = BANDSTACK_MENU;
  }
}

void start_mode() {
  int old_menu = active_menu;
  cleanup();

  if (old_menu != MODE_MENU) {
    mode_menu(main_window);
    active_menu = MODE_MENU;
  }
}

void start_filter() {
  int old_menu = active_menu;
  cleanup();

  if (old_menu != FILTER_MENU) {
    filter_menu(main_window);
    active_menu = FILTER_MENU;
  }
}

// cppcheck-suppress constParameterCallback
static gboolean mode_cb (GtkWidget *widget, GdkEvent *event, gpointer data) {
  start_mode();
  return TRUE;
}

// cppcheck-suppress constParameterCallback
static gboolean filter_cb (GtkWidget *widget, GdkEvent *event, gpointer data) {
  start_filter();
  return TRUE;
}

// cppcheck-suppress constParameterCallback
static gboolean noise_cb (GtkWidget *widget, GdkEvent *event, gpointer data) {
  start_noise();
  return TRUE;
}

// cppcheck-suppress constParameterCallback
static gboolean vfo_cb (GtkWidget *widget, GdkEvent *event, gpointer data) {
  start_vfo(active_receiver->id);
  return TRUE;
}

// cppcheck-suppress constParameterCallback
static gboolean band_cb (GtkWidget *widget, GdkEvent *event, gpointer data) {
  start_band();
  return TRUE;
}

// cppcheck-suppress constParameterCallback
static gboolean bstk_cb (GtkWidget *widget, GdkEvent *event, gpointer data) {
  start_bandstack();
  return TRUE;
}

// cppcheck-suppress constParameterCallback
static gboolean store_cb (GtkWidget *widget, GdkEvent *event, gpointer data) {
  start_store();
  return TRUE;
}

void start_noise() {
  int old_menu = active_menu;
  cleanup();

  if (old_menu != NOISE_MENU) {
    noise_menu(main_window);
    active_menu = NOISE_MENU;
  }
}

// cppcheck-suppress constParameterCallback
static gboolean agc_cb (GtkWidget *widget, GdkEvent *event, gpointer data) {
  start_agc();
  return TRUE;
}

void start_agc() {
  int old_menu = active_menu;
  cleanup();

  if (old_menu != AGC_MENU) {
    agc_menu(main_window);
    active_menu = AGC_MENU;
  }
}

void start_vox() {
  cleanup();
  vox_menu(main_window);
}

// cppcheck-suppress constParameterCallback
static gboolean vox_cb (GtkWidget *widget, GdkEvent *event, gpointer data) {
  start_vox();
  return TRUE;
}

void start_dsp() {
  cleanup();
  fft_menu(main_window);
}

// cppcheck-suppress constParameterCallback
static gboolean dsp_cb (GtkWidget *widget, GdkEvent *event, gpointer data) {
  start_dsp();
  return TRUE;
}

void start_diversity() {
  cleanup();
  diversity_menu(main_window);
}

static gboolean screen_cb (GtkWidget *widget, GdkEvent *event, gpointer data) {
  cleanup();
  screen_menu(main_window);
  return TRUE;
}

// cppcheck-suppress constParameterCallback
static gboolean diversity_cb (GtkWidget *widget, GdkEvent *event, gpointer data) {
  start_diversity();
  return TRUE;
}

void start_vfo(int vfo) {
  int old_menu = active_menu;
  cleanup();

  if (old_menu != VFO_MENU) {
    vfo_menu(main_window, vfo);
    active_menu = VFO_MENU;
  }
}

void start_store() {
  int old_menu = active_menu;
  cleanup();

  if (old_menu != STORE_MENU) {
    store_menu(main_window);
    active_menu = STORE_MENU;
  }
}

void start_tx() {
  cleanup();

  if (can_transmit) {
    tx_menu(main_window);
  }
}

// cppcheck-suppress constParameterCallback
static gboolean tx_cb (GtkWidget *widget, GdkEvent *event, gpointer data) {
  start_tx();
  return TRUE;
}

void start_ps() {
  cleanup();

  if (can_transmit) {
    ps_menu(main_window);
  }
}

// cppcheck-suppress constParameterCallback
static gboolean ps_cb (GtkWidget *widget, GdkEvent *event, gpointer data) {
  start_ps();
  return TRUE;
}

#ifdef CLIENT_SERVER
void start_server() {
  cleanup();
  server_menu(main_window);
}

// cppcheck-suppress constParameterCallback
static gboolean server_cb (GtkWidget *widget, GdkEvent *event, gpointer data) {
  start_server();
  return TRUE;
}
#endif

#ifdef MIDI
void start_midi() {
  cleanup();
  midi_menu(main_window);
}

// cppcheck-suppress constParameterCallback
static gboolean midi_cb (GtkWidget *widget, GdkEvent *event, gpointer data) {
  start_midi();
  return TRUE;
}
#endif

// test
void on_window_realize(GtkWidget *widget, gpointer data) {
    GtkWindow *window = GTK_WINDOW(widget);
    gtk_window_set_decorated(window, TRUE); 
    gtk_window_set_resizable(window, FALSE);
}

void new_menu() {
  int col, row, maxrow;

  if (sub_menu != NULL) {
    gtk_window_destroy(GTK_WINDOW(sub_menu));
    
    sub_menu = NULL;
  }

  if (main_menu == NULL) {
    main_menu = gtk_dialog_new();
    gtk_window_set_transient_for(GTK_WINDOW(main_menu), GTK_WINDOW(main_window));
    gtk_window_set_title(GTK_WINDOW(main_menu), "piHPSDR reimagined - Menu");
    g_signal_connect(main_menu, "destroy", G_CALLBACK(close_cb), NULL);
    g_signal_connect(main_menu, "realize", G_CALLBACK(on_window_realize), NULL);
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(main_menu));
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing (GTK_GRID(grid), 15);
    gtk_grid_set_row_spacing (GTK_GRID(grid), 15);
    gtk_grid_set_row_homogeneous(GTK_GRID(grid), FALSE);
    gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);

    gtk_widget_set_margin_start(grid, 10);
    gtk_widget_set_margin_end(grid, 10);
    gtk_widget_set_margin_top(grid, 10);
    gtk_widget_set_margin_bottom(grid, 10);

    //
    // First row is reserved for Restart/Exit
    //
   
    GtkWidget *restart_b = gtk_button_new_with_label("Restart");
    g_signal_connect (restart_b, "clicked", G_CALLBACK(restart_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), restart_b, 0, 0, 2, 1);
    GtkWidget *exit_b = gtk_button_new_with_label("Exit piHPSDR");
    g_signal_connect (exit_b, "clicked", G_CALLBACK(exit_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), exit_b, 4, 0, 2, 1);
    //
    // Insert small separation between top column the the "many buttons"
    //
    GtkWidget *TopSeparator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_size_request(TopSeparator, -1, 3);
    gtk_grid_attach(GTK_GRID(grid), TopSeparator, 0, 1, 6, 1);
    row = maxrow = 2;
    col = 0;
    //
    // First Column: Menus related to the Radio in general.
    //               Radio/Screen/Display/Meter/XVTR
    //
    GtkWidget *radio_b = gtk_button_new_with_label("Radio");
    g_signal_connect (radio_b, "clicked", G_CALLBACK(radio_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), radio_b, col, row, 1, 1);
    row++;
    GtkWidget *screen_b = gtk_button_new_with_label("Screen");
    g_signal_connect (screen_b, "clicked", G_CALLBACK(screen_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), screen_b, col, row, 1, 1);
    row++;
    GtkWidget *display_b = gtk_button_new_with_label("Display");
    g_signal_connect (display_b, "clicked", G_CALLBACK(display_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), display_b, col, row, 1, 1);
    row++;
    GtkWidget *meter_b = gtk_button_new_with_label("Meter");
    g_signal_connect (meter_b, "clicked", G_CALLBACK(meter_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), meter_b, col, row, 1, 1);
    row++;
    GtkWidget *xvtr_b = gtk_button_new_with_label("XVTR");
    g_signal_connect (xvtr_b, "clicked", G_CALLBACK(xvtr_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), xvtr_b, col, row, 1, 1);
    row++;
#ifdef SATURN

    if (have_saturn_xdma) { // only display on the xdma client
      GtkWidget *saturn_b = gtk_button_new_with_label("Saturn");
      g_signal_connect (saturn_b, "clicked", G_CALLBACK(saturn_cb), NULL);
      gtk_grid_attach(GTK_GRID(grid), saturn_b, col, row, 1, 1);
      row++;
    }

#endif
#ifdef CLIENT_SERVER
    GtkWidget *server_b = gtk_button_new_with_label("Server");
    g_signal_connect (server_b, "clicked", G_CALLBACK(server_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), server_b, col, row, 1, 1);
    row++;
#endif

    // cppcheck-suppress knownConditionTrueFalse
    if (row > maxrow) { maxrow = row; }

    row = 2;
    col++;
    //
    // Second column: VFO-related menus
    //                FREQ, BAND, BStack, MODE, MEM
    //
    GtkWidget *vfo_b = gtk_button_new_with_label("VFO");
    g_signal_connect (vfo_b, "clicked", G_CALLBACK(vfo_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), vfo_b, col, row++, 1, 1);
    GtkWidget *band_b = gtk_button_new_with_label("Band");
    g_signal_connect (band_b, "clicked", G_CALLBACK(band_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), band_b, col, row++, 1, 1);
    GtkWidget *bstk_b = gtk_button_new_with_label("BndStack");
    g_signal_connect (bstk_b, "clicked", G_CALLBACK(bstk_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), bstk_b, col, row++, 1, 1);
    GtkWidget *mode_b = gtk_button_new_with_label("Mode");
    g_signal_connect (mode_b, "clicked", G_CALLBACK(mode_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), mode_b, col, row++, 1, 1);
    GtkWidget *store_b = gtk_button_new_with_label("Memory");
    g_signal_connect (store_b, "clicked", G_CALLBACK(store_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), store_b, col, row++, 1, 1);

    // cppcheck-suppress knownConditionTrueFalse
    if (row > maxrow) { maxrow = row; }

    row = 2;
    col++;
    //
    // Third column:  RX-related menus
    //                RX/Filter/Noise/AGC/Diversity
    //
    GtkWidget *rx_b = gtk_button_new_with_label("RX");
    g_signal_connect (rx_b, "clicked", G_CALLBACK(rx_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), rx_b, col, row, 1, 1);
    row++;
    GtkWidget *filter_b = gtk_button_new_with_label("Filter");
    g_signal_connect (filter_b, "clicked", G_CALLBACK(filter_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), filter_b, col, row, 1, 1);
    row++;
    GtkWidget *noise_b = gtk_button_new_with_label("Noise");
    g_signal_connect (noise_b, "clicked", G_CALLBACK(noise_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), noise_b, col, row, 1, 1);
    row++;
    GtkWidget *agc_b = gtk_button_new_with_label("AGC");
    g_signal_connect (agc_b, "clicked", G_CALLBACK(agc_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), agc_b, col, row, 1, 1);
    row++;

    if (RECEIVERS == 2 && n_adc > 1) {
      GtkWidget *diversity_b = gtk_button_new_with_label("Diversity");
      g_signal_connect (diversity_b, "clicked", G_CALLBACK(diversity_cb), NULL);
      gtk_grid_attach(GTK_GRID(grid), diversity_b, col, row, 1, 1);
      row++;
    }

    if (row > maxrow) { maxrow = row; }

    row = 2;
    col++;

    //
    // Fourth column:  TX-related menus
    //                 TX, PA, VOX, PS, CW
    //
    if (can_transmit) {
      GtkWidget *tx_b = gtk_button_new_with_label("TX");
      g_signal_connect (tx_b, "clicked", G_CALLBACK(tx_cb), NULL);
      gtk_grid_attach(GTK_GRID(grid), tx_b, col, row, 1, 1);
      row++;
      GtkWidget *pa_b = gtk_button_new_with_label("PA");
      g_signal_connect (pa_b, "clicked", G_CALLBACK(pa_cb), NULL);
      gtk_grid_attach(GTK_GRID(grid), pa_b, col, row, 1, 1);
      row++;
      GtkWidget *vox_b = gtk_button_new_with_label("VOX");
      g_signal_connect (vox_b, "clicked", G_CALLBACK(vox_cb), NULL);
      gtk_grid_attach(GTK_GRID(grid), vox_b, col, row, 1, 1);
      row++;

      if (protocol == ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL) {
        GtkWidget *ps_b = gtk_button_new_with_label("PS");
        g_signal_connect (ps_b, "clicked", G_CALLBACK(ps_cb), NULL);
        gtk_grid_attach(GTK_GRID(grid), ps_b, col, row, 1, 1);
        row++;
      }
    }

    GtkWidget *cw_b = gtk_button_new_with_label("CW");
    g_signal_connect (cw_b, "clicked", G_CALLBACK(cw_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), cw_b, col, row, 1, 1);
    row++;

    if (row > maxrow) { maxrow = row; }

    row = 2;
    col++;
    //
    // Fifth column: Menus for RX and TX
    //               DSP, Equalizer, Meter, Ant, OC
    //
    GtkWidget *dsp_b = gtk_button_new_with_label("DSP");
    g_signal_connect (dsp_b, "clicked", G_CALLBACK(dsp_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), dsp_b, col, row, 1, 1);
    row++;
    GtkWidget *equalizer_b = gtk_button_new_with_label("Equalizer");
    g_signal_connect (equalizer_b, "clicked", G_CALLBACK(equalizer_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), equalizer_b, col, row, 1, 1);
    row++;
    GtkWidget *ant_b = gtk_button_new_with_label("Ant");
    g_signal_connect (ant_b, "clicked", G_CALLBACK(ant_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), ant_b, col, row, 1, 1);
    row++;

    if (protocol == ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL) {
      GtkWidget *oc_b = gtk_button_new_with_label("OC");
      g_signal_connect (oc_b, "clicked", G_CALLBACK(oc_cb), NULL);
      gtk_grid_attach(GTK_GRID(grid), oc_b, col, row, 1, 1);
      row++;
    }

    if (row > maxrow) { maxrow = row; }

    row = 2;
    col++;
    //
    // Sixth column: Menus for controlling piHPSDR
    //               Toolbar, RigCtl, MIDI, Encoders, Switches
    //
    GtkWidget *toolbar_b = gtk_button_new_with_label("Toolbar");
    g_signal_connect (toolbar_b, "clicked", G_CALLBACK(toolbar_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), toolbar_b, col, row, 1, 1);
    row++;
    GtkWidget *rigctl_b = gtk_button_new_with_label("RigCtl");
    g_signal_connect (rigctl_b, "clicked", G_CALLBACK(rigctl_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), rigctl_b, col, row, 1, 1);
    row++;
#ifdef MIDI
    GtkWidget *midi_b = gtk_button_new_with_label("MIDI");
    g_signal_connect (midi_b, "clicked", G_CALLBACK(midi_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), midi_b, col, row, 1, 1);
    row++;
#endif
#ifdef GPIO

    if (controller != NO_CONTROLLER) {
      GtkWidget *encoders_b = gtk_button_new_with_label("Encoders");
      g_signal_connect (encoders_b, "clicked", G_CALLBACK(encoder_cb), NULL);
      gtk_grid_attach(GTK_GRID(grid), encoders_b, col, row, 1, 1);
      row++;
    }

    //
    // Note the switches of CONTROLLER1 are assigned via the Toolbar menu
    //
    if (controller != NO_CONTROLLER && controller != CONTROLLER1) {
      GtkWidget *switches_b = gtk_button_new_with_label("Switches");
      g_signal_connect (switches_b, "clicked", G_CALLBACK(switch_cb), NULL);
      gtk_grid_attach(GTK_GRID(grid), switches_b, col, row, 1, 1);
      row++;
    }

#endif
    GtkWidget *playcapturebtn_b = gtk_button_new_with_label("Play");
    g_signal_connect (playcapturebtn_b, "clicked", G_CALLBACK(play_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), playcapturebtn_b, col, row, 1, 1);
    row++;    

    GtkWidget *effects_b = gtk_button_new_with_label("Effects");
    g_signal_connect (effects_b, "clicked", G_CALLBACK(effects_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), effects_b, col, row, 1, 1);
    row++;   

    // cppcheck-suppress redundantAssignment
    //row = maxrow;
    //
    // Insert small separation between the "many buttons" and the bottom row
    //
     GtkWidget *BotSeparator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
     gtk_widget_set_size_request(BotSeparator, -1, 3);
     gtk_grid_attach(GTK_GRID(grid), BotSeparator, 0, ++row, 6, 1);
    row++;
    //
    // Last row: About and Iconify Button
    //
    GtkWidget *about_b = gtk_button_new_with_label("About");
    g_signal_connect (about_b, "clicked", G_CALLBACK(about_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), about_b, 0, row, 2, 1);

    GtkWidget *minimize_b = gtk_button_new_with_label("Iconify");
    g_signal_connect (minimize_b, "clicked", G_CALLBACK(minimize_cb), NULL);
    gtk_grid_attach(GTK_GRID(grid), minimize_b, 4, row, 2, 1);
    gtk_box_append(GTK_BOX(content), grid);
    //gtk_widget_show(main_menu);
    gtk_window_present (GTK_WINDOW(main_menu));
  } else {
    gtk_window_destroy(GTK_WINDOW(main_menu));
    main_menu = NULL;
  }
}
