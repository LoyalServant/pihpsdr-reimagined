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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <semaphore.h>
#include <math.h>
#include <sys/time.h>
#include <sys/types.h>
#ifdef _WIN32
#include <WinSock2.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <termios.h>
#endif


#include <wdsp.h>

#include "appearance.h"
#include "adc.h"
#include "dac.h"
#include "audio.h"
#include "discovered.h"
#include "filter.h"
#include "main.h"
#include "mode.h"
#include "radio.h"
#include "receiver.h"
#include "transmitter.h"
#include "agc.h"
#include "band.h"
#include "channel.h"
#include "property.h"
#include "new_menu.h"
#include "new_protocol.h"
#include "old_protocol.h"
#include "store.h"
#ifdef SOAPYSDR
  #include "soapy_protocol.h"
#endif
#include "actions.h"
#include "gpio.h"
#include "vfo.h"
#include "vox.h"
#include "meter.h"
#include "rx_panadapter.h"
#include "tx_panadapter.h"
#include "waterfall.h"
#include "zoompan.h"
#include "sliders.h"
#include "toolbar.h"
#include "rigctl.h"
#include "ext.h"
#include "radio_menu.h"
#include "iambic.h"
#include "rigctl_menu.h"
#include "screen_menu.h"
#ifdef MIDI
  #include "midi.h"
  #include "alsa_midi.h"
  #include "midi_menu.h"
#endif
#ifdef CLIENT_SERVER
  #include "client_server.h"
#endif
#include "message.h"
#ifdef SATURN
  #include "saturnmain.h"
  #include "saturnserver.h"
#endif
#include "mystring.h"
#include "pihpsdr_win32.h"

#ifdef _WIN32
#else
#define min(x,y) (x<y?x:y)
#define max(x,y) (x<y?y:x)
#endif

int MENU_HEIGHT = 30;        // always set to VFO_HEIGHT/2
int MENU_WIDTH = 85;         // nowhere changed
int VFO_HEIGHT = 60;         // taken from the current VFO bar layout
int VFO_WIDTH = 530;         // taken from the current VFO bar layout
int METER_HEIGHT = 60;       // always set to  VFO_HEIGHT
int METER_WIDTH = 220;       // nowhere changed
int ZOOMPAN_HEIGHT = 50;     // nowhere changed
int SLIDERS_HEIGHT = 100;    // nowhere changed
int TOOLBAR_HEIGHT = 30;     // nowhere changed

int rx_stack_horizontal = 0;

int controller = NO_CONTROLLER;

GtkWidget *fixed;
static GtkWidget *hide_b;
static GtkWidget *menu_b;
static GtkWidget *vfo_panel;
static GtkWidget *meter;
static GtkWidget *zoompan;
static GtkWidget *sliders;
static GtkWidget *toolbar;

// RX and TX calibration
long long frequency_calibration = 0LL;

int sat_mode;

int region = REGION_OTHER;

int radio_sample_rate;   // alias for radio->info.soapy.sample_rate
gboolean iqswap;

DISCOVERED *radio = NULL;
gboolean radio_is_remote = FALSE;     // only used with CLIENT_SERVER

char property_path[128];
GMutex property_mutex;

RECEIVER *receiver[8];
RECEIVER *active_receiver;
TRANSMITTER *transmitter;

int RECEIVERS;
int PS_TX_FEEDBACK;
int PS_RX_FEEDBACK;

int atlas_penelope = 0; // 0: no TX, 1: Penelope TX, 2: PennyLane TX
int atlas_clock_source_10mhz = 0;
int atlas_clock_source_128mhz = 0;
int atlas_config = 0;
int atlas_mic_source = 0;
int atlas_janus = 0;

//
// if hl2_audio_codec is set,  audio data is included in the HPSDR
// data stream and the "dither" bit is set. This is used by a
// "compagnion board" and  a variant of the HL2 firmware
// This bit can be set in the "RADIO" menu.
//
int hl2_audio_codec = 0;

//
// if anan10E is set, we have a limited-capacity HERMES board
// with 2 RX channels max, and the PureSignal TX DAC feedback
// is hard-coded to RX2, while for the PureSignal RX feedback
// one must use RX1. This is the case for Anan-10E and Anan-100B
// radios.
//
int anan10E = 0;

int adc0_filter_bypass = 0; // Bypass ADC0 filters on receive
int adc1_filter_bypass = 0; // Bypass ADC1 filters on receiver  (ANAN-7000/8000/G2)
int mute_spkr_amp = 0;      // Mute audio amplifier in radio    (ANAN-7000, G2)

int classE = 0;

int tx_out_of_band_allowed = 0;

int alc = TXA_ALC_PK;

int filter_board = ALEX;
int pa_enabled = 1;
int pa_power = PA_1W;
const int pa_power_list[] = {1, 5, 10, 30, 50, 100, 200, 500, 1000};
double pa_trim[11];

int display_zoompan = 0;
int display_sliders = 0;
int display_toolbar = 0;

double mic_gain = 0.0;

int mic_linein = 0;        // Use microphone rather than linein in radio's audio codec
double linein_gain = 0.0;  // -34.0 ... +12.5 in steps of 1.5 dB
int mic_boost = 0;
int mic_bias_enabled = 0;
int mic_ptt_enabled = 0;
int mic_ptt_tip_bias_ring = 0;
int mic_input_xlr = 0;

int receivers;

ADC adc[2];
DAC dac[2];                            // only first entry used

int locked = 0;

int rit_increment = 10;

int cw_keys_reversed = 0;              // 0=disabled 1=enabled
int cw_keyer_speed = 16;               // 1-60 WPM
int cw_keyer_mode = KEYER_MODE_A;      // Modes A/B and STRAIGHT
int cw_keyer_weight = 50;              // 0-100
int cw_keyer_spacing = 0;              // 0=on 1=off
int cw_keyer_internal = 1;             // 0=external 1=internal
int cw_keyer_sidetone_volume = 50;     // 0-127
int cw_keyer_ptt_delay = 30;           // 0-255ms
int cw_keyer_hang_time = 500;          // ms
int cw_keyer_sidetone_frequency = 800; // Hz
int cw_breakin = 1;                    // 0=disabled 1=enabled
int cw_ramp_width = 8;                 // default value (in ms)

int enable_auto_tune = 0;
int auto_tune_flag = 0;
int auto_tune_end = 0;

int enable_tx_inhibit = 0;
int TxInhibit = 0;

int vfo_encoder_divisor = 15;

int protocol;
int device;
int new_pa_board = 0; // Indicates Rev.24 PA board for HERMES/ANGELIA/ORION
int ozy_software_version;
int mercury_software_version[2] = {0, 0};
int penelope_software_version;

int adc0_overload = 0;
int adc1_overload = 0;
int tx_fifo_underrun = 0;
int tx_fifo_overrun = 0;
int sequence_errors = 0;
int high_swr_seen = 0;

unsigned int exciter_power = 0;
unsigned int alex_forward_power = 0;
unsigned int alex_reverse_power = 0;
unsigned int ADC1 = 0;
unsigned int ADC0 = 0;

//
// At the moment we have "late mox update", this means:
// in a RX/TX or TX/RX transition, mox is updated after
// rxtx has completed (which may take a while during
// down- and up-slew).
// Sometimes one wants to know before, that a RX/TX
// change is being initiated. So rxtx() sets the pre_mox
// variable immediatedly after it has been called to the new
// state.
// This variable is used to suppress audio samples being
// sent to the radio while shutting down the receivers,
// so it shall not be used in DUPLEX mode.
//
int pre_mox = 0;

int ptt = 0;
int mox = 0;
int tune = 0;
int memory_tune = 0;
int full_tune = 0;
int have_rx_gain = 0;
int have_rx_att = 0;
int have_alex_att = 0;
int have_preamp = 0;
int have_saturn_xdma = 0;
int rx_gain_calibration = 0;

int split = 0;

unsigned char OCtune = 0;
int OCfull_tune_time = 2800; // ms
int OCmemory_tune_time = 550; // ms
long long tune_timeout;

int analog_meter = 0;
int smeter = RXA_S_AV;

int eer_pwm_min = 100;
int eer_pwm_max = 800;

int tx_filter_low = 150;
int tx_filter_high = 2850;

static int pre_tune_mode;
static int pre_tune_cw_internal;

int pre_emphasize = 0;

int vox_enabled = 0;
double vox_threshold = 0.001;
double vox_hang = 250.0;
int vox = 0;
int CAT_cw_is_active = 0;
int MIDI_cw_is_active = 0;
int radio_ptt = 0;
int cw_key_hit = 0;
int n_adc = 1;

int diversity_enabled = 0;
double div_cos = 1.0;      // I factor for diversity
double div_sin = 1.0;      // Q factor for diversity
double div_gain = 0.0;     // gain for diversity (in dB)
double div_phase = 0.0;    // phase for diversity (in degrees, 0 ... 360)

//
// Audio capture and replay
// (Equalizers are switched off during capture and replay)
//
int capture_state = CAP_INIT;
int capture_record_pointer;
int capture_replay_pointer;
//double *capture_data = NULL;

int can_transmit = 0;
int optimize_for_touchscreen = 0;

gboolean duplex = FALSE;
gboolean mute_rx_while_transmitting = FALSE;

double drive_max = 100.0;
double drive_digi_max = 100.0; // maximum drive in DIGU/DIGL

gboolean display_warnings = TRUE;
gboolean display_pacurr = TRUE;

gint window_x_pos = 0;
gint window_y_pos = 0;

int rx_height;

void print_mainwindow_size();

void radio_stop() {
  if (can_transmit) {
    t_print("radio_stop: TX: stop display update\n");
    tx_set_displaying(transmitter, 0);
    t_print("radio_stop: TX: CloseChannel: %d\n", transmitter->id);
    CloseChannel(transmitter->id);
  }

  t_print("radio_stop: RX1: stop display update\n");
  set_displaying(receiver[0], 0);
  t_print("radio_stop: RX1: CloseChannel: %d\n", receiver[0]->id);
  CloseChannel(receiver[0]->id);

  if (RECEIVERS == 2) {
    t_print("radio_stop: RX2: stop display update\n");
    set_displaying(receiver[1], 0);
    t_print("radio_stop: RX2: CloseChannel: %d\n", receiver[1]->id);
    CloseChannel(receiver[1]->id);
  }
}

static void choose_vfo_layout() {
  //
  // a) secure that vfo_layout is a valid pointer
  // b) secure that the VFO layout width fits
  //
  int rc;
  const VFO_BAR_LAYOUT *vfl;
  rc = 1;
  vfl = vfo_layout_list;

  // make sure vfo_layout points to a valid entry in vfo_layout_list
  for (;;) {
    if (vfl->width < 0) { break; }

    if ((vfl - vfo_layout_list) == vfo_layout) { rc = 0; }

    vfl++;
  }

  if (rc) {
    vfo_layout = 0;
  }

  VFO_WIDTH = full_screen ? screen_width : app_width;
  VFO_WIDTH -= (MENU_WIDTH + METER_WIDTH);

  //
  // If chosen layout does not fit:
  // Choose the first largest layout that fits
  //
  if (vfo_layout_list[vfo_layout].width > VFO_WIDTH) {
    vfl = vfo_layout_list;

    for (;;) {
      if (vfl->width < 0) {
        vfl--;
        break;
      }

      if (vfl->width <= VFO_WIDTH) { break; }

      vfl++;
    }

    vfo_layout = vfl - vfo_layout_list;
    t_print("%s: vfo_layout changed (width=%d)\n", __FUNCTION__, vfl->width);
  }
}

// how do we find out what this is??????
int titlebarheight = 41;
// this is a crazy thing that the gnome window system seems to like
// it's like the title bar thing above. if we dont clip some pixels off, it won't fullscreen.
// it will sort of "half" full screen.....
// lower values will clip the toolbar buttons.
int fiddlyvalue = 5; 

