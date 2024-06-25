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

#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sndfile.h>
#include "radio.h"
#include "receiver.h"
#include "vfo.h"

#ifdef _WIN32
#include <direct.h>
#define mkdir(dir, mode) _mkdir(dir)
#else
#include <unistd.h>
#endif

#include "message.h"
#include "wave.h"

#define NUM_CHANNELS 2
#define BITS_PER_SAMPLE 16

typedef struct {
    char chunkID[4];
    uint32_t chunkSize;
    char format[4];
    char subchunk1ID[4];
    uint32_t subchunk1Size;
    uint16_t audioFormat;
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
    char subchunk2ID[4];
    uint32_t subchunk2Size;
} WAVHeader;

FILE *wav_file = NULL; // File for capturing
SNDFILE *sf_wav_file = NULL; // File for playback
SF_INFO sf_info;
double *wav_data = NULL;
sf_count_t wav_data_length = 0;
sf_count_t wav_playback_pointer = 0;

void write_wav_header(FILE *file, uint32_t sample_rate, uint16_t num_channels, uint32_t data_length) {
    WAVHeader header;
    uint32_t bytes_per_sample = BITS_PER_SAMPLE / 8;
    uint32_t byte_rate = sample_rate * num_channels * bytes_per_sample;

    memcpy(header.chunkID, "RIFF", 4);
    header.chunkSize = 36 + data_length * bytes_per_sample;
    memcpy(header.format, "WAVE", 4);
    memcpy(header.subchunk1ID, "fmt ", 4);
    header.subchunk1Size = 16;
    header.audioFormat = 1;
    header.numChannels = num_channels;
    header.sampleRate = sample_rate;
    header.byteRate = byte_rate;
    header.blockAlign = num_channels * bytes_per_sample;
    header.bitsPerSample = BITS_PER_SAMPLE;
    memcpy(header.subchunk2ID, "data", 4);
    header.subchunk2Size = data_length * bytes_per_sample;

    fwrite(&header, sizeof(WAVHeader), 1, file);

    // Debugging information
    t_print("WAV Header written with sample rate: %u\n", sample_rate);
}

void initiate_wav_capture() {
    // capture directory exists?
    struct stat st = {0};
    if (stat("captures", &st) == -1) {
        mkdir("captures", 0700);
    }
    
    // active receiver?
    // frequency?
    int rxid = active_receiver->id;
    long long freq = vfo[rxid].frequency;

    // time?
    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    char filename[200];
    sprintf(filename, "captures/%lld_", freq);
    strftime(filename + strlen(filename), sizeof(filename) - strlen(filename), "%Y%m%d_%H%M%S.wav", t);

    // Open the WAV file and write the header
    wav_file = fopen(filename, "wb");
    if (wav_file) {
        write_wav_header(wav_file, SAMPLE_RATE, NUM_CHANNELS, 0);  // Initial header with zero data length
        t_print("WAV file opened: %s\n", filename);
    } else {
        perror("Failed to open WAV file");
    }
}

void finalize_wav_capture() {
    if (wav_file) {
        long file_length = ftell(wav_file);

        // Update the WAV header with the correct data length
        fseek(wav_file, 4, SEEK_SET);
        uint32_t chunkSize = file_length - 8;
        fwrite(&chunkSize, sizeof(uint32_t), 1, wav_file);

        fseek(wav_file, 40, SEEK_SET);
        uint32_t subchunk2Size = file_length - 44;
        fwrite(&subchunk2Size, sizeof(uint32_t), 1, wav_file);

        fclose(wav_file);
        wav_file = NULL;

        printf("WAV file finalized with file length: %ld\n", file_length);
    }
}

// Function to load WAV file for playback
void load_wav_file(const char *filename) {
    sf_wav_file = sf_open(filename, SFM_READ, &sf_info);
    if (sf_wav_file == NULL) {
        g_print("Failed to open WAV file: %s\n", filename);
        return;
    }

    wav_data_length = sf_info.frames * sf_info.channels;
    wav_data = (double *)malloc(wav_data_length * sizeof(double));

    if (wav_data == NULL) {
        g_print("Failed to allocate memory for WAV data\n");
        sf_close(sf_wav_file);
        return;
    }

    sf_read_double(sf_wav_file, wav_data, wav_data_length);
    wav_playback_pointer = 0;
    sf_close(sf_wav_file);
}

SampleResult replace_mic_samples_with_wav_data() {
    SampleResult result;
    result.completed = 0; // Default to not completed

    if (wav_playback_pointer < wav_data_length) {
        result.sample = wav_data[wav_playback_pointer++];
    } else {
        result.completed = 1; // Indicate that playback is completed
        result.sample = 0.0; // Default value if no more data
        free(wav_data); // Free the WAV data
        wav_data = NULL;
    }

    return result;
}

