/*   Copyright (C) 2024 - Mark Rutherford, KB2YCW
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
#include <stdlib.h>
#include "new_menu.h"
#include "effects.h"


GtkWidget *dialog;
double echo_delay_seconds = INITIAL_ECHO_DELAY_SECONDS;
double echo_decay = INITIAL_ECHO_DECAY;
gboolean echo_enabled = FALSE;
int echo_buffer_length = (int)(SAMPLE_RATE * INITIAL_ECHO_DELAY_SECONDS);
double *echo_buffer = NULL;
int echo_buffer_index = 0;

static void on_echo_enabled_toggled(GtkToggleButton *toggle_button, gpointer user_data) {
    echo_enabled = gtk_check_button_get_active(GTK_CHECK_BUTTON(toggle_button));
}

static void on_echo_delay_changed(GtkRange *range, gpointer user_data) {
    echo_delay_seconds = gtk_range_get_value(range);

    // Reallocate echo buffer based on delay time
    free(echo_buffer);
    echo_buffer_length = (int)(SAMPLE_RATE * echo_delay_seconds);
    echo_buffer = (double *)malloc(echo_buffer_length * sizeof(double));
    for (int i = 0; i < echo_buffer_length; ++i) {
        echo_buffer[i] = 0.0;
    }
}

static void on_echo_decay_changed(GtkRange *range, gpointer user_data) {
    echo_decay = gtk_range_get_value(range);
}


static void cleanup() {
    if (dialog != NULL) {
        GtkWidget *tmp = dialog;
        dialog = NULL;
        gtk_window_destroy(GTK_WINDOW(tmp));
        sub_menu = NULL;
        active_menu = NO_MENU;
    }
}

static gboolean close_cb () {
    cleanup();
    return TRUE;
}

void show_effects_dialog(gpointer parent) {
    dialog = gtk_window_new();
    gtk_window_set_transient_for(GTK_WINDOW(dialog), parent);
    gtk_window_set_title(GTK_WINDOW(dialog), "piHPSDR reimagined - Effects");
    gtk_window_set_default_size(GTK_WINDOW(dialog), 400, 300);
    g_signal_connect(dialog, "destroy", G_CALLBACK(close_cb), NULL);

    GtkWidget *content_area = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_window_set_child(GTK_WINDOW(dialog), content_area);

    // Create the echo enabled checkbox
    GtkWidget *echo_enabled_check = gtk_check_button_new_with_label("Enable Echo");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(echo_enabled_check), echo_enabled);
    g_signal_connect(echo_enabled_check, "toggled", G_CALLBACK(on_echo_enabled_toggled), NULL);
    gtk_box_append(GTK_BOX(content_area), echo_enabled_check);

    // Create the echo delay slider
    GtkWidget *echo_delay_label = gtk_label_new("Echo Delay (seconds)");
    gtk_box_append(GTK_BOX(content_area), echo_delay_label);
    GtkWidget *echo_delay_slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.01, 1.0, 0.01);
    gtk_range_set_value(GTK_RANGE(echo_delay_slider), echo_delay_seconds);
    g_signal_connect(echo_delay_slider, "value-changed", G_CALLBACK(on_echo_delay_changed), NULL);
    gtk_box_append(GTK_BOX(content_area), echo_delay_slider);

    // Create the echo decay slider
    GtkWidget *echo_decay_label = gtk_label_new("Echo Decay");
    gtk_box_append(GTK_BOX(content_area), echo_decay_label);
    GtkWidget *echo_decay_slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.1, 1.0, 0.01);
    gtk_range_set_value(GTK_RANGE(echo_decay_slider), echo_decay);
    g_signal_connect(echo_decay_slider, "value-changed", G_CALLBACK(on_echo_decay_changed), NULL);
    gtk_box_append(GTK_BOX(content_area), echo_decay_slider);

    gtk_widget_show(dialog);
}