void reconfigure_screen() {  
  //
  // Re-configure the piHPSDR screen after dimensions have changed
  // Start with removing the toolbar, the slider area and the zoom/pan area
  // (these will be re-constructed in due course)
  //
  int my_width  = full_screen ? screen_width  : app_width;
  int my_height = full_screen ? screen_height : app_height;

  if (full_screen) {
    gtk_window_fullscreen(GTK_WINDOW(main_window));  
    my_height = my_height - fiddlyvalue;  
  }
  else {
    gtk_window_unfullscreen(GTK_WINDOW(main_window));
    my_height = my_height - titlebarheight;
  }  

  if (toolbar) {
    gtk_widget_unparent(toolbar);

    toolbar = NULL;
  }

  if (sliders) {
    gtk_widget_unparent(sliders);
    sliders = NULL;
  }

  if (zoompan) {
    gtk_widget_unparent(zoompan);
    zoompan = NULL;
  }

  choose_vfo_layout();
  VFO_HEIGHT = vfo_layout_list[vfo_layout].height;
  MENU_HEIGHT = VFO_HEIGHT / 2;
  METER_HEIGHT = VFO_HEIGHT;


  gtk_widget_set_size_request(GTK_WIDGET(main_window), my_width, my_height);
  gtk_widget_set_size_request(hide_b, MENU_WIDTH, MENU_HEIGHT);
  gtk_widget_set_size_request(menu_b, MENU_WIDTH, MENU_HEIGHT);
  gtk_widget_set_size_request(meter,  METER_WIDTH, METER_HEIGHT);
  gtk_widget_set_size_request(vfo_panel, VFO_WIDTH, VFO_HEIGHT);
  //
  // Move Hide and Menu buttons, meter to new position
  //
  gtk_fixed_move(GTK_FIXED(fixed), hide_b, VFO_WIDTH + METER_WIDTH, 0);
  gtk_fixed_move(GTK_FIXED(fixed), menu_b, VFO_WIDTH + METER_WIDTH, MENU_HEIGHT);
  gtk_fixed_move(GTK_FIXED(fixed), meter, VFO_WIDTH, 0);

  //
  // Adjust position of the TX panel.
  // This must even be done in duplex mode, if we switch back
  // to non-duplex in the future.
  //
  if (can_transmit) {
    transmitter->x = 0;
    transmitter->y = VFO_HEIGHT;
  }

  //
  // This re-creates all the panels and the Toolbar/Slider/Zoom area
  //
  reconfigure_radio();

  g_idle_add(ext_vfo_update, NULL);
}

void reconfigure_radio() {
  int i;
  int y = 0;
  t_print("%s: receivers=%d\n", __FUNCTION__, receivers);
  int my_height = full_screen ? screen_height : app_height;
  int my_width  = full_screen ? screen_width  : app_width;

  if (!full_screen) {
    my_height = my_height - titlebarheight; 
  }
  else {
    my_height = my_height - fiddlyvalue; 
  }

  rx_height = my_height - VFO_HEIGHT;
  t_print("%s: -------------------------------------------------------------------------------------------\n", __FUNCTION__);
  t_print("%s: starting Y=%d height=%d\n", __FUNCTION__, y, my_height);
  print_mainwindow_size();
  t_print("%s rx height %d\n", __FUNCTION__, rx_height);
  
  if (display_zoompan) {
    rx_height -= ZOOMPAN_HEIGHT;
  }
  t_print("%s rx height %d\n", __FUNCTION__, rx_height);

  if (display_sliders) {
    rx_height -= SLIDERS_HEIGHT;
  }
  t_print("%s rx height %d\n", __FUNCTION__, rx_height);

  if (display_toolbar) {
    rx_height -= TOOLBAR_HEIGHT;
  }
  t_print("%s rx height %d\n", __FUNCTION__, rx_height);

  y = VFO_HEIGHT;
  t_print("%s y %d\n", __FUNCTION__, y);


  // if there is only one receiver, both cases here do the same.
  if (rx_stack_horizontal) {
    t_print("%s rx stacking horizontal height %d\n", __FUNCTION__, rx_height);
    int x = 0;

    for (i = 0; i < receivers; i++) {
      RECEIVER *rx = receiver[i];
      rx->width = my_width / receivers;
      receiver_update_zoom(rx);
      reconfigure_receiver(rx, rx_height);

      if (!isTransmitting() || duplex) {
        gtk_fixed_move(GTK_FIXED(fixed), rx->panel, x, y);
      }

      rx->x = x;
      rx->y = y;
      x = x + my_width / receivers;
    }

    y += rx_height;
  } else {
    t_print("%s rx stacking vertical height %d\n", __FUNCTION__, rx_height);
    for (i = 0; i < receivers; i++) {
      RECEIVER *rx = receiver[i];
      rx->width = my_width;
      receiver_update_zoom(rx);
      reconfigure_receiver(rx, rx_height / receivers);

      if (!isTransmitting() || duplex) {
        gtk_fixed_move(GTK_FIXED(fixed), rx->panel, 0, y);
      }

      rx->x = 0;
      rx->y = y;
      y += rx_height / receivers;
    }
  }

  t_print("%s sum of Y heights before toolbars/panels %d\n", __FUNCTION__, y);

  // FIXME
  //return;

  if (display_zoompan) {
    if (zoompan == NULL) {
      zoompan = zoompan_init(my_width, ZOOMPAN_HEIGHT);
      gtk_fixed_put(GTK_FIXED(fixed), zoompan, 0, y);
    } else {
      gtk_fixed_move(GTK_FIXED(fixed), zoompan, 0, y);
    }

    gtk_widget_show(zoompan);
    y += ZOOMPAN_HEIGHT;
  } else {
    if (zoompan != NULL) {
      gtk_widget_unparent(zoompan);
      zoompan = NULL;
    }
  }

  if (display_sliders) {
    if (sliders == NULL) {
      sliders = sliders_init(my_width, SLIDERS_HEIGHT);
      gtk_fixed_put(GTK_FIXED(fixed), sliders, 0, y);
    } else {
      gtk_fixed_move(GTK_FIXED(fixed), sliders, 0, y);
    }

    gtk_widget_show(sliders);  // ... this shows both C25 and Alex ATT/Preamp, and both Mic/Linein sliders
    sliders_update();
    att_type_changed();            // ... and this hides the „wrong“ ones.
    y += SLIDERS_HEIGHT;
  } else {
    if (sliders != NULL) {
      gtk_widget_unparent(sliders);
      sliders = NULL;
    }
  }

  if (display_toolbar) {
    if (toolbar == NULL) {
      toolbar = toolbar_init(my_width, TOOLBAR_HEIGHT);
      gtk_fixed_put(GTK_FIXED(fixed), toolbar, 0, y);
    } else {
      gtk_fixed_move(GTK_FIXED(fixed), toolbar, 0, y);
    }

    gtk_widget_show(toolbar);
  } else {
    if (toolbar != NULL) {
      gtk_widget_unparent(toolbar);
      toolbar = NULL;
    }
  }

  if (can_transmit && !duplex) {
    reconfigure_transmitter(transmitter, my_width, rx_height);
  }

  t_print("%s sum of Y heights AFTER toolbars/panels %d\n", __FUNCTION__, y);
  t_print("%s sum of Y heights PLUS TOOLBAR_HEIGHT %d\n", __FUNCTION__, y+TOOLBAR_HEIGHT);

  t_print("%s: ending Y=%d height=%d\n", __FUNCTION__, y, my_height);
  print_mainwindow_size();
  t_print("%s: -------------------------------------------------------------------------------------------\n", __FUNCTION__);  

  // try to force it back to the correct height as a test
  //gtk_window_set_default_size(GTK_WINDOW(main_window), 1920, y+TOOLBAR_HEIGHT);

}


//
// These variables are set in hideall_cb and read
// in radioSaveState.
// If the props file is written while "Hide"-ing,
// these values are written instead of the current
// hide/show status of the Zoom/Sliders/Toolbar area.
//
static int hide_status = 0;
static int old_zoom = 0;
static int old_tool = 0;
static int old_slid = 0;

static gboolean hideall_cb  (GtkWidget *widget, GdkEvent *event, gpointer data) {
  //
  // reconfigure_radio must not be called during TX
  //
  if (isTransmitting()) {
    if (!duplex) { return TRUE; }
  }

  if (hide_status == 0) {
    //
    // Hide everything but store old status
    //
    hide_status = 1;
    gtk_button_set_label(GTK_BUTTON(hide_b), "Show");
    old_zoom = display_zoompan;
    old_slid = display_sliders;
    old_tool = display_toolbar;
    display_toolbar = display_sliders = display_zoompan = 0;
    reconfigure_radio();
  } else {
    //
    // Re-display everything
    //
    hide_status = 0;
    gtk_button_set_label(GTK_BUTTON(hide_b), "Hide");
    display_zoompan = old_zoom;
    display_sliders = old_slid;
    display_toolbar = old_tool;
    reconfigure_radio();
  }

  return TRUE;
}

// cppcheck-suppress constParameterCallback
static gboolean menu_cb (GtkWidget *widget, GdkEvent *event, gpointer data) {
  new_menu();
  return TRUE;
}

static void on_size_changed(GObject *object, GParamSpec *pspec, gpointer data) {
    int width, height;
    gtk_window_get_default_size(GTK_WINDOW(object), &width, &height);
    t_print("Window resized to: width=%d, height=%d\n", width, height);
    //screen_width = width;
    //screen_height = height;
    //reconfigure_screen(); // eventually we will get here and make this all resizable.
}

void print_mainwindow_size()
{
  int width, height;
    gtk_window_get_default_size(GTK_WINDOW(main_window), &width, &height);
    t_print("Window size width=%d, height=%d\n", width, height);
}


