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
#include <gdk/gdk.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>

#include "appearance.h"
#include "band.h"
#include "receiver.h"
#include "meter.h"
#include "radio.h"
#include "radio.h"
#include "version.h"
#include "mode.h"
#include "vox.h"
#include "new_menu.h"
#include "vfo.h"
#include "message.h"

static GtkWidget *meter;
static cairo_surface_t *meter_surface = NULL;

static int last_meter_type = SMETER;

#define min_rxlvl -200.0
#define min_alc   -100.0
#define min_pwr      0.0

static double max_rxlvl = min_rxlvl;
static double max_alc   = min_alc;
static double max_pwr   = min_pwr;

static int max_count = 0;

void meter_resize_event_cb(GtkWidget* widget, int width, int height, gpointer data) {
   if (meter_surface) {
     cairo_surface_destroy (meter_surface);
   }

   meter_surface = gdk_surface_create_similar_surface(gtk_native_get_surface(gtk_widget_get_native(widget)), CAIRO_CONTENT_COLOR, METER_WIDTH, METER_HEIGHT);
   /* Initialize the surface to black */
   cairo_t *cr;
   cr = cairo_create (meter_surface);
   cairo_set_source_rgba(cr, COLOUR_VFO_BACKGND);
   cairo_paint (cr);
   cairo_destroy (cr);
}

/* Redraw the screen from the surface. Note that the ::draw
 * signal receives a ready-to-be-used cairo_t that is already
 * clipped to only draw the exposed areas of the widget
 */
static void meter_draw_cb (GtkDrawingArea *drawing_area, cairo_t *cr, int width, int height, gpointer data)  {
  cairo_set_source_surface (cr, meter_surface, 0.0, 0.0);
  cairo_paint (cr);
}


static void rx_meter_drag_begin(GtkGestureDrag *gesture, double start_x, double start_y, gpointer data) {
  start_meter();
}

GtkWidget* meter_init(int width, int height) {
  t_print("meter_init: width=%d height=%d\n", width, height);
  GtkGesture *drag;
  meter = gtk_drawing_area_new ();
  gtk_widget_set_size_request(meter, width, height);
  /* Signals used to handle the backing surface */
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(meter), meter_draw_cb, NULL, NULL);
  g_signal_connect_after(meter, "resize", G_CALLBACK (meter_resize_event_cb), NULL);

  drag = gtk_gesture_drag_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (drag), GDK_BUTTON_PRIMARY);
  gtk_widget_add_controller (meter, GTK_EVENT_CONTROLLER (drag));
  g_signal_connect(drag, "drag-begin", G_CALLBACK(rx_meter_drag_begin), NULL);


  return meter;
}

