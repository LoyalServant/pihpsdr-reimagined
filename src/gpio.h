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

#ifndef _GPIO_H
#define _GPIO_H

#define MAX_ENCODERS 5
#define MAX_SWITCHES 16
#define MAX_FUNCTIONS 6



#define MAX_GPIO_CHIPS 3
#define MAX_GPIO_PINS 32
#define MAX_MONITOR_LINES 32

typedef struct {
    struct gpiod_chip* chip;
    char* device;
    int enabled_pins[MAX_GPIO_PINS];
    int num_enabled_pins;
} GPIOChip;

typedef struct {
    int chip_index;
    int line;
} GPIOPin;


typedef struct {
    int chip_index;
    int lines[MAX_MONITOR_LINES];
    int num_lines;
} ChipMonitorLines;

typedef struct {
    int chip_index;
} gpio_chip_context_t;

extern GPIOChip gpio_chips[MAX_GPIO_CHIPS];
extern int num_gpio_chips;
extern ChipMonitorLines chip_monitor_lines[MAX_GPIO_CHIPS];

extern GPIOPin CWL_LINE;
extern GPIOPin CWR_LINE;
extern GPIOPin CWKEY_LINE;
extern GPIOPin PTTIN_LINE;
extern GPIOPin PTTOUT_LINE;
extern GPIOPin CWOUT_LINE;


typedef struct _encoder {
    gboolean bottom_encoder_enabled;  // Bottom encoder is enabled
    gboolean bottom_encoder_pullup;   // Pull-up for bottom encoder
    int bottom_encoder_chip_a;        // Chip index for bottom encoder A
    int bottom_encoder_address_a;     // Address for bottom encoder A
    int bottom_encoder_a_value;       // Value for bottom encoder A
    int bottom_encoder_chip_b;        // Chip index for bottom encoder B
    int bottom_encoder_address_b;     // Address for bottom encoder B
    int bottom_encoder_b_value;       // Value for bottom encoder B
    int bottom_encoder_pos;           // Position of bottom encoder
    int bottom_encoder_function;      // Function of bottom encoder
    guchar bottom_encoder_state;      // State of bottom encoder

    gboolean top_encoder_enabled;     // Top encoder is enabled
    gboolean top_encoder_pullup;      // Pull-up for top encoder
    int top_encoder_chip_a;           // Chip index for top encoder A
    int top_encoder_address_a;        // Address for top encoder A
    int top_encoder_a_value;          // Value for top encoder A
    int top_encoder_chip_b;           // Chip index for top encoder B
    int top_encoder_address_b;        // Address for top encoder B
    int top_encoder_b_value;          // Value for top encoder B
    int top_encoder_pos;              // Position of top encoder
    int top_encoder_function;         // Function of top encoder
    guchar top_encoder_state;         // State of top encoder

    gboolean switch_enabled;          // Switch is enabled
    gboolean switch_pullup;           // Pull-up for switch
    int switch_chip;                  // Chip index for switch
    int switch_address;               // Address for switch
    int switch_function;              // Function of switch
    gulong switch_debounce;           // Debounce time for switch
} ENCODER;

extern ENCODER* encoders;

typedef struct _switch {
	gboolean switch_enabled;
	gboolean switch_pullup;
	int switch_chip;     // Index of the chip in the chips array
	int switch_address;
	int switch_function;
	gulong switch_debounce;
} SWITCH;


extern SWITCH switches_controller1[MAX_FUNCTIONS][MAX_SWITCHES];

extern SWITCH *switches;

extern int *sw_action;

extern long settle_time;

extern void gpio_default_encoder_actions(int ctrlr);
extern void gpio_default_switch_actions(int ctrlr);
extern void gpio_set_defaults(int ctrlr);
extern void gpioRestoreActions(void);
extern void gpioRestoreState(void);
extern void gpioSaveState(void);
extern void gpioSaveActions(void);
extern int gpio_init(void);
extern void gpio_close(void);
extern void gpio_set_ptt(int state);
extern void gpio_set_cw(int state);

#endif
