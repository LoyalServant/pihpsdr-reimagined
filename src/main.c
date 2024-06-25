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

/*
 *   This is a migration from GTK3 to GTK4, based on the work of John Melton, G0ORX/N6LYT
 *   I did this as a learning experience, and I hope this is useful to someone.
 *   If it is.... buy me a beverage.. or send me a QSL card :)
 *   - Mark
 */

// this network stuff has to be before windows.h or it will complain about winsock.
#include "discovery.h"
#include "discovered.h"

#ifdef _WIN32
#include <WinSock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#define setenv(key, value, overwrite) _putenv_s(key, value)
#else
#include <unistd.h>
#include <arpa/inet.h>
#endif

#include <gtk/gtk.h>
#include <pthread.h>
#include <unistd.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include "mystring.h"
#include "message.h"
#include "radio.h"
#include "exit_menu.h"
#include "audio.h"
#include <wdsp.h>
#include "main.h"

typedef struct {
    GtkWidget *label;
    GtkWidget *splash_screen;
    GtkWidget *radio_list_box;
    GtkWidget *combo_box;
    GtkWidget *ip_entry;
    GtkWidget *discover_button;
    GtkWidget *use_ip_button;
    GtkWidget *status_label;
    GtkWidget *separator;
    GtkWidget *close_button;
} ThreadData;

typedef struct {
    ThreadData *thread_data;
    DISCOVERED *discovered;
} CallbackData;

volatile gboolean wisdom_running = FALSE;
volatile gboolean other_task_running = FALSE;
pthread_t wisdom_thread_id;
pthread_t other_task_thread_id;

DISCOVERED *selected;

// this is our reported screen size.
int screen_width = 1024; //1920;
int screen_height = 600; //1080;

// this is the app (us) dimensions
int app_width = 640;
int app_height = 400;

int full_screen = 0;

GtkWidget *main_window;
//GtkWidget *main_grid;

static void on_radio_selected(GtkWidget *widget, gpointer data);
static void on_close_button_clicked(GtkWidget *widget, gpointer data);
static void* wisdom_thread(void *arg);
void* other_task_thread(void *data);
static gboolean on_delete_event(GtkWidget *widget, GdkEvent *event, gpointer data);
static gboolean on_close_request(GtkWidget *widget, GdkEvent *event, gpointer data);
gboolean update_status_text(gpointer data);
void* startup_tasks(void *data);
static void on_splash_activate(GtkApplication *app, gpointer user_data);
void run_splash_screen();
static void on_main_activate(GtkApplication *app, gpointer user_data);
gboolean show_controls(gpointer data);

static void* wisdom_thread(void *arg) {
    WDSPwisdom((char *)arg);
    wisdom_running = FALSE;
    return NULL;
}

void* other_task_thread(void *data) {
    ThreadData *thread_data = (ThreadData *)data;    

    // Clear the existing children of the radio_list_box
    GtkWidget *child = gtk_widget_get_first_child(thread_data->radio_list_box);
    while (child != NULL) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        gtk_widget_unparent(child);
        child = next;
    }

    GtkWidget *radio_item_box;
    GtkWidget *radio_item_label;
    GtkWidget *start_button;

    // get soundcards
    audio_get_cards();
    // start radio discovery process
    discovery();
    DISCOVERED *d;
    char text[512];
    char version[16];
    char macStr[18];
    char ipStr[INET_ADDRSTRLEN];

    // what did we get?
    for(int i = 0; i < devices; i++)
    {
        d = &discovered[i];

        snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 d->info.network.mac_address[0],
                 d->info.network.mac_address[1],
                 d->info.network.mac_address[2],
                 d->info.network.mac_address[3],
                 d->info.network.mac_address[4],
                 d->info.network.mac_address[5]);

        snprintf(version, sizeof(version), "v%d.%d", d->software_version / 10, d->software_version % 10);
        
        if (inet_ntop(AF_INET, &d->info.network.address.sin_addr, ipStr, sizeof(ipStr)) == NULL) {
                perror("inet_ntop");
        }

        switch (d->protocol)
        {            

            case ORIGINAL_PROTOCOL:
            case NEW_PROTOCOL:                
                snprintf(text, sizeof(text), "%s (%s %s) %s (%s) on %s: ",
                d->name,
                d->protocol == ORIGINAL_PROTOCOL ? "Protocol 1" : "Protocol 2",
                version,
                ipStr,
                macStr,
                d->info.network.interface_name);
                break;            
            default:
                break;
        }

        radio_item_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        gtk_widget_set_hexpand(radio_item_box, TRUE);
        gtk_widget_set_vexpand(radio_item_box, TRUE);

        radio_item_label = gtk_label_new(text);
        gtk_widget_set_hexpand(radio_item_label, TRUE);
        gtk_widget_set_vexpand(radio_item_label, TRUE);
        gtk_box_append(GTK_BOX(radio_item_box), radio_item_label);

        start_button = gtk_button_new_with_label("Start This Radio");
        gtk_widget_set_hexpand(start_button, TRUE);
        gtk_widget_set_vexpand(start_button, TRUE);
        gtk_box_append(GTK_BOX(radio_item_box), start_button);

        // Allocate and populate callback data...
        CallbackData *callback_data = malloc(sizeof(CallbackData));
        if (callback_data == NULL) {
            perror("Failed to allocate memory for callback data");
            exit(EXIT_FAILURE);
        }
        callback_data->thread_data = thread_data;
        callback_data->discovered = d;

        g_signal_connect(start_button, "clicked", G_CALLBACK(on_radio_selected), callback_data);

        gtk_box_append(GTK_BOX(thread_data->radio_list_box), radio_item_box);
        gtk_widget_set_margin_start(radio_item_label, 50);
    }

    other_task_running = FALSE;

    // Signal the UI to show controls
    g_idle_add(show_controls, thread_data);

    return NULL;
}


