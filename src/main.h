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

#ifndef _MAIN_H
#define _MAIN_H

//#include <sys/utsname.h>
extern struct utsname unameData;

typedef enum _controller_enum {
  NO_CONTROLLER = 0,
  MARKS_CONTROLLER,
  // the code for these is non-functional.
  CONTROLLER1,
  CONTROLLER2_V1,
  CONTROLLER2_V2,
  G2_FRONTPANEL,  
} controller_enum;

extern int controller;

extern GdkDisplay *display;
extern int screen_height;
extern int screen_width;
extern int app_width;
extern int app_height;

extern int full_screen;
extern GtkWidget *main_window;
extern GtkWidget *main_grid;
extern void status_text(const char *text);

#endif