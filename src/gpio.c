/* Copyright (C)
* 2020 - John Melton, G0ORX/N6LYT
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

// Rewrite to use gpiod rather than wiringPi
// Note that all pin numbers are now the Broadcom GPIO

#include <gtk/gtk.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>
//#include <poll.h>
#include <sched.h>

#ifdef GPIO
  #include <gpiod.h>
  #include <linux/i2c-dev.h>
  #include <i2c/smbus.h>
  #include <sys/ioctl.h>
#endif

#include "band.h"
#include "channel.h"
#include "discovered.h"
#include "mode.h"
#include "filter.h"
#include "bandstack.h"
#include "toolbar.h"
#include "radio.h"
#include "toolbar.h"
#include "main.h"
#include "property.h"
#include "vfo.h"
#include "new_menu.h"
#include "encoder_menu.h"
#include "diversity_menu.h"
#include "actions.h"
#include "gpio.h"
#include "i2c.h"
#include "ext.h"
#include "sliders.h"
#include "new_protocol.h"
#include "zoompan.h"
#include "iambic.h"
#include "message.h"

//
// for controllers which have spare GPIO lines,
// these lines can be associated to certain
// functions, namely
//
// CWL:      input:  left paddle for internal (iambic) keyer
// CWR:      input:  right paddle for internal (iambic) keyer
// CWKEY:    input:  key-down from external keyer
// PTTIN:    input:  PTT from external keyer or microphone
// PTTOUT:   output: PTT output (indicating TX status)
//
// a value < 0 indicates "do not use". All inputs are active-low,
// but PTTOUT is active-high
//
// Avoid using GPIO lines 18, 19, 20, 21 since they are used for I2S
// by some GPIO-connected audio output "hats"
//
//

GPIOPin CWL_LINE = { -1, -1 };
GPIOPin CWR_LINE = { -1, -1 };
GPIOPin CWKEY_LINE = { -1, -1 };
GPIOPin PTTIN_LINE = { -1, -1 };
GPIOPin PTTOUT_LINE = { -1, -1 };
GPIOPin CWOUT_LINE = { -1, -1 };

static GPIOPin I2C_INTERRUPT = { -1, -1 };

#ifdef GPIO
  static struct gpiod_line *pttout_line = NULL;
  static struct gpiod_line *cwout_line = NULL;
#endif

void gpio_set_ptt(int state) {
#ifdef GPIO

  if (pttout_line) {
    //t_print("%s: state=%d\n", __FUNCTION__, state);
    if (gpiod_line_set_value(pttout_line, NOT(state)) < 0) {
      t_print("%s failed: %s\n", __FUNCTION__, g_strerror(errno));
    }
  }

#endif
}

void gpio_set_cw(int state) {
#ifdef GPIO

  if (cwout_line) {
    //t_print("%s: state=%d\n", __FUNCTION__, state);
    if (gpiod_line_set_value(cwout_line, NOT(state)) < 0) {
      t_print("%s failed: %s\n", __FUNCTION__, g_strerror(errno));
    }
  }

#endif
}

enum {
  TOP_ENCODER,
  BOTTOM_ENCODER
};

enum {
  A,
  B
};

#define DIR_NONE 0x0
// Clockwise step.
#define DIR_CW 0x10
// Anti-clockwise step.
#define DIR_CCW 0x20

//
// Encoder states for a "full cycle"
//
#define R_START     0x00
#define R_CW_FINAL  0x01
#define R_CW_BEGIN  0x02
#define R_CW_NEXT   0x03
#define R_CCW_BEGIN 0x04
#define R_CCW_FINAL 0x05
#define R_CCW_NEXT  0x06

//
// Encoder states for a "half cycle"
//
#define R_START1    0x07
#define R_START0    0x08
#define R_CW_BEG1   0x09
#define R_CW_BEG0   0x0A
#define R_CCW_BEG1  0x0B
#define R_CCW_BEG0  0x0C

//
// Few general remarks on the state machine:
// - if the levels do not change, the machinestate does not change
// - if there is bouncing on one input line, the machine oscillates
//   between two "adjacent" states but generates at most one tick
// - if both input lines change level, move to a suitable new
//   starting point but do not generate a tick
// - if one or both of the AB lines are inverted, the same cycles
//   are passed but with a different starting point. Therefore,
//   it still works.
//
guchar encoder_state_table[13][4] = {
  //
  // A "full cycle" has the following state changes
  // (first line: line levels AB, 1=pressed, 0=released,
  //  2nd   line: state names
  //
  // clockwise:  11   -->   10   -->    00    -->    01     -->  11
  //            Start --> CWbeg  -->  CWnext  -->  CWfinal  --> Start
  //
  // ccw:        11   -->   01    -->   00     -->   10      -->  11
  //            Start --> CCWbeg  --> CCWnext  --> CCWfinal  --> Start
  //
  // Emit the "tick" when moving from "final" to "start".
  //
  //                   00           10           01          11
  // -----------------------------------------------------------------------------
  /* R_START     */ {R_START,    R_CW_BEGIN,  R_CCW_BEGIN, R_START},
  /* R_CW_FINAL  */ {R_CW_NEXT,  R_START,     R_CW_FINAL,  R_START | DIR_CW},
  /* R_CW_BEGIN  */ {R_CW_NEXT,  R_CW_BEGIN,  R_START,     R_START},
  /* R_CW_NEXT   */ {R_CW_NEXT,  R_CW_BEGIN,  R_CW_FINAL,  R_START},
  /* R_CCW_BEGIN */ {R_CCW_NEXT, R_START,     R_CCW_BEGIN, R_START},
  /* R_CCW_FINAL */ {R_CCW_NEXT, R_CCW_FINAL, R_START,     R_START | DIR_CCW},
  /* R_CCW_NEXT  */ {R_CCW_NEXT, R_CCW_FINAL, R_CCW_BEGIN, R_START},
  //
  // The same sequence can be interpreted as two "half cycles"
  //
  // clockwise1:   11    -->   10   -->   00
  //             Start1  --> CWbeg1 --> Start0
  //
  // clockwise2:   00    -->   01   -->   11
  //             Start0  --> CWbeg0 --> Start1
  //
  // ccw1:         11    -->   01    -->   00
  //             Start1  --> CCWbeg1 --> Start0
  //
  // ccw2:         00    -->   10    -->   11
  //             Start0  --> CCWbeg0 --> Start1
  //
  // If both lines change, this is interpreted as a two-step move
  // without changing the orientation and without emitting a "tick".
  //
  // Emit the "tick" each time when moving from "beg" to "start".
  //
  //                   00                    10          01         11
  // -----------------------------------------------------------------------------
  /* R_START1    */ {R_START0,           R_CW_BEG1,  R_CCW_BEG1, R_START1},
  /* R_START0    */ {R_START0,           R_CCW_BEG0, R_CW_BEG0,  R_START1},
  /* R_CW_BEG1   */ {R_START0 | DIR_CW,  R_CW_BEG1,  R_CW_BEG0,  R_START1},
  /* R_CW_BEG0   */ {R_START0,           R_CW_BEG1,  R_CW_BEG0,  R_START1 | DIR_CW},
  /* R_CCW_BEG1  */ {R_START0 | DIR_CCW, R_CCW_BEG0, R_CCW_BEG1, R_START1},
  /* R_CCW_BEG0  */ {R_START0,           R_CCW_BEG0, R_CCW_BEG1, R_START1 | DIR_CCW},
};

#ifdef GPIO
  char *consumer = "pihpsdr";

#define MAX_LINES 128
  int lines = 0;
  int monitor_lines[MAX_LINES];

  GPIOChip gpio_chips[MAX_GPIO_CHIPS];
  int num_gpio_chips = 0;
  ChipMonitorLines chip_monitor_lines[MAX_GPIO_CHIPS];


  static GMutex encoder_mutex;
  static GThread *monitor_thread_id;
#endif

long settle_time = 50; // ms

//
// The "static const" data is the DEFAULT assignment for encoders,
// and for Controller2 and G2 front panel switches
// These defaults are read-only and copied to my_encoders and my_switches
// when restoring default values
//
// Controller1 has 3 small encoders + VFO, and  8 switches in 6 layers
// Controller2 has 4 small encoders + VFO, and 16 switches
// G2 panel    has 4 small encoders + VFO, and 16 switches
//
// The controller1 switches are hard-wired to the toolbar buttons
//

//
// RPI5: GPIO line 20 not available, replace "20" by "14" at four places in the following lines
//       and re-wire the controller connection from GPIO20 to GPIO14
//

static const ENCODER encoders_no_controller[MAX_ENCODERS] = {
    {FALSE, TRUE, 0, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE, 0, 0, 0, 0L},
    {FALSE, TRUE, 0, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE, 0, 0, 0, 0L},
    {FALSE, TRUE, 0, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE, 0, 0, 0, 0L},
    {FALSE, TRUE, 0, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE, 0, 0, 0, 0L},
    {FALSE, TRUE, 0, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE, 0, 0, 0, 0L},
};


