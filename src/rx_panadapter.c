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
#include <math.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>
//#include <arpa/inet.h> //??

#include <wdsp.h>

#include "appearance.h"
#include "agc.h"
#include "band.h"
#include "discovered.h"
#include "radio.h"
#include "receiver.h"
#include "transmitter.h"
#include "rx_panadapter.h"
#include "vfo.h"
#include "mode.h"
#include "actions.h"
#ifdef GPIO
  #include "gpio.h"
#endif
#ifdef CLIENT_SERVER
  #include "client_server.h"
#endif
#ifdef USBOZY
  #include "ozyio.h"
#endif
#include "message.h"


static void apply_blue_gradient(cairo_t *cr, int width, int height) {
    // Create a gradient from top to bottom
    cairo_pattern_t *gradient = cairo_pattern_create_linear(0, 0, 0, height);

    // Add color stops to the gradient
    // Dark blue at the top (RGB: 0, 0, 0)
    cairo_pattern_add_color_stop_rgb(gradient, 0.0, 0.0, 0.0, 0.0);
    // Light blue at the bottom (RGB: 0, 0, 1)
    cairo_pattern_add_color_stop_rgb(gradient, 1.0, 0.0, 0.0, 1.0);

    // Set the gradient as the source for subsequent drawing
    cairo_set_source(cr, gradient);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_fill(cr);

    // Clean up the gradient pattern
    cairo_pattern_destroy(gradient);
}


static cairo_surface_t *background_surface = NULL;

static void load_background_image(const char *filename) {
    if (background_surface) {
        cairo_surface_destroy(background_surface);
    }
    background_surface = cairo_image_surface_create_from_png(filename);
}

static void panadapter_resize_event_cb(GtkWidget* widget, int width, int height, gpointer data) {
  RECEIVER *rx = (RECEIVER *)data;
  int mywidth = gtk_widget_get_allocated_width (widget);
  int myheight = gtk_widget_get_allocated_height (widget);

  if (rx->panadapter_surface) {
    cairo_surface_destroy (rx->panadapter_surface);
  }

  rx->panadapter_surface = gdk_surface_create_similar_surface(gtk_native_get_surface(gtk_widget_get_native(widget)), CAIRO_CONTENT_COLOR, mywidth, myheight);
  cairo_t *cr = cairo_create(rx->panadapter_surface);
  cairo_set_source_rgba(cr, COLOUR_PAN_BACKGND);
  cairo_paint(cr);
  cairo_destroy(cr);
}

/* Redraw the screen from the surface. Note that the ::draw
 * signal receives a ready-to-be-used cairo_t that is already
 * clipped to only draw the exposed areas of the widget
 */
static void panadapter_draw_cb (GtkDrawingArea *drawing_area, cairo_t *cr, int width, int height, gpointer data) {
  RECEIVER *rx = (RECEIVER *)data;
  if (rx->panadapter_surface) {
    cairo_set_source_surface (cr, rx->panadapter_surface, 0.0, 0.0);
    cairo_paint (cr);
  }
}

static void rx_panadapter_left_click(GtkGestureClick *gesture, int n_press, double x, double y, gpointer data)
{
  
}

static void rx_panadapter_left_release(GtkGestureClick *gesture, int n_press, double x, double y, gpointer data)
{
  
}

static void rx_panadapter_right_click(GtkGestureClick *gesture, int n_press, double x, double y, gpointer data)
{
}

static void rx_panadapter_right_release(GtkGestureClick *gesture, int n_press, double x, double y, gpointer data)
{
}

static void rx_panadapter_scroll(GtkEventControllerScroll *controller, double dx, double dy, gpointer data)
{
  receiver_scroll_event(dy);
}

static void rx_panadapter_motion(GtkEventControllerMotion *controller, double x, double y, gpointer data)
{
   //receiver_drag_update(x, y, data);
}

static void rx_panadapter_zoom_gesture(GtkGestureZoom *gesture, gdouble scale, gpointer user_data) {
  t_print("Zoom gesture detected with scale factor: %f\n", scale);
}

volatile double x_now;