static void create_visual() {
  int y = 0;
  fixed = gtk_fixed_new();
  gtk_window_set_child(GTK_WINDOW(main_window), fixed);
  t_print("radio: vfo_init\n");

  g_signal_connect(main_window, "notify::default-width", G_CALLBACK(on_size_changed), NULL);
  g_signal_connect(main_window, "notify::default-height", G_CALLBACK(on_size_changed), NULL);


  int my_width = app_width;
  int my_height = app_height;

  VFO_WIDTH = my_width - MENU_WIDTH - METER_WIDTH;
  vfo_panel = vfo_init(VFO_WIDTH, VFO_HEIGHT);
  gtk_fixed_put(GTK_FIXED(fixed), vfo_panel, 0, y);

  //t_print("radio: meter_init\n");
  meter = meter_init(METER_WIDTH, METER_HEIGHT);
  gtk_fixed_put(GTK_FIXED(fixed), meter, VFO_WIDTH, y);

  hide_b = gtk_button_new_with_label("Hide");
  gtk_widget_set_size_request (hide_b, MENU_WIDTH, MENU_HEIGHT);
  g_signal_connect(hide_b, "clicked", G_CALLBACK(hideall_cb), NULL);
  gtk_fixed_put(GTK_FIXED(fixed), hide_b, VFO_WIDTH + METER_WIDTH, y);  


  y += MENU_HEIGHT;
  menu_b = gtk_button_new_with_label("Menu");
  gtk_widget_set_size_request (menu_b, MENU_WIDTH, MENU_HEIGHT);
  g_signal_connect (menu_b, "clicked", G_CALLBACK(menu_cb), NULL) ;
  gtk_fixed_put(GTK_FIXED(fixed), menu_b, VFO_WIDTH + METER_WIDTH, y);
  
  y += MENU_HEIGHT;
  rx_height = my_height - VFO_HEIGHT;

  if (display_zoompan) {
    rx_height -= ZOOMPAN_HEIGHT;
  }

  if (display_sliders) {
    rx_height -= SLIDERS_HEIGHT;
  }

  if (display_toolbar) {
    rx_height -= TOOLBAR_HEIGHT;
  }

  //
  // To be on the safe side, we create ALL receiver panels here
  // If upon startup, we only should display one panel, we do the switch below
  //
  for (int i = 0; i < RECEIVERS; i++) {
#ifdef CLIENT_SERVER

    if (radio_is_remote) {
      receiver_create_remote(receiver[i]);
    } else {
#endif
      receiver[i] = create_receiver(CHANNEL_RX0 + i, my_width, my_width, rx_height / RECEIVERS);
      setSquelch(receiver[i]);
#ifdef CLIENT_SERVER
    }

#endif
    receiver[i]->x = 0;
    receiver[i]->y = y;
    // Upon startup, if RIT or CTUN is active, tell WDSP.
#ifdef CLIENT_SERVER

    if (!radio_is_remote) {
#endif
      set_displaying(receiver[i], 1);
      set_offset(receiver[i], vfo[i].offset);
#ifdef CLIENT_SERVER
    }

#endif
    gtk_fixed_put(GTK_FIXED(fixed), receiver[i]->panel, 0, y);
    g_object_ref((gpointer)receiver[i]->panel);
    y += rx_height / RECEIVERS;
  }

  active_receiver = receiver[0];
  //
  // This is to detect illegal accesses to the PS receivers
  //
  receiver[PS_RX_FEEDBACK] = NULL;
  receiver[PS_TX_FEEDBACK] = NULL;
#ifdef CLIENT_SERVER

  if (!radio_is_remote) {
#endif
    //t_print("Create transmitter\n");
    transmitter = NULL;
    can_transmit = 0;
    //
    //  do not set can_transmit before transmitter exists, because we assume
    //  if (can_transmit) is equivalent to if (transmitter)
    //
    int radio_has_transmitter = 0;

    switch (protocol) {
    case ORIGINAL_PROTOCOL:
    case NEW_PROTOCOL:
      radio_has_transmitter = 1;
      break;
#ifdef SOAPYSDR

    case SOAPYSDR_PROTOCOL:
      radio_has_transmitter = (radio->info.soapy.tx_channels != 0);
      break;
#endif
    }

    if (radio_has_transmitter) {
      if (duplex) {
        transmitter = create_transmitter(CHANNEL_TX, my_width / 4, my_height / 2);
      } else {
        transmitter = create_transmitter(CHANNEL_TX, my_width, rx_height);
      }

      can_transmit = 1;
      transmitter->x = 0;
      transmitter->y = VFO_HEIGHT;
      calcDriveLevel();

      if (protocol == NEW_PROTOCOL || protocol == ORIGINAL_PROTOCOL) {
        double pk;
        tx_set_ps_sample_rate(transmitter, protocol == NEW_PROTOCOL ? 192000 : active_receiver->sample_rate);
        receiver[PS_TX_FEEDBACK] = create_pure_signal_receiver(PS_TX_FEEDBACK,
                                   protocol == ORIGINAL_PROTOCOL ? active_receiver->sample_rate : 192000, my_width);
        receiver[PS_RX_FEEDBACK] = create_pure_signal_receiver(PS_RX_FEEDBACK,
                                   protocol == ORIGINAL_PROTOCOL ? active_receiver->sample_rate : 192000, my_width);

        //
        // If the pk value is slightly too large, this does no harm, but
        // if it is slightly too small, very strange things can happen.
        // Therefore it is good to "measure" this value and then slightly
        // increase it.
        //
        switch (protocol) {
        case NEW_PROTOCOL:
          switch (device) {
          case NEW_DEVICE_SATURN:
            pk = 0.6121;
            break;

          default:
            // recommended "new protocol value"
            pk = 0.2899;
            break;
          }

          break;

        case ORIGINAL_PROTOCOL:
          switch (device) {
          case DEVICE_HERMES_LITE2:
            // measured value: 0.2386
            pk = 0.2400;
            break;

          case DEVICE_STEMLAB:
            // measured value: 0.4155
            pk = 0.4160;
            break;

          default:
            // recommended "old protocol" value
            pk = 0.4067;
            break;
          }

          break;

        default:
          // NOTREACHED
          pk = 1.000;
          break;
        }

        SetPSHWPeak(transmitter->id, pk);
      }
    }

#ifdef CLIENT_SERVER
  }

#endif

  // init local keyer if enabled
  if (cw_keyer_internal == 0) {
    t_print("Initialize keyer.....\n");
    keyer_update();
  }

#ifdef CLIENT_SERVER

  if (!radio_is_remote) {
#endif

    switch (protocol) {
    case ORIGINAL_PROTOCOL:
      old_protocol_init(receiver[0]->sample_rate);
      break;

    case NEW_PROTOCOL:
      new_protocol_init();
      break;
#ifdef SOAPYSDR

    case SOAPYSDR_PROTOCOL:
      soapy_protocol_init(FALSE);
      break;
#endif
    }

#ifdef CLIENT_SERVER
  }

#endif
// mwr testing
  //y = y - 100;

  if (display_zoompan) {
    zoompan = zoompan_init(my_width, ZOOMPAN_HEIGHT);
    gtk_fixed_put(GTK_FIXED(fixed), zoompan, 0, y);
    y += ZOOMPAN_HEIGHT;
  }

  if (display_sliders) {
    t_print("create sliders\n");
    sliders = sliders_init(my_width, SLIDERS_HEIGHT);
    if (sliders) {
      gtk_fixed_put(GTK_FIXED(fixed), sliders, 0, y);
    }
    y += SLIDERS_HEIGHT;
  }  

  if (display_toolbar) {
    toolbar = toolbar_init(my_width, TOOLBAR_HEIGHT);
    gtk_fixed_put(GTK_FIXED(fixed), toolbar, 0, y);
  }



  //
  // Now, if there should only one receiver be displayed
  // at startup, do the change. We must momentarily fake
  // the number of receivers otherwise radio_change_receivers
  // will do nothing.
  //
  t_print("create_visual: receivers=%d RECEIVERS=%d\n", receivers, RECEIVERS);

  if (receivers != RECEIVERS) {
    int r = receivers;
    receivers = RECEIVERS;
    t_print("create_visual: calling radio_change_receivers: receivers=%d r=%d\n", receivers, r);
    radio_change_receivers(r);
  }

  // FIXME: it should already be shown?
  gtk_widget_show(main_window);  // ... this shows both the HPSDR and C25 preamp/att sliders
  att_type_changed();                // ... and this hides the „wrong“ ones.
}