static const ENCODER encoders_marks_controller[MAX_ENCODERS] = {
    // bottom                                                       // top                                                        // switch
    // en   p-up, c, a, v, c, a, v, p,  function,   state,            en   p-up, c, a, v, c, a, v, p,    function,   state,         en   p-up, c, a,  function, db
    {FALSE, TRUE, 0, 0, 0, 0, 0, 0, 0, NO_ACTION, R_START,          TRUE,  TRUE, 1,17, 0, 1, 2, 0, 0, AF_GAIN_RX1, R_START,        TRUE, TRUE, 1,18,      VFOA, 0L},
    {FALSE, TRUE, 0, 0, 0, 0, 0, 0, 0, NO_ACTION, R_START,          TRUE,  TRUE, 1, 4, 0, 1, 5, 0, 0, AF_GAIN_RX2, R_START,        TRUE, TRUE, 1, 3,      VFOB, 0L},
    {FALSE, TRUE, 0, 0, 0, 0, 0, 0, 0, NO_ACTION, R_START,          TRUE,  TRUE, 0, 3, 0, 0,14, 0, 0,         VFO, R_START,       FALSE, TRUE, 0, 0, NO_ACTION, 0L},
    {FALSE, TRUE, 0, 0, 0, 0, 0, 0, 0, NO_ACTION, R_START,          FALSE, TRUE, 0, 0, 0, 0, 0, 0, 0,   NO_ACTION, R_START,       FALSE, TRUE, 0, 0, NO_ACTION, 0L},
    {FALSE, TRUE, 0, 0, 0, 0, 0, 0, 0, NO_ACTION, R_START,          FALSE, TRUE, 0, 0, 0, 0, 0, 0, 0,   NO_ACTION, R_START,       FALSE, TRUE, 0, 0, NO_ACTION, 0L},
};


static const ENCODER encoders_controller1[MAX_ENCODERS] = {
    {TRUE,  TRUE, 0, 20, 1, 0, 26, 1, 0, AF_GAIN,  R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, TRUE,  TRUE, 0, 25, MENU_BAND,       0L},
    {TRUE,  TRUE, 0, 16, 1, 0, 19, 1, 0, AGC_GAIN, R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, TRUE,  TRUE, 0,  8, MENU_BANDSTACK,  0L},
    {TRUE,  TRUE, 0,  4, 1, 0, 21, 1, 0, DRIVE,    R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, TRUE,  TRUE, 0,  7, MENU_MODE,       0L},
    {TRUE,  TRUE, 0, 18, 1, 0, 17, 1, 0, VFO,      R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE, 0,  0, NO_ACTION,       0L},
    {FALSE, TRUE, 0,  0, 0, 0,  0, 0, 1, NO_ACTION, R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE, 0,  0, NO_ACTION,       0L},
};


static const ENCODER encoders_controller2_v1[MAX_ENCODERS] = {
    {TRUE, TRUE, 0, 20, 1, 0, 26, 1, 0, AF_GAIN,  R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, TRUE,  TRUE, 0, 22, MENU_BAND,      0L},
    {TRUE, TRUE, 0,  4, 1, 0, 21, 1, 0, AGC_GAIN, R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, TRUE,  TRUE, 0, 27, MENU_BANDSTACK, 0L},
    {TRUE, TRUE, 0, 16, 1, 0, 19, 1, 0, IF_WIDTH, R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, TRUE,  TRUE, 0, 23, MENU_MODE,      0L},
    {TRUE, TRUE, 0, 25, 1, 0,  8, 1, 0, RIT,      R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, TRUE,  TRUE, 0, 24, MENU_FREQUENCY, 0L},
    {TRUE, TRUE, 0, 18, 1, 0, 17, 1, 0, VFO,      R_START, FALSE, TRUE, 0, 0, 0, 0, 0, 0, R_START, FALSE, TRUE, 0,  0, NO_ACTION,      0L},
};


static const ENCODER encoders_controller2_v2[MAX_ENCODERS] = {
    {TRUE, TRUE, 0,  5, 1, 0,  6, 1, 0, AGC_GAIN_RX1, R_START1, TRUE,  TRUE, 0, 26, 1, 0, 20, 1, 0, AF_GAIN_RX1, R_START1, TRUE,  TRUE, 0, 22, RX1,            0L}, //ENC2
    {TRUE, TRUE, 0,  9, 1, 0,  7, 1, 0, AGC_GAIN_RX2, R_START1, TRUE,  TRUE, 0, 21, 1, 0,  4, 1, 0, AF_GAIN_RX2, R_START1, TRUE,  TRUE, 0, 27, RX2,            0L}, //ENC3
    {TRUE, TRUE, 0, 11, 1, 0, 10, 1, 0, DIV_GAIN,     R_START1, TRUE,  TRUE, 0, 19, 1, 0, 16, 1, 0, DIV_PHASE,   R_START1, TRUE,  TRUE, 0, 23, DIV,            0L}, //ENC4
    {TRUE, TRUE, 0, 13, 1, 0, 12, 1, 0, XIT,          R_START1, TRUE,  TRUE, 0,  8, 1, 0, 25, 1, 0, RIT,         R_START1, TRUE,  TRUE, 0, 24, MENU_FREQUENCY, 0L}, //ENC5
    {TRUE, TRUE, 0, 18, 1, 0, 17, 1, 0, VFO,          R_START,  FALSE, TRUE, 0,  0, 0, 0,  0, 0, 0, NO_ACTION,   R_START, FALSE,  TRUE, 0,  0, NO_ACTION,      0L}, //ENC1/VFO
};


static const ENCODER encoders_g2_frontpanel[MAX_ENCODERS] = {
    {TRUE, TRUE, 0,  5, 1, 0,  6, 1, 0, DRIVE,    R_START1, TRUE,  TRUE, 0, 26, 1, 0, 20, 1, 0, MIC_GAIN,  R_START1, TRUE,  TRUE, 0, 22, PS,             0L}, //ENC1
    {TRUE, TRUE, 0,  9, 1, 0,  7, 1, 0, AGC_GAIN, R_START1, TRUE,  TRUE, 0, 21, 1, 0,  4, 1, 0, AF_GAIN,   R_START1, TRUE,  TRUE, 0, 27, MUTE,           0L}, //ENC3
    {TRUE, TRUE, 0, 11, 1, 0, 10, 1, 0, DIV_GAIN, R_START1, TRUE,  TRUE, 0, 19, 1, 0, 16, 1, 0, DIV_PHASE, R_START1, TRUE,  TRUE, 0, 23, DIV,            0L}, //ENC7
    {TRUE, TRUE, 0, 13, 1, 0, 12, 1, 0, XIT,      R_START1, TRUE,  TRUE, 0,  8, 1, 0, 25, 1, 0, RIT,       R_START1, TRUE,  TRUE, 0, 24, MENU_FREQUENCY, 0L}, //ENC5
    {TRUE, TRUE, 0, 18, 1, 0, 17, 1, 0, VFO,      R_START,  FALSE, TRUE, 0,  0, 0, 0,  0, 0, 0, NO_ACTION, R_START, FALSE,  TRUE, 0,  0, NO_ACTION,      0L}, //VFO
};


static const SWITCH switches_no_controller[MAX_SWITCHES] = {
  {FALSE, FALSE, 0, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, 0, NO_ACTION, 0L}
};

static const SWITCH switches_marks_controller[MAX_SWITCHES] = {
  {TRUE, FALSE, 0, 4, MENU_FILTER, 0L},
  {TRUE, FALSE, 0, 7, MENU_BAND, 0L},
  {TRUE, FALSE, 0, 8, MENU_MODE, 0L},
  {TRUE, FALSE, 0, 15, MOX, 0L},
  {FALSE, FALSE, 0, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, 0, NO_ACTION, 0L},
  {FALSE, FALSE, 0, 0, NO_ACTION, 0L}
};

