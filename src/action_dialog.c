/* Copyright (C)
* 2021 - John Melton, G0ORX/N6LYT
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
#include "actions.h" 
#include "gpio.h"
#include "toolbar.h"
#include "action_dialog.h"

typedef struct {
    GtkWidget *dialog;
    GtkWidget *active_button;
    int selected_action;
    SWITCH *sw;
    ENCODER *en;
    int BTS;
    GCallback update_callback;
    gpointer callback_data;
} DialogData;


static void toggle_button_clicked(GtkToggleButton *button, gpointer user_data) {
    DialogData *data = (DialogData *)user_data;
    int action_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "action_id"));

    if (data->active_button && data->active_button != GTK_WIDGET(button)) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(data->active_button), FALSE);
    }

    if (gtk_toggle_button_get_active(button)) {
        data->active_button = GTK_WIDGET(button);
        data->selected_action = action_id;
    } else {
        data->active_button = NULL;
    }
}

static void ok_button_clicked(GtkButton *button, gpointer user_data) {
    DialogData *data = (DialogData *)user_data;
    if (data->sw != NULL) {
        data->sw->switch_function = data->selected_action;
        update_toolbar_labels();
    } else {
        // probably an encoder.
        if (data->BTS == 1) {
            data->en->bottom_encoder_function = data->selected_action;
        }
        else if (data->BTS == 2) {
            data->en->top_encoder_function = data->selected_action;
        }
        else if (data->BTS == 3) {
            data->en->switch_function = data->selected_action;
        }
    }
    
    if (data->update_callback && data->callback_data) {
        ((void (*)(GtkWidget *, int))data->update_callback)(data->callback_data, data->selected_action);
    }

    gtk_window_destroy(GTK_WINDOW(data->dialog));
    g_free(data);
}


void select_action_dialog(GtkWidget *parent, int filter, SWITCH *sw, ENCODER *en, int BTS, GCallback update_callback, gpointer callback_data) {
    DialogData *data = g_new0(DialogData, 1);
    data->dialog = gtk_dialog_new();
    data->sw = sw;
    data->en = en;
    data->BTS = BTS;
    data->update_callback = update_callback;
    data->callback_data = callback_data;
    gtk_window_set_title(GTK_WINDOW(data->dialog), "Select Action");
    gtk_window_set_transient_for(GTK_WINDOW(data->dialog), GTK_WINDOW(parent));
    gtk_window_set_modal(GTK_WINDOW(data->dialog), TRUE);

    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(data->dialog));
    GtkWidget *scrolled_window = gtk_scrolled_window_new();
    GtkWidget *grid = gtk_grid_new();
    
    gtk_widget_set_margin_start(grid, 15);
    gtk_widget_set_margin_end(grid, 15);
    gtk_widget_set_margin_top(grid, 15);
    gtk_widget_set_margin_bottom(grid, 15);

    gtk_widget_set_size_request(scrolled_window, 750, 400);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_window), grid);
    gtk_box_append(GTK_BOX(content_area), scrolled_window);

    for (int i = 0, row = 0, column = 0; i < ACTIONS; i++) {
        if ((ActionTable[i].type & filter) || (ActionTable[i].type == TYPE_NONE)) {
            GtkWidget *button = gtk_toggle_button_new_with_label(ActionTable[i].str);
            g_object_set_data(G_OBJECT(button), "action_id", GINT_TO_POINTER(i));
            gtk_grid_attach(GTK_GRID(grid), button, column, row, 1, 1);
            g_signal_connect(button, "clicked", G_CALLBACK(toggle_button_clicked), data);

            // just regular switches, like on the toolbar.
            if (sw != NULL && BTS == -1 && i == sw->switch_function) {
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);
                data->active_button = button;                
            }

            // bottom encoder.
            else if (en != NULL && BTS == 1 && i == en->bottom_encoder_function) {
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);
                data->active_button = button;                
            }
            // top encoder
            else if (en != NULL && BTS == 2 && i == en->top_encoder_function) {
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);
                data->active_button = button;                
            }
            // encoder switch
            else if (en != NULL && BTS == 3 && i == en->switch_function) {
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);
                data->active_button = button;                
            }

            column++;
            if (column >= 6) { 
                column = 0;
                row++;
            }
        }
    }

    GtkWidget *ok_button = gtk_button_new_with_label("Select New Action");
    gtk_box_append(GTK_BOX(content_area), ok_button);
    g_signal_connect(ok_button, "clicked", G_CALLBACK(ok_button_clicked), data);

    gtk_widget_show(data->dialog);
}
