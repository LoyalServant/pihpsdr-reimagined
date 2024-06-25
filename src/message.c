/* Copyright (C)
* 2023 - Christoph van Wüllen, DL1YCF
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
 * Hook for logging messages to the output file
 *
 * This can be redirected with any method g_print()
 * can be re-directed.
 *
 * t_print
 *           is a g_print() but it puts a time stamp in front.
 * t_perror
 *           is a perror() replacement, it puts a time stamp in font
 *           and reports via g_print
 *
 * Note ALL messages of the program should go through these two functions
 * so it is easy to either silence them completely, or routing them to
 * a separate window for debugging purposes.
 */


#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <glib.h>
#include <unistd.h>  // For getpid()

void t_print(const gchar *format, ...) {
    static FILE *log_file = NULL;
    static int first = 1;
    va_list args;
    struct timespec ts;
    double now;
    static double starttime;
    char line[1024];

    // Get the current time
    clock_gettime(CLOCK_MONOTONIC, &ts);
    now = ts.tv_sec + 1E-9 * ts.tv_nsec;

    // FIXME: this is not that great. the directory will get filled with logfiles.
    // Initialize start time and open log file on the first call
    if (first) {
        first = 0;
        starttime = now;

        // Create a log file name with the process ID
        char log_filename[256];
        snprintf(log_filename, sizeof(log_filename), "pihpsdr-%d.log", getpid());

        log_file = fopen(log_filename, "a");
        if (!log_file) {
            g_print("Failed to open log file: %s\n", log_filename);
            return;
        }
    }

    //
    // After 11 days, the time reaches 999999.999 so we simply wrap around
    //
    if (now - starttime >= 999999.995) {
        starttime += 1000000.0;
    }

    //
    // We have to use vsnprintf to handle the varargs stuff
    // g_print() seems to be thread-safe but call it only ONCE.
    //
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);

    g_print("%10.3f %s", now - starttime, line);

    // Print to the log file
    if (log_file) {
        fprintf(log_file, "%10.3f %s", now - starttime, line);
        fflush(log_file);  // Ensure the output is written to the file
    }
}


void my_log_handler(const gchar *log_domain,
                    GLogLevelFlags log_level,
                    const gchar *message,
                    gpointer user_data) {

    t_print("GTK Log message: %s\n", message);

    //g_assert_not_reached();
}


void t_perror(const gchar *string) {
  t_print("%s: %s\n", string, strerror(errno));
}
