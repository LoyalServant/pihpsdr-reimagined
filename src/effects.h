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

#ifndef EFFECTS_H
#define EFFECTS_H

#include <gtk/gtk.h>

void show_effects_dialog(gpointer parent);

#define SAMPLE_RATE 24000

#define INITIAL_ECHO_DELAY_SECONDS 0.5
#define INITIAL_ECHO_DECAY 0.5

extern double echo_delay_seconds;
extern double echo_decay;
extern gboolean echo_enabled;
extern int echo_buffer_index;
extern double *echo_buffer;
extern int echo_buffer_length;


#endif // EFFECTS_H