SWITCH switches_controller1[MAX_FUNCTIONS][MAX_SWITCHES] = {
  { {TRUE,  TRUE, 0, 27, MOX,            0L},
    {TRUE,  TRUE, 0, 13, MENU_BAND,      0L},
    {TRUE,  TRUE, 0, 12, MENU_BANDSTACK, 0L},
    {TRUE,  TRUE, 0,  6, MENU_MODE,      0L},
    {TRUE,  TRUE, 0,  5, MENU_FILTER,    0L},
    {TRUE,  TRUE, 0, 24, MENU_NOISE,     0L},
    {TRUE,  TRUE, 0, 23, MENU_AGC,       0L},
    {TRUE,  TRUE, 0, 22, FUNCTION,       0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L}
  },
  { {TRUE,  TRUE, 0, 27, MOX,            0L},
    {TRUE,  TRUE, 0, 13, LOCK,           0L},
    {TRUE,  TRUE, 0, 12, CTUN,           0L},
    {TRUE,  TRUE, 0,  6, A_TO_B,         0L},
    {TRUE,  TRUE, 0,  5, B_TO_A,         0L},
    {TRUE,  TRUE, 0, 24, A_SWAP_B,       0L},
    {TRUE,  TRUE, 0, 23, SPLIT,          0L},
    {TRUE,  TRUE, 0, 22, FUNCTION,       0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L}
  },
  { {TRUE,  TRUE, 0, 27, MOX,            0L},
    {TRUE,  TRUE, 0, 13, MENU_FREQUENCY, 0L},
    {TRUE,  TRUE, 0, 12, MENU_MEMORY,    0L},
    {TRUE,  TRUE, 0,  6, RIT_ENABLE,     0L},
    {TRUE,  TRUE, 0,  5, RIT_PLUS,       0L},
    {TRUE,  TRUE, 0, 24, RIT_MINUS,      0L},
    {TRUE,  TRUE, 0, 23, RIT_CLEAR,      0L},
    {TRUE,  TRUE, 0, 22, FUNCTION,       0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L}
  },
  { {TRUE,  TRUE, 0, 27, MOX,            0L},
    {TRUE,  TRUE, 0, 13, MENU_FREQUENCY, 0L},
    {TRUE,  TRUE, 0, 12, MENU_MEMORY,    0L},
    {TRUE,  TRUE, 0,  6, XIT_ENABLE,     0L},
    {TRUE,  TRUE, 0,  5, XIT_PLUS,       0L},
    {TRUE,  TRUE, 0, 24, XIT_MINUS,      0L},
    {TRUE,  TRUE, 0, 23, XIT_CLEAR,      0L},
    {TRUE,  TRUE, 0, 22, FUNCTION,       0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L}
  },
  { {TRUE,  TRUE, 0, 27, MOX,            0L},
    {TRUE,  TRUE, 0, 13, MENU_FREQUENCY, 0L},
    {TRUE,  TRUE, 0, 12, SPLIT,          0L},
    {TRUE,  TRUE, 0,  6, DUPLEX,         0L},
    {TRUE,  TRUE, 0,  5, SAT,            0L},
    {TRUE,  TRUE, 0, 24, RSAT,           0L},
    {TRUE,  TRUE, 0, 23, MENU_BAND,      0L},
    {TRUE,  TRUE, 0, 22, FUNCTION,       0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L}
  },
  { {TRUE,  TRUE, 0, 27, MOX,            0L},
    {TRUE,  TRUE, 0, 13, TUNE,           0L},
    {TRUE,  TRUE, 0, 12, TUNE_FULL,      0L},
    {TRUE,  TRUE, 0,  6, TUNE_MEMORY,    0L},
    {TRUE,  TRUE, 0,  5, MENU_BAND,      0L},
    {TRUE,  TRUE, 0, 24, MENU_MODE,      0L},
    {TRUE,  TRUE, 0, 23, MENU_FILTER,    0L},
    {TRUE,  TRUE, 0, 22, FUNCTION,       0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L},
    {FALSE, FALSE, 0, 0, NO_ACTION,      0L}
  }
};


static const SWITCH switches_controller2_v1[MAX_SWITCHES] = {
  {FALSE, FALSE, 0, 0, MOX,              0L},
  {FALSE, FALSE, 0, 0, TUNE,             0L},
  {FALSE, FALSE, 0, 0, PS,               0L},
  {FALSE, FALSE, 0, 0, TWO_TONE,         0L},
  {FALSE, FALSE, 0, 0, NR,               0L},
  {FALSE, FALSE, 0, 0, A_TO_B,           0L},
  {FALSE, FALSE, 0, 0, B_TO_A,           0L},
  {FALSE, FALSE, 0, 0, MODE_MINUS,       0L},
  {FALSE, FALSE, 0, 0, BAND_MINUS,       0L},
  {FALSE, FALSE, 0, 0, MODE_PLUS,        0L},
  {FALSE, FALSE, 0, 0, BAND_PLUS,        0L},
  {FALSE, FALSE, 0, 0, XIT_ENABLE,       0L},
  {FALSE, FALSE, 0, 0, NB,               0L},
  {FALSE, FALSE, 0, 0, SPNB,              0L},
  {FALSE, FALSE, 0, 0, LOCK,             0L},
  {FALSE, FALSE, 0, 0, CTUN,             0L}
};


static const SWITCH switches_controller2_v2[MAX_SWITCHES] = {
  {FALSE, FALSE, 0, 0, MOX,              0L},  //GPB7 SW2
  {FALSE, FALSE, 0, 0, TUNE,             0L},  //GPB6 SW3
  {FALSE, FALSE, 0, 0, PS,               0L},  //GPB5 SW4
  {FALSE, FALSE, 0, 0, TWO_TONE,         0L},  //GPB4 SW5
  {FALSE, FALSE, 0, 0, NR,               0L},  //GPA3 SW6
  {FALSE, FALSE, 0, 0, NB,               0L},  //GPB3 SW14
  {FALSE, FALSE, 0, 0, SPNB,              0L},  //GPB2 SW15
  {FALSE, FALSE, 0, 0, XIT_ENABLE,       0L},  //GPA7 SW13
  {FALSE, FALSE, 0, 0, BAND_PLUS,        0L},  //GPA6 SW12
  {FALSE, FALSE, 0, 0, MODE_PLUS,        0L},  //GPA5 SW11
  {FALSE, FALSE, 0, 0, BAND_MINUS,       0L},  //GPA4 SW10
  {FALSE, FALSE, 0, 0, MODE_MINUS,       0L},  //GPA0 SW9
  {FALSE, FALSE, 0, 0, A_TO_B,           0L},  //GPA2 SW7
  {FALSE, FALSE, 0, 0, B_TO_A,           0L},  //GPA1 SW8
  {FALSE, FALSE, 0, 0, LOCK,             0L},  //GPB1 SW16
  {FALSE, FALSE, 0, 0, CTUN,             0L}   //GPB0 SW17
};


static const SWITCH switches_g2_frontpanel[MAX_SWITCHES] = {
  {FALSE, FALSE, 0, 0, XIT_ENABLE,       0L},  //GPB7 SW22
  {FALSE, FALSE, 0, 0, RIT_ENABLE,       0L},  //GPB6 SW21
  {FALSE, FALSE, 0, 0, FUNCTION,         0L},  //GPB5 SW20
  {FALSE, FALSE, 0, 0, SPLIT,            0L},  //GPB4 SW19
  {FALSE, FALSE, 0, 0, LOCK,             0L},  //GPA3 SW9
  {FALSE, FALSE, 0, 0, B_TO_A,           0L},  //GPB3 SW18
  {FALSE, FALSE, 0, 0, A_TO_B,           0L},  //GPB2 SW17
  {FALSE, FALSE, 0, 0, MODE_MINUS,       0L},  //GPA7 SW13
  {FALSE, FALSE, 0, 0, BAND_PLUS,        0L},  //GPA6 SW12
  {FALSE, FALSE, 0, 0, FILTER_PLUS,      0L},  //GPA5 SW11
  {FALSE, FALSE, 0, 0, MODE_PLUS,        0L},  //GPA4 SW10
  {FALSE, FALSE, 0, 0, MOX,              0L},  //GPA0 SW6
  {FALSE, FALSE, 0, 0, CTUN,             0L},  //GPA2 SW8
  {FALSE, FALSE, 0, 0, TUNE,             0L},  //GPA1 SW7
  {FALSE, FALSE, 0, 0, BAND_MINUS,       0L},  //GPB1 SW16
  {FALSE, FALSE, 0, 0, FILTER_MINUS,     0L}   //GPB0 SW15
};


ENCODER my_encoders[MAX_ENCODERS];
SWITCH  my_switches[MAX_SWITCHES];

ENCODER *encoders = NULL;
SWITCH *switches = NULL;

#ifdef GPIO

static GThread *rotary_encoder_thread_id;
static uint64_t epochMilli;

static void initialiseEpoch() {
  struct timespec ts ;
  clock_gettime (CLOCK_MONOTONIC_RAW, &ts) ;
  epochMilli = (uint64_t)ts.tv_sec * (uint64_t)1000    + (uint64_t)(ts.tv_nsec / 1000000L) ;
}

static unsigned int millis () {
  uint64_t now ;
  struct  timespec ts ;
  clock_gettime (CLOCK_MONOTONIC_RAW, &ts) ;
  now  = (uint64_t)ts.tv_sec * (uint64_t)1000 + (uint64_t)(ts.tv_nsec / 1000000L) ;
  return (uint32_t)(now - epochMilli) ;
}

