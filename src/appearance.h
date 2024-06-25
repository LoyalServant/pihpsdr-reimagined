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
 * This file contains lots of #defines that control the appearance
 * of piHPSDR, e.g.
 * - window sizes
 * - font sizes
 * colours.
 *
 * The purpose of this file is that the appearance can be
 * changed easily at compile time.
 *
 * DO NOT CHANGE the "Default" values in the comments, since
 * these define the original look-and-feel of piHPSDR.
 *
 * IMPORTANT: think twice before adding new colours or font  sizes,
 *            and then decide to re-use an existing one.
 */

//
// Fonts and sizes for VFO, meter, panadapter etc.
//
#define DISPLAY_FONT "FreeSans"                     // Default: FreeSans
#define DISPLAY_FONT_SIZE1 10                       // Default: 10, used for small ticks in meter
#define DISPLAY_FONT_SIZE2 12                       // Default: 12, used for SWR, FWD in Tx meter, and panadapter labels
#define DISPLAY_FONT_SIZE3 16                       // Default: 16, used for warning/info in panadapters
#define DISPLAY_FONT_SIZE4 20                       // Default: 22, only used for server IP addr in client mode

//
// Colours. They are given as a 4-tuple (RGB and opacity).
// The default value for the opacity (1.0) is used  in most cases.
// "weak" versions of some colours (e.g. for the non-active receiver) are also available
//
// It is intended that these three colours are used in most of the cases.
//

//
// There are three "traffic light" colors ALARM, ATTN, OK (default: red, yellow, green)
// that are used in various places. All three colours should be clearly readable
// when written on a (usually dark) background.
//
#define COLOUR_ALARM         1.00, 0.00, 0.00, 1.00 // Default: 1.00, 0.00, 0.00, 1.00
#define COLOUR_ALARM_WEAK    0.50, 0.00, 0.00, 1.00 // Default: 0.50, 0.00, 0.00, 1.00
#define COLOUR_ATTN          1.00, 1.00, 0.00, 1.00 // Default: 1.00, 1.00, 0.00, 1.00
#define COLOUR_ATTN_WEAK     0.50, 0.50, 0.00, 1.00 // Default: 0.50, 0.50, 0.00, 1.00
#define COLOUR_OK            0.00, 1.00, 0.00, 1.00 // Default: 0.00, 1.00, 0.00, 1.00
#define COLOUR_OK_WEAK       0.00, 0.50, 0.00, 1.00 // Default: 0.00, 0.50, 0.00, 1.00

//
// Colours for drawing horizontal (dBm) and vertical (Frequency)
// lines in the panadapters, and indicating filter passbands and
// 60m band segments.
//
// The PAN_FILTER must be somewhat transparent, such that it does not hide a PAN_LINE.
//

#define COLOUR_PAN_FILTER    0.30, 0.30, 0.30, 0.66 // Default: 0.25, 0.25, 0.25, 0.75
#define COLOUR_PAN_LINE      0.00, 1.00, 1.00, 1.00 // Default: 0.00, 1.00, 1.00, 1.00
#define COLOUR_PAN_LINE_WEAK 0.00, 0.50, 0.50, 1.00 // Default: 0.00, 0.50, 0.50, 1.00
#define COLOUR_PAN_60M       0.60, 0.30, 0.30, 1.00 // Default: 0.60, 0.30, 0.30, 1.00

//
// Main background colours, allowing different colors for the panadapters and
// the VFO/meter bar.
// Writing with SHADE on a BACKGND should be visible,
// but need not be "alerting"
// METER is a special colour for data/ticks in the "meter" surface
//

#define COLOUR_PAN_BACKGND   0.15, 0.15, 0.15, 1.00 // Default: 0.00, 0.00, 0.00, 1.00
#define COLOUR_VFO_BACKGND   0.15, 0.15, 0.15, 1.00 // Default: 0.00, 0.00, 0.00, 1.00
#define COLOUR_SHADE         0.70, 0.70, 0.70, 1.00 // Default: 0.70, 0.70, 0.70, 1.00
#define COLOUR_METER         1.00, 1.00, 1.00, 1.00 // Default: 1.00, 1.00, 1.00, 1.00