void start_radio() {
  int i;

  //
  // Debug code. Placed here at the start of the program. piHPSDR  implicitly assumes
  //             that the entires in the action table (actions.c) are sorted by their
  //             action enum values (actions.h).
  //             This will produce no output if the ActionTable is sorted correctly.
  //             If the warning appears, correct the order of actions in actions.h
  //             and re-compile.
  //
  for (i = 0; i < ACTIONS; i++) {
    if (i != (int)ActionTable[i].action) {
      t_print("WARNING: action table messed up\n");
      t_print("WARNING: Position %d Action=%d str=%s\n", i, ActionTable[i].action, ActionTable[i].button_str);
    }
  }

#ifdef GPIO

  if (gpio_init() < 0) {
    t_print("GPIO failed to initialize\n");
  }

#endif
  //t_print("start_radio: selected radio=%p device=%d\n",radio,radio->device);
  //gdk_window_set_cursor(gtk_widget_get_window(top_window), gdk_cursor_new(GDK_WATCH));
  //
  // The behaviour of pop-up menus (Combo-Boxes) can be set to
  // "mouse friendly" (standard case) and "touchscreen friendly"
  // menu pops up upon press, and stays upon release, and the selection can
  // be made with a second press).
  //
  // Here we set it to "touch-screen friendly" by default, since it does
  // not harm MUCH if it set to touch-screen for a mouse, but it can be
  // it VERY DIFFICULT if "mouse friendly" settings are encountered with
  // a touch screen.
  //
  // The setting can be changed in the RADIO menu and is stored in the
  // props file, so will be restored therefrom as well.
  //
  optimize_for_touchscreen = 0;

  for (int id = 0; id < MAX_SERIAL; id++) {
    //
    // Apply some default values
    //
    SerialPorts[id].enable = 0;
    SerialPorts[id].andromeda = 0;
    SerialPorts[id].baud = 0;
    snprintf(SerialPorts[id].port, sizeof(SerialPorts[id].port), "/dev/ttyACM%d", id);
  }

  protocol = radio->protocol;
  device = radio->device;

  if (device == NEW_DEVICE_SATURN && (strcmp(radio->info.network.interface_name, "XDMA") == 0)) {
    have_saturn_xdma = 1;
  }

  if (device == DEVICE_METIS || device == DEVICE_OZY || device == NEW_DEVICE_ATLAS) {
    //
    // by default, assume there is a penelope board (no PennyLane)
    // when using an ATLAS bus system, to avoid TX overdrive due to
    // missing IQ scaling. Furthermore, piHPSDR assumes the presence
    // of a Mercury board, so use that as the default clock source
    // (until changed in the RADIO menu)
    //
    atlas_penelope = 1;                 // TX present, do IQ scaling
    atlas_clock_source_10mhz = 2;       // default: Mercury
    atlas_clock_source_128mhz = 1;      // default: Mercury
    atlas_mic_source = 1;               // default: Mic source = Penelope
  }

  // set the default power output and max drive value
  drive_max = 100.0;

  switch (device) {
  case DEVICE_METIS:
  case DEVICE_OZY:
  case NEW_DEVICE_ATLAS:
    pa_power = PA_1W;
    break;

  case DEVICE_HERMES_LITE2:
  case NEW_DEVICE_HERMES_LITE2:
    pa_power = PA_5W;
    break;

  case DEVICE_STEMLAB:
    pa_power = PA_10W;
    break;

  case DEVICE_HERMES:
  case DEVICE_GRIFFIN:
  case DEVICE_ANGELIA:
  case DEVICE_ORION:
  case DEVICE_STEMLAB_Z20:
  case NEW_DEVICE_HERMES:
  case NEW_DEVICE_HERMES2:
  case NEW_DEVICE_ANGELIA:
  case NEW_DEVICE_ORION:
  case NEW_DEVICE_SATURN:  // make 100W the default for G2
    pa_power = PA_100W;
    break;

  case DEVICE_ORION2:
  case NEW_DEVICE_ORION2:
    pa_power = PA_200W; // So ANAN-8000 is the default, not ANAN-7000
    break;

  case SOAPYSDR_USB_DEVICE:
    if (strcmp(radio->name, "lime") == 0) {
      drive_max = 64.0;
    } else if (strcmp(radio->name, "plutosdr") == 0) {
      drive_max = 89.0;
    }

    pa_power = PA_1W;
    break;

  default:
    pa_power = PA_1W;
    break;
  }

  drive_digi_max = drive_max; // To be updated when reading props file

  for (i = 0; i < 11; i++) {
    pa_trim[i] = i * pa_power_list[pa_power] * 0.1;
  }

  //
  // Set various capabilities, depending in the radio model
  //
  switch (device) {
  case DEVICE_METIS:
  case DEVICE_OZY:
  case NEW_DEVICE_ATLAS:
    have_rx_att = 1; // Sure?
    have_alex_att = 1;
    have_preamp = 1;
    break;

  case DEVICE_HERMES:
  case DEVICE_GRIFFIN:
  case DEVICE_ANGELIA:
  case DEVICE_ORION:
  case NEW_DEVICE_HERMES:
  case NEW_DEVICE_ANGELIA:
  case NEW_DEVICE_ORION:
    have_rx_att = 1;
    have_alex_att = 1;
    break;

  case DEVICE_ORION2:
  case NEW_DEVICE_ORION2:
  case NEW_DEVICE_SATURN:
    // ANAN7000/8000/G2 boards have no ALEX attenuator
    have_rx_att = 1;
    break;

  case DEVICE_HERMES_LITE:
  case DEVICE_HERMES_LITE2:
  case NEW_DEVICE_HERMES_LITE:
  case NEW_DEVICE_HERMES_LITE2:
    have_rx_gain = 1;
    rx_gain_calibration = 14;
    break;

  case SOAPYSDR_USB_DEVICE:
    have_rx_gain = 1;
    rx_gain_calibration = 10;
    break;

  default:
    //
    // DEFAULT: we have a step attenuator nothing else
    //
    have_rx_att = 1;
    break;
  }

  //
  // The GUI expects that we either have a gain or an attenuation slider,
  // but not both.
  //

  if (have_rx_gain) {
    have_rx_att = 0;
  }

  //
  // A semaphore for safely writing to the props file
  //
  g_mutex_init(&property_mutex);
  char p[32];
  char version[32];
  char mac[32];
  char ip[32];
  char iface[64];
  char text[1024];

  switch (protocol) {
  case ORIGINAL_PROTOCOL:
    STRLCPY(p, "Protocol 1", 32);
    snprintf(version, 32, "v%d.%d)",
             radio->software_version / 10,
             radio->software_version % 10);
    snprintf(mac, 32, "%02X:%02X:%02X:%02X:%02X:%02X",
             radio->info.network.mac_address[0],
             radio->info.network.mac_address[1],
             radio->info.network.mac_address[2],
             radio->info.network.mac_address[3],
             radio->info.network.mac_address[4],
             radio->info.network.mac_address[5]);
    snprintf(ip, 32, "%s", inet_ntoa(radio->info.network.address.sin_addr));
    snprintf(iface, 64, "%s", radio->info.network.interface_name);
    break;

  case NEW_PROTOCOL:
    STRLCPY(p, "Protocol 2", 32);
    snprintf(version, 32, "v%d.%d)",
             radio->software_version / 10,
             radio->software_version % 10);
    snprintf(mac, 32, "%02X:%02X:%02X:%02X:%02X:%02X",
             radio->info.network.mac_address[0],
             radio->info.network.mac_address[1],
             radio->info.network.mac_address[2],
             radio->info.network.mac_address[3],
             radio->info.network.mac_address[4],
             radio->info.network.mac_address[5]);
    snprintf(ip, 32, "%s", inet_ntoa(radio->info.network.address.sin_addr));
    snprintf(iface, 64, "%s", radio->info.network.interface_name);
    break;
#ifdef SOAPYSDR

  case SOAPYSDR_PROTOCOL:
    STRLCPY(p, "SoapySDR", 32);
    snprintf(version, 32, "%4.20s v%d.%d.%d",
             radio->info.soapy.driver_key,
             (radio->software_version % 10000) / 100,
             (radio->software_version % 100) / 10,
             radio->software_version % 10);
    break;
#endif
  }

  //
  // text for top bar of piHPSDR Window
  //
  switch (protocol) {
  case ORIGINAL_PROTOCOL:
  case NEW_PROTOCOL:
    if (have_saturn_xdma) {
      // radio has no ip and MAC
      snprintf(text, 1024, "piHPSDR reimagined: %s (%s v%d) on %s",
               radio->name,
               p,
               radio->software_version,
               iface);
    } else if (device == DEVICE_OZY) {
      // radio has no ip, mac, and name is "Ozy USB"
      snprintf(text, 1024, "piHPSDR reimagined: %s (%s %s)",
               radio->name,
               p,
               version);
    } else {
      // radio MAC address removed from the top bar otherwise
      // it does not fit  in windows 640 pixels wide.
      // if needed, the MAC address of the radio can be
      // found in the ABOUT menu.
      snprintf(text, 1024, "piHPSDR reimagined: %s (%s %s) %s on %s",
               radio->name,
               p,
               version,
               ip,
               iface);
    }

    break;

  case SOAPYSDR_PROTOCOL:
    snprintf(text, 1024, "piHPSDR reimagined: %s (%s %s)",
             radio->name,
             p,
             version);
    break;
  }

  gtk_window_set_title (GTK_WINDOW (main_window), text);

  //
  // determine name of the props file
  //
  switch (device) {
  case DEVICE_OZY:
    snprintf(property_path, sizeof(property_path), "ozy.props");
    break;

  case SOAPYSDR_USB_DEVICE:
    snprintf(property_path, sizeof(property_path), "%s.props", radio->name);
    break;

  default:
    snprintf(property_path, sizeof(property_path), "%02X-%02X-%02X-%02X-%02X-%02X.props",
             radio->info.network.mac_address[0],
             radio->info.network.mac_address[1],
             radio->info.network.mac_address[2],
             radio->info.network.mac_address[3],
             radio->info.network.mac_address[4],
             radio->info.network.mac_address[5]);
    break;
  }

  //
  // Determine number of ADCs in the device
  //
  switch (device) {
  case DEVICE_METIS:
  case DEVICE_OZY:
  case DEVICE_HERMES:
  case DEVICE_HERMES_LITE:
  case DEVICE_HERMES_LITE2:
  case NEW_DEVICE_ATLAS:
  case NEW_DEVICE_HERMES:
  case NEW_DEVICE_HERMES2:
  case NEW_DEVICE_HERMES_LITE:
  case NEW_DEVICE_HERMES_LITE2:
    //
    // If there are two MERCURY cards on the ATLAS bus, this is detected
    // in old_protocol.c, But, n_adc can keep the value of 1 since the
    // ADC assignment is fixed in that case (RX1: first mercury card,
    // RX2: second mercury card).
    //
    n_adc = 1;
    break;

  case SOAPYSDR_USB_DEVICE:
    if (strcmp(radio->name, "lime") == 0) {
      n_adc = 2;
    } else {
      n_adc = 1;
    }

    break;

  default:
    n_adc = 2;
    break;
  }

  iqswap = 0;
  //
  // In most cases, ALEX is the best default choice for the filter board.
  // here we set filter_board to a different default value for some
  // "special" hardware. The choice made here will possibly overwritten
  // with data from the props file.
  //

  if (device == SOAPYSDR_USB_DEVICE) {
    iqswap = 1;
    receivers = 1;
    filter_board = NO_FILTER_BOARD;
  }

  if (device == DEVICE_HERMES_LITE2 || device == NEW_DEVICE_HERMES_LITE2)  {
    filter_board = N2ADR;
    n2adr_oc_settings(); // Apply default OC settings for N2ADR board
  }

  if (device == DEVICE_STEMLAB || device == DEVICE_STEMLAB_Z20) {
    filter_board = CHARLY25;
  }

  /* Set defaults */
  adc[0].antenna = ANTENNA_1;
  adc[0].filters = AUTOMATIC;
  adc[0].hpf = HPF_13;
  adc[0].lpf = LPF_30_20;
  adc[0].dither = FALSE;
  adc[0].random = FALSE;
  adc[0].preamp = FALSE;
  adc[0].attenuation = 0;
  adc[0].enable_step_attenuation = 0;
  adc[0].gain = rx_gain_calibration;
  adc[0].min_gain = 0.0;
  adc[0].max_gain = 100.0;
  dac[0].antenna = 1;
  dac[0].gain = 0;

  if (have_rx_gain && (protocol == ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL)) {
    //
    // The "magic values" here are for the AD98656 chip that is used in radios
    // such as the HermesLite and the RadioBerry. This is a best estimate and
    // will be overwritten with data from the props file.
    //
    adc[0].min_gain = -12.0;
    adc[0].max_gain = +48.0;
  }

  adc[0].agc = FALSE;
#ifdef SOAPYSDR

  if (device == SOAPYSDR_USB_DEVICE) {
    if (radio->info.soapy.rx_gains > 0) {
      adc[0].min_gain = radio->info.soapy.rx_range[0].minimum;
      adc[0].max_gain = radio->info.soapy.rx_range[0].maximum;;
      adc[0].gain = adc[0].min_gain;
    }
  }

#endif
  adc[1].antenna = ANTENNA_1;
  adc[1].filters = AUTOMATIC;
  adc[1].hpf = HPF_9_5;
  adc[1].lpf = LPF_60_40;
  adc[1].dither = FALSE;
  adc[1].random = FALSE;
  adc[1].preamp = FALSE;
  adc[1].attenuation = 0;
  adc[1].enable_step_attenuation = 0;
  adc[1].gain = rx_gain_calibration;
  adc[1].min_gain = 0.0;
  adc[1].max_gain = 100.0;
  dac[1].antenna = 1;
  dac[1].gain = 0;

  if (have_rx_gain && (protocol == ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL)) {
    adc[1].min_gain = -12.0;
    adc[1].max_gain = +48.0;
  }

  adc[1].agc = FALSE;
#ifdef SOAPYSDR

  if (device == SOAPYSDR_USB_DEVICE) {
    if (radio->info.soapy.rx_gains > 0) {
      adc[1].min_gain = radio->info.soapy.rx_range[0].minimum;
      adc[1].max_gain = radio->info.soapy.rx_range[0].maximum;;
      adc[1].gain = adc[1].min_gain;
    }

    radio_sample_rate = radio->info.soapy.sample_rate;
  }

#endif
#ifdef GPIO

  switch (controller) {
  case NO_CONTROLLER:
    display_zoompan = 1;
    display_sliders = 1;
    display_toolbar = 1;
    break;

  case CONTROLLER2_V1:
  case CONTROLLER2_V2:
  case G2_FRONTPANEL:
    display_zoompan = 1;
    display_sliders = 0;
    display_toolbar = 0;
    break;
  }

#else
  display_zoompan = 1;
  display_sliders = 1;
  display_toolbar = 1;
#endif
  t_print("%s: setup RECEIVERS protocol=%d\n", __FUNCTION__, protocol);

  switch (protocol) {
  case SOAPYSDR_PROTOCOL:
    t_print("%s: setup RECEIVERS SOAPYSDR\n", __FUNCTION__);
    RECEIVERS = 1;
    PS_TX_FEEDBACK = 1;
    PS_RX_FEEDBACK = 2;
    break;

  default:
    t_print("%s: setup RECEIVERS default\n", __FUNCTION__);
    RECEIVERS = 2;
    PS_TX_FEEDBACK = (RECEIVERS);
    PS_RX_FEEDBACK = (RECEIVERS + 1);
    break;
  }

  receivers = RECEIVERS;
  radioRestoreState();
  radio_change_region(region);
  create_visual();
  reconfigure_screen();

  // save every 30 seconds
  // save_timer_id=gdk_threads_add_timeout(30000, save_cb, NULL);

  if (rigctl_enable) {
    launch_rigctl();
#ifdef _WIN32
#else
    for (int id = 0; id < MAX_SERIAL; id++) {
      if (SerialPorts[id].enable) {
        launch_serial(id);
      }
    }
  } else {
    // since we do not spawn the serial thread,
    // disable serial
    for (int id = 0; id < MAX_SERIAL; id++) {
      SerialPorts[id].enable = 0;
    }
#endif
  }


  if (can_transmit) {
    calcDriveLevel();

    if (transmitter->puresignal) {
      tx_set_ps(transmitter, transmitter->puresignal);
    }
  }

  schedule_high_priority();
#ifdef SOAPYSDR

  if (protocol == SOAPYSDR_PROTOCOL) {
    RECEIVER *rx = receiver[0];
    soapy_protocol_create_receiver(rx);

    if (can_transmit) {
      soapy_protocol_create_transmitter(transmitter);
      soapy_protocol_set_tx_antenna(transmitter, dac[0].antenna);
      soapy_protocol_set_tx_gain(transmitter, transmitter->drive);
      soapy_protocol_set_tx_frequency(transmitter);
      soapy_protocol_start_transmitter(transmitter);
    }

    soapy_protocol_set_rx_antenna(rx, adc[0].antenna);
    soapy_protocol_set_rx_frequency(rx, VFO_A);
    soapy_protocol_set_automatic_gain(rx, adc[0].agc);
    soapy_protocol_set_gain(rx);

    if (vfo[0].ctun) {
      receiver_set_frequency(rx, vfo[0].ctun_frequency);
    }

    soapy_protocol_start_receiver(rx);
    //t_print("radio: set rf_gain=%f\n",rx->rf_gain);
    soapy_protocol_set_gain(rx);
  }

#endif
  g_idle_add(ext_vfo_update, NULL);
  //gdk_window_set_cursor(gtk_widget_get_window(top_window), gdk_cursor_new(GDK_ARROW));
#ifdef MIDI

  for (i = 0; i < n_midi_devices; i++) {
    if (midi_devices[i].active) {
      //
      // Normally the "active" flags marks a MIDI device that is up and running.
      // It is hi-jacked by the props file to indicate the device should be
      // opened, so we set it to zero. Upon successfull opening of the MIDI device,
      // it will be set again.
      //
      midi_devices[i].active = 0;
      register_midi_device(i);
    }
  }

#endif
#ifdef SATURN

  if (have_saturn_xdma && saturn_server_en) {
    start_saturn_server();
  }

#endif
#ifdef CLIENT_SERVER

  if (hpsdr_server) {
    create_hpsdr_server();
  }

#endif
}

void radio_change_receivers(int r) {
  t_print("radio_change_receivers: from %d to %d\n", receivers, r);

  // The button in the radio menu will call this function even if the
  // number of receivers has not changed.
  if (receivers == r) { return; }  // This is always the case if RECEIVERS==1

  //
  // When changing the number of receivers, restart the
  // old protocol
  //
#ifdef CLIENT_SERVER

  if (!radio_is_remote) {
#endif

    if (protocol == ORIGINAL_PROTOCOL) {
      old_protocol_stop();
    }

#ifdef CLIENT_SERVER
  }

#endif

  switch (r) {
  case 1:
    set_displaying(receiver[1], 0);
    gtk_widget_unparent(receiver[1]->panel);
    receivers = 1;
    break;

  case 2:
    gtk_fixed_put(GTK_FIXED(fixed), receiver[1]->panel, 0, 0);
    set_displaying(receiver[1], 1);
    receivers = 2;

    //
    // Make sure RX2 shares the sample rate  with RX1 when running P1.
    //
    if (protocol == ORIGINAL_PROTOCOL && receiver[1]->sample_rate != receiver[0]->sample_rate) {
      receiver_change_sample_rate(receiver[1], receiver[0]->sample_rate);
    }

    break;
  }

  reconfigure_screen();
  receiver_set_active(receiver[0]);
#ifdef CLIENT_SERVER

  if (!radio_is_remote) {
#endif
    schedule_high_priority();

    if (protocol == ORIGINAL_PROTOCOL) {
      old_protocol_run();
    }

#ifdef CLIENT_SERVER
  }

#endif
}

