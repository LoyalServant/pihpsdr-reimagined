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

#ifndef _VFO_H
#define _VFO_H

#include "mode.h"

enum _vfo_enum {
  VFO_A = 0,
  VFO_B,
  MAX_VFOS
};

struct _vfo {
  //
  // Band data
  //
  int band;
  int bandstack;
  //
  // Frequency data,including CTUN, RIT, XIT
  //
  long long frequency;
  int ctun;
  long long ctun_frequency;

  int rit_enabled;
  long long rit;
  int xit_enabled;
  long long xit;

  long long lo;
  long long offset;

  //
  // Mode data and step size
  //
  int mode;
  int step;

  //
  // Filter data
  //
  int filter;
  int  deviation;         // only for FMN
  int cwAudioPeakFilter;  // only for CWU/CWL

  //
  // temporary storage for direct "NumPad" frequency entry
  //
  char entered_frequency[16];  // need 13, but rounded up to next multiple of 4

};

extern struct _vfo vfo[MAX_VFOS];

//
// Store settings on a per-mode basis for settings that
// are likely to be "bound" to a mode rather than to a band
// If a mode changes, these settings are restored to what
// has been effective the last time the "new" mode has
// been used.
//
//
struct _mode_settings {
  int filter;               // actual filter used
  int nb;                   // Noise blanker (0..2)
  int nr;                   // Noise reduction (0..2 or 0..4)
  int anf;                  // Automatic notch filter
  int snb;                  // Spectral noise blanker
  int agc;                  // AGC characteristics (slow/medium/fast etc.)
  int en_txeq;              // TX equalizer on/off
  int en_rxeq;              // RX equalizer on/off
  double tx_eq_freq[7];     // TX equalizer settings
  double tx_eq_gain[7];
  int    tx_eq_sixband;
  double rx_eq_freq[7];     // RX equalizer settings
  double rx_eq_gain[7];
  int    rx_eq_sixband;
  long long step;           // VFO step size
  int compressor;           // TX compressor on/off
  double compressor_level;  // TX compressor level
};

extern struct _mode_settings mode_settings[];

#define STEPS 17
extern char *step_labels[];

extern GtkWidget* vfo_init(int width, int height);
extern int  vfo_get_stepindex(int id);
extern void vfo_set_step_from_index(int id, int index);
extern int  vfo_get_step_from_index(int index);
extern void vfo_step(int steps);
extern void vfo_id_step(int id, int steps);
extern void vfo_move(long long hz, int round);
extern void vfo_id_move(int id, long long hz, int round);
extern void vfo_id_move_to(int id, long long hz);
extern void vfo_move_to(long long hz);
extern void vfo_update(void);

extern void vfoSaveState(void);
extern void vfoRestoreState(void);

extern void vfo_band_changed(int id, int b);
extern void vfo_bandstack_changed(int b);
extern void vfo_id_mode_changed(int id, int m);
extern void vfo_mode_changed(int m);
extern void vfo_id_filter_changed(int id, int f);
extern void vfo_filter_changed(int f);
extern void vfo_id_deviation_changed(int id, int dev);
extern void vfo_deviation_changed(int dev);
extern void vfo_a_to_b(void);
extern void vfo_b_to_a(void);
extern void vfo_a_swap_b(void);

extern int get_tx_vfo(void);
extern int get_tx_mode(void);
extern long long get_tx_freq(void);

extern void vfo_xvtr_changed(void);

extern void vfo_xit_toggle(void);
extern void vfo_xit_incr(int incr);
extern void vfo_xit_onoff(int enable);
extern void vfo_xit_value(long long value);

extern void vfo_rit_toggle(int rx);
extern void vfo_rit_onoff(int rx, int enable);
extern void vfo_rit_incr(int rx, int incr);
extern void vfo_rit_value(int rx, long long value);

extern void vfo_set_frequency(int vfo, long long f);

extern void vfo_ctun_update(int id, int state);
extern void vfo_apply_mode_settings(RECEIVER *rx);
extern void vfos_changed(void);

extern void num_pad(int val, int vfo);

#endif
