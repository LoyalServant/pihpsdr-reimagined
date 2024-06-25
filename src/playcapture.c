// #include <gtk/gtk.h>
// #include <stdlib.h>
// #include "actions.h"
// #include "radio.h"
// #include "wave.h"
// #include "new_menu.h"


// static GtkWidget *dialog = NULL;

// static void cleanup() {
//   if (dialog != NULL) {
//     GtkWidget *tmp = dialog;
//     dialog = NULL;
//     gtk_window_destroy(GTK_WINDOW(tmp));
//     sub_menu = NULL;
//     active_menu  = NO_MENU;
//   }
// }

// static gboolean close_cb () {
//   cleanup();
//   return TRUE;
// }

// static void play_button_clicked(GtkButton *button, gpointer user_data) {
//     const char *filename = (const char *)user_data;

//     if (isTransmitting())
//     {
//         return;
//     }

//     load_wav_file(filename);
//     schedule_action(MOX, ACTION_PRESSED, 0);
//     capture_state = CAP_AVAIL;
//     schedule_action(CAPTURE, ACTION_PRESSED, 0);
// }

// static void delete_button_clicked(GtkButton *button, gpointer user_data) {
//     const char *filename = (const char *)user_data;

//     if (isTransmitting())
//     {
//         return;
//     }

//     if (remove(filename) == 0) {
//         g_print("Deleted: %s\n", filename);
//         GtkWidget *row = gtk_widget_get_parent(GTK_WIDGET(button));
//         gtk_widget_unparent(row);  // Remove deleted row from the list
//     } else {
//         g_print("Error deleting: %s\n", filename);
//     }
// }

// static GtkWidget* create_list_row(const char *filename) {
//     GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);

//     GtkWidget *label = gtk_label_new(filename);
//     gtk_box_append(GTK_BOX(box), label);

//     GtkWidget *play_button = gtk_button_new_with_label("Play");
//     g_signal_connect(play_button, "clicked", G_CALLBACK(play_button_clicked), g_strdup(filename));
//     gtk_box_append(GTK_BOX(box), play_button);

//     GtkWidget *delete_button = gtk_button_new_with_label("Delete");
//     g_signal_connect(delete_button, "clicked", G_CALLBACK(delete_button_clicked), g_strdup(filename));
//     gtk_box_append(GTK_BOX(box), delete_button);

//     return box;
// }

// static void populate_list(GtkWidget *list_box) {
//     GDir *dir;
//     const gchar *filename;
//     const gchar *path = "captures";

//     dir = g_dir_open(path, 0, NULL);
//     if (dir) {
//         while ((filename = g_dir_read_name(dir)) != NULL) {
//             if (g_str_has_suffix(filename, ".wav")) {
//                 gchar *filepath = g_build_filename(path, filename, NULL);
//                 GtkWidget *row = create_list_row(filepath);
//                 gtk_list_box_append(GTK_LIST_BOX(list_box), row);
//                 g_free(filepath);
//             }
//         }
//         g_dir_close(dir);
//     }
// }

// void play_capture_dialog(gpointer parent) {
//     dialog = gtk_dialog_new();
//     gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
//     gtk_window_set_title(GTK_WINDOW(dialog), "piHPSDR reimagined - play captured WAV Files");
//     gtk_window_set_default_size(GTK_WINDOW(dialog), 500, 400);
//     g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);

//     GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
//     GtkWidget *list_box = gtk_list_box_new();
//     populate_list(list_box);
//     gtk_box_append(GTK_BOX(content_area), list_box);

//     gtk_widget_set_margin_start(list_box, 10);
//     gtk_widget_set_margin_end(list_box, 10);
//     gtk_widget_set_margin_top(list_box, 10);
//     gtk_widget_set_margin_bottom(list_box, 10);

//     gtk_widget_show(dialog);
// }


#include <gtk/gtk.h>
#include <stdlib.h>
#include "actions.h"
#include "radio.h"
#include "wave.h"
#include "new_menu.h"
#include "message.h"

static GtkWidget *dialog = NULL;