static gboolean on_delete_event(GtkWidget *widget, GdkEvent *event, gpointer data) {
    wisdom_running = FALSE;
    other_task_running = FALSE;
    return TRUE;
}

static gboolean on_close_request(GtkWidget *widget, GdkEvent *event, gpointer data) {
    return GDK_EVENT_STOP;
}

int radio_selected = 0;
static void on_close_button_clicked(GtkWidget *widget, gpointer data) {    
    gtk_window_destroy(GTK_WINDOW(((ThreadData*)data)->splash_screen));
}

gboolean update_status_text(gpointer data) {
    ThreadData *thread_data = (ThreadData *)data;
    
    if (wisdom_running || other_task_running) {
        char text[1024] = "This window will update when wisdom and discovery are completed.\nPlease be patient!!\n\n";
        
        if (wisdom_running) {
            char wisdom_status[512];
            snprintf(wisdom_status, sizeof(wisdom_status), "%s\n", wisdom_get_status());
            STRLCAT(text, wisdom_status, sizeof(text));
        }
        
        if (other_task_running) {
            char other_status[512];
            snprintf(other_status, sizeof(other_status), "Discovering Radios\n");
            STRLCAT(text, other_status, sizeof(text));
        }

        char markup_text[2048];
        snprintf(markup_text, sizeof(markup_text), "<span foreground='yellow'>%s</span>", text);

        gtk_label_set_markup(GTK_LABEL(thread_data->status_label), markup_text);
        gtk_widget_set_halign(GTK_WIDGET(thread_data->status_label), GTK_ALIGN_CENTER);
        return TRUE;
    }
    return FALSE;
}

gboolean show_controls(gpointer data) {
    ThreadData *thread_data = (ThreadData *)data;

    // Hide the status label
    gtk_widget_set_visible(thread_data->status_label, FALSE);

    // Show the controls
    gtk_widget_set_visible(thread_data->radio_list_box, TRUE);
#ifdef GPIO    
    gtk_widget_set_visible(thread_data->combo_box, TRUE);
#endif
    gtk_widget_set_visible(thread_data->discover_button, TRUE);
    gtk_widget_set_visible(thread_data->ip_entry, TRUE);
    gtk_widget_set_visible(thread_data->use_ip_button, TRUE);
    gtk_widget_set_visible(thread_data->close_button, TRUE);
    gtk_widget_set_visible(thread_data->separator, TRUE);

    return FALSE;
}