static gpointer rotary_encoder_thread(gpointer data) {
    int i;
    enum ACTION action;
    enum ACTION_MODE mode;
    int val;
    usleep(250000);
    t_print("%s\n", __FUNCTION__);

	/*for (int i = 0; i < MAX_ENCODERS; i++) {
		t_print("Encoder config: Encoder %d top_enabled=%d, bottom_enabled=%d, top_function=%i, bottom_function=%i\n", i, encoders[i].top_encoder_enabled, encoders[i].bottom_encoder_enabled, encoders[i].top_encoder_function, encoders[i].bottom_encoder_function);
	}*/


    while (TRUE) {
        g_mutex_lock(&encoder_mutex);


        //t_print("%s LOOP!\n", __FUNCTION__);

        for (i = 0; i < MAX_ENCODERS; i++) {

            //t_print("%s LOOP: FOR\n", __FUNCTION__);


            if (encoders[i].bottom_encoder_enabled && encoders[i].bottom_encoder_pos != 0) {
                //t_print("%s: BOTTOM encoder %d pos=%d\n",__FUNCTION__,i,encoders[i].bottom_encoder_pos);
                action = encoders[i].bottom_encoder_function;
                mode = ACTION_RELATIVE;

                if (action == VFO && vfo_encoder_divisor > 1) {
                    val = encoders[i].bottom_encoder_pos / vfo_encoder_divisor;
                    encoders[i].bottom_encoder_pos = encoders[i].bottom_encoder_pos - (val * vfo_encoder_divisor);
                }
                else {
                    val = encoders[i].bottom_encoder_pos;
                    encoders[i].bottom_encoder_pos = 0;
                }

                if (val != 0) { schedule_action(action, mode, val); }
            }

            if (encoders[i].top_encoder_enabled && encoders[i].top_encoder_pos != 0) {
                //t_print("%s: TOP encoder %d pos=%d\n",__FUNCTION__,i,encoders[i].top_encoder_pos);
                action = encoders[i].top_encoder_function;
                mode = ACTION_RELATIVE;

                //t_print("%s: TOP encoder %d pos=%d action=%i\n\n\n\n\n", __FUNCTION__, i, encoders[i].top_encoder_pos, action);

                if (action == VFO && vfo_encoder_divisor > 1) {
                    val = encoders[i].top_encoder_pos / vfo_encoder_divisor;
                    encoders[i].top_encoder_pos = encoders[i].top_encoder_pos - (val * vfo_encoder_divisor);
                }
                else {
                    val = encoders[i].top_encoder_pos;
                    encoders[i].top_encoder_pos = 0;
                }

                if (val != 0) { schedule_action(action, mode, val); }
            }
        }

        g_mutex_unlock(&encoder_mutex);
        usleep(100000); // sleep for 100ms
    }

    return NULL;
}

static void process_encoder(int e, int l, int addr, int val) {
    guchar pinstate;
    //t_print("%s: encoder=%d level=%d addr=0x%02X val=%d\n",__FUNCTION__,e,l,addr,val);
    g_mutex_lock(&encoder_mutex);

    switch (l) {
    case BOTTOM_ENCODER:
        switch (addr) {
        case A:
            encoders[e].bottom_encoder_a_value = val;
            pinstate = (encoders[e].bottom_encoder_b_value << 1) | encoders[e].bottom_encoder_a_value;
            encoders[e].bottom_encoder_state = encoder_state_table[encoders[e].bottom_encoder_state & 0xf][pinstate];

            //t_print("%s: state=%02X\n",__FUNCTION__,encoders[e].bottom_encoder_state);
            switch (encoders[e].bottom_encoder_state & 0x30) {
            case DIR_NONE:
                break;

            case DIR_CW:
                encoders[e].bottom_encoder_pos++;
                break;

            case DIR_CCW:
                encoders[e].bottom_encoder_pos--;
                break;

            default:
                break;
            }

            //t_print("%s: %d BOTTOM pos=%d\n",__FUNCTION__,e,encoders[e].bottom_encoder_pos);
            break;

        case B:
            encoders[e].bottom_encoder_b_value = val;
            pinstate = (encoders[e].bottom_encoder_b_value << 1) | encoders[e].bottom_encoder_a_value;
            encoders[e].bottom_encoder_state = encoder_state_table[encoders[e].bottom_encoder_state & 0xf][pinstate];

            //t_print("%s: state=%02X\n",__FUNCTION__,encoders[e].bottom_encoder_state);
            switch (encoders[e].bottom_encoder_state & 0x30) {
            case DIR_NONE:
                break;

            case DIR_CW:
                encoders[e].bottom_encoder_pos++;
                break;

            case DIR_CCW:
                encoders[e].bottom_encoder_pos--;
                break;

            default:
                break;
            }

            //t_print("%s: %d BOTTOM pos=%d\n",__FUNCTION__,e,encoders[e].bottom_encoder_pos);
            break;
        }

        break;

    case TOP_ENCODER:
        switch (addr) {
        case A:
            encoders[e].top_encoder_a_value = val;
            pinstate = (encoders[e].top_encoder_b_value << 1) | encoders[e].top_encoder_a_value;
            encoders[e].top_encoder_state = encoder_state_table[encoders[e].top_encoder_state & 0xf][pinstate];

            //t_print("%s: state=%02X\n",__FUNCTION__,encoders[e].top_encoder_state);
            switch (encoders[e].top_encoder_state & 0x30) {
            case DIR_NONE:
                break;

            case DIR_CW:
                encoders[e].top_encoder_pos++;
                break;

            case DIR_CCW:
                encoders[e].top_encoder_pos--;
                break;

            default:
                break;
            }

            //t_print("%s: %d TOP pos=%d\n",__FUNCTION__,e,encoders[e].top_encoder_pos);
            break;

        case B:
            encoders[e].top_encoder_b_value = val;
            pinstate = (encoders[e].top_encoder_b_value << 1) | encoders[e].top_encoder_a_value;
            encoders[e].top_encoder_state = encoder_state_table[encoders[e].top_encoder_state & 0xf][pinstate];

            //t_print("%s: state=%02X\n",__FUNCTION__,encoders[e].top_encoder_state);
            switch (encoders[e].top_encoder_state & 0x30) {
            case DIR_NONE:
                break;

            case DIR_CW:
                encoders[e].top_encoder_pos++;
                break;

            case DIR_CCW:
                encoders[e].top_encoder_pos--;
                break;

            default:
                break;
            }

            //t_print("%s: %d TOP pos=%d\n",__FUNCTION__,e,encoders[e].top_encoder_pos);
            break;
        }

        break;
    }

    g_mutex_unlock(&encoder_mutex);
}

static void process_edge(GPIOPin pin, int value) {
    int i;
    unsigned int t;
    gboolean found;
    int offset = pin.line;
    int chip_index = pin.chip_index;

    found = FALSE;

    //t_print("%s: searching for what to do with pin %i on chip %i\n", __FUNCTION__, offset, chip_index);

    // Priority 1 (highest): check encoder
    for (i = 0; i < MAX_ENCODERS; i++) {
        // Checking bottom encoder pins based on chip index
        if (encoders[i].bottom_encoder_enabled) {
            //t_print("%s: bottom encoder is enabled\n", __FUNCTION__);
            if (encoders[i].bottom_encoder_chip_a == chip_index && encoders[i].bottom_encoder_address_a == offset) {
                //t_print("%s: found %d encoder %d bottom A\n",__FUNCTION__,offset,i);
                process_encoder(i, BOTTOM_ENCODER, A, SET(value == ACTION_PRESSED));
                found = TRUE;
                break;
            }
            if (encoders[i].bottom_encoder_chip_b == chip_index && encoders[i].bottom_encoder_address_b == offset) {
                //t_print("%s: found %d encoder %d bottom B\n",__FUNCTION__,offset,i);
                process_encoder(i, BOTTOM_ENCODER, B, SET(value == ACTION_PRESSED));
                found = TRUE;
                break;
            }
        }
        // Checking top encoder pins based on chip index
        if (encoders[i].top_encoder_enabled) {
            //t_print("%s: top encoder is enabled\n", __FUNCTION__);
            if (encoders[i].top_encoder_chip_a == chip_index && encoders[i].top_encoder_address_a == offset) {
                //t_print("%s: found %d encoder %d top A\n", __FUNCTION__, offset, i);
                process_encoder(i, TOP_ENCODER, A, SET(value == ACTION_PRESSED));
                found = TRUE;
                break;
            }
            if (encoders[i].top_encoder_chip_b == chip_index && encoders[i].top_encoder_address_b == offset) {
                //t_print("%s: found %d encoder %d top B\n", __FUNCTION__, offset, i);
                process_encoder(i, TOP_ENCODER, B, SET(value == ACTION_PRESSED));
                found = TRUE;
                break;
            }
        }
        // Checking switch based on chip index
        if (encoders[i].switch_enabled && encoders[i].switch_chip == chip_index && encoders[i].switch_address == offset) {
            t = millis();
            if (t < encoders[i].switch_debounce) {
                return;
            }
            encoders[i].switch_debounce = t + settle_time;
            schedule_action(encoders[i].switch_function, value, 0);
            found = TRUE;
            break;
        }
    }

    if (found) {
        return;
    }

    // Priority 2: check "non-controller" inputs
    // take care for "external" debouncing!
    if (offset == CWL_LINE.line && chip_index == CWL_LINE.chip_index) {
        schedule_action(CW_LEFT, value, 0);
        found = TRUE;
    }

    if (offset == CWR_LINE.line && chip_index == CWR_LINE.chip_index) {
        schedule_action(CW_RIGHT, value, 0);
        found = TRUE;
    }

    if (offset == CWKEY_LINE.line && chip_index == CWKEY_LINE.chip_index) {
        schedule_action(CW_KEYER_KEYDOWN, value, 0);
        found = TRUE;
    }

    if (offset == PTTIN_LINE.line && chip_index == PTTIN_LINE.chip_index) {
        schedule_action(CW_KEYER_PTT, value, 0);
        found = TRUE;
    }

    if (found) {
        return;
    }

    // Priority 3: handle i2c interrupt and i2c switches
    if (controller == CONTROLLER2_V1 || controller == CONTROLLER2_V2 || controller == G2_FRONTPANEL) {
        if (I2C_INTERRUPT.line == offset && chip_index == I2C_INTERRUPT.chip_index) {
            if (value == ACTION_PRESSED) {
                i2c_interrupt();
            }
            found = TRUE;
        }
    }

    if (found) {
        return;
    }

    /*for (int i = 0; i < MAX_SWITCHES; i++) {
        t_print("debug: our switches: Switch %d: enabled=%d, chip=%d, address=%d, function=%i\n", i, switches[i].switch_enabled, switches[i].switch_chip, switches[i].switch_address, switches[i].switch_function);
    }*/

    // Priority 4: handle "normal" (non-I2C) switches
    for (i = 0; i < MAX_SWITCHES; i++) {
        if (switches[i].switch_enabled && switches[i].switch_address == offset && switches[i].switch_chip == chip_index) {
            t = millis();
            found = TRUE;

            if (t < switches[i].switch_debounce) {
                t_print("%s: found normal switch for pin %i on chip %i - debounced\n", __FUNCTION__, offset, chip_index);
                return;
            }

            switches[i].switch_debounce = t + settle_time;
            schedule_action(switches[i].switch_function, value, 0);
            t_print("%s: found normal switch for pin %i on chip %i, switch %i, action %i\n", __FUNCTION__, offset, chip_index, i, switches[i].switch_function);
            break;
        }
    }

    if (found) {
        return;
    }

    t_print("%s: could not find action for pin %i on chip %i\n", __FUNCTION__, offset, chip_index);
}





