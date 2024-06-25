/* Copyright (C)
 * 2017 - John Melton, G0ORX/N6LYT
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

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#include <wdsp.h>

#include "new_menu.h"
#include "about_menu.h"
#include "discovered.h"
#include "radio.h"
#include "version.h"
#include "mystring.h"

#include <gtk/gtk.h>

// Static variables
static GtkWidget *dialog = NULL;
static GtkWidget *label;

// Cleanup function
static void cleanup()
{
    if (dialog != NULL)
    {
        GtkWidget *tmp = dialog;
        dialog = NULL;
        gtk_window_destroy(GTK_WINDOW(tmp));
        sub_menu = NULL;
        active_menu = NO_MENU;
    }
}

// Callback function for closing the dialog
static gboolean close_cb(GtkWidget *widget, gpointer data)
{
    cleanup();
    return TRUE;
}

char* get_compile_info() {
    // Get compile date and time
    const char *compile_date = __DATE__;
    const char *compile_time = __TIME__;

    // Determine the platform
    const char *platform;
    #if defined(_WIN32) || defined(_WIN64)
        platform = "Windows";
    #elif defined(__linux__)
        platform = "Linux";
    #elif defined(__APPLE__)
        platform = "macOS";
    #else
        platform = "Unknown";
    #endif

    char *compile_info = malloc(256);
    if (compile_info == NULL) {
        return NULL;
    }

    snprintf(compile_info, 256, "v1.0 Compile date: %s\nCompile time: %s\nPlatform: %s\n", compile_date, compile_time, platform);

    return compile_info;
}

void about_menu(GtkWidget *parent)
{
    char line[512];
    char text[2048];

    GtkWidget *about_dialog = gtk_about_dialog_new();
    gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(about_dialog), "piHPSDR");
    gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(about_dialog), get_compile_info());
    gtk_about_dialog_set_license_type(GTK_ABOUT_DIALOG(about_dialog), GTK_LICENSE_GPL_3_0);
    gtk_about_dialog_set_authors(GTK_ABOUT_DIALOG(about_dialog), (const char *[]){
                                                                     "John Melton G0ORX/N6LYT",
                                                                     "with help from:",
                                                                     "Steve Wilson, KA6S: RIGCTL (CAT over TCP)",
                                                                     "Laurence Barker, G8NJJ: USB OZY Support",
                                                                     "Ken Hopper, N9VV: Testing and Documentation",
                                                                     "Christoph van Wüllen, DL1YCF: CW, PureSignal, Diversity, MIDI",
                                                                     "Mark Rutherford, KB2YCW: GTK4 and Windows port",
                                                                     NULL});    

    snprintf(text, sizeof(text), "WDSP version: %d.%02d\n\n", GetWDSPVersion() / 100, GetWDSPVersion() % 100);

    switch (radio->protocol)
    {
    case ORIGINAL_PROTOCOL:
    case NEW_PROTOCOL:
        if (device == DEVICE_OZY)
        {
            snprintf(line, sizeof(line), "Device:  OZY (via USB)  Protocol %s v%d.%d", radio->protocol == ORIGINAL_PROTOCOL ? "1" : "2",
                     radio->software_version / 10, radio->software_version % 10);
            strncat(text, line, sizeof(text) - strlen(text) - 1);
        }
        else
        {
            char interface_addr[64];
            char addr[64];
            inet_ntop(AF_INET, &radio->info.network.address.sin_addr, addr, sizeof(addr));
            inet_ntop(AF_INET, &radio->info.network.interface_address.sin_addr, interface_addr, sizeof(interface_addr));
            snprintf(line, sizeof(line), "Device: %s, Protocol %s, v%d.%d\n"
                                         "    Mac Address: %02X:%02X:%02X:%02X:%02X:%02X\n"
                                         "    IP Address: %s on %s (%s)",
                     radio->name, radio->protocol == ORIGINAL_PROTOCOL ? "1" : "2",
                     radio->software_version / 10, radio->software_version % 10,
                     radio->info.network.mac_address[0],
                     radio->info.network.mac_address[1],
                     radio->info.network.mac_address[2],
                     radio->info.network.mac_address[3],
                     radio->info.network.mac_address[4],
                     radio->info.network.mac_address[5],
                     addr,
                     radio->info.network.interface_name,
                     interface_addr);
            strncat(text, line, sizeof(text) - strlen(text) - 1);
        }
        break;
#ifdef SOAPYSDR
    case SOAPYSDR_PROTOCOL:
        snprintf(line, sizeof(line), "Device: %s (via SoapySDR)\n"
                                     "    %s (%s)",
                 radio->name, radio->info.soapy.hardware_key, radio->info.soapy.driver_key);
        strncat(text, line, sizeof(text) - strlen(text) - 1);
        break;
#endif
    }

    gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(about_dialog), text);

    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file("assets/hpsdr.png", NULL);
    if (pixbuf) {
        GdkTexture *texture = gdk_texture_new_for_pixbuf(pixbuf);
        gtk_about_dialog_set_logo(GTK_ABOUT_DIALOG(about_dialog), GDK_PAINTABLE(texture));
        g_object_unref(texture);
        g_object_unref(pixbuf);
    } else {
        g_warning("Failed to load logo image");
    }

    gtk_window_set_default_size(GTK_WINDOW(about_dialog), 600, 500);
    gtk_window_set_transient_for(GTK_WINDOW(about_dialog), GTK_WINDOW(parent));
    gtk_window_present(GTK_WINDOW(about_dialog));
}
