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

#include "gpio.h"

//extern void select_action_dialog(GtkWidget *parent, int filter, SWITCH *sw, GCallback update_callback, gpointer callback_data);
extern void select_action_dialog(GtkWidget *parent, int filter, SWITCH *sw, ENCODER *en, int BTS, GCallback update_callback, gpointer callback_data);