void* startup_tasks(void *data) {
    ThreadData *thread_data = (ThreadData *)data;
    char wisdom_directory[1024];

    if (getcwd(wisdom_directory, sizeof(wisdom_directory)) == NULL) {
        perror("getcwd");
        g_free(thread_data);
        return NULL;
    }
    STRLCAT(wisdom_directory, "/", sizeof(wisdom_directory)); 
    t_print("Securing wisdom file in directory: %s\n", wisdom_directory);    

    // Start the wisdom task
    wisdom_running = TRUE;
    if (pthread_create(&wisdom_thread_id, NULL, wisdom_thread, wisdom_directory) != 0) {
        perror("pthread_create wisdom_thread");
        wisdom_running = FALSE;
        g_free(thread_data);
        return NULL;
    }

    // Update the status text periodically
    g_timeout_add(1000, update_status_text, thread_data);

    // Wait for the wisdom task to complete
    while (wisdom_running) {
        usleep(100000); // 100ms
    }

    // Start the other task after wisdom completes
    other_task_running = TRUE;
    if (pthread_create(&other_task_thread_id, NULL, other_task_thread, thread_data) != 0) {
        perror("pthread_create other_task_thread");
        other_task_running = FALSE;
        g_free(thread_data);
        return NULL;
    }

    // Wait for the other task to complete
    while (other_task_running) {
        usleep(100000); // 100ms
    }

    // Show controls and hide status label after tasks are complete
    g_idle_add(show_controls, thread_data);

    return NULL;
}


static void on_radio_selected(GtkWidget *widget, gpointer data) {
    CallbackData *callback_data = (CallbackData *)data;
    ThreadData *thread_data = callback_data->thread_data;
    DISCOVERED *discovered = callback_data->discovered;

    radio_selected = 1;

    gtk_window_destroy(GTK_WINDOW(thread_data->splash_screen));

    selected = discovered;

    // Free the callback data
    free(callback_data);
}


static void on_discover_button_clicked(GtkWidget *widget, gpointer data) {
    ThreadData *thread_data = (ThreadData *)data;

    // Hide the controls
    gtk_widget_set_visible(thread_data->radio_list_box, FALSE);
#ifdef GPIO    
    gtk_widget_set_visible(thread_data->combo_box, FALSE);
#endif    
    gtk_widget_set_visible(thread_data->discover_button, FALSE);
    gtk_widget_set_visible(thread_data->ip_entry, FALSE);
    gtk_widget_set_visible(thread_data->use_ip_button, FALSE);
    gtk_widget_set_visible(thread_data->close_button, FALSE);
    gtk_widget_set_visible(thread_data->separator, FALSE);

    // Show the status label
    gtk_widget_set_visible(thread_data->status_label, TRUE);

    // Update the status text
    gtk_label_set_text(GTK_LABEL(thread_data->status_label), "Discovering Radios...");

    // Start the other_task thread again
    other_task_running = TRUE;
    if (pthread_create(&other_task_thread_id, NULL, other_task_thread, thread_data) != 0) {
        perror("pthread_create other_task_thread");
        other_task_running = FALSE;
        return;
    }

    // Wait for the other task to complete in the background
    g_timeout_add(100, (GSourceFunc)update_status_text, thread_data);
}

static void on_discover_by_ip_clicked(GtkWidget *widget, gpointer data) {
    ThreadData *thread_data = (ThreadData *)data;
    ipaddr_radio = (char *)gtk_editable_get_text(GTK_EDITABLE(thread_data->ip_entry));
    on_discover_button_clicked(widget, data);
}

void on_controller_changed(GtkDropDown *combo_box, GParamSpec *pspec, gpointer user_data) {
    guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(combo_box));
    controller_enum selected_enum = (controller_enum)selected;

    switch (selected_enum) {
        case NO_CONTROLLER:
            t_print("Selected: NO_CONTROLLER\n");
            break;
        case MARKS_CONTROLLER:
            t_print("Selected: MARKS_CONTROLLER\n");
            break;            
        case CONTROLLER1:
            t_print("Selected: CONTROLLER1\n");
            break;
        case CONTROLLER2_V1:
            t_print("Selected: CONTROLLER2_V1\n");
            break;
        case CONTROLLER2_V2:
            t_print("Selected: CONTROLLER2_V2\n");
            break;
        case G2_FRONTPANEL:
            t_print("Selected: G2_FRONTPANEL\n");
            break;        
        default:
            t_print("Selected: Unknown\n");
            break;
    }

    controller = selected;
#ifdef GPIO    
    gpio_set_defaults(controller);
    gpioSaveState();
#endif
}

