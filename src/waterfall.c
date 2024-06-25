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
#include <semaphore.h>
#include <string.h>
#include "radio.h"
#include "vfo.h"
#include "band.h"
#include "waterfall.h"
#include "message.h"
#ifdef CLIENT_SERVER
  #include "client_server.h"
#endif

static int colorLowR = 0; // black
static int colorLowG = 0;
static int colorLowB = 0;

static int colorHighR = 255; // yellow
static int colorHighG = 255;
static int colorHighB = 0;

static double hz_per_pixel;

static int my_width;
static int my_heigt;

/* Create a new surface of the appropriate size to store our scribbles */
static void waterfall_resize_event_cb(GtkWidget* widget, int width, int height, gpointer data) {
  RECEIVER *rx = (RECEIVER *)data;
  my_width = gtk_widget_get_allocated_width (widget);
  my_heigt = gtk_widget_get_allocated_height (widget);
  rx->pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, my_width, my_heigt);
  unsigned char *pixels = gdk_pixbuf_get_pixels (rx->pixbuf);
  memset(pixels, 0, my_width * my_heigt * 3);
}

/* Redraw the screen from the surface. Note that the ::draw
 * signal receives a ready-to-be-used cairo_t that is already
 * clipped to only draw the exposed areas of the widget
 */
static void waterfall_draw_cb (GtkDrawingArea *drawing_area, cairo_t *cr, int width, int height, gpointer data) {
  const RECEIVER *rx = (RECEIVER *)data;
  gdk_cairo_set_source_pixbuf (cr, rx->pixbuf, 0, 0);
  cairo_paint (cr);
}

static gboolean waterfall_button_press_event_cb (GtkWidget *widget, GdkEvent *event, gpointer data) {
  t_print("%s File: %s, Line: %d FIXME\n", __FUNCTION__, __FILE__, __LINE__);
  //return receiver_button_press_event(widget, event, data);
  return FALSE;
}

static gboolean waterfall_button_release_event_cb (GtkWidget  *widget, GdkEvent *event, gpointer data) {
  //return receiver_button_release_event(widget, event, data);
  t_print("%s File: %s, Line: %d FIXME\n", __FUNCTION__, __FILE__, __LINE__);
  return FALSE;
}

static gboolean waterfall_motion_notify_event_cb (GtkWidget *widget, GdkEvent *event, gpointer data) {
  //return receiver_motion_notify_event(widget, event, data);
  t_print("%s File: %s, Line: %d FIXME\n", __FUNCTION__, __FILE__, __LINE__);
  return FALSE;
}

static void rx_waterfall_scroll(GtkEventControllerScroll *controller, double dx, double dy, gpointer data)
{
  receiver_scroll_event(dy);
}

static void rx_waterfall_motion(GtkEventControllerMotion *controller, double x, double y, gpointer data)
{
   //receiver_drag_update(x, y, data);
}

volatile double wf_x_now;

static void rx_waterfall_drag_begin(GtkGestureDrag *gesture, double start_x, double start_y, gpointer data)
{
  receiver_button_press_event(GDK_BUTTON_PRIMARY, start_x, start_y, data);
  wf_x_now = start_x;
}

static void rx_waterfall_drag_update(GtkGestureDrag *gesture, double offset_x, double offset_y, gpointer data)
{
  receiver_drag_update(offset_x+wf_x_now, offset_y, data);
}

static void rx_waterfall_drag_end(GtkGestureDrag *gesture, double offset_x, double offset_y, gpointer data)
{
  receiver_button_release_event(GDK_BUTTON_PRIMARY, offset_x, offset_y, data);
}