static void rx_panadapter_drag_begin(GtkGestureDrag *gesture, double start_x, double start_y, gpointer data)
{
  receiver_button_press_event(GDK_BUTTON_PRIMARY, start_x, start_y, data);
  x_now = start_x;
}

static void rx_panadapter_drag_update(GtkGestureDrag *gesture, double offset_x, double offset_y, gpointer data)
{
  //t_print("drag:: offset_x:%f now:%f to:%f\n", offset_x, x_now, offset_x+x_now);
  receiver_drag_update(offset_x+x_now, offset_y, data);
}

static void rx_panadapter_drag_end(GtkGestureDrag *gesture, double offset_x, double offset_y, gpointer data)
{
  receiver_button_release_event(GDK_BUTTON_PRIMARY, offset_x, offset_y, data);
}


static void rx_panadapter_long_press(GtkGestureLongPress *gesture, gdouble x, gdouble y, gpointer data) {
    receiver_button_press_event(GDK_BUTTON_SECONDARY, x, y, data);
}


void rx_panadapter_update(RECEIVER *rx) {
  int i;
  float *samples;
  cairo_text_extents_t extents;
  long long f;
  long long divisor;
  double soffset;
  gboolean active = active_receiver == rx;
  int mywidth = gtk_widget_get_allocated_width (rx->panadapter);
  int myheight = gtk_widget_get_allocated_height (rx->panadapter);
  samples = rx->pixel_samples;
  cairo_t *cr;
  cr = cairo_create (rx->panadapter_surface);

  if (background_surface && cairo_surface_status(background_surface) == CAIRO_STATUS_SUCCESS) {
      int img_width = cairo_image_surface_get_width(background_surface);
      int img_height = cairo_image_surface_get_height(background_surface);
      double scalex = (double)mywidth / img_width;
      double scaley = (double)myheight / img_height;

      cairo_save(cr);

      cairo_scale(cr, scalex, scaley);
      cairo_set_source_surface(cr, background_surface, 0, 0);
      cairo_paint_with_alpha(cr, 0.5);

      cairo_restore(cr);
  }
  else {
    apply_blue_gradient(cr, mywidth, myheight);
    //cairo_set_source_rgba(cr, COLOUR_PAN_BACKGND);
    //cairo_rectangle(cr, 0, 0, mywidth, myheight);
    cairo_fill(cr);
  }
  
  double HzPerPixel = rx->hz_per_pixel;  // need this many times
  int mode = vfo[rx->id].mode;
  long long frequency = vfo[rx->id].frequency;
  int vfoband = vfo[rx->id].band;
  long long offset = vfo[rx->id].ctun ? vfo[rx->id].offset : 0;
  //
  // soffset contains all corrections for attenuation and preamps
  // Perhaps some adjustment is necessary for those old radios which have
  // switchable preamps.
  //
  const BAND *band = band_get_band(vfoband);
  int calib = rx_gain_calibration - band->gain;
  soffset = (double) calib + (double)adc[rx->adc].attenuation - adc[rx->adc].gain;

  if (filter_board == ALEX && rx->adc == 0) {
    soffset += (double)(10 * rx->alex_attenuation - 20 * rx->preamp);
  }

  if (filter_board == CHARLY25 && rx->adc == 0) {
    soffset += (double)(12 * rx->alex_attenuation - 18 * rx->preamp - 18 * rx->dither);
  }

  // In diversity mode, the RX2 frequency tracks the RX1 frequency
  if (diversity_enabled && rx->id == 1) {
    frequency = vfo[0].frequency;
    vfoband = vfo[0].band;
    mode = vfo[0].mode;
  }

  long long half = (long long)rx->sample_rate / 2LL;
  double vfofreq = ((double) rx->pixels * 0.5) - (double)rx->pan;

  //
  //
  // The CW frequency is the VFO frequency and the center of the spectrum
  // then is at the VFO frequency plus or minus the sidetone frequency. However we
  // will keep the center of the PANADAPTER at the VFO frequency and shift the
  // pixels of the spectrum.
  //

  if (mode == modeCWU) {
    frequency -= cw_keyer_sidetone_frequency;
    vfofreq += (double) cw_keyer_sidetone_frequency / HzPerPixel;
  } else if (mode == modeCWL) {
    frequency += cw_keyer_sidetone_frequency;
    vfofreq -= (double) cw_keyer_sidetone_frequency / HzPerPixel;
  }

  long long min_display = frequency - half + (long long)((double)rx->pan * HzPerPixel);
  long long max_display = min_display + (long long)((double)rx->width * HzPerPixel);

  if (vfoband == band60) {
    for (i = 0; i < channel_entries; i++) {
      long long low_freq = band_channels_60m[i].frequency - (band_channels_60m[i].width / (long long)2);
      long long hi_freq = band_channels_60m[i].frequency + (band_channels_60m[i].width / (long long)2);
      double x1 = (double) (low_freq - min_display) / HzPerPixel;
      double x2 = (double) (hi_freq - min_display) / HzPerPixel;
      cairo_set_source_rgba(cr, COLOUR_PAN_60M);
      cairo_rectangle(cr, x1, 0.0, x2 - x1, myheight);
      cairo_fill(cr);
    }
  }

  // filter
  cairo_set_source_rgba (cr, COLOUR_PAN_FILTER);
  double filter_left = ((double)rx->pixels * 0.5) - (double)rx->pan + (((double)rx->filter_low + offset) / HzPerPixel);
  double filter_right = ((double)rx->pixels * 0.5) - (double)rx->pan + (((double)rx->filter_high + offset) / HzPerPixel);
  cairo_rectangle(cr, filter_left, 0.0, filter_right - filter_left, myheight);
  cairo_fill(cr);

  // plot the levels
  if (active) {
    cairo_set_source_rgba(cr, COLOUR_PAN_LINE);
  } else {
    cairo_set_source_rgba(cr, COLOUR_PAN_LINE_WEAK);
  }

  double dbm_per_line = (double)myheight / ((double)rx->panadapter_high - (double)rx->panadapter_low);
  cairo_set_line_width(cr, PAN_LINE_THIN);
  cairo_select_font_face(cr, DISPLAY_FONT, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, DISPLAY_FONT_SIZE2);
  char v[32];

  for (i = rx->panadapter_high; i >= rx->panadapter_low; i--) {
    int mod = abs(i) % rx->panadapter_step;

    if (mod == 0) {
      double y = (double)(rx->panadapter_high - i) * dbm_per_line;
      cairo_move_to(cr, 0.0, y);
      cairo_line_to(cr, mywidth, y);
      snprintf(v, 32, "%d dBm", i);
      cairo_move_to(cr, 1, y);
      cairo_show_text(cr, v);
    }
  }

  cairo_set_line_width(cr, PAN_LINE_THIN);
  cairo_stroke(cr);
  //
  // plot frequency markers
  // calculate a divisor such that we have about 65
  // pixels distance between frequency markers,
  // and then round upwards to the  next 1/2/5 seris
  //
  divisor = (rx->sample_rate * 65) / rx->pixels;

  if (divisor > 500000LL) { divisor = 1000000LL; }
  else if (divisor > 200000LL) { divisor = 500000LL; }
  else if (divisor > 100000LL) { divisor = 200000LL; }
  else if (divisor >  50000LL) { divisor = 100000LL; }
  else if (divisor >  20000LL) { divisor =  50000LL; }
  else if (divisor >  10000LL) { divisor =  20000LL; }
  else if (divisor >   5000LL) { divisor =  10000LL; }
  else if (divisor >   2000LL) { divisor =   5000LL; }
  else if (divisor >   1000LL) { divisor =   2000LL; }
  else { divisor =   1000LL; }

  //
  // Calculate the actual distance of frequency markers
  // (in pixels)
  //
  int marker_distance = (rx->pixels * divisor) / rx->sample_rate;
  f = ((min_display / divisor) * divisor) + divisor;
  cairo_select_font_face(cr, DISPLAY_FONT,
                         CAIRO_FONT_SLANT_NORMAL,
                         CAIRO_FONT_WEIGHT_BOLD);
  //
  // If space is available, increase font size of freq. labels a bit
  //
  int marker_extra = (marker_distance > 100) ? 2 : 0;
  cairo_set_font_size(cr, DISPLAY_FONT_SIZE2 + marker_extra);

  while (f < max_display) {
    double x = (double)(f - min_display) / HzPerPixel;
    cairo_move_to(cr, x, 0);
    cairo_line_to(cr, x, myheight);

    //
    // For frequency marker lines very close to the left or right
    // edge, do not print a frequency since this probably won't fit
    // on the screen
    //
    if ((f >= min_display + divisor / 2) && (f <= max_display - divisor / 2)) {
      //
      // For frequencies larger than 10 GHz, we cannot
      // display all digits here so we give three dots
      // and three "Mhz" digits
      //
      if (f > 10000000000LL && marker_distance < 80) {
        snprintf(v, 32, "...%03lld.%03lld", (f / 1000000) % 1000, (f % 1000000) / 1000);
      } else {
        snprintf(v, 32, "%0lld.%03lld", f / 1000000, (f % 1000000) / 1000);
      }

      // center text at "x" position
      cairo_text_extents(cr, v, &extents);
      cairo_move_to(cr, x - (extents.width / 2.0), 10 + marker_extra);
      cairo_show_text(cr, v);
    }

    f += divisor;
  }

  cairo_set_line_width(cr, PAN_LINE_THIN);
  cairo_stroke(cr);

  if (vfoband != band60) {
    // band edges
    if (band->frequencyMin != 0LL) {
      cairo_set_source_rgba(cr, COLOUR_ALARM);
      cairo_set_line_width(cr, PAN_LINE_THICK);

      if ((min_display < band->frequencyMin) && (max_display > band->frequencyMin)) {
        double x = (double)(band->frequencyMin - min_display) / HzPerPixel;
        cairo_move_to(cr, x, 0);
        cairo_line_to(cr, x, myheight);
        cairo_set_line_width(cr, PAN_LINE_EXTRA);
        cairo_stroke(cr);
      }

      if ((min_display < band->frequencyMax) && (max_display > band->frequencyMax)) {
        double x = (double) (band->frequencyMax - min_display) / HzPerPixel;
        cairo_move_to(cr, x, 0);
        cairo_line_to(cr, x, myheight);
        cairo_set_line_width(cr, PAN_LINE_EXTRA);
        cairo_stroke(cr);
      }
    }
  }

#ifdef CLIENT_SERVER

  if (clients != NULL) {
    char text[64];
    cairo_select_font_face(cr, DISPLAY_FONT,
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_source_rgba(cr, COLOUR_SHADE);
    cairo_set_font_size(cr, DISPLAY_FONT_SIZE4);
    inet_ntop(AF_INET, &(((struct sockaddr_in *)&clients->address)->sin_addr), text, 64);
    cairo_text_extents(cr, text, &extents);
    cairo_move_to(cr, ((double)mywidth / 2.0) - (extents.width / 2.0), (double)myheight / 2.0);
    cairo_show_text(cr, text);
  }

#endif

  // agc
  if (rx->agc != AGC_OFF) {
    cairo_set_line_width(cr, PAN_LINE_THICK);
    double knee_y = rx->agc_thresh + soffset;
    knee_y = floor((rx->panadapter_high - knee_y)
                   * (double) myheight
                   / (rx->panadapter_high - rx->panadapter_low));
    double hang_y = rx->agc_hang + soffset;
    hang_y = floor((rx->panadapter_high - hang_y)
                   * (double) myheight
                   / (rx->panadapter_high - rx->panadapter_low));

    if (rx->agc != AGC_MEDIUM && rx->agc != AGC_FAST) {
      if (active) {
        cairo_set_source_rgba(cr, COLOUR_ATTN);
      } else {
        cairo_set_source_rgba(cr, COLOUR_ATTN_WEAK);
      }

      cairo_move_to(cr, 40.0, hang_y - 8.0);
      cairo_rectangle(cr, 40, hang_y - 8.0, 8.0, 8.0);
      cairo_fill(cr);
      cairo_move_to(cr, 40.0, hang_y);
      cairo_line_to(cr, (double)mywidth - 40.0, hang_y);
      cairo_set_line_width(cr, PAN_LINE_THICK);
      cairo_stroke(cr);
      cairo_move_to(cr, 48.0, hang_y);
      cairo_show_text(cr, "-H");
    }

    if (active) {
      cairo_set_source_rgba(cr, COLOUR_OK);
    } else {
      cairo_set_source_rgba(cr, COLOUR_OK_WEAK);
    }

    cairo_move_to(cr, 40.0, knee_y - 8.0);
    cairo_rectangle(cr, 40, knee_y - 8.0, 8.0, 8.0);
    cairo_fill(cr);
    cairo_move_to(cr, 40.0, knee_y);
    cairo_line_to(cr, (double)mywidth - 40.0, knee_y);
    cairo_set_line_width(cr, PAN_LINE_THICK);
    cairo_stroke(cr);
    cairo_move_to(cr, 48.0, knee_y);
    cairo_show_text(cr, "-G");
  }

  // cursor
  if (active) {
    cairo_set_source_rgba(cr, COLOUR_ALARM);
  } else {
    cairo_set_source_rgba(cr, COLOUR_ALARM_WEAK);
  }

  cairo_move_to(cr, vfofreq + (offset / HzPerPixel), 0.0);
  cairo_line_to(cr, vfofreq + (offset / HzPerPixel), myheight);
  cairo_set_line_width(cr, PAN_LINE_THIN);
  cairo_stroke(cr);
  // signal
  double s1;
  int pan = rx->pan;

  if (radio_is_remote) {
    pan = 0;
  }

  samples[pan] = -200.0;
  samples[mywidth - 1 + pan] = -200.0;
  //
  // most HPSDR only have attenuation (no gain), while HermesLite-II and SOAPY use gain (no attenuation)
  //
  s1 = (double)samples[pan] + soffset;
  s1 = floor((rx->panadapter_high - s1)
             * (double) myheight
             / (rx->panadapter_high - rx->panadapter_low));
  cairo_move_to(cr, 0.0, s1);

  for (i = 1; i < mywidth; i++) {
    double s2;
    s2 = (double)samples[i + pan] + soffset;
    s2 = floor((rx->panadapter_high - s2)
               * (double) myheight
               / (rx->panadapter_high - rx->panadapter_low));
    cairo_line_to(cr, i, s2);
  }

  cairo_pattern_t *gradient;
  gradient = NULL;

  if (rx->display_gradient) {
    gradient = cairo_pattern_create_linear(0.0, myheight, 0.0, 0.0);
    // calculate where S9 is
    double S9 = -73;

    if (vfo[rx->id].frequency > 30000000LL) {
      S9 = -93;
    }

    S9 = floor((rx->panadapter_high - S9)
               * (double) myheight
               / (rx->panadapter_high - rx->panadapter_low));
    S9 = 1.0 - (S9 / (double)myheight);

    if (active) {
      cairo_pattern_add_color_stop_rgba(gradient, 0.0,         COLOUR_GRAD1);
      cairo_pattern_add_color_stop_rgba(gradient, S9 / 3.0,      COLOUR_GRAD2);
      cairo_pattern_add_color_stop_rgba(gradient, (S9 / 3.0) * 2.0, COLOUR_GRAD3);
      cairo_pattern_add_color_stop_rgba(gradient, S9,          COLOUR_GRAD4);
    } else {
      cairo_pattern_add_color_stop_rgba(gradient, 0.0,         COLOUR_GRAD1_WEAK);
      cairo_pattern_add_color_stop_rgba(gradient, S9 / 3.0,      COLOUR_GRAD2_WEAK);
      cairo_pattern_add_color_stop_rgba(gradient, (S9 / 3.0) * 2.0, COLOUR_GRAD3_WEAK);
      cairo_pattern_add_color_stop_rgba(gradient, S9,          COLOUR_GRAD4_WEAK);
    }

    cairo_set_source(cr, gradient);
  } else {
    //
    // Different shades of white
    //
    if (active) {
      if (!rx->display_filled) {
        cairo_set_source_rgba(cr, COLOUR_PAN_FILL3);
      } else {
        cairo_set_source_rgba(cr, COLOUR_PAN_FILL2);
      }
    } else {
      cairo_set_source_rgba(cr, COLOUR_PAN_FILL1);
    }
  }

  if (rx->display_filled) {
    cairo_close_path (cr);
    cairo_fill_preserve (cr);
    cairo_set_line_width(cr, PAN_LINE_THIN);
  } else {
    //
    // if not filling, use thicker line
    //
    cairo_set_line_width(cr, PAN_LINE_THICK);
  }

  cairo_stroke(cr);

  if (gradient) {
    cairo_pattern_destroy(gradient);
  }

  /*
  #ifdef GPIO
    if(rx->id==0 && controller==CONTROLLER1) {

      cairo_set_source_rgba(cr,COLOUR_ATTN);
      cairo_set_font_size(cr,DISPLAY_FONT_SIZE3);
      if(ENABLE_E2_ENCODER) {
        cairo_move_to(cr, mywidth-200,70);
        snprintf(text,"%s (%s)",encoder_string[e2_encoder_action],sw_string[e2_sw_action]);
        cairo_show_text(cr, text);
      }

      if(ENABLE_E3_ENCODER) {
        cairo_move_to(cr, mywidth-200,90);
        snprintf(text, 64, "%s (%s)",encoder_string[e3_encoder_action],sw_string[e3_sw_action]);
        cairo_show_text(cr, text);
      }

      if(ENABLE_E4_ENCODER) {
        cairo_move_to(cr, mywidth-200,110);
        snprintf(text, 64, "%s (%s)",encoder_string[e4_encoder_action],sw_string[e4_sw_action]);
        cairo_show_text(cr, text);
      }
    }
  #endif
  */

  if (rx->id == 0 && !radio_is_remote) {
    display_panadapter_messages(cr, mywidth, rx->fps);
  }

  //
  // For horizontal stacking, draw a vertical separator,
  // at the right edge of RX1, and at the left
  // edge of RX2.
  //
  if (rx_stack_horizontal && receivers > 1) {
    if (rx->id == 0) {
      cairo_move_to(cr, mywidth - 1, 0);
      cairo_line_to(cr, mywidth - 1, myheight);
    } else {
      cairo_move_to(cr, 0, 0);
      cairo_line_to(cr, 0, myheight);
    }

    cairo_set_source_rgba(cr, COLOUR_PAN_LINE);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
  }

  cairo_destroy (cr);
  gtk_widget_queue_draw (rx->panadapter);
}

void rx_panadapter_init(RECEIVER *rx, int width, int height) {
  GtkGesture *drag;
  GtkGesture *leftclick;
  GtkGesture *rightclick;

  rx->panadapter_surface = NULL;
  rx->panadapter = gtk_drawing_area_new();
  gtk_widget_set_size_request(rx->panadapter, width, height);
  /* Signals used to handle the backing surface */
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(rx->panadapter), panadapter_draw_cb, rx, NULL);
  g_signal_connect_after(rx->panadapter, "resize", G_CALLBACK (panadapter_resize_event_cb), rx);
  /* Event signals */

  // set up the left mouse button.
  leftclick = gtk_gesture_click_new();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE(leftclick), GDK_BUTTON_PRIMARY);
  gtk_widget_add_controller (rx->panadapter, GTK_EVENT_CONTROLLER (leftclick));
  g_signal_connect(leftclick, "pressed", G_CALLBACK (rx_panadapter_left_click), rx);
  g_signal_connect(leftclick, "released", G_CALLBACK (rx_panadapter_left_release), rx);

  // long press
  GtkGesture *long_press = gtk_gesture_long_press_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(long_press), GDK_BUTTON_PRIMARY);
  gtk_widget_add_controller(rx->panadapter, GTK_EVENT_CONTROLLER(long_press));
  g_signal_connect(long_press, "pressed", G_CALLBACK(rx_panadapter_long_press), rx);

  // set up the right mouse button
  rightclick = gtk_gesture_click_new();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE(rightclick), GDK_BUTTON_SECONDARY);
  gtk_widget_add_controller (rx->panadapter, GTK_EVENT_CONTROLLER (rightclick));
  g_signal_connect(rightclick, "pressed", G_CALLBACK (rx_panadapter_right_click), rx);
  g_signal_connect(rightclick, "released", G_CALLBACK (rx_panadapter_right_release), rx);

  drag = gtk_gesture_drag_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (drag), GDK_BUTTON_PRIMARY);
  gtk_widget_add_controller (rx->panadapter, GTK_EVENT_CONTROLLER (drag));
  // look into these.
   g_signal_connect(drag, "drag-begin", G_CALLBACK(rx_panadapter_drag_begin), rx);
   g_signal_connect(drag, "drag-update", G_CALLBACK(rx_panadapter_drag_update), rx);
   g_signal_connect(drag, "drag-end", G_CALLBACK(rx_panadapter_drag_end), rx);

  // scroll wheel
  GtkEventController *scrollcontroller = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
  g_signal_connect(scrollcontroller, "scroll", G_CALLBACK(rx_panadapter_scroll), rx);
  gtk_widget_add_controller(rx->panadapter, scrollcontroller);

  // motion.
  GtkEventController *motioncontroller;
  motioncontroller = gtk_event_controller_motion_new();
  g_signal_connect(motioncontroller, "motion", G_CALLBACK(rx_panadapter_motion), rx);
  gtk_widget_add_controller(rx->panadapter, motioncontroller);

  // pinch-to-zoom
  GtkGesture *zoom = gtk_gesture_zoom_new();
  g_signal_connect(zoom, "scale-changed", G_CALLBACK(rx_panadapter_zoom_gesture), rx);
  gtk_widget_add_controller(rx->panadapter, GTK_EVENT_CONTROLLER(zoom));

  load_background_image("assets/panadapter_bg.png");

}