static void on_splash_activate(GtkApplication *app, gpointer user_data) {
    GtkWidget *splash_screen = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(splash_screen), "pihpsdr startup");
    gtk_window_set_default_size(GTK_WINDOW(splash_screen), 650, 400);
    gtk_window_set_resizable(GTK_WINDOW(splash_screen), FALSE);
    gtk_window_set_deletable(GTK_WINDOW(splash_screen), FALSE);    

    g_signal_connect(splash_screen, "close-request", G_CALLBACK(on_close_request), user_data);
    g_signal_connect(splash_screen, "destroy", G_CALLBACK(on_delete_event), user_data);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_homogeneous(GTK_GRID(grid), FALSE);
    gtk_grid_set_row_homogeneous(GTK_GRID(grid), FALSE);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);

    GtkWidget *image = gtk_image_new_from_file("assets/hpsdr.png");
    gtk_widget_set_size_request(image, 200, 200);
    gtk_widget_set_margin_start(image, 15);
    gtk_widget_set_margin_top(image, 15);

    GtkWidget *text_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(text_label), "<b>piHPSDR reimagined by Mark Rutherford, KB2YCW\n\nBased on piHPSDR by John Melton G0ORX/N6LYT\n\nSee about for more details</b>");
    gtk_widget_set_halign(text_label, GTK_ALIGN_START);
    gtk_label_set_justify(GTK_LABEL(text_label), GTK_JUSTIFY_LEFT);

    ThreadData *thread_data = g_malloc(sizeof(ThreadData));

    // Create a box for the discovered radios and other controls
    GtkWidget *radio_list_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    GtkWidget *radio_label = gtk_label_new("Available Radios:");
    gtk_box_append(GTK_BOX(radio_list_box), radio_label);

#ifdef GPIO
    gpioRestoreState();
    gpio_set_defaults(controller);

    const char *items[] = {"No Controller", "Mark's Controller", NULL};
    GtkStringList *string_list = gtk_string_list_new(items);
    GtkWidget *combo_box = gtk_drop_down_new(G_LIST_MODEL(string_list), NULL);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(combo_box), controller);
    g_signal_connect(combo_box, "notify::selected", G_CALLBACK(on_controller_changed), NULL);
#endif

    GtkWidget *discover_button = gtk_button_new_with_label("Discover");
    GtkWidget *ip_entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(ip_entry), "10.1.20.44");
    GtkWidget *use_ip_button = gtk_button_new_with_label("Use IP for discovery");
    g_signal_connect(use_ip_button, "clicked", G_CALLBACK(on_discover_by_ip_clicked), thread_data);

    GtkWidget *status_label = gtk_label_new("Starting up...");
    gtk_widget_set_halign(status_label, GTK_ALIGN_CENTER);

    GtkWidget *close_button = gtk_button_new_with_label("Close");
    gtk_widget_set_halign(close_button, GTK_ALIGN_END);
    gtk_widget_set_size_request(close_button, 100, 30);

    
    g_signal_connect(close_button, "clicked", G_CALLBACK(on_close_button_clicked), thread_data);
    g_signal_connect(discover_button, "clicked", G_CALLBACK(on_discover_button_clicked), thread_data); // pass thread_data

    GtkWidget *separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);

    // Add widgets to the grid
    gtk_grid_attach(GTK_GRID(grid), image, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), text_label, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), status_label, 0, 1, 2, 1);
    gtk_grid_attach(GTK_GRID(grid), radio_list_box, 0, 2, 2, 1);
    gtk_grid_attach(GTK_GRID(grid), separator, 0, 3, 2, 1);

    // Add margins to the controls below the separator
#ifdef GPIO    
    gtk_widget_set_margin_start(combo_box, 20);
#endif
    gtk_widget_set_margin_start(discover_button, 50);
    gtk_widget_set_margin_end(discover_button, 20);
    gtk_widget_set_margin_start(ip_entry, 20);
    gtk_widget_set_margin_start(use_ip_button, 50);
    gtk_widget_set_margin_end(use_ip_button, 20);
    gtk_widget_set_margin_start(close_button, 20);
    gtk_widget_set_margin_end(close_button, 20);
    gtk_widget_set_margin_bottom(close_button, 20);
    gtk_widget_set_halign(close_button, GTK_ALIGN_END);
    gtk_widget_set_margin_start(separator, 20);

#ifdef GPIO
    gtk_grid_attach(GTK_GRID(grid), combo_box, 0, 4, 1, 1);
#endif    
    gtk_grid_attach(GTK_GRID(grid), discover_button, 1, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), ip_entry, 0, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), use_ip_button, 1, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), close_button, 1, 6, 1, 1);

    gtk_window_set_child(GTK_WINDOW(splash_screen), grid);

    // Initially hide the controls
    gtk_widget_set_visible(radio_list_box, FALSE);
#ifdef GPIO    
    gtk_widget_set_visible(combo_box, FALSE);