#endif

void gpio_default_encoder_actions(int ctrlr) {
    const ENCODER* default_encoders;

    switch (ctrlr) {
    case NO_CONTROLLER:
    default:
        default_encoders = NULL;
        break;

    case CONTROLLER1:
        default_encoders = encoders_controller1;
        break;

    case CONTROLLER2_V1:
        default_encoders = encoders_controller2_v1;
        break;

    case CONTROLLER2_V2:
        default_encoders = encoders_controller2_v2;
        break;

    case G2_FRONTPANEL:
        default_encoders = encoders_g2_frontpanel;
        break;

    case MARKS_CONTROLLER:
        default_encoders = encoders_marks_controller;
        break;
    }
 	

    if (default_encoders) {
        //
        // Copy (only) actions
        //
        for (int i = 0; i < MAX_ENCODERS; i++) {
            my_encoders[i].bottom_encoder_function = default_encoders[i].bottom_encoder_function;
            my_encoders[i].top_encoder_function = default_encoders[i].top_encoder_function;
            my_encoders[i].switch_function = default_encoders[i].switch_function;
        }
    }
}

void gpio_default_switch_actions(int ctrlr) {
    const SWITCH* default_switches;

    t_print("%s:\n", __FUNCTION__);

    switch (ctrlr) {
    case NO_CONTROLLER:
    case CONTROLLER1:
    default:
        default_switches = NULL;
        break;

    case CONTROLLER2_V1:
        default_switches = switches_controller2_v1;
        break;

    case CONTROLLER2_V2:
        default_switches = switches_controller2_v2;
        break;

    case G2_FRONTPANEL:
        default_switches = switches_g2_frontpanel;
        break;

    case MARKS_CONTROLLER:
        default_switches = switches_marks_controller;
        break;
    }


    if (default_switches) {
        //
        // Copy (only) actions
        //
        for (int i = 0; i < MAX_SWITCHES; i++) {
            t_print("%s: copying action %i on switch %i\n", __FUNCTION__, default_switches[i].switch_function, i);
            my_switches[i].switch_function = default_switches[i].switch_function;
        }
    }

    /*for (int i = 0; i < MAX_SWITCHES; i++) {
        t_print("%s debug: our switches: Switch %d: enabled=%d, chip=%d, address=%d, function=%i\n", __FUNCTION__, i, switches[i].switch_enabled, switches[i].switch_chip, switches[i].switch_address, switches[i].switch_function);
    }*/

}

//
// If there is non-standard hardware at the GPIO lines
// the code below in the NO_CONTROLLER section must
// be adjusted such that "occupied" GPIO lines are not
// used for CW or PTT.
// For CONTROLLER1 and CONTROLLER2_V1, GPIO
// lines 9,10,11,14 are "free" and can be
// used for CW and PTT.
//
//  At this place, copy complete data structures to my_encoders
//  and my_switches, including GPIO lines etc.
//

volatile int gpiodone = 0;

void gpio_set_defaults(int ctrlr) {

    if (gpiodone)
    {
        t_print("%s: we already did this....\n", __FUNCTION__);
        //return;
    }

    t_print("%s: %d\n", __FUNCTION__, ctrlr);

    switch (ctrlr) {
    case CONTROLLER1:
        // GPIO lines not used by controller: 9, 10, 11, 14, 15
        CWL_LINE = (GPIOPin){ .chip_index = 0, .line = 9 }; // Update chip_index as necessary
        CWR_LINE = (GPIOPin){ .chip_index = 0, .line = 11 };
        CWKEY_LINE = (GPIOPin){ .chip_index = 0, .line = 10 };
        PTTIN_LINE = (GPIOPin){ .chip_index = 0, .line = 14 };
        PTTOUT_LINE = (GPIOPin){ .chip_index = 0, .line = 15 };
        CWOUT_LINE = (GPIOPin){ .chip_index = -1, .line = -1 }; // Not used
        memcpy(my_encoders, encoders_controller1, sizeof(my_encoders));
        encoders = my_encoders;
        switches = switches_controller1[0];
        break;

    case CONTROLLER2_V1:
        // GPIO lines not used by controller: 5, 6, 7, 9, 10, 11, 12, 13, 14
        CWL_LINE = (GPIOPin){ .chip_index = 0, .line = 9 };
        CWR_LINE = (GPIOPin){ .chip_index = 0, .line = 11 };
        CWKEY_LINE = (GPIOPin){ .chip_index = 0, .line = 10 };
        PTTIN_LINE = (GPIOPin){ .chip_index = 0, .line = 14 };
        PTTOUT_LINE = (GPIOPin){ .chip_index = 0, .line = 13 };
        CWOUT_LINE = (GPIOPin){ .chip_index = 0, .line = 12 };
        memcpy(my_encoders, encoders_controller2_v1, sizeof(my_encoders));
        memcpy(my_switches, switches_controller2_v1, sizeof(my_switches));
        encoders = my_encoders;
        switches = my_switches;
        break;

    case CONTROLLER2_V2:
        // GPIO lines not used by controller: 14. Assigned to PTTIN by default
        CWL_LINE = (GPIOPin){ .chip_index = -1, .line = -1 }; // Not used
        CWR_LINE = (GPIOPin){ .chip_index = -1, .line = -1 }; // Not used
        PTTIN_LINE = (GPIOPin){ .chip_index = 0, .line = 14 };
        CWKEY_LINE = (GPIOPin){ .chip_index = -1, .line = -1 }; // Not used
        PTTOUT_LINE = (GPIOPin){ .chip_index = -1, .line = -1 }; // Not used
        CWOUT_LINE = (GPIOPin){ .chip_index = -1, .line = -1 }; // Not used
        memcpy(my_encoders, encoders_controller2_v2, sizeof(my_encoders));
        memcpy(my_switches, switches_controller2_v2, sizeof(my_switches));
        encoders = my_encoders;
        switches = my_switches;
        break;

    case G2_FRONTPANEL:
        // Regard all GPIO lines as "used"
        CWL_LINE = (GPIOPin){ .chip_index = -1, .line = -1 }; // Not used
        CWR_LINE = (GPIOPin){ .chip_index = -1, .line = -1 }; // Not used
        PTTIN_LINE = (GPIOPin){ .chip_index = -1, .line = -1 }; // Not used
        CWKEY_LINE = (GPIOPin){ .chip_index = -1, .line = -1 }; // Not used
        PTTOUT_LINE = (GPIOPin){ .chip_index = -1, .line = -1 }; // Not used
        CWOUT_LINE = (GPIOPin){ .chip_index = -1, .line = -1 }; // Not used
        memcpy(my_encoders, encoders_g2_frontpanel, sizeof(my_encoders));
        memcpy(my_switches, switches_g2_frontpanel, sizeof(my_switches));
        encoders = my_encoders;
        switches = my_switches;
        break;

    case MARKS_CONTROLLER:
        t_print("%s: defaults: marks controller\n", __FUNCTION__);
        CWL_LINE = (GPIOPin){ .chip_index = -1, .line = -1 }; // Not used
        CWR_LINE = (GPIOPin){ .chip_index = -1, .line = -1 }; // Not used
        PTTIN_LINE = (GPIOPin){ .chip_index = -1, .line = -1 }; // Not used
        CWKEY_LINE = (GPIOPin){ .chip_index = -1, .line = -1 }; // Not used
        PTTOUT_LINE = (GPIOPin){ .chip_index = -1, .line = -1 }; // Not used
        CWOUT_LINE = (GPIOPin){ .chip_index = -1, .line = -1 }; // Not used
        // Print before memcpy
        /*for (int i = 0; i < MAX_ENCODERS; i++) {
            t_print("Before memcpy: Encoder %d top_enabled=%d, bottom_enabled=%d, top func=%i, bot func=%i\n", i, encoders_marks_controller[i].top_encoder_enabled, encoders_marks_controller[i].bottom_encoder_enabled, encoders_marks_controller[i].top_encoder_function, encoders_marks_controller[i].bottom_encoder_function);
        }*/
        memcpy(my_encoders, encoders_marks_controller, sizeof(my_encoders));
        memcpy(my_switches, switches_marks_controller, sizeof(my_switches));
        encoders = my_encoders;
        switches = my_switches;
        // Print after memcpy
        /*for (int i = 0; i < MAX_ENCODERS; i++) {
            t_print("after memcpy: Encoder %d top_enabled=%d, bottom_enabled=%d, top func=%i, bot func=%i\n", i, encoders_marks_controller[i].top_encoder_enabled, encoders_marks_controller[i].bottom_encoder_enabled, encoders_marks_controller[i].top_encoder_function, encoders_marks_controller[i].bottom_encoder_function);
        }*/
        /*for (int i = 0; i < MAX_SWITCHES; i++) {
            t_print("Switch %d: enabled=%d, chip=%d, address=%d, function=%i\n", i, switches[i].switch_enabled, switches[i].switch_chip, switches[i].switch_address, switches[i].switch_function);
        }*/

        break;

    case NO_CONTROLLER:
    default:
	t_print("%s: NO_CONTROLLER/default\n", __FUNCTION__);
        // GPIO lines that are not used elsewhere: 5, 6, 12, 16, 22, 23, 24, 25, 27
        CWL_LINE = (GPIOPin){ .chip_index = 0, .line = 5 };
        CWR_LINE = (GPIOPin){ .chip_index = 0, .line = 6 };
        CWKEY_LINE = (GPIOPin){ .chip_index = 0, .line = 12 };
        PTTIN_LINE = (GPIOPin){ .chip_index = 0, .line = 16 };
        PTTOUT_LINE = (GPIOPin){ .chip_index = 0, .line = 22 };
        CWOUT_LINE = (GPIOPin){ .chip_index = 0, .line = 23 };
        memcpy(my_encoders, encoders_no_controller, sizeof(my_encoders));
        memcpy(my_switches, switches_no_controller, sizeof(my_switches));
        encoders = my_encoders;
        switches = my_switches;
        break;
    }

    gpiodone = 1;

}