void radio_change_sample_rate(int rate) {
  int i;

  //
  // The radio menu calls this function even if the sample rate
  // has not changed. Do nothing in this case.
  //
  switch (protocol) {
  case ORIGINAL_PROTOCOL:
    if (receiver[0]->sample_rate != rate) {
      protocol_stop();

      for (i = 0; i < receivers; i++) {
        receiver_change_sample_rate(receiver[i], rate);
      }

      receiver_change_sample_rate(receiver[PS_RX_FEEDBACK], rate);
      old_protocol_set_mic_sample_rate(rate);
      protocol_run();

      if (can_transmit) {
        tx_set_ps_sample_rate(transmitter, rate);
      }
    }

    break;
#ifdef SOAPYSDR

  case SOAPYSDR_PROTOCOL:
    if (receiver[0]->sample_rate != rate) {
      protocol_stop();
      receiver_change_sample_rate(receiver[0], rate);
      protocol_run();
    }

    break;
#endif
  }
}

static void rxtx(int state) {
  int i;

  if (!can_transmit) {
    t_print("WARNING: rxtx called but no transmitter!");
    return;
  }

  pre_mox = state && !duplex;

  if (state) {
    // switch to tx
    RECEIVER *rx_feedback = receiver[PS_RX_FEEDBACK];
    RECEIVER *tx_feedback = receiver[PS_TX_FEEDBACK];

    if (rx_feedback) { rx_feedback->samples = 0; }

    if (tx_feedback) { tx_feedback->samples = 0; }

    if (!duplex) {
      for (i = 0; i < receivers; i++) {
        // Delivery of RX samples
        // to WDSP via fexchange0() may come to an abrupt stop
        // (especially with PureSignal or DIVERSITY).
        // Therefore, wait for *all* receivers to complete
        // their slew-down before going TX.
        SetChannelState(receiver[i]->id, 0, 1);
        set_displaying(receiver[i], 0);
        g_object_ref((gpointer)receiver[i]->panel);

        if (receiver[i]->panadapter != NULL) {
          g_object_ref((gpointer)receiver[i]->panadapter);
        }

        if (receiver[i]->waterfall != NULL) {
          g_object_ref((gpointer)receiver[i]->waterfall);
        }
        gtk_widget_unparent(receiver[i]->panel);
      }
    }

    if (transmitter->dialog) {
      gtk_widget_show(transmitter->dialog);

      if (transmitter->dialog_x != -1 && transmitter->dialog_y != -1) {
        //gtk_window_move(GTK_WINDOW(transmitter->dialog), transmitter->dialog_x, transmitter->dialog_y);
        //FIXME: move window
        t_print("%s %s %d fixme", __FUNCTION__, __FILE__, __LINE__);
      }
    } else {
      gtk_fixed_put(GTK_FIXED(fixed), transmitter->panel, transmitter->x, transmitter->y);
    }

    if (transmitter->puresignal) {
      SetPSMox(transmitter->id, 1);
    }

    SetChannelState(transmitter->id, 1, 0);
    tx_set_displaying(transmitter, 1);

    switch (protocol) {
#ifdef SOAPYSDR

    case SOAPYSDR_PROTOCOL:
      soapy_protocol_set_tx_frequency(transmitter);
      //soapy_protocol_start_transmitter(transmitter);
      break;
#endif
    }
#ifdef DUMP_TX_DATA
    rxiq_count=0;
#endif
  } else {
    // switch to rx
#ifdef DUMP_TX_DATA
    static int snapshot=0;
    snapshot++;
    char fname[32];
    snprintf(fname, 32, "TXDUMP%d.iqdata", snapshot);
    FILE *fp=fopen(fname,"w");
    if (fp) {
      for (int i=0; i<rxiq_count; i++) {
        fprintf(fp,"%d  %ld  %ld\n", i, rxiqi[i],rxiqq[i]);
      }
     fclose(fp);
    }
#endif
    switch (protocol) {
#ifdef SOAPYSDR

    case SOAPYSDR_PROTOCOL:
      //soapy_protocol_stop_transmitter(transmitter);
      break;
#endif
    }

    if (transmitter->puresignal) {
      SetPSMox(transmitter->id, 0);
    }

    SetChannelState(transmitter->id, 0, 1);
    tx_set_displaying(transmitter, 0);

    if (transmitter->dialog) {
      // not sure i get this one.
      // why do we care about where it is if its being hidden?      
      //gtk_window_get_position(GTK_WINDOW(transmitter->dialog), &transmitter->dialog_x, &transmitter->dialog_y);
      gtk_widget_hide(transmitter->dialog);
    } else {
      //gtk_container_remove(GTK_CONTAINER(fixed), transmitter->panel);
      gtk_widget_unparent(transmitter->panel);
    }

    if (!duplex) {
      //
      // Set parameters for the "silence first RXIQ samples after TX/RX transition" feature
      // the default is "no silence", that is, fastest turnaround.
      // Seeing "tails" of the own TX signal (from crosstalk at the T/R relay) has been observed
      // for RedPitayas (the identify themself as STEMlab or HERMES) and HermesLite2 devices,
      // we also include the original HermesLite in this list (which can be enlarged if necessary).
      //
      int do_silence = 0;

      if (device == DEVICE_HERMES_LITE2 || device == DEVICE_HERMES_LITE ||
          device == DEVICE_HERMES || device == DEVICE_STEMLAB || device == DEVICE_STEMLAB_Z20) {
        //
        // These systems get a significant "tail" of the RX feedback signal into the RX after TX/RX,
        // leading to AGC pumping. The problem is most severe if there is a carrier until the end of
        // the TX phase (TUNE, AM, FM), the problem is virtually non-existent for CW, and of medium
        // importance in SSB. On the other hand, one wants a very fast turnaround in CW.
        // So there is no "muting" for CW, 31 msec "muting" for TUNE/AM/FM, and 16 msec for other modes.
        //
        // Note that for doing "TwoTone" the silence is built into tx_set_twotone().
        //
        switch (get_tx_mode()) {
        case modeCWU:
        case modeCWL:
          do_silence = 0; // no "silence"
          break;

        case modeAM:
        case modeFMN:
          do_silence = 5; // leads to 31 ms "silence"
          break;

        default:
          do_silence = 6; // leads to 16 ms "silence"
          break;
        }

        if (tune) { do_silence = 5; } // 31 ms "silence" for TUNEing in any mode
      }

      for (i = 0; i < receivers; i++) {
        gtk_fixed_put(GTK_FIXED(fixed), receiver[i]->panel, receiver[i]->x, receiver[i]->y);
        SetChannelState(receiver[i]->id, 1, 0);
        set_displaying(receiver[i], 1);
        //
        // There might be some left-over samples in the RX buffer that were filled in
        // *before* going TX, delete them
        //
        receiver[i]->samples = 0;

        if (do_silence) {
          receiver[i]->txrxmax = receiver[i]->sample_rate >> do_silence;
        } else {
          receiver[i]->txrxmax = 0;
        }

        receiver[i]->txrxcount = 0;
      }
    }
  }

  gpio_set_ptt(state);
}

void setMox(int state) {
  //t_print("%s: mox=%d vox=%d tune=%d NewState=%d\n", __FUNCTION__, mox,vox,tune,state);
  if (!can_transmit) { return; }

  if (state && TxInhibit) { return; }

  //
  // - setting MOX (no matter in which direction) stops TUNEing
  // - setting MOX (no matter in which direction) ends a pending VOX
  // - activating MOX while VOX is pending continues transmission
  // - deactivating MOX while VOX is pending makes a TX/RX transition
  //
  if (tune) {
    setTune(0);
  }

  vox_cancel();  // remove time-out

  //
  // If MOX is activated while VOX is already pending,
  // then switch from VOX to MOX mode but no RX/TX
  // transition is necessary.
  //
  if (state != isTransmitting()) {
    rxtx(state);
  }

  mox  = state;
  tune = 0;
  vox  = 0;

  switch (protocol) {
  case NEW_PROTOCOL:
    schedule_high_priority();
    schedule_receive_specific();
    break;

  default:
    break;
  }
}

int getMox() {
  return mox;
}

void setVox(int state) {
  //t_print("%s: mox=%d vox=%d tune=%d NewState=%d\n", __FUNCTION__, mox,vox,tune,state);
  if (!can_transmit) { return; }

  if (mox || tune) { return; }

  if (state && TxInhibit) { return; }

  if (vox != state) {
    rxtx(state);
  }

  vox = state;
  schedule_high_priority();
  schedule_receive_specific();
}

void setTune(int state) {
  //t_print("%s: mox=%d vox=%d tune=%d NewState=%d\n", __FUNCTION__, mox,vox,tune,state);
  if (!can_transmit) { return; }

  if (state && TxInhibit) { return; }

  // if state==tune, this function is a no-op

  if (tune != state) {
    vox_cancel();

    if (vox || mox) {
      rxtx(0);
      vox = 0;
      mox = 0;
    }

    if (state) {
      if (transmitter->puresignal && ! transmitter->ps_oneshot) {
        //
        // DL1YCF:
        // Some users have reported that especially when having
        // very long (10 hours) operating times with PS, hitting
        // the "TUNE" button makes the PS algorithm crazy, such that
        // it produces a very broad line spectrum. Experimentally, it
        // has been observed that this can be avoided by hitting
        // "Off" in the PS menu before hitting "TUNE", and hitting
        // "Restart" in the PS menu when tuning is complete.
        //
        // It is therefore suggested to to so implicitly when PS
        // is enabled. 
        // Added April 2024: if in "OneShot" mode, this is probably
        //                   not necessary and the PS reset also
        //                   most likely not wanted here
        //
        // So before start tuning: Reset PS engine
        //
        SetPSControl(transmitter->id, 1, 0, 0, 0);
        sleep_ms(50);
      }

      if (full_tune) {
        if (OCfull_tune_time != 0) {
          struct timeval te;
          gettimeofday(&te, NULL);
          tune_timeout = (te.tv_sec * 1000LL + te.tv_usec / 1000) + (long long)OCfull_tune_time;
        }
      }

      if (memory_tune) {
        if (OCmemory_tune_time != 0) {
          struct timeval te;
          gettimeofday(&te, NULL);
          tune_timeout = (te.tv_sec * 1000LL + te.tv_usec / 1000) + (long long)OCmemory_tune_time;
        }
      }
    }

    schedule_high_priority();
    //schedule_general();

    if (state) {
      if (!duplex) {
        for (int i = 0; i < receivers; i++) {
          // Delivery of RX samples
          // to WDSP via fexchange0() may come to an abrupt stop
          // (especially with PureSignal or DIVERSITY)
          // Therefore, wait for *all* receivers to complete
          // their slew-down before going TX.
          SetChannelState(receiver[i]->id, 0, 1);
          set_displaying(receiver[i], 0);
          schedule_high_priority();
        }
      }

      int txmode = get_tx_mode();
      pre_tune_mode = txmode;
      pre_tune_cw_internal = cw_keyer_internal;
#if 0

      //
      // in USB/DIGU      tune 1000 Hz above carrier
      // in LSB/DIGL,     tune 1000 Hz below carrier
      // all other (CW, AM, FM): tune on carrier freq.
      //
      switch (txmode) {
      case modeLSB:
      case modeDIGL:
        SetTXAPostGenToneFreq(transmitter->id, -(double)1000.0);
        break;

      case modeUSB:
      case modeDIGU:
        SetTXAPostGenToneFreq(transmitter->id, (double)1000.0);
        break;

      default:
        SetTXAPostGenToneFreq(transmitter->id, (double)0.0);
        break;
      }

#else
      //
      // Perhaps it it best to *always* tune on the dial frequency
      //
      SetTXAPostGenToneFreq(transmitter->id, (double)0.0);
#endif
      SetTXAPostGenToneMag(transmitter->id, 0.99999);
      SetTXAPostGenMode(transmitter->id, 0);
      SetTXAPostGenRun(transmitter->id, 1);

      switch (txmode) {
      case modeCWL:
        cw_keyer_internal = 0;
        tx_set_mode(transmitter, modeLSB);
        break;

      case modeCWU:
        cw_keyer_internal = 0;
        tx_set_mode(transmitter, modeUSB);
        break;
      }

      tune = state;
      calcDriveLevel();
      rxtx(state);
    } else {
      rxtx(state);
      SetTXAPostGenRun(transmitter->id, 0);

      switch (pre_tune_mode) {
      case modeCWL:
      case modeCWU:
        tx_set_mode(transmitter, pre_tune_mode);
        cw_keyer_internal = pre_tune_cw_internal;
        break;
      }

      if (transmitter->puresignal && !transmitter->ps_oneshot) {
        //
        // DL1YCF:
        // If we have done a "PS reset" when we started tuning,
        // resume PS engine now.
        //
        SetPSControl(transmitter->id, 0, 0, 1, 0);
      }

      tune = state;
      calcDriveLevel();
    }
  }

  schedule_high_priority();
  schedule_transmit_specific();
  schedule_receive_specific();
}