void waterfall_update(RECEIVER *rx) {
  int i;
  const float *samples;
  long long vfofreq = vfo[rx->id].frequency; // access only once to be thread-safe
  int  freq_changed = 0;                    // flag whether we have just "rotated"
  int pan = rx->pan;
  int zoom = rx->zoom;
#ifdef CLIENT_SERVER

  if (radio_is_remote) {
    pan = 0;
    zoom = 1;
  }

#endif

  if (rx->pixbuf) {
    unsigned char *pixels = gdk_pixbuf_get_pixels (rx->pixbuf);
    int width = gdk_pixbuf_get_width(rx->pixbuf);
    int height = gdk_pixbuf_get_height(rx->pixbuf);
    int rowstride = gdk_pixbuf_get_rowstride(rx->pixbuf);
    hz_per_pixel = (double)rx->sample_rate / ((double)my_width * rx->zoom);

    //
    // The existing waterfall corresponds to a VFO frequency rx->waterfall_frequency, a zoom value rx->waterfall_zoom and
    // a pan value rx->waterfall_pan. If the zoom value changes, or if the waterfill needs horizontal shifting larger
    // than the width of the waterfall (band change or big frequency jump), re-init the waterfall.
    // Otherwise, shift the waterfall by an appropriate number of pixels.
    //
    // Note that VFO frequency changes can occur in very many very small steps, such that in each step, the horizontal
    // shifting is only a fraction of one pixel. In this case, there will be every now and then a horizontal shift that
    // corrects for a number of VFO update steps.
    //
    if (rx->waterfall_frequency != 0 && (rx->sample_rate == rx->waterfall_sample_rate)
        && (rx->zoom == rx->waterfall_zoom)) {
      if (rx->waterfall_frequency != vfofreq || rx->waterfall_pan != pan) {
        //
        // Frequency and/or PAN value changed: possibly shift waterfall
        //
        int rotfreq = (int)((double)(rx->waterfall_frequency - vfofreq) / hz_per_pixel); // shift due to freq. change
        int rotpan  = rx->waterfall_pan - pan;                                        // shift due to pan   change
        int rotate_pixels = rotfreq + rotpan;

        if (rotate_pixels >= my_width || rotate_pixels <= -my_width) {
          //
          // If horizontal shift is too large, re-init waterfall
          //
          memset(pixels, 0, my_width * my_heigt * 3);
          rx->waterfall_frequency = vfofreq;
          rx->waterfall_pan = pan;
        } else {
          //
          // If rotate_pixels != 0, shift waterfall horizontally and set "freq changed" flag
          // calculated which VFO/pan value combination the shifted waterfall corresponds to
          //
          //
          if (rotate_pixels < 0) {
            // shift left, and clear the right-most part
            memmove(pixels, &pixels[-rotate_pixels * 3], ((my_width * my_heigt) + rotate_pixels) * 3);

            for (i = 0; i < my_heigt; i++) {
              memset(&pixels[((i * my_width) + (width + rotate_pixels)) * 3], 0, -rotate_pixels * 3);
            }
          } else if (rotate_pixels > 0) {
            // shift right, and clear left-most part
            memmove(&pixels[rotate_pixels * 3], pixels, ((my_width * my_heigt) - rotate_pixels) * 3);

            for (i = 0; i < my_heigt; i++) {
              memset(&pixels[(i * my_width) * 3], 0, rotate_pixels * 3);
            }
          }

          if (rotfreq != 0) {
            freq_changed = 1;
            rx->waterfall_frequency -= lround(rotfreq * hz_per_pixel); // this is not necessarily vfofreq!
          }

          rx->waterfall_pan = pan;
        }
      }
    } else {
      //
      // waterfall frequency not (yet) set, sample rate changed, or zoom value changed:
      // (re-) init waterfall
      //
      // NOTE: this memset() call is a crash location when changing from one receiver to two receivers and so on.
      // not sure why. 'pixels' is not null when this happens, nor are the other variables.
      // :example: 0x00007ffbfdb19aac in msvcrt!memmove () from C:\WINDOWS\System32\msvcrt.dll
      if (pixels != NULL) {
        memset(pixels, 0, my_width * my_heigt * 3);
        rx->waterfall_frequency = vfofreq;
        rx->waterfall_pan = pan;
        rx->waterfall_zoom = zoom;
        rx->waterfall_sample_rate = rx->sample_rate;
      } else {
        t_print("%s File: %s, Line: %d 'pixels' is NULL'\n", __FUNCTION__, __FILE__, __LINE__);
      }
    }

    //
    // If we have just shifted the waterfall befause the VFO frequency has changed,
    // there are  still IQ samples in the input queue corresponding to the "old"
    // VFO frequency, and this produces artifacts both on the panadaper and on the
    // waterfall. However, for the panadapter these are overwritten in due course,
    // while artifacts "stay" on the waterfall. We therefore refrain from updating
    // the waterfall *now* and continue updating when the VFO frequency has
    // stabilized. This will not remove the artifacts in any case but is a big
    // improvement.
    //
    if (!freq_changed) {
      memmove(&pixels[rowstride], pixels, (height - 1)*rowstride);
      float soffset;
      float average;
      unsigned char *p;
      p = pixels;
      samples = rx->pixel_samples;
      float wf_low, wf_high, rangei;
      int id = rx->id;
      int b = vfo[id].band;
      const BAND *band = band_get_band(b);
      int calib = rx_gain_calibration - band->gain;
      //
      // soffset contains all corrections due to attenuation, preamps, etc.
      //
      soffset = (float)(calib + adc[rx->adc].attenuation - adc[rx->adc].gain);

      if (filter_board == ALEX && rx->adc == 0) {
        soffset += (float)(10 * rx->alex_attenuation - 20 * rx->preamp);
      }

      if (filter_board == CHARLY25 && rx->adc == 0) {
        soffset += (float)(12 * rx->alex_attenuation - 18 * rx->preamp - 18 * rx->dither);
      }

      average = 0.0F;

      for (i = 0; i < width; i++) {
        average += (samples[i + pan] + soffset);
      }

      if (rx->waterfall_automatic) {
        wf_low = average / (float)width;
        wf_high = wf_low + 50.0F;
      } else {
        wf_low  = (float) rx->waterfall_low;
        wf_high = (float) rx->waterfall_high;
      }

      rangei = 1.0F / (wf_high - wf_low);

      for (i = 0; i < width; i++) {
        float sample = samples[i + pan] + soffset;

        if (sample < wf_low) {
          *p++ = colorLowR;
          *p++ = colorLowG;
          *p++ = colorLowB;
        } else if (sample > wf_high) {
          *p++ = colorHighR;
          *p++ = colorHighG;
          *p++ = colorHighB;
        } else {
          float percent = (sample - wf_low) * rangei;

          if (percent < 0.222222f) {
            float local_percent = percent * 4.5f;
            *p++ = (int)((1.0f - local_percent) * colorLowR);
            *p++ = (int)((1.0f - local_percent) * colorLowG);
            *p++ = (int)(colorLowB + local_percent * (255 - colorLowB));
          } else if (percent < 0.333333f) {
            float local_percent = (percent - 0.222222f) * 9.0f;
            *p++ = 0;
            *p++ = (int)(local_percent * 255);
            *p++ = 255;
          } else if (percent < 0.444444f) {
            float local_percent = (percent - 0.333333) * 9.0f;
            *p++ = 0;
            *p++ = 255;
            *p++ = (int)((1.0f - local_percent) * 255);
          } else if (percent < 0.555555f) {
            float local_percent = (percent - 0.444444f) * 9.0f;
            *p++ = (int)(local_percent * 255);
            *p++ = 255;
            *p++ = 0;
          } else if (percent < 0.777777f) {
            float local_percent = (percent - 0.555555f) * 4.5f;
            *p++ = 255;
            *p++ = (int)((1.0f - local_percent) * 255);
            *p++ = 0;
          } else if (percent < 0.888888f) {
            float local_percent = (percent - 0.777777f) * 9.0f;
            *p++ = 255;
            *p++ = 0;
            *p++ = (int)(local_percent * 255);
          } else {
            float local_percent = (percent - 0.888888f) * 9.0f;
            *p++ = (int)((0.75f + 0.25f * (1.0f - local_percent)) * 255.0f);
            *p++ = (int)(local_percent * 255.0f * 0.5f);
            *p++ = 255;
          }
        }
      }
    }

    gtk_widget_queue_draw (rx->waterfall);
  }
}