#endif    
    gtk_widget_set_visible(discover_button, FALSE);
    gtk_widget_set_visible(ip_entry, FALSE);
    gtk_widget_set_visible(use_ip_button, FALSE);
    gtk_widget_set_visible(close_button, FALSE);
    gtk_widget_set_visible(separator, FALSE);

    gtk_window_present(GTK_WINDOW(splash_screen));

    thread_data->label = status_label;
    thread_data->splash_screen = splash_screen;
    thread_data->radio_list_box = radio_list_box;
#ifdef GPIO    
    thread_data->combo_box = combo_box;
#endif
    thread_data->ip_entry = ip_entry;
    thread_data->discover_button = discover_button;
    thread_data->use_ip_button = use_ip_button;
    thread_data->status_label = status_label;
    thread_data->close_button = close_button;
    thread_data->separator = separator;

    pthread_t thread;
    pthread_create(&thread, NULL, startup_tasks, thread_data);
    pthread_detach(thread);
}

void run_splash_screen() {
    GtkApplication *app = gtk_application_new("com.pihpsdr.splashscreen", 0);
    g_signal_connect(app, "activate", G_CALLBACK(on_splash_activate), NULL);
    g_application_run(G_APPLICATION(app), 0, NULL);
    g_object_unref(app);
}

void on_main_destroy (GtkWidget *widget, gpointer user_data) {
   if (radio != NULL) {
     stop_program();
   }
#ifdef _WIN32
    WSACleanup();
#endif
   g_application_quit(G_APPLICATION(user_data));
}

// this doesnt work. it just returns 0.
// i have no idea how to determine the size of the title bar so we can perform accurate calculations...
// see 
static void on_realize(GtkWidget *widget, gpointer data) {
    // int total_height, content_height, border_height;
    // total_height = gtk_widget_get_allocated_height(widget);
    // GtkWidget *content_area = gtk_window_get_child(GTK_WINDOW(widget));
    // content_height = gtk_widget_get_allocated_height(content_area);
    // border_height = total_height - content_height;
    // t_print("Estimated title bar and border height: %d pixels\n", border_height);
}

void get_default_monitor_size(GtkWindow *window) {
    GdkDisplay *display = gdk_display_get_default();
    if (!display) {
        g_print("No display found\n");
        return;
    }

    GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(window));
    if (!surface) {
        g_print("No surface found\n");
        return;
    }

    GdkMonitor *monitor = gdk_display_get_monitor_at_surface(display, surface);
    if (!monitor) {
        g_print("No monitor found for this surface\n");
        return;
    }

    // Get the geometry of the monitor
    GdkRectangle geometry;
    gdk_monitor_get_geometry(monitor, &geometry);

    // Print the monitor size in one line
    t_print("Monitor size: width = %d, height = %d\n", geometry.width, geometry.height);
    screen_width = geometry.width;
    screen_height = geometry.height;
}

static void on_main_activate(GtkApplication *app, gpointer user_data) {

    main_window = gtk_application_window_new(app);
    if (main_window == NULL) {
        t_print("main window was not created. wha???\n");
    }
    else {
        t_print("main window created.\n");
    }
    gtk_window_set_title(GTK_WINDOW(main_window), "piHPSDR - reimagined");
    gtk_window_set_default_size(GTK_WINDOW(main_window), 640, 400);

    gtk_window_set_resizable(GTK_WINDOW(main_window), true);

    g_signal_connect(main_window, "destroy", G_CALLBACK(on_main_destroy), app);
    g_signal_connect(main_window, "realize", G_CALLBACK(on_realize), NULL);

    gtk_window_present(GTK_WINDOW(main_window));

    get_default_monitor_size(GTK_WINDOW(main_window));

    start_selected_radio(selected);

}

int main(int argc, char *argv[]) {
    
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
        fprintf(stderr, "WSAStartup failed.\n");
        return -1;
    }
#endif

    g_log_set_handler("Gtk", G_LOG_LEVEL_WARNING | G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL, my_log_handler, NULL);

    setenv("GTK_THEME", "Adwaita-dark", 1);
    
    run_splash_screen();

    if (!radio_selected)
    {
        return 0;
    }

    GtkApplication *main_app = gtk_application_new("com.pihpsdr.mainApplication", 0);

    g_signal_connect(main_app, "activate", G_CALLBACK(on_main_activate), NULL);

    int status = g_application_run(G_APPLICATION(main_app), argc, argv);

    g_object_unref(main_app);

    return status;
}