int getTune() {
  return tune;
}

int isTransmitting() {
  return mox | vox | tune;
}

double getDrive() {
  if (can_transmit) {
    return transmitter->drive;
  } else {
    return 0.0;
  }
}

static int calcLevel(double d) {
  int level = 0;
  int v = get_tx_vfo();
  const BAND *band = band_get_band(vfo[v].band);
  double target_dbm = 10.0 * log10(d * 1000.0);
  double gbb = band->pa_calibration;
  target_dbm -= gbb;
  double target_volts = sqrt(pow(10, target_dbm * 0.1) * 0.05);
  double volts = min((target_volts / 0.8), 1.0);
  double actual_volts = volts * (1.0 / 0.98);

  if (actual_volts < 0.0) {
    actual_volts = 0.0;
  } else if (actual_volts > 1.0) {
    actual_volts = 1.0;
  }

  level = (int)(actual_volts * 255.0);
  return level;
}

void calcDriveLevel() {
  int level;

  if (!can_transmit) { return; }

  if (tune && !transmitter->tune_use_drive) {
    level = calcLevel(transmitter->tune_drive);
  } else {
    level = calcLevel(transmitter->drive);
  }

  //
  // For most of the radios, just copy the "level" and switch off scaling
  //
  transmitter->do_scale = 0;
  transmitter->drive_level = level;

  //
  // For the original Penelope transmitter, the drive level has no effect. Instead, the TX IQ
  // samples must be scaled.
  // The HermesLite-II needs a combination of hardware attenuation and TX IQ scaling.
  // The inverse of the scale factor is needed to reverse the scaling for the TX DAC feedback
  // samples used in the PureSignal case.
  //
  // The constants have been rounded off so the drive_scale is slightly (0.01%) smaller then needed
  // so we have to reduce the inverse a little bit to avoid overflows.
  //
  if ((device == NEW_DEVICE_ATLAS || device == DEVICE_OZY || device == DEVICE_METIS) && atlas_penelope == 1) {
    transmitter->drive_scale = level * 0.0039215;
    transmitter->drive_level = 255;
    transmitter->drive_iscal = 0.9999 / transmitter->drive_scale;
    transmitter->do_scale = 1;
  }

  if (device == DEVICE_HERMES_LITE2 || device == NEW_DEVICE_HERMES_LITE2) {
    //
    // Calculate a combination of TX attenuation (values from -7.5 to 0 dB are encoded as 0, 16, 32, ..., 240)
    // and a TX IQ scaling. If level is above 107, the scale factor will be between 0.94 and 1.00, but if
    // level is smaller than 107 it may adopt any value between 0.0 and 1.0
    //
    double d = level;

    if (level > 240) {
      transmitter->drive_level = 240;                     //  0.0 dB hardware ATT
      transmitter->drive_scale = d * 0.0039215;
    } else if (level > 227) {
      transmitter->drive_level = 224;                     // -0.5 dB hardware ATT
      transmitter->drive_scale = d * 0.0041539;
    } else if (level > 214) {
      transmitter->drive_level = 208;                     // -1.0 dB hardware ATT
      transmitter->drive_scale = d * 0.0044000;
    } else if (level > 202) {
      transmitter->drive_level = 192;
      transmitter->drive_scale = d * 0.0046607;
    } else if (level > 191) {
      transmitter->drive_level = 176;
      transmitter->drive_scale = d * 0.0049369;
    } else if (level > 180) {
      transmitter->drive_level = 160;
      transmitter->drive_scale = d * 0.0052295;
    } else if (level > 170) {
      transmitter->drive_level = 144;
      transmitter->drive_scale = d * 0.0055393;
    } else if (level > 160) {
      transmitter->drive_level = 128;
      transmitter->drive_scale = d * 0.0058675;
    } else if (level > 151) {
      transmitter->drive_level = 112;
      transmitter->drive_scale = d * 0.0062152;
    } else if (level > 143) {
      transmitter->drive_level = 96;
      transmitter->drive_scale = d * 0.0065835;
    } else if (level > 135) {
      transmitter->drive_level = 80;
      transmitter->drive_scale = d * 0.0069736;
    } else if (level > 127) {
      transmitter->drive_level = 64;
      transmitter->drive_scale = d * 0.0073868;
    } else if (level > 120) {
      transmitter->drive_level = 48;
      transmitter->drive_scale = d * 0.0078245;
    } else if (level > 113) {
      transmitter->drive_level = 32;
      transmitter->drive_scale = d * 0.0082881;
    } else if (level > 107) {
      transmitter->drive_level = 16;
      transmitter->drive_scale = d * 0.0087793;
    } else {
      transmitter->drive_level = 0;
      transmitter->drive_scale = d * 0.0092995;    // can be between 0.0 and 0.995
    }

    transmitter->drive_iscal = 0.9999 / transmitter->drive_scale;
    transmitter->do_scale = 1;
  }

  //if (transmitter->do_scale) {
  //  t_print("%s: Level=%d Fac=%f\n", __FUNCTION__, transmitter->drive_level, transmitter->drive_scale);
  //} else {
  //  t_print("%s: Level=%d\n", __FUNCTION__, transmitter->drive_level);
  //}
  schedule_high_priority();
}

void setDrive(double value) {
  //t_print("%s: drive=%f\n", __FUNCTION__, value);
  if (!can_transmit) { return; }

  transmitter->drive = value;

  switch (protocol) {
  case ORIGINAL_PROTOCOL:
  case NEW_PROTOCOL:
    calcDriveLevel();
    break;
#ifdef SOAPYSDR

  case SOAPYSDR_PROTOCOL:
    soapy_protocol_set_tx_gain(transmitter, transmitter->drive);
    break;
#endif
  }
}

void setSquelch(const RECEIVER *rx) {
  //
  // This is now called whenever
  // - "squelch enable" changes
  // - "squelch value"  changes
  // - mode changes
  //
  double value;
  int    fm_squelch = 0;
  int    am_squelch = 0;
  int    voice_squelch = 0;

  //
  // the "slider" value goes from 0 (no squelch) to 100 (fully engaged)
  // and has to be mapped to
  //
  // AM    squelch:   -160.0 ... 0.00 dBm  linear interpolation
  // FM    squelch:      1.0 ... 0.01      expon. interpolation
  // Voice squelch:      0.0 ... 0.75      linear interpolation
  //
  switch (vfo[rx->id].mode) {
  case modeAM:
  case modeSAM:

  // My personal experience is that "Voice squelch" is of very
  // little use  when doing CW (this may apply to "AM squelch", too).
  case modeCWU:
  case modeCWL:
    //
    // Use AM squelch
    //
    value = ((rx->squelch / 100.0) * 160.0) - 160.0;
    SetRXAAMSQThreshold(rx->id, value);
    am_squelch = rx->squelch_enable;
    break;

  case modeLSB:
  case modeUSB:
  case modeDSB:
    //
    // Use Voice squelch (new in WDSP 1.21)
    //
    value = 0.0075 * rx->squelch;
    voice_squelch = rx->squelch_enable;
    SetRXASSQLThreshold(rx->id, value);
    SetRXASSQLTauMute(rx->id, 0.1);
    SetRXASSQLTauUnMute(rx->id, 0.1);
    break;

  case modeFMN:
    //
    // Use FM squelch
    //
    value = pow(10.0, -2.0 * rx->squelch / 100.0);
    SetRXAFMSQThreshold(rx->id, value);
    fm_squelch = rx->squelch_enable;
    break;

  default:
    // no squelch for digital and other modes
    // (this can be discussed).
    break;
  }

  //
  // activate the desired squelch, and deactivate
  // all others
  //
  SetRXAAMSQRun(rx->id, am_squelch);
  SetRXAFMSQRun(rx->id, fm_squelch);
  SetRXASSQLRun(rx->id, voice_squelch);
}

void radio_set_satmode(int mode) {
#ifdef CLIENT_SERVER

  if (radio_is_remote) {
    send_sat(client_socket, mode);
    return;
  }

#endif
  sat_mode = mode;
}

void radio_set_rf_gain(const RECEIVER *rx) {
#ifdef SOAPYSDR
  soapy_protocol_set_gain_element(rx, radio->info.soapy.rx_gain[rx->adc], (int)adc[rx->adc].gain);
#endif
}

void set_attenuation(int value) {
  switch (protocol) {
  case NEW_PROTOCOL:
    schedule_high_priority();
    break;
#ifdef SOAPYSDR

  case SOAPYSDR_PROTOCOL:
    // I think we should never arrive here
    t_print("%s: NOTREACHED assessment failed\n", __FUNCTION__);
    soapy_protocol_set_gain_element(active_receiver, radio->info.soapy.rx_gain[0], (int)adc[0].gain);
    break;
#endif
  }
}

void set_alex_antennas() {
  //
  // Obtain band of VFO-A and transmitter, set ALEX RX/TX antennas
  // and the step attenuator
  // This function is a no-op when running SOAPY.
  // This function also takes care of updating the PA dis/enable
  // status for P2.
  //
  const BAND *band;

  if (protocol == ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL) {
    band = band_get_band(vfo[VFO_A].band);
    receiver[0]->alex_antenna = band->alexRxAntenna;

    if (filter_board != CHARLY25) {
      receiver[0]->alex_attenuation = band->alexAttenuation;
    }

    if (can_transmit) {
      band = band_get_band(vfo[get_tx_vfo()].band);
      transmitter->alex_antenna = band->alexTxAntenna;
    }
  }

  schedule_high_priority();         // possibly update RX/TX antennas
  schedule_general();               // possibly update PA disable
}

void tx_vfo_changed() {
  //
  // When changing the active receiver or changing the split status,
  // the VFO that controls the transmitter my flip between VFOA/VFOB.
  // In these cases, we have to update the TX mode,
  // and re-calculate the drive level from the band-specific PA calibration
  // values. For SOAPY, the only thing to do is the update the TX mode.
  //
  // Note each time tx_vfo_changed() is called, calling set_alex_antennas()
  // is also due.
  //
  if (can_transmit) {
    tx_set_mode(transmitter, get_tx_mode());
    calcDriveLevel();
  }

  schedule_high_priority();         // possibly update RX/TX antennas
  schedule_transmit_specific();     // possibly un-set "CW mode"
  schedule_general();               // possibly update PA disable
}

void set_alex_attenuation(int v) {
  //
  // Change the value of the step attenuator. Store it
  // in the "band" data structure of the current band,
  // and in the receiver[0] data structure
  //
  if (protocol == ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL) {
    //
    // Store new value of the step attenuator in band data structure
    // (v can be 0,1,2,3)
    //
    BAND *band = band_get_band(vfo[VFO_A].band);
    band->alexAttenuation = v;
    receiver[0]->alex_attenuation = v;
  }

  schedule_high_priority();
}

void radio_split_toggle() {
  radio_set_split(!split);
}

void radio_set_split(int val) {
  //
  // "split" *must only* be set through this interface,
  // since it may change the TX band and thus requires
  // tx_vfo_changed() and set_alex_antennas().
  //
  if (can_transmit) {
    split = val;
    tx_vfo_changed();
    set_alex_antennas();
    g_idle_add(ext_vfo_update, NULL);
  }
}