void waterfall_init(RECEIVER *rx, int width, int height) {
  my_width = width;
  my_heigt = height;
  rx->pixbuf = NULL;
  rx->waterfall_frequency = 0;
  rx->waterfall_sample_rate = 0;
  rx->waterfall = gtk_drawing_area_new ();
  GtkGesture *drag;
  gtk_widget_set_size_request (rx->waterfall, width, height);

  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA(rx->waterfall), waterfall_draw_cb, rx, NULL);
  g_signal_connect_after (rx->waterfall, "resize", G_CALLBACK (waterfall_resize_event_cb), rx);

  drag = gtk_gesture_drag_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (drag), GDK_BUTTON_PRIMARY);
  gtk_widget_add_controller (rx->waterfall, GTK_EVENT_CONTROLLER (drag));
  // look into these.
   g_signal_connect(drag, "drag-begin", G_CALLBACK(rx_waterfall_drag_begin), rx);
   g_signal_connect(drag, "drag-update", G_CALLBACK(rx_waterfall_drag_update), rx);
   g_signal_connect(drag, "drag-end", G_CALLBACK(rx_waterfall_drag_end), rx);

  // scroll wheel
  GtkEventController *scrollcontroller = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
  g_signal_connect(scrollcontroller, "scroll", G_CALLBACK(rx_waterfall_scroll), rx);
  gtk_widget_add_controller(rx->waterfall, scrollcontroller);

}