//
// Settings for a coloured (gradient) spectrum, only availabe for RX.
// The first and last colour are also used for the digital S-meter bar graph
//

#define COLOUR_GRAD1         0.00, 1.00, 0.00, 1.00 // Default: 0.00, 1.00, 0.00, 1.0
#define COLOUR_GRAD2         1.00, 0.66, 0.00, 1.00 // Default: 1.00, 0.66, 0.00, 1.00
#define COLOUR_GRAD3         1.00, 1.00, 0.00, 1.00 // Default: 1.00, 1.00, 0.00, 1.00
#define COLOUR_GRAD4         1.00, 0.00, 0.00, 1.00 // Default: 1.00, 0.00, 0.00, 1.00
#define COLOUR_GRAD1_WEAK    0.00, 0.50, 0.00, 1.00 // Default: 0.00, 0.50, 0.00, 1.00
#define COLOUR_GRAD2_WEAK    0.50, 0.33, 0.00, 1.00 // Default: 0.50, 0.33, 0.00, 1.00
#define COLOUR_GRAD3_WEAK    0.50, 0.50, 0.00, 1.00 // Default: 0.50, 0.50, 0.00, 1.00
#define COLOUR_GRAD4_WEAK    0.50, 0.00, 0.00, 1.00 // Default: 0.50, 0.00, 0.00, 1.00

//
// Settings for a "black and white" spectrum (not the TX spectrum is always B&W).
//
// FILL1 is used for a filled spectrum of a non-active receiver
// FILL2 is used for a filled spectrum of an active receiver,
//           and for a line spectrum of a non-active receiver
// FILL3 is used for a line spectrum of an active receiver
//

#define COLOUR_PAN_FILL1     1.00, 1.00, 1.00, 0.25 // Default: 1.00, 1.00, 1.00, 0.25
#define COLOUR_PAN_FILL2     1.00, 1.00, 1.00, 0.50 // Default: 1.00, 1.00, 1.00, 0.50
#define COLOUR_PAN_FILL3     1.00, 1.00, 1.00, 0.75 // Default: 1.00, 1.00, 1.00, 0.75

//
// thin and thick line widths in the panadapers
// "thick" and "extra" also used in the analog meter
//
#define PAN_LINE_THIN  0.5
#define PAN_LINE_THICK 1.0
#define PAN_LINE_EXTRA 2.0  // used for really important things such as band edges

struct _VFO_BAR_LAYOUT {
  const char *description; // Text appearing in the screen menu combobox
  int width;               // overall width required
  int height;              // overall height required
  int size1;               // Font size for the "LED markers"
  int size2;               // Font size for the "small dial digits"
  int size3;               // Font size for the "large dial digits"

  int vfo_a_x, vfo_a_y;    // coordinates of VFO A/B dial
  int vfo_b_x, vfo_b_y;

  int mode_x,  mode_y;     // Mode/Filter/CW wpm string
  int zoom_x,  zoom_y;     // "Zoom x1"
  int ps_x,    ps_y;       // "PS"
  int rit_x,   rit_y ;     // "RIT -9999Hz"
  int xit_x,   xit_y;      // "XIT -9999Hz"
  int nb_x,    nb_y;       // NB/NB2
  int nr_x,    nr_y;
  int anf_x,   anf_y;
  int snb_x,   snb_y;
  int agc_x,   agc_y;      // "AGC slow"
  int cmpr_x,  cmpr_y;
  int eq_x,    eq_y;
  int div_x,   div_y;
  int step_x,  step_y;     // "Step 100 kHz"
  int ctun_x,  ctun_y;
  int cat_x,   cat_y;
  int vox_x,   vox_y;
  int lock_x,  lock_y;
  int split_x, split_y;
  int sat_x,   sat_y;
  int dup_x,   dup_y;
  int filter_x, filter_y;
  int multifn_x, multifn_y;
};

typedef struct _VFO_BAR_LAYOUT VFO_BAR_LAYOUT;
extern const VFO_BAR_LAYOUT vfo_layout_list[];
extern int vfo_layout;