void display_panadapter_messages(cairo_t *cr, int width, int fps) {
  char text[64];

  if (display_warnings) {
    //
    // Sequence errors
    // ADC overloads
    // TX FIFO under- and overruns
    // high SWR warning
    //
    // Are shown on display for 2 seconds
    //
    cairo_set_source_rgba(cr, COLOUR_ALARM);
    cairo_set_font_size(cr, DISPLAY_FONT_SIZE2);

    if (sequence_errors != 0) {
      static unsigned int sequence_error_count = 0;
      cairo_move_to(cr, 100.0, 50.0);
      cairo_show_text(cr, "Sequence Error");
      sequence_error_count++;

      if (sequence_error_count == 2 * (unsigned)fps) {
        sequence_errors = 0;
        sequence_error_count = 0;
      }
    }

    if (adc0_overload || adc1_overload) {
      static unsigned int adc_error_count = 0;
      cairo_move_to(cr, 100.0, 70.0);

      if (adc0_overload && !adc1_overload) {
        cairo_show_text(cr, "ADC0 overload");
      }

      if (adc1_overload && !adc0_overload) {
        cairo_show_text(cr, "ADC1 overload");
      }

      if (adc0_overload && adc1_overload) {
        cairo_show_text(cr, "ADC0+1 overload");
      }

      adc_error_count++;

      if (adc_error_count > 2 * (unsigned)fps) {
        adc_error_count = 0;
        adc0_overload = 0;
        adc1_overload = 0;
#ifdef USBOZY
        mercury_overload[0] = 0;
        mercury_overload[1] = 0;
#endif
      }
    }

    if (high_swr_seen) {
      static unsigned int swr_protection_count = 0;
      cairo_move_to(cr, 100.0, 90.0);
      snprintf(text, 64, "! High SWR");
      cairo_show_text(cr, text);
      swr_protection_count++;

      if (swr_protection_count >= 3 * (unsigned)fps) {
        high_swr_seen = 0;
        swr_protection_count = 0;
      }
    }

    static unsigned int tx_fifo_count = 0;

    if (tx_fifo_underrun) {
      cairo_move_to(cr, 100.0, 110.0);
      cairo_show_text(cr, "TX Underrun");
      tx_fifo_count++;
    }

    if (tx_fifo_overrun) {
      cairo_move_to(cr, 100.0, 130.0);
      cairo_show_text(cr, "TX Overrun");
      tx_fifo_count++;
    }

    if (tx_fifo_count >= 2 * (unsigned)fps) {
      tx_fifo_underrun = 0;
      tx_fifo_overrun = 0;
      tx_fifo_count = 0;
    }
  }

  if (TxInhibit) {
    cairo_set_source_rgba(cr, COLOUR_ALARM);
    cairo_set_font_size(cr, DISPLAY_FONT_SIZE3);
    cairo_move_to(cr, 100.0, 30.0);
    cairo_show_text(cr, "TX Inhibit");
  }

  if (display_pacurr && isTransmitting() && !TxInhibit) {
    double v;  // value
    int flag;  // 0: dont, 1: do
    static int count = 0;
    //
    // Display a maximum value twice per second
    // to avoid flicker
    //
    static double max1 = 0.0;
    static double max2 = 0.0;
    cairo_set_source_rgba(cr, COLOUR_ATTN);
    cairo_set_font_size(cr, DISPLAY_FONT_SIZE3);

    //
    // Supply voltage or PA temperature
    //
    switch (device) {
    case DEVICE_HERMES_LITE2:
      // (3.26*(ExPwr/4096.0) - 0.5) /0.01
      v = 0.0795898 * exciter_power - 50.0;

      if (v < 0) { v = 0; }

      if (count == 0) { max1 = v; }

      snprintf(text, 64, "%0.0f°C", max1);
      flag = 1;
      break;

    case DEVICE_ORION2:
    case NEW_DEVICE_ORION2:
    case NEW_DEVICE_SATURN:
      // 5 (ADC0_avg / 4095 )* VDiv, VDiv = (22.0 + 1.0) / 1.1
      v = 0.02553 * ADC0;

      if (v < 0) { v = 0; }

      if (count == 0) { max1 = v; }

      snprintf(text, 64, "%0.1fV", max1);
      flag = 1;
      break;

    default:
      flag = 0;
      break;
    }

    if (flag) {
      cairo_move_to(cr, 100.0, 30.0);
      cairo_show_text(cr, text);
    }

    //
    // PA current
    //
    switch (device) {
    case DEVICE_HERMES_LITE2:
      // 1270 ((3.26f * (ADC0 / 4096)) / 50) / 0.04
      v = 0.505396 * ADC0;

      if (v < 0) { v = 0; }

      if (count == 0) { max2 = v; }

      snprintf(text, 64, "%0.0fmA", max2);
      flag = 1;
      break;

    case DEVICE_ORION2:
    case NEW_DEVICE_ORION2:
      // ((ADC1*5000)/4095 - Voff)/Sens, Voff = 360, Sens = 120
      v = 0.0101750 * ADC1 - 3.0;

      if (v < 0) { v = 0; }

      if (count == 0) { max2 = v; }

      snprintf(text, 64, "%0.1fA", max2);
      flag = 1;
      break;

    case NEW_DEVICE_SATURN:
      // ((ADC1*5000)/4095 - Voff)/Sens, Voff = 0, Sens = 66.23
      v = 0.0184358 * ADC1;

      if (v < 0) { v = 0; }

      if (count == 0) { max2 = v; }

      snprintf(text, 64, "%0.1fA", max2);
      flag = 1;
      break;

    default:
      flag = 0;
      break;
    }

    if (flag) {
      cairo_move_to(cr, 160.0, 30.0);
      cairo_show_text(cr, text);
    }

    if (++count >= fps / 2) { count = 0; }
  }

  if (capture_state == CAP_RECORDING || capture_state == CAP_REPLAY) {
      cairo_set_source_rgba(cr, COLOUR_ATTN);
      cairo_set_font_size(cr, DISPLAY_FONT_SIZE3);
      cairo_move_to(cr, (double) width -100.0, 30.0);
      if (capture_state == CAP_RECORDING) {
        cairo_show_text(cr, "Recording");
      } else {
        cairo_show_text(cr, "Replay");
      }
  }
}