static void open_folder_button_clicked(GtkButton *button, gpointer user_data) {
    const gchar *path = "captures";
#ifdef _WIN32
    ShellExecute(NULL, "open", path, NULL, NULL, SW_SHOWNORMAL);
#elif __APPLE__
    char command[256];
    snprintf(command, sizeof(command), "open \"%s\"", path);
    system(command);
#else //?
    char command[256];
    snprintf(command, sizeof(command), "xdg-open \"%s\"", path);
    system(command);
#endif
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

static void play_button_clicked(GtkButton *button, gpointer user_data) {
    const char *filename = (const char *)user_data;

    if (isTransmitting()) {
        return;
    }

    load_wav_file(filename);
    schedule_action(MOX, ACTION_PRESSED, 0);
    capture_state = CAP_AVAIL;
    schedule_action(CAPTURE, ACTION_PRESSED, 0);
}

static void delete_button_clicked(GtkButton *button, gpointer user_data) {
    const char *filename = (const char *)user_data;

    if (isTransmitting()) {
        return;
    }

    if (remove(filename) == 0) {
        t_print("Deleted: %s\n", filename);
        GtkWidget *row = gtk_widget_get_parent(GTK_WIDGET(button));

        // Traverse up the widget hierarchy to find the GtkListBoxRow
        while (row != NULL && !GTK_IS_LIST_BOX_ROW(row)) {
            row = gtk_widget_get_parent(row);
        }

        if (row != NULL) {
            GtkWidget *list_box = gtk_widget_get_parent(row);
            gtk_list_box_remove(GTK_LIST_BOX(list_box), row);
        }
    } else {
        t_print("Error deleting: %s\n", filename);
    }
}



static GtkWidget* create_list_row(const char *filename) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);

    GtkWidget *label = gtk_label_new(filename);
    gtk_box_append(GTK_BOX(box), label);

    GtkWidget *play_button = gtk_button_new_with_label("Play");
    g_signal_connect(play_button, "clicked", G_CALLBACK(play_button_clicked), g_strdup(filename));
    gtk_box_append(GTK_BOX(box), play_button);

    GtkWidget *delete_button = gtk_button_new_with_label("Delete");
    g_signal_connect(delete_button, "clicked", G_CALLBACK(delete_button_clicked), g_strdup(filename));
    gtk_box_append(GTK_BOX(box), delete_button);

    return box;
}

static void populate_list(GtkWidget *list_box) {
    GDir *dir;
    const gchar *filename;
    const gchar *path = "captures";

    dir = g_dir_open(path, 0, NULL);
    if (dir) {
        while ((filename = g_dir_read_name(dir)) != NULL) {
            if (g_str_has_suffix(filename, ".wav")) {
                gchar *filepath = g_build_filename(path, filename, NULL);
                GtkWidget *row = create_list_row(filepath);
                gtk_list_box_append(GTK_LIST_BOX(list_box), row);
                g_free(filepath);
            }
        }
        g_dir_close(dir);
    }
}

void play_capture_dialog(gpointer parent) {
    dialog = gtk_dialog_new();
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
    gtk_window_set_title(GTK_WINDOW(dialog), "piHPSDR reimagined - play captured WAV Files");
    gtk_window_set_default_size(GTK_WINDOW(dialog), 500, 400);
    g_signal_connect(dialog, "destroy", G_CALLBACK(close_cb), NULL);

    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_set_homogeneous(GTK_BOX(main_box), FALSE);
    gtk_box_append(GTK_BOX(content_area), main_box);

    GtkWidget *list_box = gtk_list_box_new();
    populate_list(list_box);
    gtk_widget_set_vexpand(list_box, TRUE);  // Allow list_box to expand vertically
    gtk_widget_set_hexpand(list_box, TRUE);  // Allow list_box to expand horizontally
    gtk_box_append(GTK_BOX(main_box), list_box);
    gtk_widget_set_margin_start(main_box, 10);
    gtk_widget_set_margin_end(main_box, 10);
    gtk_widget_set_margin_top(main_box, 10);
    gtk_widget_set_margin_bottom(main_box, 10);

    // Create a button box to hold the "Open Folder" button
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_set_homogeneous(GTK_BOX(button_box), FALSE);

    GtkWidget *open_folder_button = gtk_button_new_with_label("Open Folder");
    g_signal_connect(open_folder_button, "clicked", G_CALLBACK(open_folder_button_clicked), NULL);

    gtk_box_append(GTK_BOX(button_box), open_folder_button);

    // Align the button box to the start of the main box
    gtk_widget_set_halign(button_box, GTK_ALIGN_START);
    gtk_widget_set_margin_start(button_box, 10);
    gtk_widget_set_margin_end(button_box, 10);
    gtk_widget_set_margin_top(button_box, 10);
    gtk_widget_set_margin_bottom(button_box, 10);

    // Append the button box to the main box
    gtk_box_append(GTK_BOX(main_box), button_box);

    gtk_widget_show(dialog);
}