void radioRestoreState() {
  t_print("radioRestoreState: %s\n", property_path);
  g_mutex_lock(&property_mutex);
  loadProperties(property_path);
  //
  // For consistency, all variables should get default values HERE,
  // but this is too much for the moment. TODO: initialize at least all
  // variables that are needed if the radio is remote
  //
  GetPropI0("WindowPositionX",                               window_x_pos);
  GetPropI0("WindowPositionY",                               window_y_pos);
  GetPropI0("display_zoompan",                               display_zoompan);
  GetPropI0("display_sliders",                               display_sliders);
  GetPropI0("display_toolbar",                               display_toolbar);
  GetPropI0("app_width",                                     app_width);
  GetPropI0("app_height",                                    app_height);
  GetPropI0("full_screen",                                   full_screen);
  GetPropI0("rx_stack_horizontal",                           rx_stack_horizontal);
  GetPropI0("vfo_layout",                                    vfo_layout);
  GetPropI0("optimize_touchscreen",                          optimize_for_touchscreen);

     

#ifdef CLIENT_SERVER
  GetPropI0("radio.hpsdr_server",                            hpsdr_server);
  GetPropI0("radio.hpsdr_server.listen_port",                listen_port);

  if (radio_is_remote) { return; }

#endif
  GetPropI0("enable_auto_tune",                              enable_auto_tune);
  GetPropI0("enable_tx_inhibit",                             enable_tx_inhibit);
  GetPropI0("radio_sample_rate",                             radio_sample_rate);
  GetPropI0("diversity_enabled",                             diversity_enabled);
  GetPropF0("diversity_gain",                                div_gain);
  GetPropF0("diversity_phase",                               div_phase);
  GetPropF0("diversity_cos",                                 div_cos);
  GetPropF0("diversity_sin",                                 div_sin);
  GetPropI0("new_pa_board",                                  new_pa_board);
  GetPropI0("region",                                        region);
  GetPropI0("atlas_penelope",                                atlas_penelope);
  GetPropI0("atlas_clock_source_10mhz",                      atlas_clock_source_10mhz);
  GetPropI0("atlas_clock_source_128mhz",                     atlas_clock_source_128mhz);
  GetPropI0("atlas_mic_source",                              atlas_mic_source);
  GetPropI0("atlas_janus",                                   atlas_janus);
  GetPropI0("hl2_audio_codec",                               hl2_audio_codec);
  GetPropI0("anan10E",                                       anan10E);
  GetPropI0("tx_out_of_band",                                tx_out_of_band_allowed);
  GetPropI0("filter_board",                                  filter_board);
  GetPropI0("pa_enabled",                                    pa_enabled);
  GetPropI0("pa_power",                                      pa_power);
  GetPropF0("mic_gain",                                      mic_gain);
  GetPropI0("mic_boost",                                     mic_boost);
  GetPropI0("mic_linein",                                    mic_linein);
  GetPropF0("linein_gain",                                   linein_gain);
  GetPropI0("mic_ptt_enabled",                               mic_ptt_enabled);
  GetPropI0("mic_bias_enabled",                              mic_bias_enabled);
  GetPropI0("mic_ptt_tip_bias_ring",                         mic_ptt_tip_bias_ring);
  GetPropI0("mic_input_xlr",                                 mic_input_xlr);
  GetPropI0("tx_filter_low",                                 tx_filter_low);
  GetPropI0("tx_filter_high",                                tx_filter_high);
  GetPropI0("cw_keys_reversed",                              cw_keys_reversed);
  GetPropI0("cw_keyer_speed",                                cw_keyer_speed);
  GetPropI0("cw_keyer_mode",                                 cw_keyer_mode);
  GetPropI0("cw_keyer_weight",                               cw_keyer_weight);
  GetPropI0("cw_keyer_spacing",                              cw_keyer_spacing);
  GetPropI0("cw_keyer_internal",                             cw_keyer_internal);
  GetPropI0("cw_keyer_sidetone_volume",                      cw_keyer_sidetone_volume);
  GetPropI0("cw_keyer_ptt_delay",                            cw_keyer_ptt_delay);
  GetPropI0("cw_keyer_hang_time",                            cw_keyer_hang_time);
  GetPropI0("cw_keyer_sidetone_frequency",                   cw_keyer_sidetone_frequency);
  GetPropI0("cw_breakin",                                    cw_breakin);
  GetPropI0("cw_ramp_width",                                 cw_ramp_width);
  GetPropI0("vfo_encoder_divisor",                           vfo_encoder_divisor);
  GetPropI0("OCtune",                                        OCtune);
  GetPropI0("OCfull_tune_time",                              OCfull_tune_time);
  GetPropI0("OCmemory_tune_time",                            OCmemory_tune_time);
  GetPropI0("analog_meter",                                  analog_meter);
  GetPropI0("smeter",                                        smeter);
  GetPropI0("alc",                                           alc);
  GetPropI0("rit_increment",                                 rit_increment);
  GetPropI0("pre_emphasize",                                 pre_emphasize);
  GetPropI0("vox_enabled",                                   vox_enabled);
  GetPropF0("vox_threshold",                                 vox_threshold);
  GetPropF0("vox_hang",                                      vox_hang);
  GetPropI0("calibration",                                   frequency_calibration);
  GetPropI0("receivers",                                     receivers);
  GetPropI0("iqswap",                                        iqswap);
  GetPropI0("rx_gain_calibration",                           rx_gain_calibration);
  GetPropF0("drive_digi_max",                                drive_digi_max);
  GetPropI0("split",                                         split);
  GetPropI0("duplex",                                        duplex);
  GetPropI0("sat_mode",                                      sat_mode);
  GetPropI0("mute_rx_while_transmitting",                    mute_rx_while_transmitting);
  GetPropI0("radio.display_warnings",                        display_warnings);
  GetPropI0("radio.display_pacurr",                          display_pacurr);
  GetPropI0("rigctl_enable",                                 rigctl_enable);
  GetPropI0("rigctl_start_with_autoreporting",               rigctl_start_with_autoreporting);
  GetPropI0("rigctl_port_base",                              rigctl_port);
  GetPropI0("mute_spkr_amp",                                 mute_spkr_amp);
  GetPropI0("adc0_filter_bypass",                            adc0_filter_bypass);
  GetPropI0("adc1_filter_bypass",                            adc1_filter_bypass);
#ifdef SATURN
  GetPropI0("client_enable_tx",                              client_enable_tx);
  GetPropI0("saturn_server_en",                              saturn_server_en);
#endif

  for (int i = 0; i < 11; i++) {
    GetPropF1("pa_trim[%d]", i,                              pa_trim[i]);
  }

  for (int id = 0; id < MAX_SERIAL; id++) {
    GetPropI1("rigctl_serial_enable[%d]", id,                SerialPorts[id].enable);
    GetPropI1("rigctl_serial_andromeda[%d]", id,             SerialPorts[id].andromeda);
    GetPropI1("rigctl_serial_baud_rate[%i]", id,             SerialPorts[id].baud);
    GetPropS1("rigctl_serial_port[%d]", id,                  SerialPorts[id].port);
#ifdef _WIN32
#else
    if (SerialPorts[id].andromeda) {
      SerialPorts[id].baud = B9600;
    }
#endif
  }

  for (int i = 0; i < n_adc; i++) {
    GetPropI1("radio.adc[%d].filters", i,                    adc[i].filters);
    GetPropI1("radio.adc[%d].hpf", i,                        adc[i].hpf);
    GetPropI1("radio.adc[%d].lpf", i,                        adc[i].lpf);
    GetPropI1("radio.adc[%d].antenna", i,                    adc[i].antenna);
    GetPropI1("radio.adc[%d].dither", i,                     adc[i].dither);
    GetPropI1("radio.adc[%d].random", i,                     adc[i].random);
    GetPropI1("radio.adc[%d].preamp", i,                     adc[i].preamp);

    if (have_rx_att) {
      GetPropI1("radio.adc[%d].attenuation", i,              adc[i].attenuation);
      GetPropI1("radio.adc[%d].enable_step_attenuation", i,  adc[i].enable_step_attenuation);
    }

    if (have_rx_gain) {
      GetPropF1("radio.adc[%d].gain", i,                     adc[i].gain);
      GetPropF1("radio.adc[%d].min_gain", i,                 adc[i].min_gain);
      GetPropF1("radio.adc[%d].max_gain", i,                 adc[i].max_gain);
    }

    if (device == SOAPYSDR_USB_DEVICE) {
      GetPropI1("radio.adc[%d].agc", i,                      adc[i].agc);
    }

    GetPropI1("radio.dac[%d].antenna", i,                    dac[i].antenna);
    GetPropF1("radio.dac[%d].gain", i,                       dac[i].gain);
  }

  filterRestoreState();
  bandRestoreState();
  memRestoreState();
  vfoRestoreState();
  gpioRestoreActions();
#ifdef MIDI
  midiRestoreState();
#endif
  t_print("%s: radio state (except receiver/transmitter) restored.\n", __FUNCTION__);

  //
  // Sanity check part 2:
  //
  // 1.) If the radio does not have 2 ADCs, there is no DIVERSITY
  //
  if (RECEIVERS < 2 || n_adc < 2) {
    diversity_enabled = 0;
  }

  //
  // 2.) Selecting the N2ADR filter board overrides most OC settings
  //
  if (filter_board == N2ADR) {
    n2adr_oc_settings(); // Apply default OC settings for N2ADR board
  }

  g_mutex_unlock(&property_mutex);
}