void gpioRestoreState() {
    loadProperties("gpio.props");
    GetPropI0("controller", controller);
    gpio_set_defaults(controller);

    for (int i = 0; i < MAX_ENCODERS; i++) {
        GetPropI1("encoders[%d].bottom_encoder_enabled", i, encoders[i].bottom_encoder_enabled);
        GetPropI1("encoders[%d].bottom_encoder_pullup", i, encoders[i].bottom_encoder_pullup);
        GetPropI1("encoders[%d].bottom_encoder_chip_a", i, encoders[i].bottom_encoder_chip_a);
        GetPropI1("encoders[%d].bottom_encoder_address_a", i, encoders[i].bottom_encoder_address_a);
        GetPropI1("encoders[%d].bottom_encoder_chip_b", i, encoders[i].bottom_encoder_chip_b);
        GetPropI1("encoders[%d].bottom_encoder_address_b", i, encoders[i].bottom_encoder_address_b);
        GetPropI1("encoders[%d].top_encoder_enabled", i, encoders[i].top_encoder_enabled);
        GetPropI1("encoders[%d].top_encoder_pullup", i, encoders[i].top_encoder_pullup);
        GetPropI1("encoders[%d].top_encoder_chip_a", i, encoders[i].top_encoder_chip_a);
        GetPropI1("encoders[%d].top_encoder_address_a", i, encoders[i].top_encoder_address_a);
        GetPropI1("encoders[%d].top_encoder_chip_b", i, encoders[i].top_encoder_chip_b);
        GetPropI1("encoders[%d].top_encoder_address_b", i, encoders[i].top_encoder_address_b);
        GetPropI1("encoders[%d].switch_enabled", i, encoders[i].switch_enabled);
        GetPropI1("encoders[%d].switch_pullup", i, encoders[i].switch_pullup);
        GetPropI1("encoders[%d].switch_chip", i, encoders[i].switch_chip);
        GetPropI1("encoders[%d].switch_address", i, encoders[i].switch_address);
    }

    for (int f = 0; f < MAX_FUNCTIONS; f++) {
        for (int i = 0; i < MAX_SWITCHES; i++) {
            GetPropI2("switches[%d,%d].switch_enabled", f, i, switches_controller1[f][i].switch_enabled);
            GetPropI2("switches[%d,%d].switch_pullup", f, i, switches_controller1[f][i].switch_pullup);
            GetPropI2("switches[%d,%d].switch_chip", f, i, switches_controller1[f][i].switch_chip);
            GetPropI2("switches[%d,%d].switch_address", f, i, switches_controller1[f][i].switch_address);
        }
    }

    if (controller != CONTROLLER1) {
        for (int i = 0; i < MAX_SWITCHES; i++) {
            GetPropI1("switches[%d].switch_enabled", i, switches[i].switch_enabled);
            GetPropI1("switches[%d].switch_pullup", i, switches[i].switch_pullup);
            GetPropI1("switches[%d].switch_chip", i, switches[i].switch_chip);
            GetPropI1("switches[%d].switch_address", i, switches[i].switch_address);
        }
    }
}

void gpioSaveState() {
    clearProperties();
    SetPropI0("controller", controller);

    for (int i = 0; i < MAX_ENCODERS; i++) {
        SetPropI1("encoders[%d].bottom_encoder_enabled", i, encoders[i].bottom_encoder_enabled);
        SetPropI1("encoders[%d].bottom_encoder_pullup", i, encoders[i].bottom_encoder_pullup);
        SetPropI1("encoders[%d].bottom_encoder_chip_a", i, encoders[i].bottom_encoder_chip_a);
        SetPropI1("encoders[%d].bottom_encoder_address_a", i, encoders[i].bottom_encoder_address_a);
        SetPropI1("encoders[%d].bottom_encoder_chip_b", i, encoders[i].bottom_encoder_chip_b);
        SetPropI1("encoders[%d].bottom_encoder_address_b", i, encoders[i].bottom_encoder_address_b);
        SetPropI1("encoders[%d].top_encoder_enabled", i, encoders[i].top_encoder_enabled);
        SetPropI1("encoders[%d].top_encoder_pullup", i, encoders[i].top_encoder_pullup);
        SetPropI1("encoders[%d].top_encoder_chip_a", i, encoders[i].top_encoder_chip_a);
        SetPropI1("encoders[%d].top_encoder_address_a", i, encoders[i].top_encoder_address_a);
        SetPropI1("encoders[%d].top_encoder_chip_b", i, encoders[i].top_encoder_chip_b);
        SetPropI1("encoders[%d].top_encoder_address_b", i, encoders[i].top_encoder_address_b);
        SetPropI1("encoders[%d].switch_enabled", i, encoders[i].switch_enabled);
        SetPropI1("encoders[%d].switch_pullup", i, encoders[i].switch_pullup);
        SetPropI1("encoders[%d].switch_chip", i, encoders[i].switch_chip);
        SetPropI1("encoders[%d].switch_address", i, encoders[i].switch_address);
    }

    for (int f = 0; f < MAX_FUNCTIONS; f++) {
        for (int i = 0; i < MAX_SWITCHES; i++) {
            SetPropI2("switches[%d,%d].switch_enabled", f, i, switches_controller1[f][i].switch_enabled);
            SetPropI2("switches[%d,%d].switch_pullup", f, i, switches_controller1[f][i].switch_pullup);
            SetPropI2("switches[%d,%d].switch_chip", f, i, switches_controller1[f][i].switch_chip);
            SetPropI2("switches[%d,%d].switch_address", f, i, switches_controller1[f][i].switch_address);
        }
    }

    if (controller != CONTROLLER1) {
        for (int i = 0; i < MAX_SWITCHES; i++) {
            SetPropI1("switches[%d].switch_enabled", i, switches[i].switch_enabled);
            SetPropI1("switches[%d].switch_pullup", i, switches[i].switch_pullup);
            SetPropI1("switches[%d].switch_chip", i, switches[i].switch_chip);
            SetPropI1("switches[%d].switch_address", i, switches[i].switch_address);
        }
    }

    saveProperties("gpio.props");
}

