/* Copyright (C)
* 2019 - Christoph van Wüllen, DL1YCF
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
 * MIDI support for pihpsdr
 *
 * Midi support works in three layers
 *
 * Layer-1: hardware specific
 * --------------------------
 *
 * Layer1 either implements a callback function (if the operating system
 * supports MIDI) or a separate thread polling MIDI data. Whenever a
 * MIDI command arrives, such as Note on/off or Midi-Controller value
 * changed, it calls Layer 2.
 *
 * Layer-2: MIDI device specific
 * -----------------------------
 *
 * Layer2 translates MIDI commands into pihpsdr actions. This is done with
 * a table-driven algorithm, such that the same translator can be used for
 * any MIDI device provided the tables have been set up correctly.
 * It seems overly complicated to create a user interface for setting up
 * these tables, instead a standard text file describing the MIDI device
 * is read and the tables are set up.
 * Layer-2 has SDR applications in mind, but is not necessarily specific
 * to pihpsr. It calls the Layer-3 function.
 *
 * Layer-3: pihpsdr specific
 * -------------------------
 *
 * Layer 3, finally, implements all the "actions" we can make, such as TUNE
 * or VFO. This Layer calls pihpsdr functions.
 *
 * One word to MIDI channels. Usually, a MIDI device can be configured to use
 * a specific channel, such that different keyboards use different channels.
 * The Layer-2 tables can either specify that the MIDI command has to come from
 * a specific channel, or can specify that the action will be taken not matter which
 * channel the MIDI message comes from. The latter case should be the default, but
 * if we want to connect more than one MIDI device, we need to speficy the channel.
 *
 * In principle this supports more than one MIDI device, but in this case they
 * must generate MIDI events on different channels
 */

#ifndef _MIDI_H
#define _MIDI_H

//
// MIDIevent encodes the actual MIDI event "seen" in Layer-1 and
// passed to Layer-2. MIDI_NOTE events end up as MIDI_KEY and
// MIDI_PITCH as MIDI_KNOB, while MIDI_CTRL can end up both as
// MIDI_KNOB or MIDI_WHEEL, depending on the device description.
//
enum MIDIevent {
  EVENT_NONE = 0,
  MIDI_NOTE,
  MIDI_CTRL,
  MIDI_PITCH
};

//
// Data structure for Layer-2
//

//
// There is linked list of all specified MIDI events for a given "Note" value,
// which contains the defined actions for all MIDI_NOTE and MIDI_CTRL events
// with that given note and for all channels
//
// Note that with a MIDI KEY, normally only "Note on" messages
// are processed, except for the actions
// CW_KEYER, CW_LEFT, CW_RIGHT, PTT_KEYER which generate actions
// also for Note-Off.
//

struct desc {
  int               channel;     // -1 for ANY channel
  enum MIDIevent    event;       // type of event (NOTE on/off, Controller change, Pitch value)
  enum ACTIONtype   type;        // Key, Knob, or Wheel
  int               vfl1, vfl2;  // Wheel only: range of controller values for "very fast left"
  int               fl1, fl2;    // Wheel only: range of controller values for "fast left"
  int               lft1, lft2;  // Wheel only: range of controller values for "slow left"
  int               vfr1, vfr2;  // Wheel only: range of controller values for "very fast right"
  int               fr1, fr2;    // Wheel only: range of controller values for "fast right"
  int               rgt1, rgt2;  // Wheel only: range of controller values for "slow right"
  int               action;      // SDR "action" to generate
  struct desc       *next;       // Next defined action for a controller/key with that note value (NULL for end of list)
};

extern struct desc *MidiCommandsTable[129];  // slot #128 is for the pitch-bend

extern int midiIgnoreCtrlPairs;

//
// Layer-1 entry point, called once for all the MIDI devices
// that have been defined.
//
void register_midi_device(int index);
void close_midi_device(int index);
void configure_midi_device(gboolean state);

//
// Layer-2 entry point (called by Layer1)
//
// When Layer-1 has received a MIDI message, it calls
// NewMidiEvent.
//

void NewMidiEvent(enum MIDIevent event, int channel, int note, int val);
int ReadLegacyMidiFile(char *filename);
void MidiAddCommand(int note, struct desc *desc);
void MidiReleaseCommands(void);

//
// Layer-3 entry point (called by Layer2). In Layer-3, all the pihpsdr
// actions (such as changing the VFO frequency) are performed.
// The implementation of DoTheMIDI is tightly bound to pihpsr and contains
// tons of invocations of g_idle_add with routines from ext.c
//

void DoTheMidi(int code, enum ACTIONtype type, int val);
#endif