void meter_update(RECEIVER *rx, int meter_type, double value, double alc, double swr) {
  double rxlvl;   // only used for RX input level, clones "value"
  double pwr;     // only used for TX power, clones "value"
  char sf[32];
  int  units = 2;          // 0: xxx mW, 1: xx.y W, 2: xxx W
  double interval = 10.0;  // 1/10 of full reflection
  cairo_t *cr = cairo_create (meter_surface);
  int txvfo = get_tx_vfo();
  const BAND *band = band_get_band(vfo[txvfo].band);

  //
  // First, do all the work that  does not depend on whether the
  // meter is analog or digital.
  //

  if (last_meter_type != meter_type) {
    last_meter_type = meter_type;
    //
    // reset max values
    //
    max_rxlvl = min_rxlvl;
    max_pwr   = min_pwr;
    max_alc   = min_alc;
    max_count =    0;
  }

  //
  // Only the values max_rxlvl/max_pwr/max_alc are "on display"
  // The algorithm to calculate these "sedated" values from the
  // (possibly fluctuating)  input ones is as follows:
  //
  // - if counter > CNTMAX then move max_value towards current_value by exponential averaging
  //                            with parameter EXPAV1, EXPAV2 (but never go below the minimum value)
  // - if current_value >  max_value then set max_value to current_value and reset counter
  //
  // A new max value will therefore be immediately visible, the display stays (if not surpassed) for
  // CNTMAX cycles and then the displayed value will gradually approach the new one(s).
#define CNTMAX 5
#define EXPAV1 0.75
#define EXPAV2 0.25

  switch (meter_type) {
  case POWER:
    pwr = value;

    if (band->disablePA || !pa_enabled) {
      units = 0;  // x mW
      interval = 0.1;
    } else {
      interval = 0.1 * pa_power_list[pa_power];
    }

    if (max_count > CNTMAX) {
      max_pwr = EXPAV1 * max_pwr + EXPAV2 * pwr;
      max_alc = EXPAV1 * max_alc + EXPAV2 * alc;

      // This is perhaps not necessary ...
      if (max_pwr < min_pwr) { max_pwr = min_pwr; }

      // ... but alc goes to -Infinity during CW
      if (max_alc < min_alc) { max_alc = min_alc; }
    }

    if (pwr > max_pwr) {
      max_pwr = pwr;
      max_count = 0;
    }

    if (alc > max_alc) {
      max_alc = alc;
      max_count = 0;
    }

    break;

  case SMETER:
    rxlvl = value; // all corrections now in receiver.c

    if (max_count > CNTMAX) {
      max_rxlvl = EXPAV1 * max_rxlvl + EXPAV2 * rxlvl;

      if (max_rxlvl < min_rxlvl) { max_rxlvl = min_rxlvl; }
    }

    if (rxlvl > max_rxlvl) {
      max_rxlvl = rxlvl;
      max_count = 0;
    }

    break;
  }

  max_count++;

  //
  // From now on, DO NOT USE rxlvl,pwr,alc but use max_rxlvl etc.
  //

  if (analog_meter) {
    cairo_set_source_rgba(cr, COLOUR_VFO_BACKGND);
    cairo_paint (cr);
    cairo_set_font_size(cr, DISPLAY_FONT_SIZE2);

    switch (meter_type) {
    case SMETER: {
      //
      // Analog RX display
      //
      double offset = 210.0;
      int i;
      double x;
      double y;
      double angle;
      double radians;
      double cx = (double)METER_WIDTH / 2.0;
      double radius = cx - 20.0;
      cairo_set_line_width(cr, PAN_LINE_THICK);
      cairo_set_source_rgba(cr, COLOUR_METER);
      cairo_arc(cr, cx, cx, radius, 216.0 * M_PI / 180.0, 324.0 * M_PI / 180.0);
      cairo_stroke(cr);
      cairo_set_line_width(cr, PAN_LINE_EXTRA);
      cairo_set_source_rgba(cr, COLOUR_ALARM);
      cairo_arc(cr, cx, cx, radius + 2, 264.0 * M_PI / 180.0, 324.0 * M_PI / 180.0);
      cairo_stroke(cr);
      cairo_set_line_width(cr, PAN_LINE_THICK);
      cairo_set_source_rgba(cr, COLOUR_METER);

      for (i = 1; i < 10; i++) {
        angle = ((double)i * 6.0) + offset;
        radians = angle * M_PI / 180.0;

        if ((i % 2) == 1) {
          cairo_arc(cr, cx, cx, radius + 4, radians, radians);
          cairo_get_current_point(cr, &x, &y);
          cairo_arc(cr, cx, cx, radius, radians, radians);
          cairo_line_to(cr, x, y);
          cairo_stroke(cr);
          snprintf(sf, 32, "%d", i);
          cairo_arc(cr, cx, cx, radius + 5, radians, radians);
          cairo_get_current_point(cr, &x, &y);
          cairo_new_path(cr);
          x -= 4.0;
          cairo_move_to(cr, x, y);
          cairo_show_text(cr, sf);
        } else {
          cairo_arc(cr, cx, cx, radius + 2, radians, radians);
          cairo_get_current_point(cr, &x, &y);
          cairo_arc(cr, cx, cx, radius, radians, radians);
          cairo_line_to(cr, x, y);
          cairo_stroke(cr);
        }

        cairo_new_path(cr);
      }

      for (i = 20; i <= 60; i += 20) {
        angle = ((double)i + 54.0) + offset;
        radians = angle * M_PI / 180.0;
        cairo_arc(cr, cx, cx, radius + 4, radians, radians);
        cairo_get_current_point(cr, &x, &y);
        cairo_arc(cr, cx, cx, radius, radians, radians);
        cairo_line_to(cr, x, y);
        cairo_stroke(cr);
        snprintf(sf, 32, "+%d", i);
        cairo_arc(cr, cx, cx, radius + 5, radians, radians);
        cairo_get_current_point(cr, &x, &y);
        cairo_new_path(cr);
        x -= 4.0;
        cairo_move_to(cr, x, y);
        cairo_show_text(cr, sf);
        cairo_new_path(cr);
      }

      cairo_set_line_width(cr, PAN_LINE_THICK);
      cairo_set_source_rgba(cr, COLOUR_METER);

      if (vfo[active_receiver->id].frequency > 30000000LL) {
        //
        // VHF/UHF (beyond 30 Mhz): -147 dBm is S0
        //
        angle = fmax(-147.0, max_rxlvl) + 147.0 + offset;
      } else {
        //
        // HF (up to 30 Mhz): -127 dBm is S0
        //
        angle = fmax(-127.0, max_rxlvl) + 127.0 + offset;
      }

      radians = angle * M_PI / 180.0;
      cairo_arc(cr, cx, cx, radius + 8, radians, radians);
      cairo_line_to(cr, cx, cx);
      cairo_stroke(cr);
      cairo_set_source_rgba(cr, COLOUR_METER);
      snprintf(sf, 32, "%d dBm", (int)(max_rxlvl - 0.5)); // assume max_rxlvl < 0 in roundig
      cairo_move_to(cr, 80, cx - radius + 30);
      cairo_show_text(cr, sf);
    }
    break;

    case POWER: {
      //
      // Analog TX display
      //
      double offset = 220.0;
      int i;
      double x;
      double y;
      double angle;
      double radians;
      double cx = (double)METER_WIDTH / 2.0;
      double radius = cx - 20.0;
      cairo_set_line_width(cr, PAN_LINE_THICK);
      cairo_set_source_rgba(cr, COLOUR_METER);
      cairo_arc(cr, cx, cx, radius, 220.0 * M_PI / 180.0, 320.0 * M_PI / 180.0);
      cairo_stroke(cr);
      cairo_set_line_width(cr, PAN_LINE_THICK);
      cairo_set_source_rgba(cr, COLOUR_METER);

      for (i = 0; i <= 100; i++) {
        angle = (double)i + offset;
        radians = angle * M_PI / 180.0;

        if ((i % 10) == 0) {
          cairo_arc(cr, cx, cx, radius + 4, radians, radians);
          cairo_get_current_point(cr, &x, &y);
          cairo_arc(cr, cx, cx, radius, radians, radians);
          cairo_line_to(cr, x, y);
          cairo_stroke(cr);

          if ((i % 20) == 0) {
            switch (units) {
            case 0:
            case 1:
              snprintf(sf, 32, "%0.1f", 0.1 * interval * i);
              break;

            case 2:
              snprintf(sf, 32, "%d", (int) (0.1 * interval * i));
              break;
            }

            cairo_arc(cr, cx, cx, radius + 5, radians, radians);
            cairo_get_current_point(cr, &x, &y);
            cairo_new_path(cr);
            x -= 6.0;
            cairo_move_to(cr, x, y);
            cairo_show_text(cr, sf);
          }
        }

        cairo_new_path(cr);
      }

      cairo_set_line_width(cr, PAN_LINE_THICK);
      cairo_set_source_rgba(cr, COLOUR_METER);
      angle = (max_pwr * 10.0 / interval) + offset;

      if (angle > 110.0 + offset) { angle = 110.0 + offset; }

      radians = angle * M_PI / 180.0;
      cairo_arc(cr, cx, cx, radius + 8, radians, radians);
      cairo_line_to(cr, cx, cx);
      cairo_stroke(cr);
      cairo_set_source_rgba(cr, COLOUR_METER);

      switch (pa_power) {
      case PA_1W:
        snprintf(sf, 32, "%dmW", (int)(1000.0 * max_pwr + 0.5));
        break;

      case PA_5W:
      case PA_10W:
        snprintf(sf, 32, "%0.1fW", max_pwr);
        break;

      default:
        snprintf(sf, 32, "%dW", (int)(max_pwr + 0.5));
        break;
      }

      cairo_move_to(cr, 80, cx - radius + 15);
      cairo_show_text(cr, sf);

      if (can_transmit) {
        if (swr > transmitter->swr_alarm) {
          cairo_set_source_rgba(cr, COLOUR_ALARM);  // display SWR in red color
        } else {
          cairo_set_source_rgba(cr, COLOUR_METER); // display SWR in white color
        }
      }

      snprintf(sf, 32, "SWR: %1.1f:1", swr);
      cairo_move_to(cr, 60, cx - radius + 28);
      cairo_show_text(cr, sf);
      cairo_set_source_rgba(cr, COLOUR_METER);
      snprintf(sf, 32, "ALC: %2.1f dB", max_alc);
      cairo_move_to(cr, 60, cx - radius + 41);
      cairo_show_text(cr, sf);
    }
    break;
    }

    //
    // Both analog and digital, VOX status
    //
    if ((meter_type == POWER) || (vox_enabled)) {
      double offset = ((double)METER_WIDTH - 100.0) / 2.0;
      double peak = vox_get_peak();
      if (peak > 1.0) peak = 1.0;
      peak = peak * 100.0;
      cairo_set_source_rgba(cr, COLOUR_OK);
      cairo_rectangle(cr, offset, 0.0, peak, 5.0);
      cairo_fill(cr);
      cairo_select_font_face(cr, DISPLAY_FONT,
                             CAIRO_FONT_SLANT_NORMAL,
                             CAIRO_FONT_WEIGHT_BOLD);
      cairo_set_font_size(cr, DISPLAY_FONT_SIZE1);
      cairo_set_source_rgba(cr, COLOUR_METER);
      cairo_move_to(cr, 0.0, 8.0);
      cairo_show_text(cr, "Mic Lvl");
      cairo_move_to(cr, offset, 0.0);
      cairo_line_to(cr, offset, 5.0);
      cairo_stroke(cr);
      cairo_move_to(cr, offset + 50.0, 0.0);
      cairo_line_to(cr, offset + 50.0, 5.0);
      cairo_stroke(cr);
      cairo_move_to(cr, offset + 100.0, 0.0);
      cairo_line_to(cr, offset + 100.0, 5.0);
      cairo_stroke(cr);
      cairo_move_to(cr, offset, 5.0);
      cairo_line_to(cr, offset + 100.0, 5.0);
      cairo_stroke(cr);

      if (vox_enabled) {
        cairo_set_source_rgba(cr, COLOUR_ALARM);
        cairo_move_to(cr, offset + (vox_threshold * 100.0), 0.0);
        cairo_line_to(cr, offset + (vox_threshold * 100.0), 5.0);
        cairo_stroke(cr);
      }
    }
  } else {
    int text_location;
    int Y1 = METER_HEIGHT / 4;
    int Y2 = Y1 + METER_HEIGHT / 3;
    int Y4 = 4 * Y1 - 6;
    int size;
    cairo_set_source_rgba(cr, COLOUR_VFO_BACKGND);
    cairo_paint (cr);
    //
    // Digital meter, both RX and TX:
    // Mic level display
    //
    cairo_select_font_face(cr, DISPLAY_FONT,
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_line_width(cr, PAN_LINE_THICK);

    if (can_transmit) {
      cairo_set_source_rgba(cr, COLOUR_METER);
      cairo_move_to(cr, 5.0, Y1);
      cairo_line_to(cr, 5.0, Y1 - 10);
      cairo_move_to(cr, 5.0 + 25.0, Y1);
      cairo_line_to(cr, 5.0 + 25.0, Y1 - 5);
      cairo_move_to(cr, 5.0 + 50.0, Y1);
      cairo_line_to(cr, 5.0 + 50.0, Y1 - 10);
      cairo_move_to(cr, 5.0 + 75.0, Y1);
      cairo_line_to(cr, 5.0 + 75.0, Y1 - 5);
      cairo_move_to(cr, 5.0 + 100.0, Y1);
      cairo_line_to(cr, 5.0 + 100.0, Y1 - 10);
      cairo_stroke(cr);
      double peak = vox_get_peak();
      if (peak > 1.0) peak = 1.0;
      peak = peak * 100.0;
      cairo_set_source_rgba(cr, COLOUR_OK);
      cairo_rectangle(cr, 5.0, Y1 - 10, peak, 5);
      cairo_fill(cr);

      if (vox_enabled) {
        cairo_set_source_rgba(cr, COLOUR_ALARM);
        cairo_move_to(cr, 5.0 + (vox_threshold * 100.0), Y1 - 10);
        cairo_line_to(cr, 5.0 + (vox_threshold * 100.0), Y1);
        cairo_stroke(cr);
      }

      size = (METER_WIDTH - 120) / 6;

      if (size > METER_HEIGHT / 4) { size = METER_HEIGHT / 4; }

      if (size >= 10) {
        cairo_set_source_rgba(cr, COLOUR_ATTN);
        cairo_set_font_size(cr, size);
        cairo_move_to(cr, 120.0, Y1);
        cairo_show_text(cr, "Mic Level");
      }
    }

    cairo_set_source_rgba(cr, COLOUR_METER);

    switch (meter_type) {
    case SMETER:
      //
      // Digital meter, RX
      //
      // value is dBm
      text_location = 10;

      if (METER_WIDTH >= 150) {
        int i;
        cairo_set_line_width(cr, PAN_LINE_THICK);
        cairo_set_source_rgba(cr, COLOUR_METER);

        for (i = 0; i < 54; i++) {
          cairo_move_to(cr, 5 + i, Y4 - 10);

          if (i % 18 == 0) {
            cairo_line_to(cr, 5 + i, Y4 - 20);
          } else if (i % 6 == 0) {
            cairo_line_to(cr, 5 + i, Y4 - 15);
          }
        }

        cairo_stroke(cr);
        cairo_set_font_size(cr, DISPLAY_FONT_SIZE1);
        cairo_move_to(cr, 20, Y4);
        cairo_show_text(cr, "3");
        cairo_move_to(cr, 38, Y4);
        cairo_show_text(cr, "6");
        cairo_set_source_rgba(cr, COLOUR_ALARM);
        cairo_move_to(cr, 5 + 54, Y4 - 10);
        cairo_line_to(cr, 5 + 54, Y4 - 20);
        cairo_move_to(cr, 5 + 74, Y4 - 10);
        cairo_line_to(cr, 5 + 74, Y4 - 20);
        cairo_move_to(cr, 5 + 94, Y4 - 10);
        cairo_line_to(cr, 5 + 94, Y4 - 20);
        cairo_move_to(cr, 5 + 114, Y4 - 10);
        cairo_line_to(cr, 5 + 114, Y4 - 20);
        cairo_stroke(cr);
        cairo_move_to(cr, 56, Y4);
        cairo_show_text(cr, "9");
        cairo_move_to(cr, 5 + 74 - 12, Y4);
        cairo_show_text(cr, "+20");
        cairo_move_to(cr, 5 + 94 - 9, Y4);
        cairo_show_text(cr, "+40");
        cairo_move_to(cr, 5 + 114 - 6, Y4);
        cairo_show_text(cr, "+60");

        //
        // The scale for l is:
        //   0.0  --> S0
        //  54.0  --> S9
        // 114.0  --> S9+60
        // 
        double l = max_rxlvl+127.0;

        if (vfo[active_receiver->id].frequency > 30000000LL) {
          // S9 is -93 dBm for frequencies above 30 MHz
          l = max_rxlvl + 147.0;
        }

        //
        // Restrict bar to S9+60
        //
        if (l > 114.0) { l = 114.0; }

        // use colours from the "gradient" panadapter display,
        // but use no gradient: S0-S9 first colour, <S9 last colour
        cairo_pattern_t *pat = cairo_pattern_create_linear(0.0, 0.0, 114.0, 0.0);
        cairo_pattern_add_color_stop_rgba(pat, 0.00, COLOUR_GRAD1);
        cairo_pattern_add_color_stop_rgba(pat, 0.50, COLOUR_GRAD1);
        cairo_pattern_add_color_stop_rgba(pat, 0.50, COLOUR_GRAD4);
        cairo_pattern_add_color_stop_rgba(pat, 1.00, COLOUR_GRAD4);
        cairo_set_source(cr, pat);
        cairo_rectangle(cr, 5, Y2 - 20, l, 20.0);
        cairo_fill(cr);
        cairo_pattern_destroy(pat);

        //
        // Mark right edge of S-meter bar with a line in ATTN colour
        //
        cairo_set_source_rgba(cr, COLOUR_ATTN);
        cairo_move_to(cr, 5 + l, (double)Y2);
        cairo_line_to(cr, 5 + l, (double)(Y2 - 20));

        cairo_stroke(cr);
        text_location = 124;
      }

      size = (METER_WIDTH - text_location) / 5;

      if (size > METER_HEIGHT / 3) { size = METER_HEIGHT / 3; }

      if (size >= 12) {
        cairo_set_source_rgba(cr, COLOUR_ATTN);
        cairo_set_font_size(cr, size);
        snprintf(sf, 32, "%d dBm", (int)(max_rxlvl - 0.5));  // assume max_rxlvl < 0 in rounding
        cairo_move_to(cr, text_location, Y2);
        cairo_show_text(cr, sf);
      }

      break;

    case POWER:
      cairo_select_font_face(cr, DISPLAY_FONT,
                             CAIRO_FONT_SLANT_NORMAL,
                             CAIRO_FONT_WEIGHT_BOLD);
      cairo_set_font_size(cr, DISPLAY_FONT_SIZE2);

      if (protocol == ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL) {
        //
        // Power/Alc/SWR not available for SOAPY.
        //
        switch (pa_power) {
        case PA_1W:
          snprintf(sf, 32, "FWD: %dmW", (int)(1000.0 * max_pwr + 0.5));
          break;

        case PA_5W:
        case PA_10W:
          snprintf(sf, 32, "FWD: %0.1fW", max_pwr);
          break;

        default:
          snprintf(sf, 32, "FWD: %dW", (int)(max_pwr + 0.5));
          break;
        }

        cairo_move_to(cr, 10, Y2);
        cairo_show_text(cr, sf);

        if (can_transmit) {
          if (swr > transmitter->swr_alarm) {
            cairo_set_source_rgba(cr, COLOUR_ALARM);  // display SWR in red color
          } else {
            cairo_set_source_rgba(cr, COLOUR_METER); // display SWR in white color
          }
        }

        cairo_select_font_face(cr, DISPLAY_FONT,
                               CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, DISPLAY_FONT_SIZE2);
        snprintf(sf, 32, "SWR: %1.1f:1", swr);
        cairo_move_to(cr, 10, Y4);
        cairo_show_text(cr, sf);
      }

      cairo_set_source_rgba(cr, COLOUR_METER);  // revert to white color
      snprintf(sf, 32, "ALC: %2.1f dB", max_alc);
      cairo_move_to(cr, METER_WIDTH / 2, Y2);
      cairo_show_text(cr, sf);
      break;
    }
  }

  //
  // This is the same for analog and digital metering
  //
  cairo_destroy(cr);
  gtk_widget_queue_draw (meter);
}
