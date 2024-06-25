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

#include "main.h"
#include "new_menu.h"
#include "exit_menu.h"
#include "discovered.h"
#include "discovery.h"
#include "radio.h"
#include "new_protocol.h"
#include "old_protocol.h"
#ifdef SOAPYSDR
  #include "soapy_protocol.h"
#endif
#include "actions.h"
#ifdef GPIO
  #include "gpio.h"
#endif
#include "message.h"
#ifdef SATURN
  #include "saturnmain.h"
#endif

static GtkWidget *dialog = NULL;

void stop_program() {
#ifdef GPIO
  gpio_close();
  t_print("%s: GPIO closed\n", __FUNCTION__);
#endif
#ifdef CLIENT_SERVER

  if (!radio_is_remote) {
#endif
    protocol_stop();
    t_print("%s: protocol stopped\n", __FUNCTION__);
    radio_stop();
    t_print("%s: radio stopped\n", __FUNCTION__);

    if (have_saturn_xdma) {
#ifdef SATURN
      saturn_exit();
#endif
    }

#ifdef CLIENT_SERVER
  }

#endif
  radioSaveState();
  t_print("%s: radio state saved\n", __FUNCTION__);
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

// cppcheck-suppress constParameterCallback
static gboolean exit_cb (GtkWidget *widget, GdkEvent *event, gpointer data) {
  stop_program();
  _exit(0);
}

// cppcheck-suppress constParameterCallback
static gboolean reboot_cb (GtkWidget *widget, GdkEvent *event, gpointer data) {
  stop_program();
  (void) system("reboot");
  _exit(0);
}

// cppcheck-suppress constParameterCallback
static gboolean shutdown_cb (GtkWidget *widget, GdkEvent *event, gpointer data) {
  stop_program();
  (void) system("shutdown -h -P now");
  _exit(0);
}

void exit_menu(GtkWidget *parent) {
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  gtk_window_set_title(GTK_WINDOW(dialog), "piHPSDR - Exit");
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_spacing (GTK_GRID(grid), 10);
  gtk_grid_set_row_spacing (GTK_GRID(grid), 10);
  gtk_grid_set_row_homogeneous(GTK_GRID(grid), TRUE);
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);

  gtk_widget_set_margin_start(grid, 10);
  gtk_widget_set_margin_end(grid, 10);
  gtk_widget_set_margin_top(grid, 10);
  gtk_widget_set_margin_bottom(grid, 10);

  int row = 0;
  int col = 0;
  row++;
  col = 0;
  GtkWidget *exit_b = gtk_button_new_with_label("Exit");
  g_signal_connect (exit_b, "clicked", G_CALLBACK(exit_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), exit_b, col, row, 1, 1);
  col++;
  GtkWidget *reboot_b = gtk_button_new_with_label("Reboot");
  g_signal_connect (reboot_b, "clicked", G_CALLBACK(reboot_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), reboot_b, col, row, 1, 1);
  col++;
  GtkWidget *shutdown_b = gtk_button_new_with_label("Shutdown");
  g_signal_connect (shutdown_b, "clicked", G_CALLBACK(shutdown_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), shutdown_b, col, row, 1, 1);
  gtk_box_append(GTK_BOX(content), grid);
  sub_menu = dialog;
  gtk_widget_show(dialog);
}