void gpioRestoreActions() {
    int props_controller = NO_CONTROLLER;
    gpio_set_defaults(controller);

    /*for (int i = 0; i < MAX_SWITCHES; i++) {
        t_print("%s 1 - debug: our switches: Switch %d: enabled=%d, chip=%d, address=%d, function=%i\n", __FUNCTION__, i, switches[i].switch_enabled, switches[i].switch_chip, switches[i].switch_address, switches[i].switch_function);
    }*/


    //
    //  "toolbar" functions
    //
    GetPropI0("switches.function", function);

    for (int f = 0; f < MAX_FUNCTIONS; f++) {
        for (int i = 0; i < MAX_SWITCHES; i++) {
            GetPropA2("switches[%d,%d].switch_function", f, i, switches_controller1[f][i].switch_function);
        }
    }

    GetPropI0("controller", props_controller);

    //
    // If the props file refers to another controller, skip props data
    //
    if (controller != props_controller) { return; }

    for (int i = 0; i < MAX_ENCODERS; i++) {
        GetPropA1("encoders[%d].bottom_encoder_function", i, encoders[i].bottom_encoder_function);
        GetPropA1("encoders[%d].top_encoder_function", i, encoders[i].top_encoder_function);
        GetPropA1("encoders[%d].switch_function", i, encoders[i].switch_function);
    }

    if (controller != CONTROLLER1) {
        for (int i = 0; i < MAX_SWITCHES; i++) {
            GetPropA1("switches[%d].switch_function", i, switches[i].switch_function);
        }
    }

    /*for (int i = 0; i < MAX_SWITCHES; i++) {
        t_print("%s 2 - debug: our switches: Switch %d: enabled=%d, chip=%d, address=%d, function=%i\n", __FUNCTION__, i, switches[i].switch_enabled, switches[i].switch_chip, switches[i].switch_address, switches[i].switch_function);
    }*/

}

void gpioSaveActions() {
    char name[128];
    char value[128];
    //
    //  "toolbar" functions
    //
    SetPropI0("switches.function", function);

    for (int f = 0; f < MAX_FUNCTIONS; f++) {
        for (int i = 0; i < MAX_SWITCHES; i++) {
            SetPropA2("switches[%d,%d].switch_function", f, i, switches_controller1[f][i].switch_function);
        }
    }

    SetPropI0("controller", controller);

    //
    // If there is no controller, there is nothing to store
    //
    if (controller == NO_CONTROLLER) { return; }

    for (int i = 0; i < MAX_ENCODERS; i++) {
        SetPropA1("encoders[%d].bottom_encoder_function", i, encoders[i].bottom_encoder_function);
        SetPropA1("encoders[%d].top_encoder_function", i, encoders[i].top_encoder_function);
        SetPropA1("encoders[%d].switch_function", i, encoders[i].switch_function);
    }

    if (controller != CONTROLLER1) {
        for (int i = 0; i < MAX_SWITCHES; i++) {
            SetPropA1("switches[%d].switch_function", i, switches[i].switch_function);
        }
    }

    snprintf(value, sizeof(value), "%d", function);
    setProperty("switches.function", value);

    for (int f = 0; f < MAX_FUNCTIONS; f++) {
        for (int i = 0; i < MAX_SWITCHES; i++) {
            snprintf(name, sizeof(name), "switches[%d,%d].switch_function", f, i);
            Action2String(switches_controller1[f][i].switch_function, value, sizeof(value));
            setProperty(name, value);
        }
    }

    if (controller == CONTROLLER2_V1 || controller == CONTROLLER2_V2 || controller == G2_FRONTPANEL || controller == MARKS_CONTROLLER) {
        for (int i = 0; i < MAX_SWITCHES; i++) {
            snprintf(name, sizeof(name), "switches[%d].switch_function", i);
            Action2String(switches[i].switch_function, value, sizeof(value));
            setProperty(name, value);
        }
    }
}

#ifdef GPIO

static int interrupt_cb(int event_type, unsigned int line, const struct timespec* timeout, void* data) {
    int chip_index = GPOINTER_TO_INT(data);
    //t_print("%s: chip_index from context: %d\n", __FUNCTION__, chip_index);

    if (chip_index < 0 || chip_index >= num_gpio_chips) {
        t_print("%s: Invalid chip index %d\n", __FUNCTION__, chip_index);
        return GPIOD_CTXLESS_EVENT_CB_RET_ERR;
    }

    GPIOPin pin;
    pin.chip_index = chip_index;
    pin.line = line;

    // Ensure the line belongs to the correct chip
    int line_found = 0;
    for (int j = 0; j < gpio_chips[chip_index].num_enabled_pins; j++) {
        if (gpio_chips[chip_index].enabled_pins[j] == line) {
            line_found = 1;
            break;
        }
    }

    if (!line_found) {
        t_print("%s: Line %d does not belong to chip %d\n", __FUNCTION__, line, chip_index);
        return GPIOD_CTXLESS_EVENT_CB_RET_ERR;
    }

    switch (event_type) {
    case GPIOD_CTXLESS_EVENT_CB_TIMEOUT:
        // Timeout - ignore
        break;

    case GPIOD_CTXLESS_EVENT_CB_RISING_EDGE:
        process_edge(pin, ACTION_RELEASED);
        break;

    case GPIOD_CTXLESS_EVENT_CB_FALLING_EDGE:
        process_edge(pin, ACTION_PRESSED);
        break;

    default:
        t_print("%s: Unknown event type %d on line %d\n", __FUNCTION__, event_type, line);
        break;
    }

    return GPIOD_CTXLESS_EVENT_CB_RET_OK;
}



static gpointer monitor_thread(gpointer arg) {
    int chip_index = GPOINTER_TO_INT(arg);

    if (chip_monitor_lines[chip_index].num_lines > 0) {
        t_print("%s: monitoring chip %d with %d lines.\n", __FUNCTION__, chip_index, chip_monitor_lines[chip_index].num_lines);

        int ret = gpiod_ctxless_event_monitor_multiple(
            gpio_chips[chip_index].device, GPIOD_CTXLESS_EVENT_BOTH_EDGES,
            (unsigned int*)chip_monitor_lines[chip_index].lines, chip_monitor_lines[chip_index].num_lines, FALSE,
            consumer, NULL, NULL, interrupt_cb, GINT_TO_POINTER(chip_index));

        if (ret < 0) {
            t_print("%s: ctxless event monitor failed for chip %d: %s\n", __FUNCTION__, chip_index, g_strerror(errno));
        }
    } else {
        t_print("%s: no lines to monitor for chip %d\n", __FUNCTION__, chip_index);
    }

    t_print("%s: exit\n", __FUNCTION__);
    return NULL;
}



static int setup_input_line(int chip_index, int offset, gboolean pullup) {
    if (chip_index < 0 || chip_index >= num_gpio_chips) {
        t_print("%s: invalid chip index %d\n", __FUNCTION__, chip_index);
        return -1; // Invalid chip index
    }

    struct gpiod_chip* chip = gpio_chips[chip_index].chip;
    struct gpiod_line_request_config config;
    t_print("%s: chip index %d, offset %d\n", __FUNCTION__, chip_index, offset);

    struct gpiod_line* line = gpiod_chip_get_line(chip, offset);
    if (!line) {
        t_print("%s: get line %d failed: %s\n", __FUNCTION__, offset, g_strerror(errno));
        return -1;
    }

    config.consumer = consumer;
    config.request_type = GPIOD_LINE_REQUEST_DIRECTION_INPUT | GPIOD_LINE_REQUEST_EVENT_BOTH_EDGES;
#ifdef OLD_GPIOD
    config.flags = pullup ? GPIOD_LINE_REQUEST_FLAG_ACTIVE_LOW : 0;
#else
    config.flags = pullup ? GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_UP : GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_DOWN;
#endif

    t_print("%s: pullup: %i\n", __FUNCTION__, pullup);

    int ret = gpiod_line_request(line, &config, 1);
    if (ret < 0) {
        t_print("%s: line %d gpiod_line_request failed: %s\n", __FUNCTION__, offset, g_strerror(errno));
        return ret;
    }

    gpiod_line_release(line);  // release line since the event monitor will request it later

    // Track the enabled pin
    gpio_chips[chip_index].enabled_pins[gpio_chips[chip_index].num_enabled_pins++] = offset;

    t_print("%s: tracking chip %i pin %i\n", __FUNCTION__, chip_index, offset);

    // Add to monitor lines
    monitor_lines[lines] = offset;
    lines++;
    return 0;
}


static struct gpiod_line* setup_output_line(int chip_index, int offset, int initialValue) {
    if (chip_index < 0 || chip_index >= num_gpio_chips) {
        t_print("%s: invalid chip index %d\n", __FUNCTION__, chip_index);
        return NULL; // Invalid chip index
    }

    struct gpiod_chip* chip = gpio_chips[chip_index].chip;
    struct gpiod_line_request_config config;
    t_print("%s: chip index %d, offset %d\n", __FUNCTION__, chip_index, offset);

    struct gpiod_line* line = gpiod_chip_get_line(chip, offset);
    if (!line) {
        t_print("%s: get_line failed for offset %d: %s\n", __FUNCTION__, offset, g_strerror(errno));
        return NULL;
    }