void radioSaveState() {
  t_print("radioSaveState: %s\n", property_path);
  g_mutex_lock(&property_mutex);
  clearProperties();

  //
  // Save the receiver and transmitter data structures. These
  // are restored in create_receiver/create_transmitter
  //
  for (int i = 0; i < RECEIVERS; i++) {
    receiverSaveState(receiver[i]);
  }

  if (can_transmit) {
    // The only variables of interest in this receiver are
    // the alex_antenna an the adc
    if (receiver[PS_RX_FEEDBACK]) {
      receiverSaveState(receiver[PS_RX_FEEDBACK]);
    }

    transmitterSaveState(transmitter);
  }

  //
  // Obtain window position and save in props file
  //
  //gtk_window_get_position(GTK_WINDOW(top_window), &window_x_pos, &window_y_pos);
  t_print("%s %s %d fixme", __FUNCTION__, __FILE__, __LINE__);
  SetPropI0("WindowPositionX",                               window_x_pos);
  SetPropI0("WindowPositionY",                               window_y_pos);
  //
  // What comes now is essentially copied from radioRestoreState,
  // with "GetProp" replaced by "SetProp".
  //
  //
  // Use the "saved" Zoompan/Slider/Toolbar display status
  // if they are currently hidden via the "Hide" button
  //
  SetPropI0("display_zoompan",                               hide_status ? old_zoom : display_zoompan);
  SetPropI0("display_sliders",                               hide_status ? old_slid : display_sliders);
  SetPropI0("display_toolbar",                               hide_status ? old_tool : display_toolbar);
  SetPropI0("app_width",                                     app_width);
  SetPropI0("app_height",                                    app_height);
  SetPropI0("full_screen",                                   full_screen);
  SetPropI0("rx_stack_horizontal",                           rx_stack_horizontal);
  SetPropI0("vfo_layout",                                    vfo_layout);
  SetPropI0("optimize_touchscreen",                          optimize_for_touchscreen);
  //
  // TODO: I think some further options related to the GUI
  // have to be moved up here for Client-Server operation
  //
#ifdef CLIENT_SERVER
  SetPropI0("radio.hpsdr_server",                            hpsdr_server);
  SetPropI0("radio.hpsdr_server.listen_port",                listen_port);

  if (radio_is_remote) { return; }

#endif
  SetPropI0("enable_auto_tune",                              enable_auto_tune);
  SetPropI0("enable_tx_inhibit",                             enable_tx_inhibit);
  SetPropI0("radio_sample_rate",                             radio_sample_rate);
  SetPropI0("diversity_enabled",                             diversity_enabled);
  SetPropF0("diversity_gain",                                div_gain);
  SetPropF0("diversity_phase",                               div_phase);
  SetPropF0("diversity_cos",                                 div_cos);
  SetPropF0("diversity_sin",                                 div_sin);
  SetPropI0("new_pa_board",                                  new_pa_board);
  SetPropI0("region",                                        region);
  SetPropI0("atlas_penelope",                                atlas_penelope);
  SetPropI0("atlas_clock_source_10mhz",                      atlas_clock_source_10mhz);
  SetPropI0("atlas_clock_source_128mhz",                     atlas_clock_source_128mhz);
  SetPropI0("atlas_mic_source",                              atlas_mic_source);
  SetPropI0("atlas_janus",                                   atlas_janus);
  SetPropI0("hl2_audio_codec",                               hl2_audio_codec);
  SetPropI0("anan10E",                                       anan10E);
  SetPropI0("tx_out_of_band",                                tx_out_of_band_allowed);
  SetPropI0("filter_board",                                  filter_board);
  SetPropI0("pa_enabled",                                    pa_enabled);
  SetPropI0("pa_power",                                      pa_power);
  SetPropF0("mic_gain",                                      mic_gain);
  SetPropI0("mic_boost",                                     mic_boost);
  SetPropI0("mic_linein",                                    mic_linein);
  SetPropF0("linein_gain",                                   linein_gain);
  SetPropI0("mic_ptt_enabled",                               mic_ptt_enabled);
  SetPropI0("mic_bias_enabled",                              mic_bias_enabled);
  SetPropI0("mic_ptt_tip_bias_ring",                         mic_ptt_tip_bias_ring);
  SetPropI0("mic_input_xlr",                                 mic_input_xlr);
  SetPropI0("tx_filter_low",                                 tx_filter_low);
  SetPropI0("tx_filter_high",                                tx_filter_high);
  SetPropI0("cw_keys_reversed",                              cw_keys_reversed);
  SetPropI0("cw_keyer_speed",                                cw_keyer_speed);
  SetPropI0("cw_keyer_mode",                                 cw_keyer_mode);
  SetPropI0("cw_keyer_weight",                               cw_keyer_weight);
  SetPropI0("cw_keyer_spacing",                              cw_keyer_spacing);
  SetPropI0("cw_keyer_internal",                             cw_keyer_internal);
  SetPropI0("cw_keyer_sidetone_volume",                      cw_keyer_sidetone_volume);
  SetPropI0("cw_keyer_ptt_delay",                            cw_keyer_ptt_delay);
  SetPropI0("cw_keyer_hang_time",                            cw_keyer_hang_time);
  SetPropI0("cw_keyer_sidetone_frequency",                   cw_keyer_sidetone_frequency);
  SetPropI0("cw_breakin",                                    cw_breakin);
  SetPropI0("cw_ramp_width",                                 cw_ramp_width);
  SetPropI0("vfo_encoder_divisor",                           vfo_encoder_divisor);
  SetPropI0("OCtune",                                        OCtune);
  SetPropI0("OCfull_tune_time",                              OCfull_tune_time);
  SetPropI0("OCmemory_tune_time",                            OCmemory_tune_time);
  SetPropI0("analog_meter",                                  analog_meter);
  SetPropI0("smeter",                                        smeter);
  SetPropI0("alc",                                           alc);
  SetPropI0("rit_increment",                                 rit_increment);
  SetPropI0("pre_emphasize",                                 pre_emphasize);
  SetPropI0("vox_enabled",                                   vox_enabled);
  SetPropF0("vox_threshold",                                 vox_threshold);
  SetPropF0("vox_hang",                                      vox_hang);
  SetPropI0("calibration",                                   frequency_calibration);
  SetPropI0("receivers",                                     receivers);
  SetPropI0("iqswap",                                        iqswap);
  SetPropI0("rx_gain_calibration",                           rx_gain_calibration);
  SetPropF0("drive_digi_max",                                drive_digi_max);
  SetPropI0("split",                                         split);
  SetPropI0("duplex",                                        duplex);
  SetPropI0("sat_mode",                                      sat_mode);
  SetPropI0("mute_rx_while_transmitting",                    mute_rx_while_transmitting);
  SetPropI0("radio.display_warnings",                        display_warnings);
  SetPropI0("radio.display_pacurr",                          display_pacurr);
  SetPropI0("rigctl_enable",                                 rigctl_enable);
  SetPropI0("rigctl_start_with_autoreporting",               rigctl_start_with_autoreporting);
  SetPropI0("rigctl_port_base",                              rigctl_port);
  SetPropI0("mute_spkr_amp",                                 mute_spkr_amp);
  SetPropI0("adc0_filter_bypass",                            adc0_filter_bypass);
  SetPropI0("adc1_filter_bypass",                            adc1_filter_bypass);
#ifdef SATURN
  SetPropI0("client_enable_tx",                              client_enable_tx);
  SetPropI0("saturn_server_en",                              saturn_server_en);
#endif

  for (int i = 0; i < 11; i++) {
    SetPropF1("pa_trim[%d]", i,                              pa_trim[i]);
  }

  for (int id = 0; id < MAX_SERIAL; id++) {
    SetPropI1("rigctl_serial_enable[%d]", id,                SerialPorts[id].enable);
    SetPropI1("rigctl_serial_andromeda[%d]", id,             SerialPorts[id].andromeda);
    SetPropI1("rigctl_serial_baud_rate[%i]", id,             SerialPorts[id].baud);
    SetPropS1("rigctl_serial_port[%d]", id,                  SerialPorts[id].port);
  }

  for (int i = 0; i < n_adc; i++) {
    SetPropI1("radio.adc[%d].filters", i,                    adc[i].filters);
    SetPropI1("radio.adc[%d].hpf", i,                        adc[i].hpf);
    SetPropI1("radio.adc[%d].lpf", i,                        adc[i].lpf);
    SetPropI1("radio.adc[%d].antenna", i,                    adc[i].antenna);
    SetPropI1("radio.adc[%d].dither", i,                     adc[i].dither);
    SetPropI1("radio.adc[%d].random", i,                     adc[i].random);
    SetPropI1("radio.adc[%d].preamp", i,                     adc[i].preamp);

    if (have_rx_att) {
      SetPropI1("radio.adc[%d].attenuation", i,              adc[i].attenuation);
      SetPropI1("radio.adc[%d].enable_step_attenuation", i,  adc[i].enable_step_attenuation);
    }

    if (have_rx_gain) {
      SetPropF1("radio.adc[%d].gain", i,                     adc[i].gain);
      SetPropF1("radio.adc[%d].min_gain", i,                 adc[i].min_gain);
      SetPropF1("radio.adc[%d].max_gain", i,                 adc[i].max_gain);
    }

    if (device == SOAPYSDR_USB_DEVICE) {
      SetPropI1("radio.adc[%d].agc", i,                      adc[i].agc);
    }

    SetPropI1("radio.dac[%d].antenna", i,                    dac[i].antenna);
    SetPropF1("radio.dac[%d].gain", i,                       dac[i].gain);
  }

  filterSaveState();
  bandSaveState();
  memSaveState();
  vfoSaveState();
  gpioSaveActions();
#ifdef MIDI
  midiSaveState();
#endif
  saveProperties(property_path);
  g_mutex_unlock(&property_mutex);
}

void calculate_display_average(const RECEIVER *rx) {
  double display_avb;
  int display_average;
  double t = 0.001 * rx->display_average_time;
  display_avb = exp(-1.0 / ((double)rx->fps * t));
  display_average = max(2, (int)fmin(60, (double)rx->fps * t));
  SetDisplayAvBackmult(rx->id, 0, display_avb);
  SetDisplayNumAverage(rx->id, 0, display_average);
}

#ifdef CLIENT_SERVER
// cppcheck-suppress constParameterPointer
int remote_start(void *data) {
  const char *server = (const char *)data;
  snprintf(property_path, sizeof(property_path), "%s@%s.props", radio->name, server);
  radio_is_remote = TRUE;
  optimize_for_touchscreen = 1;

  switch (controller) {
  case CONTROLLER2_V1:
  case CONTROLLER2_V2:
  case G2_FRONTPANEL:
    display_zoompan = 1;
    display_sliders = 0;
    display_toolbar = 0;
    break;

  default:
    display_zoompan = 1;
    display_sliders = 1;
    display_toolbar = 1;
    break;
  }

  RECEIVERS = 2;
  PS_RX_FEEDBACK = 2;
  PS_TX_FEEDBACK = 2;
  radioRestoreState();
  create_visual();
  reconfigure_screen();

  if (can_transmit) {
    if (transmitter->local_microphone) {
      if (audio_open_input() != 0) {
        t_print("audio_open_input failed\n");
        transmitter->local_microphone = 0;
      }
    }
  }

  for (int i = 0; i < receivers; i++) {
    receiverRestoreState(receiver[i]);  // this ONLY restores local display settings

    if (receiver[i]->local_audio) {
      if (audio_open_output(receiver[i])) {
        receiver[i]->local_audio = 0;
      }
    }
  }

  reconfigure_radio();
  g_idle_add(ext_vfo_update, NULL);
  gdk_window_set_cursor(gtk_widget_get_window(top_window), gdk_cursor_new(GDK_ARROW));

  for (int i = 0; i < receivers; i++) {
    (void) gdk_threads_add_timeout_full(G_PRIORITY_DEFAULT_IDLE, 100, start_spectrum, receiver[i], NULL);
  }

  start_vfo_timer();
  remote_started = TRUE;
  return 0;
}
#endif



//
// This function has to be called instead of "gtk_grid_attach" for ComboBoxes.
// Basically, it creates an EventBox and puts the ComboBox therein,
// such that all events (mouse clicks) go to the EventBox. This ignores
// everything except "button release" events, in this case it lets the ComboBox
// pop-up the menu which then goes to the foreground.
// Then, the choice can be made from the menu in the usual way.
//
//void my_combo_attach(GtkGrid *grid, GtkWidget *combo, int row, int col, int spanrow, int spancol) {
  // if (optimize_for_touchscreen) {
  //   GtkWidget *eventbox = gtk_event_box_new();
  //   g_signal_connect( eventbox, "event",   G_CALLBACK(eventbox_callback),   combo);
  //   gtk_box_append(GTK_BOX(eventbox),combo);
  //   gtk_event_box_set_above_child(GTK_EVENT_BOX(eventbox), TRUE);
  //   gtk_grid_attach(GTK_GRID(grid), eventbox, row, col, spanrow, spancol);
  // } else {
  //  gtk_grid_attach(GTK_GRID(grid), combo, row, col, spanrow, spancol);
  //}
//}

//
// This is used in several places (ant_menu, oc_menu, pa_menu)
// and determines the highest band that the radio can use
// (xvtr bands are not counted here)
//

int max_band() {
  int max = BANDS - 1;

  switch (device) {
  case DEVICE_HERMES_LITE:
  case DEVICE_HERMES_LITE2:
  case NEW_DEVICE_HERMES_LITE:
  case NEW_DEVICE_HERMES_LITE2:
    max = band10;
    break;

  case SOAPYSDR_USB_DEVICE:
    // This function will not be called for SOAPY
    max = BANDS - 1;
    break;

  default:
    max = band6;
    break;
  }

  return max;
}

void protocol_stop() {
  //
  // paranoia ...
  //
  mox_update(0);
  sleep_ms(100);

  switch (protocol) {
  case ORIGINAL_PROTOCOL:
    old_protocol_stop();
    break;

  case NEW_PROTOCOL:
    new_protocol_menu_stop();
    break;
#ifdef SOAPYSDR

  case SOAPYSDR_PROTOCOL:
    soapy_protocol_stop_receiver(receiver[0]);
    break;
#endif
  }
}

void protocol_run() {
  switch (protocol) {
  case ORIGINAL_PROTOCOL:
    old_protocol_run();
    break;

  case NEW_PROTOCOL:
    new_protocol_menu_start();
    break;
#ifdef SOAPYSDR

  case SOAPYSDR_PROTOCOL:
    soapy_protocol_start_receiver(receiver[0]);
    break;
#endif
  }
}

void protocol_restart() {
  protocol_stop();
  sleep_ms(200);
  protocol_run();
}

gpointer auto_tune_thread(gpointer data) {
  //
  // This routine is triggered when an "auto tune" event
  // occurs, which usually is triggered by an input.
  //
  // Start TUNEing and keep TUNEing until the auto_tune_flag
  // becomes zero. Abort TUNEing if it takes too long
  //
  // To avoid race conditions, there are two flags:
  // auto_tune_flag is set while this thread is running
  // auto_tune_end  signals that tune can stop
  //
  // The thread will not terminate until auto_tune_end is flagged,
  // but  it may stop tuning before.
  //
  int count = 0;
  g_idle_add(ext_tune_update, GINT_TO_POINTER(1));

  for (;;) {
    if (count >= 0) {
      count++;
    }

    sleep_ms(50);

    if (auto_tune_end) {
      g_idle_add(ext_tune_update, GINT_TO_POINTER(0));
      break;
    }

    if (count >= 200) {
      g_idle_add(ext_tune_update, GINT_TO_POINTER(0));
      count = -1;
    }
  }

  sleep_ms(50);       // debouncing
  auto_tune_flag = 0;
  return NULL;
}

//
// The next four functions implement a temporary change
// of settings during capture/replay.
//
void start_capture() {
  //
  // - turn off  equalizers for both RX
  //
  SetRXAEQRun(0, 0);
  SetRXAEQRun(1, 0);
}

void end_capture() {
  //
  // - set capture flag to  "available"
  // - restore  RX equalizer on/off flags
  //
  SetRXAEQRun(0, receiver[0]->eq_enable);
  SetRXAEQRun(1, receiver[1]->eq_enable);
}

void start_playback() {
  //
  // - turn off TX equalizer
  // - turn off TX compression
  // - set mic gain  to zero
  //
  SetTXAEQRun(transmitter->id, 0);
  SetTXACompressorRun(transmitter->id, 0);
  SetTXAPanelGain1(transmitter->id, 1.0);
}

void end_playback() {
  //
  // - restore TX equalizer on/off flag
  // - restore TX compressor on/off flag
  // - reatore TX mic gain setting
  //
  capture_state = CAP_AVAIL;
  SetTXAEQRun(transmitter->id, transmitter->eq_enable);
  SetTXACompressorRun(transmitter->id, transmitter->compressor);
  SetTXAPanelGain1(transmitter->id, pow(10.0, 0.05 * mic_gain));
}

