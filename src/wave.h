/*   Copyright (C) 2024 - Mark Rutherford, KB2YCW
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

#ifndef WAVE_H
#define WAVE_H

#include <stdio.h>
#include <stdint.h>

extern FILE *wav_file;
#define SAMPLE_RATE 24000 // Correct sample rate

typedef struct {
    double sample;
    int completed;
} SampleResult;

// Function prototypes
void initiate_wav_capture();
void finalize_wav_capture();
SampleResult replace_mic_samples_with_wav_data();
void load_wav_file(const char *filename);

#endif // WAVE_H