    config.consumer = consumer;
    config.request_type = GPIOD_LINE_REQUEST_DIRECTION_OUTPUT;
    config.flags = 0;  // active High

    if (gpiod_line_request(line, &config, initialValue) < 0) {
        t_print("%s: line_request failed for offset %d: %s\n", __FUNCTION__, offset, g_strerror(errno));
        gpiod_line_release(line);
        return NULL;
    }

    // Track the enabled pin
    gpio_chips[chip_index].enabled_pins[gpio_chips[chip_index].num_enabled_pins++] = offset;

    return line;
}

#endif

int gpio_init() {
#ifdef GPIO
    int ret = 0;
    initialiseEpoch();
    g_mutex_init(&encoder_mutex);
    gpio_set_defaults(controller);

    num_gpio_chips = 0;

    // Try to open multiple GPIO devices
    char* gpio_devices[] = { "/dev/gpiochip1", "/dev/gpiochip3" };
    int gpio_device_count = sizeof(gpio_devices) / sizeof(gpio_devices[0]);

    for (int i = 0; i < gpio_device_count && num_gpio_chips < MAX_GPIO_CHIPS; i++) {
        struct gpiod_chip* chip = gpiod_chip_open(gpio_devices[i]);
        if (chip != NULL) {
            gpio_chips[num_gpio_chips].chip = chip;
            gpio_chips[num_gpio_chips].device = gpio_devices[i];
            gpio_chips[num_gpio_chips].num_enabled_pins = 0; // Initialize the enabled pins count
            num_gpio_chips++;
        }
    }

    if (num_gpio_chips == 0) {
        t_print("%s: open chip failed: %s\n", __FUNCTION__, g_strerror(errno));
        ret = -1;
        goto err;
    }

    t_print("%s: GPIO devices opened\n", __FUNCTION__);
    for (int i = 0; i < num_gpio_chips; i++) {
        t_print("%s: GPIO device=%s\n", __FUNCTION__, gpio_chips[i].device);
    }

    // Initialize chip_monitor_lines
    for (int i = 0; i < num_gpio_chips; i++) {
        chip_monitor_lines[i].chip_index = i;
        chip_monitor_lines[i].num_lines = 0;
    }

    // Setup encoders and switches with multiple chips
    if (controller == CONTROLLER1 || controller == CONTROLLER2_V1 || controller == CONTROLLER2_V2 || controller == G2_FRONTPANEL || controller == MARKS_CONTROLLER) {
        // Setup encoders
        if (encoders != NULL) {
            t_print("%s: setup encoders\n", __FUNCTION__);
            for (int i = 0; i < MAX_ENCODERS; i++) {
                if (encoders[i].bottom_encoder_enabled) {
                    t_print("%s: setup encoders, bottom enabled on %i\n", __FUNCTION__, i);
                    if (setup_input_line(encoders[i].bottom_encoder_chip_a, encoders[i].bottom_encoder_address_a, encoders[i].bottom_encoder_pullup) < 0) continue;
                    if (setup_input_line(encoders[i].bottom_encoder_chip_b, encoders[i].bottom_encoder_address_b, encoders[i].bottom_encoder_pullup) < 0) continue;
                }
                if (encoders[i].top_encoder_enabled) {
                    t_print("%s: setup encoders, top enabled on %i\n", __FUNCTION__, i);
                    if (setup_input_line(encoders[i].top_encoder_chip_a, encoders[i].top_encoder_address_a, encoders[i].top_encoder_pullup) < 0) continue;
                    if (setup_input_line(encoders[i].top_encoder_chip_b, encoders[i].top_encoder_address_b, encoders[i].top_encoder_pullup) < 0) continue;
                }
                if (encoders[i].switch_enabled) {
                    t_print("%s: setup encoders, switch enabled on %i\n", __FUNCTION__, i);
                    if (setup_input_line(encoders[i].switch_chip, encoders[i].switch_address, encoders[i].switch_pullup) < 0) continue;
                }
            }
        } else {
            t_print("%s: setup encoders: encoders NULL\n", __FUNCTION__);
        }

        // Setup switches
        t_print("%s: setup switches\n", __FUNCTION__);
        for (int i = 0; i < MAX_SWITCHES; i++) {
            if (switches[i].switch_enabled) {
                t_print("%s - 4: Switch %d: enabled=%d, chip=%d, address=%d, action=%i\n", __FUNCTION__, i, switches[i].switch_enabled, switches[i].switch_chip, switches[i].switch_address, switches[i].switch_function);
                if (setup_input_line(switches[i].switch_chip, switches[i].switch_address, switches[i].switch_pullup) < 0) continue;
            }
        }
    }

    if (controller == CONTROLLER2_V1 || controller == CONTROLLER2_V2 || controller == G2_FRONTPANEL) {
        i2c_init();
        t_print("%s: setup i2c interrupt %d\n", __FUNCTION__, I2C_INTERRUPT.line);
        if ((ret = setup_input_line(I2C_INTERRUPT.chip_index, I2C_INTERRUPT.line, TRUE)) < 0) goto err;
    }

    int have_button = 0;
    if (CWL_LINE.line >= 0) {
        if (setup_input_line(CWL_LINE.chip_index, CWL_LINE.line, TRUE) < 0) {
            t_print("%s: CWL line setup failed\n", __FUNCTION__);
        } else {
            have_button = 1;
        }
    }
    if (CWR_LINE.line >= 0) {
        if (setup_input_line(CWR_LINE.chip_index, CWR_LINE.line, TRUE) < 0) {
            t_print("%s: CWR line setup failed\n", __FUNCTION__);
        } else {
            have_button = 1;
        }
    }
    if (CWKEY_LINE.line >= 0) {
        if (setup_input_line(CWKEY_LINE.chip_index, CWKEY_LINE.line, TRUE) < 0) {
            t_print("%s: CWKEY line setup failed\n", __FUNCTION__);
        } else {
            have_button = 1;
        }
    }
    if (PTTIN_LINE.line >= 0) {
        if (setup_input_line(PTTIN_LINE.chip_index, PTTIN_LINE.line, TRUE) < 0) {
            t_print("%s: PTTIN line setup failed\n", __FUNCTION__);
        } else {
            have_button = 1;
        }
    }
    if (PTTOUT_LINE.line >= 0) {
        pttout_line = setup_output_line(PTTOUT_LINE.chip_index, PTTOUT_LINE.line, 1);
    }
    if (CWOUT_LINE.line >= 0) {
        cwout_line = setup_output_line(CWOUT_LINE.chip_index, CWOUT_LINE.line, 1);
    }

    if (have_button || controller != NO_CONTROLLER) {
        t_print("%s: monitoring %d lines on %i chips\n", __FUNCTION__, lines, num_gpio_chips);
        for (int i = 0; i < lines; i++) {
            t_print("... monitoring line %u\n", monitor_lines[i]);
        }

        // Initialize chip_monitor_lines
        for (int i = 0; i < num_gpio_chips; i++) {
            chip_monitor_lines[i].num_lines = 0;
        }

        // Assign lines to chips
        for (int chip_index = 0; chip_index < num_gpio_chips; chip_index++) {
            for (int line_index = 0; line_index < gpio_chips[chip_index].num_enabled_pins; line_index++) {
                int offset = gpio_chips[chip_index].enabled_pins[line_index];
                chip_monitor_lines[chip_index].lines[chip_monitor_lines[chip_index].num_lines++] = offset;
                t_print("%s: Assigning line %d to chip %d\n", __FUNCTION__, offset, chip_index);
            }
        }

        // Create a thread for each chip
        for (int i = 0; i < num_gpio_chips; i++) {
            if (chip_monitor_lines[i].num_lines > 0) {
                GThread* monitor_thread_id = g_thread_new("gpiod monitor", monitor_thread, GINT_TO_POINTER(i));
                t_print("%s: monitor_thread: id=%p for chip %d\n", __FUNCTION__, monitor_thread_id, i);
            } else {
                t_print("%s: no lines to monitor for chip %d\n", __FUNCTION__, i);
            }
        }
    }

    if (controller != NO_CONTROLLER) {
        rotary_encoder_thread_id = g_thread_new("encoders", rotary_encoder_thread, NULL);
        t_print("%s: rotary_encoder_thread: id=%p\n", __FUNCTION__, rotary_encoder_thread_id);
    }

#endif
    return 0;
#ifdef GPIO
err:
    for (int i = 0; i < num_gpio_chips; i++) {
        gpiod_chip_close(gpio_chips[i].chip);
    }
    num_gpio_chips = 0;
    return ret;
#endif
}


void gpio_close() {
#ifdef GPIO

    for (int i = 0; i < num_gpio_chips; i++) {
        if (gpio_chips[i].chip != NULL) {
            gpiod_chip_close(gpio_chips[i].chip);
            gpio_chips[i].chip = NULL; // Ensure we mark it as closed
        }
    }

    num_gpio_chips = 0; // Reset the count of GPIO chips

#endif
}

