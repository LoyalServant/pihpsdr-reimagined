/* Copyright (C)
* 2020 - John Melton, G0ORX/N6LYT
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
#include <gdk/gdk.h>
#include <math.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>
#ifdef _WIN32
#include <WinSock2.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif
#include "radio.h"
#include "protocols.h"
#include "property.h"

static GtkWidget *dialog;

gboolean enable_protocol_1;
gboolean enable_protocol_2;
gboolean enable_soapy_protocol;
gboolean enable_stemlab;
gboolean enable_usbozy;
gboolean enable_saturn_xdma;
gboolean autostart;

static void protocolsSaveState() {
  clearProperties();
  SetPropI0("enable_protocol_1",     enable_protocol_1);
  SetPropI0("enable_protocol_2",     enable_protocol_2);
  SetPropI0("enable_soapy_protocol", enable_soapy_protocol);
  SetPropI0("enable_stemlab",        enable_stemlab);
  SetPropI0("enable_usbozy",         enable_usbozy);
  SetPropI0("enable_saturn_xdma",    enable_saturn_xdma);
  SetPropI0("autostart",             autostart);
  saveProperties("protocols.props");
}

void protocolsRestoreState() {
  loadProperties("protocols.props");
  //
  // Set defauls
  //
  enable_protocol_1 = TRUE;
  enable_protocol_2 = TRUE;
  enable_stemlab = TRUE;
  enable_usbozy = TRUE;
  enable_soapy_protocol = TRUE;
  enable_saturn_xdma = TRUE;
  autostart = FALSE;
  GetPropI0("enable_protocol_1",     enable_protocol_1);
  GetPropI0("enable_protocol_2",     enable_protocol_2);
  GetPropI0("enable_soapy_protocol", enable_soapy_protocol);
  GetPropI0("enable_stemlab",        enable_stemlab);
  GetPropI0("enable_usbozy",         enable_usbozy);
  GetPropI0("enable_saturn_xdma",    enable_saturn_xdma);
  GetPropI0("autostart",             autostart);
  clearProperties();
}

static void cleanup() {
  if (dialog != NULL) {
    gtk_window_destroy(GTK_WINDOW(dialog));
    dialog = NULL;
    protocolsSaveState();
  }
}

static gboolean close_cb () {
  cleanup();
  return TRUE;
}

static void protocol_1_cb(GtkCheckButton *widget, gpointer data) {
  enable_protocol_1 = gtk_check_button_get_active(widget);
}

static void protocol_2_cb(GtkCheckButton *widget, gpointer data) {
  enable_protocol_2 = gtk_check_button_get_active(widget);
}

#ifdef SOAPYSDR
static void soapy_protocol_cb(GtkCheckButton *widget, gpointer data) {
  enable_soapy_protocol = gtk_check_button_get_active(widget);
}
#endif

#ifdef STEMLAB_DISCOVERY
static void stemlab_cb(GtkCheckButton *widget, gpointer data) {
  enable_stemlab = gtk_check_button_get_active(widget);
}
#endif

#ifdef SATURN
static void saturn_xdma_cb(GtkCheckButton *widget, gpointer data) {
  enable_saturn_xdma = gtk_check_button_get_active(widget);
}
#endif

#ifdef USBOZY
static void usbozy_cb(GtkCheckButton *widget, gpointer data) {
  enable_usbozy = gtk_check_button_get_active(widget);
}
#endif

static void autostart_cb(GtkCheckButton *widget, gpointer data) {
  autostart = gtk_check_button_get_active(widget);
}

void configure_protocols(GtkWidget *parent) {
  int row;
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  gtk_window_set_title(GTK_WINDOW(dialog),"piHPSDR - Protocols");
  g_signal_connect (dialog, "destroy", G_CALLBACK(close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_row_homogeneous(GTK_GRID(grid), TRUE);
  row = 0;
  
  GtkWidget *b_enable_protocol_1 = gtk_check_button_new_with_label("Enable Protocol 1");
  gtk_check_button_set_active(GTK_CHECK_BUTTON(b_enable_protocol_1), enable_protocol_1);
  gtk_widget_show(b_enable_protocol_1);
  g_signal_connect(b_enable_protocol_1, "toggled", G_CALLBACK(protocol_1_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), b_enable_protocol_1, 0, row, 1, 1);
  row++;

  GtkWidget *b_enable_protocol_2 = gtk_check_button_new_with_label("Enable Protocol 2");
  gtk_check_button_set_active(GTK_CHECK_BUTTON(b_enable_protocol_2), enable_protocol_2);
  gtk_widget_show(b_enable_protocol_2);
  g_signal_connect(b_enable_protocol_2, "toggled", G_CALLBACK(protocol_2_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), b_enable_protocol_2, 0, row, 1, 1);
  row++;

#ifdef SATURN
  GtkWidget *b_saturn_xdma = gtk_check_button_new_with_label("Enable Saturn XDMA");
  gtk_check_button_set_active(GTK_CHECK_BUTTON(b_saturn_xdma), enable_saturn_xdma);
  gtk_widget_show(b_saturn_xdma);
  g_signal_connect(b_saturn_xdma, "toggled", G_CALLBACK(saturn_xdma_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), b_saturn_xdma, 0, row, 1, 1);
  row++;
#endif

#ifdef USBOZY
  GtkWidget *b_usbozy = gtk_check_button_new_with_label("Enable USB OZY");
  gtk_check_button_set_active(GTK_CHECK_BUTTON(b_usbozy), enable_usbozy);
  gtk_widget_show(b_usbozy);
  g_signal_connect(b_usbozy, "toggled", G_CALLBACK(usbozy_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), b_usbozy, 0, row, 1, 1);
  row++;
#endif

#ifdef SOAPYSDR
  GtkWidget *b_enable_soapy_protocol = gtk_check_button_new_with_label("Enable SoapySDR");
  gtk_check_button_set_active(GTK_CHECK_BUTTON(b_enable_soapy_protocol), enable_soapy_protocol);
  gtk_widget_show(b_enable_soapy_protocol);
  g_signal_connect(b_enable_soapy_protocol, "toggled", G_CALLBACK(soapy_protocol_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), b_enable_soapy_protocol, 0, row, 1, 1);
  row++;
#endif

#ifdef STEMLAB_DISCOVERY
  GtkWidget *b_enable_stemlab = gtk_check_button_new_with_label("Enable STEMlab");
  gtk_check_button_set_active(GTK_CHECK_BUTTON(b_enable_stemlab), enable_stemlab);
  gtk_widget_show(b_enable_stemlab);
  g_signal_connect(b_enable_stemlab, "toggled", G_CALLBACK(stemlab_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), b_enable_stemlab, 0, row, 1, 1);
  row++;
#endif

  GtkWidget *b_autostart = gtk_check_button_new_with_label("Auto start if only one device");
  gtk_check_button_set_active(GTK_CHECK_BUTTON(b_autostart), autostart);
  gtk_widget_show(b_autostart);
  g_signal_connect(b_autostart, "toggled", G_CALLBACK(autostart_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), b_autostart, 0, row, 1, 1);
  gtk_box_append(GTK_BOX(content), grid);
  gtk_widget_show(dialog);
}
