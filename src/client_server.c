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

#include <gtk/gtk.h>

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <strings.h>
#ifndef __APPLE__
  #include <endian.h>
#endif
#include <semaphore.h>

#include "discovered.h"
#include "adc.h"
#include "dac.h"
#include "receiver.h"
#include "transmitter.h"
#include "radio.h"
#include "main.h"
#include "vfo.h"
#ifdef CLIENT_SERVER
  #include "client_server.h"
#endif
#include "ext.h"
#include "audio.h"
#include "zoompan.h"
#include "noise_menu.h"
#include "radio_menu.h"
#include "sliders.h"
#include "message.h"
#include "mystring.h"

#define DISCOVERY_PORT 4992
#define LISTEN_PORT 50000

int listen_port = LISTEN_PORT;

REMOTE_CLIENT *clients = NULL;

GMutex client_mutex;

#define MAX_COMMAND 256

static char title[128];

gboolean hpsdr_server = FALSE;

int client_socket = -1;
GThread *client_thread_id;
int start_spectrum(void *data);
gboolean remote_started = FALSE;

static GThread *listen_thread_id;
static gboolean running;
static int listen_socket;

static int audio_buffer_index = 0;
AUDIO_DATA audio_data;

static int remote_command(void * data);

GMutex accumulated_mutex;
static int accumulated_steps = 0;
static long long accumulated_hz = 0LL;
static gboolean accumulated_round = FALSE;
guint check_vfo_timer_id = 0;

REMOTE_CLIENT *add_client(REMOTE_CLIENT *client) {
  t_print("add_client: %p\n", client);
  // add to front of queue
  g_mutex_lock(&client_mutex);
  client->next = clients;
  clients = client;
  g_mutex_unlock(&client_mutex);
  t_print("add_client: clients=%p\n", clients);
  return client;
}

void delete_client(REMOTE_CLIENT *client) {
  t_print("delete_client: %p\n", client);
  g_mutex_lock(&client_mutex);

  if (clients == client) {
    clients = client->next;
    g_free(client);
  } else {
    REMOTE_CLIENT* c = clients;
    REMOTE_CLIENT* last_c = NULL;

    while (c != NULL && c != client) {
      last_c = c;
      c = c->next;
    }

    if (c != NULL) {
      last_c->next = c->next;
      g_free(c);
    }
  }

  t_print("delete_client: clients=%p\n", clients);
  g_mutex_unlock(&client_mutex);
}

static int recv_bytes(int s, char *buffer, int bytes) {
  int bytes_read = 0;

  while (bytes_read != bytes) {
    int rc = recv(s, &buffer[bytes_read], bytes - bytes_read, 0);

    if (rc < 0) {
      // return -1, so we need not check downstream
      // on incomplete messages received
      t_print("%s: read %d bytes, but expected %d.\n", __FUNCTION__, bytes_read, bytes);
      bytes_read = -1;
      t_perror("recv_bytes");
      break;
    } else {
      bytes_read += rc;
    }
  }

  return bytes_read;
}

static int send_bytes(int s, char *buffer, int bytes) {
  int bytes_sent = 0;

  if (s < 0) { return -1; }

  while (bytes_sent != bytes) {
    int rc = send(s, &buffer[bytes_sent], bytes - bytes_sent, 0);

    if (rc < 0) {
      // return -1, so we need not check downstream
      // on incomplete messages sent
      t_print("%s: sent %d bytes, but tried %d.\n", __FUNCTION__, bytes_sent, bytes);
      bytes_sent = -1;
      t_perror("send_bytes");
      break;
    } else {
      bytes_sent += rc;
    }
  }

  return bytes_sent;
}

void remote_audio(const RECEIVER *rx, short left_sample, short right_sample) {
  int i = audio_buffer_index * 2;
  audio_data.sample[i] = htons(left_sample);
  audio_data.sample[i + 1] = htons(right_sample);
  audio_buffer_index++;

  if (audio_buffer_index >= AUDIO_DATA_SIZE) {
    g_mutex_lock(&client_mutex);
    REMOTE_CLIENT *c = clients;

    while (c != NULL && c->socket != -1) {
      audio_data.header.sync = REMOTE_SYNC;
      audio_data.header.data_type = htons(INFO_AUDIO);
      audio_data.header.version = htonll(CLIENT_SERVER_VERSION);
      audio_data.rx = rx->id;
      audio_data.samples = ntohs(audio_buffer_index);
      int bytes_sent = send_bytes(c->socket, (char *)&audio_data, sizeof(audio_data));

      if (bytes_sent < 0) {
        t_perror("remote_audio");
        close(c->socket);
      }

      c = c->next;
    }

    g_mutex_unlock(&client_mutex);
    audio_buffer_index = 0;
  }
}

static int send_spectrum(void *arg) {
  REMOTE_CLIENT *client = (REMOTE_CLIENT *)arg;
  const float *samples;
  short s;
  SPECTRUM_DATA spectrum_data;
  int result;
  result = TRUE;

  if (!(client->receiver[0].send_spectrum || client->receiver[1].send_spectrum) || !client->running) {
    client->spectrum_update_timer_id = 0;
    t_print("send_spectrum: no more receivers\n");
    return FALSE;
  }

  for (int r = 0; r < receivers; r++) {
    RECEIVER *rx = receiver[r];

    if (client->receiver[r].send_spectrum) {
      if (rx->displaying && (rx->pixels > 0) && (rx->pixel_samples != NULL)) {
        g_mutex_lock(&rx->display_mutex);
        spectrum_data.header.sync = REMOTE_SYNC;
        spectrum_data.header.data_type = htons(INFO_SPECTRUM);
        spectrum_data.header.version = htonll(CLIENT_SERVER_VERSION);
        spectrum_data.rx = r;
        spectrum_data.vfo_a_freq = htonll(vfo[VFO_A].frequency);
        spectrum_data.vfo_b_freq = htonll(vfo[VFO_B].frequency);
        spectrum_data.vfo_a_ctun_freq = htonll(vfo[VFO_A].ctun_frequency);
        spectrum_data.vfo_b_ctun_freq = htonll(vfo[VFO_B].ctun_frequency);
        spectrum_data.vfo_a_offset = htonll(vfo[VFO_A].offset);
        spectrum_data.vfo_b_offset = htonll(vfo[VFO_B].offset);
        spectrum_data.meter = htond(receiver[r]->meter);
        spectrum_data.samples = htons(rx->width);
        samples = rx->pixel_samples;

        for (int i = 0; i < rx->width; i++) {
          s = (short)samples[i + rx->pan];
          spectrum_data.sample[i] = htons(s);
        }

        // send the buffer
        int bytes_sent = send_bytes(client->socket, (char *)&spectrum_data, sizeof(spectrum_data));

        if (bytes_sent < 0) {
          result = FALSE;
        }

        g_mutex_unlock(&rx->display_mutex);
      }
    }
  }

  return result;
}

void send_radio_data(const REMOTE_CLIENT *client) {
  RADIO_DATA radio_data;
  radio_data.header.sync = REMOTE_SYNC;
  radio_data.header.data_type = htons(INFO_RADIO);
  radio_data.header.version = htonl(CLIENT_SERVER_VERSION);
  STRLCPY(radio_data.name, radio->name, sizeof(radio_data.name));
  radio_data.protocol = htons(protocol);
  radio_data.device = htons(device);
  uint64_t temp = (uint64_t)radio->frequency_min;
  radio_data.frequency_min = htonll(temp);
  temp = (uint64_t)radio->frequency_max;
  radio_data.frequency_max = htonll(temp);
  long long rate = (long long)radio_sample_rate;
  radio_data.sample_rate = htonll(rate);
  radio_data.locked = locked;
  radio_data.supported_receivers = htons(radio->supported_receivers);
  radio_data.receivers = htons(receivers);
  radio_data.can_transmit = can_transmit;
  radio_data.split = split;
  radio_data.sat_mode = sat_mode;
  radio_data.duplex = duplex;
  radio_data.have_rx_gain = have_rx_gain;
  radio_data.rx_gain_calibration = htons(rx_gain_calibration);
  radio_data.filter_board = htons(filter_board);
  int bytes_sent = send_bytes(client->socket, (char *)&radio_data, sizeof(radio_data));
  t_print("send_radio_data: %d\n", bytes_sent);

  if (bytes_sent < 0) {
    t_perror("send_radio_data");
  } else {
    //t_print("send_radio_data: %d\n",bytes_sent);
  }
}

void send_adc_data(const REMOTE_CLIENT *client, int i) {
  ADC_DATA adc_data;
  adc_data.header.sync = REMOTE_SYNC;
  adc_data.header.data_type = htons(INFO_ADC);
  adc_data.header.version = htonl(CLIENT_SERVER_VERSION);
  adc_data.adc = i;
  adc_data.filters = htons(adc[i].filters);
  adc_data.hpf = htons(adc[i].hpf);
  adc_data.lpf = htons(adc[i].lpf);
  adc_data.antenna = htons(adc[i].antenna);
  adc_data.dither = adc[i].dither;
  adc_data.random = adc[i].random;
  adc_data.preamp = adc[i].preamp;
  adc_data.attenuation = htons(adc[i].attenuation);
  adc_data.gain = htond(adc[i].gain);
  adc_data.min_gain = htond(adc[i].min_gain);
  adc_data.max_gain = htond(adc[i].max_gain);
  int bytes_sent = send_bytes(client->socket, (char *)&adc_data, sizeof(adc_data));

  if (bytes_sent < 0) {
    t_perror("send_adc_data");
  } else {
    //t_print("send_adc_data: %d\n",bytes_sent);
  }
}

void send_receiver_data(const REMOTE_CLIENT *client, int rx) {
  RECEIVER_DATA receiver_data;
  receiver_data.header.sync = REMOTE_SYNC;
  receiver_data.header.data_type = htons(INFO_RECEIVER);
  receiver_data.header.version = htonl(CLIENT_SERVER_VERSION);
  receiver_data.rx = rx;
  receiver_data.adc = htons(receiver[rx]->adc);
  long long rate = (long long)receiver[rx]->sample_rate;
  receiver_data.sample_rate = htonll(rate);
  receiver_data.displaying = receiver[rx]->displaying;
  receiver_data.display_panadapter = receiver[rx]->display_panadapter;
  receiver_data.display_waterfall = receiver[rx]->display_waterfall;
  receiver_data.fps = htons(receiver[rx]->fps);
  receiver_data.agc = receiver[rx]->agc;
  receiver_data.agc_hang = htond(receiver[rx]->agc_hang);
  receiver_data.agc_thresh = htond(receiver[rx]->agc_thresh);
  receiver_data.agc_hang_thresh = htond(receiver[rx]->agc_hang_threshold);
  receiver_data.nb = receiver[rx]->nb;
  receiver_data.nr = receiver[rx]->nr;
  receiver_data.anf = receiver[rx]->anf;
  receiver_data.snb = receiver[rx]->snb;
  receiver_data.filter_low = htons(receiver[rx]->filter_low);
  receiver_data.filter_high = htons(receiver[rx]->filter_high);
  receiver_data.panadapter_low = htons(receiver[rx]->panadapter_low);
  receiver_data.panadapter_high = htons(receiver[rx]->panadapter_high);
  receiver_data.panadapter_step = htons(receiver[rx]->panadapter_step);
  receiver_data.waterfall_low = htons(receiver[rx]->waterfall_low);
  receiver_data.waterfall_high = htons(receiver[rx]->waterfall_high);
  receiver_data.waterfall_automatic = receiver[rx]->waterfall_automatic;
  receiver_data.pixels = htons(receiver[rx]->pixels);
  receiver_data.zoom = htons(receiver[rx]->zoom);
  receiver_data.pan = htons(receiver[rx]->pan);
  receiver_data.width = htons(receiver[rx]->width);
  receiver_data.height = htons(receiver[rx]->height);
  receiver_data.x = htons(receiver[rx]->x);
  receiver_data.y = htons(receiver[rx]->y);
  receiver_data.volume = htond(receiver[rx]->volume);
  receiver_data.agc_gain = htond(receiver[rx]->agc_gain);
  receiver_data.display_gradient = receiver[rx]->display_gradient;
  receiver_data.display_filled = receiver[rx]->display_filled;
  receiver_data.display_detector_mode = receiver[rx]->display_detector_mode;
  receiver_data.display_average_mode = receiver[rx]->display_average_mode;
  receiver_data.display_average_time = htons((int)receiver[rx]->display_average_time);
  int bytes_sent = send_bytes(client->socket, (char *)&receiver_data, sizeof(receiver_data));

  if (bytes_sent < 0) {
    t_perror("send_receiver_data");
  } else {
    //t_print("send_receiver_data: bytes sent %d\n",bytes_sent);
  }
}

void send_vfo_data(const REMOTE_CLIENT *client, int v) {
  VFO_DATA vfo_data;
  vfo_data.header.sync = REMOTE_SYNC;
  vfo_data.header.data_type = htons(INFO_VFO);
  vfo_data.header.version = htonl(CLIENT_SERVER_VERSION);
  vfo_data.vfo = v;
  vfo_data.band = htons(vfo[v].band);
  vfo_data.bandstack = htons(vfo[v].bandstack);
  vfo_data.frequency = htonll(vfo[v].frequency);
  vfo_data.mode = htons(vfo[v].mode);
  vfo_data.filter = htons(vfo[v].filter);
  vfo_data.ctun = vfo[v].ctun;
  vfo_data.ctun_frequency = htonll(vfo[v].ctun_frequency);
  vfo_data.rit_enabled = vfo[v].rit_enabled;
  vfo_data.rit = htonll(vfo[v].rit);
  vfo_data.lo = htonll(vfo[v].lo);
  vfo_data.offset = htonll(vfo[v].offset);
  vfo_data.step   = htonll(vfo[v].step);
  int bytes_sent = send_bytes(client->socket, (char *)&vfo_data, sizeof(vfo_data));

  if (bytes_sent < 0) {
    t_perror("send_vfo_data");
  } else {
    //t_print("send_vfo_data: bytes sent %d\n",bytes_sent);
  }
}

static void *server_client_thread(void *arg) {
  REMOTE_CLIENT *client = (REMOTE_CLIENT *)arg;
  HEADER header;
  t_print("Client connected on port %d\n", client->address.sin_port);
  send_radio_data(client);
  send_adc_data(client, 0);
  send_adc_data(client, 1);

  for (int i = 0; i < RECEIVERS; i++) {
    send_receiver_data(client, i);
  }

  send_vfo_data(client, VFO_A);
  send_vfo_data(client, VFO_B);

  // get and parse client commands
  while (client->running) {
    int bytes_read = recv_bytes(client->socket, (char *)&header.sync, sizeof(header.sync));

    if (bytes_read <= 0) {
      t_print("server_client_thread: short read for HEADER SYNC\n");
      t_perror("server_client_thread");
      client->running = FALSE;
      continue;
    }

    t_print("header.sync is %x\n", header.sync);

    if (header.sync != REMOTE_SYNC) {
      t_print("header.sync is %x wanted %x\n", header.sync, REMOTE_SYNC);
      int syncs = 0;
      char c;

      while (syncs != sizeof(header.sync) && client->running) {
        // try to resync on 2 0xFA bytes
        bytes_read = recv_bytes(client->socket, (char *)&c, 1);

        if (bytes_read <= 0) {
          t_print("server_client_thread: short read for HEADER RESYNC\n");
          t_perror("server_client_thread");
          client->running = FALSE;
          continue;
        }

        if (c == (char)0xFA) {
          syncs++;
        } else {
          syncs = 0;
        }
      }
    }

    bytes_read = recv_bytes(client->socket, (char *)&header.data_type, sizeof(header) - sizeof(header.sync));

    if (bytes_read <= 0) {
      t_print("server_client_thread: short read for HEADER\n");
      t_perror("server_client_thread");
      client->running = FALSE;
      continue;
    }

    t_print("server_client_thread: received header: type=%d\n", ntohs(header.data_type));

    switch (ntohs(header.data_type)) {
    case CMD_RESP_SPECTRUM: {
      SPECTRUM_COMMAND spectrum_command;
      bytes_read = recv_bytes(client->socket, (char *)&spectrum_command.id, sizeof(SPECTRUM_COMMAND) - sizeof(header));

      if (bytes_read <= 0) {
        t_print("server_client_thread: short read for SPECTRUM_COMMAND\n");
        t_perror("server_client_thread");
        // dialog box?
        return NULL;
      }

      // cppcheck-suppress uninitStructMember
      int rx = spectrum_command.id;
      // cppcheck-suppress uninitvar
      int state = spectrum_command.start_stop;
      t_print("server_client_thread: CMD_RESP_SPECTRUM rx=%d state=%d timer_id=%d\n", rx, state,
              client->spectrum_update_timer_id);

      if (state) {
        client->receiver[rx].receiver = rx;
        client->receiver[rx].spectrum_fps = receiver[rx]->fps;
        client->receiver[rx].spectrum_port = 0;
        client->receiver[rx].send_spectrum = TRUE;

        if (client->spectrum_update_timer_id == 0) {
          t_print("start send_spectrum thread: fps=%d\n", client->receiver[rx].spectrum_fps);
          client->spectrum_update_timer_id = gdk_threads_add_timeout_full(G_PRIORITY_HIGH_IDLE,
                                             1000 / client->receiver[rx].spectrum_fps, send_spectrum, client, NULL);
          t_print("spectrum_update_timer_id=%d\n", client->spectrum_update_timer_id);
        } else {
          t_print("send_spectrum thread already running\n");
        }
      } else {
        client->receiver[rx].send_spectrum = FALSE;
      }
    }
    break;

    case CMD_RESP_RX_FREQ:
      t_print("server_client_thread: CMD_RESP_RX_FREQ\n");
      {
        FREQ_COMMAND *freq_command = g_new(FREQ_COMMAND, 1);
        freq_command->header.data_type = header.data_type;
        freq_command->header.version = header.version;
        freq_command->header.context.client = client;
        bytes_read = recv_bytes(client->socket, (char *)&freq_command->id, sizeof(FREQ_COMMAND) - sizeof(header));

        if (bytes_read <= 0) {
          t_print("server_client_thread: short read for FREQ_COMMAND\n");
          t_perror("server_client_thread");
          // dialog box?
          return NULL;
        }

        g_idle_add(remote_command, freq_command);
      }
      break;

    case CMD_RESP_RX_STEP:
      t_print("server_client_thread: CMD_RESP_RX_STEP\n");
      {
        STEP_COMMAND *step_command = g_new(STEP_COMMAND, 1);
        step_command->header.data_type = header.data_type;
        step_command->header.version = header.version;
        step_command->header.context.client = client;
        bytes_read = recv_bytes(client->socket, (char *)&step_command->id, sizeof(STEP_COMMAND) - sizeof(header));

        if (bytes_read <= 0) {
          t_print("server_client_thread: short read for STEP_COMMAND\n");
          t_perror("server_client_thread");
          // dialog box?
          return NULL;
        }

        g_idle_add(remote_command, step_command);
      }
      break;

    case CMD_RESP_RX_MOVE:
      t_print("server_client_thread: CMD_RESP_RX_MOVE\n");
      {
        MOVE_COMMAND *move_command = g_new(MOVE_COMMAND, 1);
        move_command->header.data_type = header.data_type;
        move_command->header.version = header.version;
        move_command->header.context.client = client;
        bytes_read = recv_bytes(client->socket, (char *)&move_command->id, sizeof(MOVE_COMMAND) - sizeof(header));

        if (bytes_read <= 0) {
          t_print("server_client_thread: short read for MOVE_COMMAND\n");
          t_perror("server_client_thread");
          // dialog box?
          return NULL;
        }

        g_idle_add(remote_command, move_command);
      }
      break;

    case CMD_RESP_RX_MOVETO:
      t_print("server_client_thread: CMD_RESP_RX_MOVETO\n");
      {
        MOVE_TO_COMMAND *move_to_command = g_new(MOVE_TO_COMMAND, 1);
        move_to_command->header.data_type = header.data_type;
        move_to_command->header.version = header.version;
        move_to_command->header.context.client = client;
        bytes_read = recv_bytes(client->socket, (char *)&move_to_command->id, sizeof(MOVE_TO_COMMAND) - sizeof(header));

        if (bytes_read <= 0) {
          t_print("server_client_thread: short read for MOVE_TO_COMMAND\n");
          t_perror("server_client_thread");
          // dialog box?
          return NULL;
        }

        g_idle_add(remote_command, move_to_command);
      }
      break;

    case CMD_RESP_RX_ZOOM:
      t_print("server_client_thread: CMD_RESP_RX_ZOOM\n");
      {
        ZOOM_COMMAND *zoom_command = g_new(ZOOM_COMMAND, 1);
        zoom_command->header.data_type = header.data_type;
        zoom_command->header.version = header.version;
        zoom_command->header.context.client = client;
        bytes_read = recv_bytes(client->socket, (char *)&zoom_command->id, sizeof(ZOOM_COMMAND) - sizeof(header));

        if (bytes_read <= 0) {
          t_print("server_client_thread: short read for ZOOM_COMMAND\n");
          t_perror("server_client_thread");
          // dialog box?
          return NULL;
        }

        g_idle_add(remote_command, zoom_command);
      }
      break;

    case CMD_RESP_RX_PAN:
      t_print("server_client_thread: CMD_RESP_RX_PAN\n");
      {
        PAN_COMMAND *pan_command = g_new(PAN_COMMAND, 1);
        pan_command->header.data_type = header.data_type;
        pan_command->header.version = header.version;
        pan_command->header.context.client = client;
        bytes_read = recv_bytes(client->socket, (char *)&pan_command->id, sizeof(PAN_COMMAND) - sizeof(header));

        if (bytes_read <= 0) {
          t_print("server_client_thread: short read for PAN_COMMAND\n");
          t_perror("server_client_thread");
          // dialog box?
          return NULL;
        }

        g_idle_add(remote_command, pan_command);
      }
      break;

    case CMD_RESP_RX_VOLUME:
      t_print("server_client_thread: CMD_RESP_RX_VOLUME\n");
      {
        VOLUME_COMMAND *volume_command = g_new(VOLUME_COMMAND, 1);
        volume_command->header.data_type = header.data_type;
        volume_command->header.version = header.version;
        volume_command->header.context.client = client;
        bytes_read = recv_bytes(client->socket, (char *)&volume_command->id, sizeof(VOLUME_COMMAND) - sizeof(header));

        if (bytes_read <= 0) {
          t_print("server_client_thread: short read for VOLUME_COMMAND\n");
          t_perror("server_client_thread");
          // dialog box?
          return NULL;
        }

        g_idle_add(remote_command, volume_command);
      }
      break;

    case CMD_RESP_RX_AGC:
      t_print("server_client_thread: CMD_RESP_RX_AGC\n");
      {
        AGC_COMMAND *agc_command = g_new(AGC_COMMAND, 1);
        agc_command->header.data_type = header.data_type;
        agc_command->header.version = header.version;
        agc_command->header.context.client = client;
        bytes_read = recv_bytes(client->socket, (char *)&agc_command->id, sizeof(AGC_COMMAND) - sizeof(header));

        if (bytes_read <= 0) {
          t_print("server_client_thread: short read for AGC_COMMAND\n");
          t_perror("server_client_thread");
          // dialog box?
          return NULL;
        }

        t_print("CMD_RESP_RX_AGC: id=%d agc=%d\n", agc_command->id, ntohs(agc_command->agc));
        g_idle_add(remote_command, agc_command);
      }
      break;

    case CMD_RESP_RX_AGC_GAIN:
      t_print("server_client_thread: CMD_RESP_RX_AGC_GAIN\n");
      {
        AGC_GAIN_COMMAND *agc_gain_command = g_new(AGC_GAIN_COMMAND, 1);
        agc_gain_command->header.data_type = header.data_type;
        agc_gain_command->header.version = header.version;
        agc_gain_command->header.context.client = client;
        bytes_read = recv_bytes(client->socket, (char *)&agc_gain_command->id, sizeof(AGC_GAIN_COMMAND) - sizeof(header));

        if (bytes_read <= 0) {
          t_print("server_client_thread: short read for AGC_GAIN_COMMAND\n");
          t_perror("server_client_thread");
          // dialog box?
          return NULL;
        }

        g_idle_add(remote_command, agc_gain_command);
      }
      break;

    case CMD_RESP_RX_GAIN:
      t_print("server_client_thread: CMD_RESP_RX_GAIN\n");
      {
        RFGAIN_COMMAND *command = g_new(RFGAIN_COMMAND, 1);
        command->header.data_type = header.data_type;
        command->header.version = header.version;
        command->header.context.client = client;
        bytes_read = recv_bytes(client->socket, (char *)&command->id, sizeof(RFGAIN_COMMAND) - sizeof(header));

        if (bytes_read <= 0) {
          t_print("server_client_thread: short read for RFGAIN_COMMAND\n");
          t_perror("server_client_thread");
          // dialog box?
          return NULL;
        }

        g_idle_add(remote_command, command);
      }
      break;

    case CMD_RESP_RX_ATTENUATION:
      t_print("server_client_thread: CMD_RESP_RX_ATTENUATION\n");
      {
        ATTENUATION_COMMAND *attenuation_command = g_new(ATTENUATION_COMMAND, 1);
        attenuation_command->header.data_type = header.data_type;
        attenuation_command->header.version = header.version;
        attenuation_command->header.context.client = client;
        bytes_read = recv_bytes(client->socket, (char *)&attenuation_command->id, sizeof(ATTENUATION_COMMAND) - sizeof(header));

        if (bytes_read <= 0) {
          t_print("server_client_thread: short read for ATTENUATION_COMMAND\n");
          t_perror("server_client_thread");
          // dialog box?
          return NULL;
        }

        g_idle_add(remote_command, attenuation_command);
      }
      break;

    case CMD_RESP_RX_SQUELCH:
      t_print("server_client_thread: CMD_RESP_RX_SQUELCH\n");
      {
        SQUELCH_COMMAND *squelch_command = g_new(SQUELCH_COMMAND, 1);
        squelch_command->header.data_type = header.data_type;
        squelch_command->header.version = header.version;
        squelch_command->header.context.client = client;
        bytes_read = recv_bytes(client->socket, (char *)&squelch_command->id, sizeof(SQUELCH_COMMAND) - sizeof(header));

        if (bytes_read <= 0) {
          t_print("server_client_thread: short read for SQUELCH_COMMAND\n");
          t_perror("server_client_thread");
          // dialog box?
          return NULL;
        }

        g_idle_add(remote_command, squelch_command);
      }
      break;

    case CMD_RESP_RX_NOISE:
      t_print("server_client_thread: CMD_RESP_RX_NOISE\n");
      {
        NOISE_COMMAND *noise_command = g_new(NOISE_COMMAND, 1);
        noise_command->header.data_type = header.data_type;
        noise_command->header.version = header.version;
        noise_command->header.context.client = client;
        bytes_read = recv_bytes(client->socket, (char *)&noise_command->id, sizeof(NOISE_COMMAND) - sizeof(header));

        if (bytes_read <= 0) {
          t_print("server_client_thread: short read for NOISE_COMMAND\n");
          t_perror("server_client_thread");
          // dialog box?
          return NULL;
        }

        g_idle_add(remote_command, noise_command);
      }
      break;

    case CMD_RESP_RX_BAND:
      t_print("server_client_thread: CMD_RESP_RX_BAND\n");
      {
        BAND_COMMAND *band_command = g_new(BAND_COMMAND, 1);
        band_command->header.data_type = header.data_type;
        band_command->header.version = header.version;
        band_command->header.context.client = client;
        bytes_read = recv_bytes(client->socket, (char *)&band_command->id, sizeof(BAND_COMMAND) - sizeof(header));

        if (bytes_read <= 0) {
          t_print("server_client_thread: short read for BAND_COMMAND\n");
          t_perror("server_client_thread");
          // dialog box?
          return NULL;
        }

        g_idle_add(remote_command, band_command);
      }
      break;

    case CMD_RESP_RX_MODE:
      t_print("server_client_thread: CMD_RESP_RX_MODE\n");
      {
        MODE_COMMAND *mode_command = g_new(MODE_COMMAND, 1);
        mode_command->header.data_type = header.data_type;
        mode_command->header.version = header.version;
        mode_command->header.context.client = client;
        bytes_read = recv_bytes(client->socket, (char *)&mode_command->id, sizeof(MODE_COMMAND) - sizeof(header));

        if (bytes_read <= 0) {
          t_print("server_client_thread: short read for MODE_COMMAND\n");
          t_perror("server_client_thread");
          // dialog box?
          return NULL;
        }

        g_idle_add(remote_command, mode_command);
      }
      break;

    case CMD_RESP_RX_FILTER:
      t_print("server_client_thread: CMD_RESP_RX_FILTER\n");
      {
        FILTER_COMMAND *filter_command = g_new(FILTER_COMMAND, 1);
        filter_command->header.data_type = header.data_type;
        filter_command->header.version = header.version;
        filter_command->header.context.client = client;
        bytes_read = recv_bytes(client->socket, (char *)&filter_command->id, sizeof(FILTER_COMMAND) - sizeof(header));

        if (bytes_read <= 0) {
          t_print("server_client_thread: short read for FILTER_COMMAND\n");
          t_perror("server_client_thread");
          // dialog box?
          return NULL;
        }

        g_idle_add(remote_command, filter_command);
      }
      break;

    case CMD_RESP_SPLIT:
      t_print("server_client_thread: CMD_RESP_RX_SPLIT\n");
      {
        SPLIT_COMMAND *split_command = g_new(SPLIT_COMMAND, 1);
        split_command->header.data_type = header.data_type;
        split_command->header.version = header.version;
        split_command->header.context.client = client;
        bytes_read = recv_bytes(client->socket, (char *)&split_command->split, sizeof(SPLIT_COMMAND) - sizeof(header));

        if (bytes_read <= 0) {
          t_print("server_client_thread: short read for SPLIT_COMMAND\n");
          t_perror("server_client_thread");
          // dialog box?
          return NULL;
        }

        g_idle_add(remote_command, split_command);
      }
      break;

    case CMD_RESP_SAT:
      t_print("server_client_thread: CMD_RESP_RX_SAT\n");
      {
        SAT_COMMAND *sat_command = g_new(SAT_COMMAND, 1);
        sat_command->header.data_type = header.data_type;
        sat_command->header.version = header.version;
        sat_command->header.context.client = client;
        bytes_read = recv_bytes(client->socket, (char *)&sat_command->sat, sizeof(SAT_COMMAND) - sizeof(header));

        if (bytes_read <= 0) {
          t_print("server_client_thread: short read for SAT_COMMAND\n");
          t_perror("server_client_thread");
          // dialog box?
          return NULL;
        }

        g_idle_add(remote_command, sat_command);
      }
      break;

    case CMD_RESP_DUP:
      t_print("server_client_thread: CMD_RESP_DUP\n");
      {
        DUP_COMMAND *dup_command = g_new(DUP_COMMAND, 1);
        dup_command->header.data_type = header.data_type;
        dup_command->header.version = header.version;
        dup_command->header.context.client = client;
        bytes_read = recv_bytes(client->socket, (char *)&dup_command->dup, sizeof(DUP_COMMAND) - sizeof(header));

        if (bytes_read <= 0) {
          t_print("server_client_thread: short read for DUP\n");
          t_perror("server_client_thread");
          // dialog box?
          return NULL;
        }

        g_idle_add(remote_command, dup_command);
      }
      break;

    case CMD_RESP_LOCK:
      t_print("server_client_thread: CMD_RESP_LOCK\n");
      {
        LOCK_COMMAND *lock_command = g_new(LOCK_COMMAND, 1);
        lock_command->header.data_type = header.data_type;
        lock_command->header.version = header.version;
        lock_command->header.context.client = client;
        bytes_read = recv_bytes(client->socket, (char *)&lock_command->lock, sizeof(LOCK_COMMAND) - sizeof(header));

        if (bytes_read <= 0) {
          t_print("server_client_thread: short read for LOCK\n");
          t_perror("server_client_thread");
          // dialog box?
          return NULL;
        }

        g_idle_add(remote_command, lock_command);
      }
      break;

    case CMD_RESP_CTUN:
      t_print("server_client_thread: CMD_RESP_CTUN\n");
      {
        CTUN_COMMAND *ctun_command = g_new(CTUN_COMMAND, 1);
        ctun_command->header.data_type = header.data_type;
        ctun_command->header.version = header.version;
        ctun_command->header.context.client = client;
        bytes_read = recv_bytes(client->socket, (char *)&ctun_command->id, sizeof(CTUN_COMMAND) - sizeof(header));

        if (bytes_read <= 0) {
          t_print("server_client_thread: short read for CTUN\n");
          t_perror("server_client_thread");
          // dialog box?
          return NULL;
        }

        g_idle_add(remote_command, ctun_command);
      }
      break;

    case CMD_RESP_RX_FPS:
      t_print("server_client_thread: CMD_RESP_RX_FPS\n");
      {
        FPS_COMMAND *fps_command = g_new(FPS_COMMAND, 1);
        fps_command->header.data_type = header.data_type;
        fps_command->header.version = header.version;
        fps_command->header.context.client = client;
        bytes_read = recv_bytes(client->socket, (char *)&fps_command->id, sizeof(FPS_COMMAND) - sizeof(header));

        if (bytes_read <= 0) {
          t_print("server_client_thread: short read for FPS\n");
          t_perror("server_client_thread");
          // dialog box?
          return NULL;
        }

        g_idle_add(remote_command, fps_command);
      }
      break;

    case CMD_RESP_RX_SELECT:
      t_print("server_client_thread: CMD_RESP_RX_SELECT\n");
      {
        RX_SELECT_COMMAND *rx_select_command = g_new(RX_SELECT_COMMAND, 1);
        rx_select_command->header.data_type = header.data_type;
        rx_select_command->header.version = header.version;
        rx_select_command->header.context.client = client;
        bytes_read = recv_bytes(client->socket, (char *)&rx_select_command->id, sizeof(RX_SELECT_COMMAND) - sizeof(header));

        if (bytes_read <= 0) {
          t_print("server_client_thread: short read for RX_SELECT\n");
          t_perror("server_client_thread");
          // dialog box?
          return NULL;
        }

        g_idle_add(remote_command, rx_select_command);
      }
      break;

    case CMD_RESP_VFO:
      t_print("server_client_thread: CMD_RESP_VFO\n");
      {
        VFO_COMMAND *vfo_command = g_new(VFO_COMMAND, 1);
        vfo_command->header.data_type = header.data_type;
        vfo_command->header.version = header.version;
        vfo_command->header.context.client = client;
        bytes_read = recv_bytes(client->socket, (char *)&vfo_command->id, sizeof(VFO_COMMAND) - sizeof(header));

        if (bytes_read <= 0) {
          t_print("server_client_thread: short read for VFO\n");
          t_perror("server_client_thread");
          // dialog box?
          return NULL;
        }

        g_idle_add(remote_command, vfo_command);
      }
      break;

    case CMD_RESP_RIT_TOGGLE:
      t_print("server_client_thread: CMD_RESP_RIT_TOGGLE\n");
      {
        RIT_TOGGLE_COMMAND *rit_toggle_command = g_new(RIT_TOGGLE_COMMAND, 1);
        rit_toggle_command->header.data_type = header.data_type;
        rit_toggle_command->header.version = header.version;
        rit_toggle_command->header.context.client = client;
        bytes_read = recv_bytes(client->socket, (char *)&rit_toggle_command->id, sizeof(RIT_TOGGLE_COMMAND) - sizeof(header));

        if (bytes_read <= 0) {
          t_print("server_client_thread: short read for RIT_TOGGLE\n");
          t_perror("server_client_thread");
          // dialog box?
          return NULL;
        }

        g_idle_add(remote_command, rit_toggle_command);
      }
      break;

    case CMD_RESP_RIT_CLEAR:
      t_print("server_client_thread: CMD_RESP_RIT_CLEAR\n");
      {
        RIT_CLEAR_COMMAND *rit_clear_command = g_new(RIT_CLEAR_COMMAND, 1);
        rit_clear_command->header.data_type = header.data_type;
        rit_clear_command->header.version = header.version;
        rit_clear_command->header.context.client = client;
        bytes_read = recv_bytes(client->socket, (char *)&rit_clear_command->id, sizeof(RIT_CLEAR_COMMAND) - sizeof(header));

        if (bytes_read <= 0) {
          t_print("server_client_thread: short read for RIT_CLEAR\n");
          t_perror("server_client_thread");
          // dialog box?
          return NULL;
        }

        g_idle_add(remote_command, rit_clear_command);
      }
      break;

    case CMD_RESP_RIT:
      t_print("server_client_thread: CMD_RESP_RIT\n");
      {
        RIT_COMMAND *rit_command = g_new(RIT_COMMAND, 1);
        rit_command->header.data_type = header.data_type;
        rit_command->header.version = header.version;
        rit_command->header.context.client = client;
        bytes_read = recv_bytes(client->socket, (char *)&rit_command->id, sizeof(RIT_COMMAND) - sizeof(header));

        if (bytes_read <= 0) {
          t_print("server_client_thread: short read for RIT\n");
          t_perror("server_client_thread");
          // dialog box?
          return NULL;
        }

        g_idle_add(remote_command, rit_command);
      }
      break;

    case CMD_RESP_XIT_TOGGLE:
      t_print("server_client_thread: CMD_RESP_XIT_TOGGLE\n");
      {
        XIT_TOGGLE_COMMAND *xit_toggle_command = g_new(XIT_TOGGLE_COMMAND, 1);
        xit_toggle_command->header.data_type = header.data_type;
        xit_toggle_command->header.version = header.version;
        xit_toggle_command->header.context.client = client;
        g_idle_add(remote_command, xit_toggle_command);
      }
      break;

    case CMD_RESP_XIT_CLEAR:
      t_print("server_client_thread: CMD_RESP_XIT_CLEAR\n");
      {
        XIT_CLEAR_COMMAND *xit_clear_command = g_new(XIT_CLEAR_COMMAND, 1);
        xit_clear_command->header.data_type = header.data_type;
        xit_clear_command->header.version = header.version;
        xit_clear_command->header.context.client = client;
        g_idle_add(remote_command, xit_clear_command);
      }
      break;

    case CMD_RESP_XIT:
      t_print("server_client_thread: CMD_RESP_XIT\n");
      {
        XIT_COMMAND *xit_command = g_new(XIT_COMMAND, 1);
        xit_command->header.data_type = header.data_type;
        xit_command->header.version = header.version;
        xit_command->header.context.client = client;
        bytes_read = recv_bytes(client->socket, (char *)&xit_command->xit, sizeof(XIT_COMMAND) - sizeof(header));

        if (bytes_read <= 0) {
          t_print("server_client_thread: short read for XIT\n");
          t_perror("server_client_thread");
          // dialog box?
          return NULL;
        }
        g_idle_add(remote_command, xit_command);
      }
      break;

    case CMD_RESP_SAMPLE_RATE:
      t_print("server_client_thread: CMD_RESP_SAMPLE_RATE\n");
      {
        SAMPLE_RATE_COMMAND *sample_rate_command = g_new(SAMPLE_RATE_COMMAND, 1);
        sample_rate_command->header.data_type = header.data_type;
        sample_rate_command->header.version = header.version;
        sample_rate_command->header.context.client = client;
        bytes_read = recv_bytes(client->socket, (char *)&sample_rate_command->id, sizeof(SAMPLE_RATE_COMMAND) - sizeof(header));

        if (bytes_read <= 0) {
          t_print("server_client_thread: short read for SAMPLE_RATE\n");
          t_perror("server_client_thread");
          // dialog box?
          return NULL;
        }

        g_idle_add(remote_command, sample_rate_command);
      }
      break;

    case CMD_RESP_RECEIVERS:
      t_print("server_client_thread: CMD_RESP_RECEIVERS\n");
      {
        RECEIVERS_COMMAND *receivers_command = g_new(RECEIVERS_COMMAND, 1);
        receivers_command->header.data_type = header.data_type;
        receivers_command->header.version = header.version;
        receivers_command->header.context.client = client;
        bytes_read = recv_bytes(client->socket, (char *)&receivers_command->receivers,
                                sizeof(RECEIVERS_COMMAND) - sizeof(header));

        if (bytes_read <= 0) {
          t_print("server_client_thread: short read for RECEIVERS\n");
          t_perror("server_client_thread");
          // dialog box?
          return NULL;
        }

        g_idle_add(remote_command, receivers_command);
      }
      break;

    case CMD_RESP_RIT_INCREMENT:
      t_print("server_client_thread: CMD_RESP_RIT_INCREMENT\n");
      {
        RIT_INCREMENT_COMMAND *rit_increment_command = g_new(RIT_INCREMENT_COMMAND, 1);
        rit_increment_command->header.data_type = header.data_type;
        rit_increment_command->header.version = header.version;
        rit_increment_command->header.context.client = client;
        bytes_read = recv_bytes(client->socket, (char *)&rit_increment_command->increment,
                                sizeof(RIT_INCREMENT_COMMAND) - sizeof(header));

        if (bytes_read <= 0) {
          t_print("server_client_thread: short read for RIT_INCREMENT\n");
          t_perror("server_client_thread");
          // dialog box?
          return NULL;
        }

        g_idle_add(remote_command, rit_increment_command);
      }
      break;

    case CMD_RESP_FILTER_BOARD:
      t_print("server_client_thread: CMD_RESP_FILTER_BOARD\n");
      {
        FILTER_BOARD_COMMAND *filter_board_command = g_new(FILTER_BOARD_COMMAND, 1);
        filter_board_command->header.data_type = header.data_type;
        filter_board_command->header.version = header.version;
        filter_board_command->header.context.client = client;
        bytes_read = recv_bytes(client->socket, (char *)&filter_board_command->filter_board,
                                sizeof(FILTER_BOARD_COMMAND) - sizeof(header));

        if (bytes_read <= 0) {
          t_print("server_client_thread: short read for FILTER_BOARD\n");
          t_perror("server_client_thread");
          // dialog box?
          return NULL;
        }

        g_idle_add(remote_command, filter_board_command);
      }
      break;

    case CMD_RESP_SWAP_IQ:
      t_print("server_client_thread: CMD_RESP_SWAP_IQ\n");
      {
        SWAP_IQ_COMMAND *swap_iq_command = g_new(SWAP_IQ_COMMAND, 1);
        swap_iq_command->header.data_type = header.data_type;
        swap_iq_command->header.version = header.version;
        swap_iq_command->header.context.client = client;
        bytes_read = recv_bytes(client->socket, (char *)&swap_iq_command->iqswap, sizeof(SWAP_IQ_COMMAND) - sizeof(header));

        if (bytes_read <= 0) {
          t_print("server_client_thread: short read for SWAP_IQ\n");
          t_perror("server_client_thread");
          // dialog box?
          return NULL;
        }

        g_idle_add(remote_command, swap_iq_command);
      }
      break;

    case CMD_RESP_REGION:
      t_print("server_client_thread: CMD_RESP_REGION\n");
      {
        REGION_COMMAND *region_command = g_new(REGION_COMMAND, 1);
        region_command->header.data_type = header.data_type;
        region_command->header.version = header.version;
        region_command->header.context.client = client;
        bytes_read = recv_bytes(client->socket, (char *)&region_command->region, sizeof(REGION_COMMAND) - sizeof(header));

        if (bytes_read <= 0) {
          t_print("server_client_thread: short read for REGION\n");
          t_perror("server_client_thread");
          // dialog box?
          return NULL;
        }

        g_idle_add(remote_command, region_command);
      }
      break;

    case CMD_RESP_MUTE_RX:
      t_print("server_client_thread: CMD_RESP_MUTE_RX\n");
      {
        MUTE_RX_COMMAND *mute_rx_command = g_new(MUTE_RX_COMMAND, 1);
        mute_rx_command->header.data_type = header.data_type;
        mute_rx_command->header.version = header.version;
        mute_rx_command->header.context.client = client;
        bytes_read = recv_bytes(client->socket, (char *)&mute_rx_command->mute, sizeof(MUTE_RX_COMMAND) - sizeof(header));

        if (bytes_read <= 0) {
          t_print("server_client_thread: short read for MUTE_RX\n");
          t_perror("server_client_thread");
          // dialog box?
          return NULL;
        }

        g_idle_add(remote_command, mute_rx_command);
      }
      break;

    default:
      t_print("server_client_thread: UNKNOWN command: %d\n", ntohs(header.data_type));
      break;
    }
  }

  // close the socket to force listen to terminate
  t_print("client disconnected\n");

  if (client->socket != -1) {
    close(client->socket);
    client->socket = -1;
  }

  delete_client(client);
  return NULL;
}

void send_start_spectrum(int s, int rx) {
  SPECTRUM_COMMAND command;
  command.header.sync = REMOTE_SYNC;
  command.header.data_type = htons(CMD_RESP_SPECTRUM);
  command.header.version = htonl(CLIENT_SERVER_VERSION);
  command.id = rx;
  command.start_stop = 1;
  int bytes_sent = send_bytes(s, (char *)&command, sizeof(command));

  if (bytes_sent < 0) {
    t_perror("send_command");
  } else {
    //t_print("send_command: %d\n",bytes_sent);
  }
}

void send_vfo_frequency(int s, int rx, long long hz) {
  FREQ_COMMAND command;
  t_print("send_vfo_frequency\n");
  command.header.sync = REMOTE_SYNC;
  command.header.data_type = htons(CMD_RESP_RX_FREQ);
  command.header.version = htonl(CLIENT_SERVER_VERSION);
  command.id = rx;
  command.hz = htonll(hz);
  int bytes_sent = send_bytes(s, (char *)&command, sizeof(command));

  if (bytes_sent < 0) {
    t_perror("send_command");
  } else {
    //t_print("send_command: %d\n",bytes_sent);
  }
}

void send_vfo_move_to(int s, int rx, long long hz) {
  MOVE_TO_COMMAND command;
  t_print("send_vfo_move_to\n");
  command.header.sync = REMOTE_SYNC;
  command.header.data_type = htons(CMD_RESP_RX_MOVETO);
  command.header.version = htonl(CLIENT_SERVER_VERSION);
  command.id = rx;
  command.hz = htonll(hz);
  int bytes_sent = send_bytes(s, (char *)&command, sizeof(command));

  if (bytes_sent < 0) {
    t_perror("send_command");
  } else {
    //t_print("send_command: %d\n",bytes_sent);
  }
}

void send_vfo_move(int s, int rx, long long hz, int round) {
  MOVE_COMMAND command;
  t_print("send_vfo_move\n");
  command.header.sync = REMOTE_SYNC;
  command.header.data_type = htons(CMD_RESP_RX_MOVE);
  command.header.version = htonl(CLIENT_SERVER_VERSION);
  command.id = rx;
  command.hz = htonll(hz);
  command.round = round;
  int bytes_sent = send_bytes(s, (char *)&command, sizeof(command));

  if (bytes_sent < 0) {
    t_perror("send_command");
  } else {
    //t_print("send_command: %d\n",bytes_sent);
  }
}

void update_vfo_move(int rx, long long hz, int round) {
  g_mutex_lock(&accumulated_mutex);
  accumulated_hz += hz;
  accumulated_round = round;
  g_mutex_unlock(&accumulated_mutex);
}

void send_vfo_step(int s, int rx, int steps) {
  STEP_COMMAND command;
  short stps = (short)steps;
  t_print("send_vfo_step rx=%d steps=%d s=%d\n", rx, steps, stps);
  command.header.sync = REMOTE_SYNC;
  command.header.data_type = htons(CMD_RESP_RX_STEP);
  command.header.version = htonl(CLIENT_SERVER_VERSION);
  command.id = rx;
  command.steps = htons(stps);
  int bytes_sent = send_bytes(s, (char *)&command, sizeof(command));

  if (bytes_sent < 0) {
    t_perror("send_command");
  } else {
    //t_print("send_command: %d\n",bytes_sent);
  }
}

void update_vfo_step(int rx, int steps) {
  g_mutex_lock(&accumulated_mutex);
  accumulated_steps += steps;
  g_mutex_unlock(&accumulated_mutex);
}

void send_zoom(int s, int rx, int zoom) {
  ZOOM_COMMAND command;
  short z = (short)zoom;
  t_print("send_zoom rx=%d zoom=%d\n", rx, zoom);
  command.header.sync = REMOTE_SYNC;
  command.header.data_type = htons(CMD_RESP_RX_ZOOM);
  command.header.version = htonl(CLIENT_SERVER_VERSION);
  command.id = rx;
  command.zoom = htons(z);
  int bytes_sent = send_bytes(s, (char *)&command, sizeof(command));

  if (bytes_sent < 0) {
    t_perror("send_command");
  } else {
    //t_print("send_command: %d\n",bytes_sent);
  }
}

void send_pan(int s, int rx, int pan) {
  PAN_COMMAND command;
  short p = (short)pan;
  t_print("send_pan rx=%d pan=%d\n", rx, pan);
  command.header.sync = REMOTE_SYNC;
  command.header.data_type = htons(CMD_RESP_RX_PAN);
  command.header.version = htonl(CLIENT_SERVER_VERSION);
  command.id = rx;
  command.pan = htons(p);
  int bytes_sent = send_bytes(s, (char *)&command, sizeof(command));

  if (bytes_sent < 0) {
    t_perror("send_command");
  } else {
    //t_print("send_command: %d\n",bytes_sent);
  }
}

void send_volume(int s, int rx, double volume) {
  VOLUME_COMMAND command;
  t_print("send_volume rx=%d volume=%f\n", rx, volume);
  command.header.sync = REMOTE_SYNC;
  command.header.data_type = htons(CMD_RESP_RX_VOLUME);
  command.header.version = htonl(CLIENT_SERVER_VERSION);
  command.id = rx;
  command.volume = htond(volume);
  int bytes_sent = send_bytes(s, (char *)&command, sizeof(command));

  if (bytes_sent < 0) {
    t_perror("send_command");
  } else {
    //t_print("send_command: %d\n",bytes_sent);
  }
}

void send_agc(int s, int rx, int agc) {
  AGC_COMMAND command;
  short a = (short)agc;
  t_print("send_agc rx=%d agc=%d\n", rx, agc);
  command.header.sync = REMOTE_SYNC;
  command.header.data_type = htons(CMD_RESP_RX_AGC);
  command.header.version = htonl(CLIENT_SERVER_VERSION);
  command.id = rx;
  command.agc = htons(a);
  int bytes_sent = send_bytes(s, (char *)&command, sizeof(command));

  if (bytes_sent < 0) {
    t_perror("send_command");
  } else {
    //t_print("send_command: %d\n",bytes_sent);
  }
}

void send_agc_gain(int s, int rx, double gain, double hang, double thresh, double hang_thresh) {
  AGC_GAIN_COMMAND command;
  t_print("send_agc_gain rx=%d gain=%f hang=%f thresh=%f hang_thresh=%f\n", rx, gain, hang, thresh, hang_thresh);
  command.header.sync = REMOTE_SYNC;
  command.header.data_type = htons(CMD_RESP_RX_AGC_GAIN);
  command.header.version = htonl(CLIENT_SERVER_VERSION);
  command.id = rx;
  command.gain = htond(gain);
  command.hang = htond(hang);
  command.thresh = htond(thresh);
  command.hang_thresh = htond(hang_thresh);
  int bytes_sent = send_bytes(s, (char *)&command, sizeof(command));

  if (bytes_sent < 0) {
    t_perror("send_command");
  } else {
    //t_print("send_command: %d\n",bytes_sent);
  }
}

void send_rfgain(int s, int id, double gain) {
  RFGAIN_COMMAND command;
  t_print("send_rfgain rx=%d gain=%f\n", id, gain);
  command.header.sync = REMOTE_SYNC;
  command.header.data_type = htons(CMD_RESP_RX_GAIN);
  command.header.version = htonl(CLIENT_SERVER_VERSION);
  command.id = id;
  command.gain = htond(gain);
  int bytes_sent = send_bytes(s, (char *)&command, sizeof(command));

  if (bytes_sent < 0) {
    t_perror("send_command");
  } else {
    t_print("send_command RFGAIN: %d\n", bytes_sent);
  }
}

void send_attenuation(int s, int rx, int attenuation) {
  ATTENUATION_COMMAND command;
  short a = (short)attenuation;
  t_print("send_attenuation rx=%d attenuation=%d\n", rx, attenuation);
  command.header.sync = REMOTE_SYNC;
  command.header.data_type = htons(CMD_RESP_RX_ATTENUATION);
  command.header.version = htonl(CLIENT_SERVER_VERSION);
  command.id = rx;
  command.attenuation = htons(a);
  int bytes_sent = send_bytes(s, (char *)&command, sizeof(command));

  if (bytes_sent < 0) {
    t_perror("send_command");
  } else {
    //t_print("send_command: %d\n",bytes_sent);
  }
}

void send_squelch(int s, int rx, int enable, int squelch) {
  SQUELCH_COMMAND command;
  short sq = (short)squelch;
  t_print("send_squelch rx=%d enable=%d squelch=%d\n", rx, enable, squelch);
  command.header.sync = REMOTE_SYNC;
  command.header.data_type = htons(CMD_RESP_RX_SQUELCH);
  command.header.version = htonl(CLIENT_SERVER_VERSION);
  command.id = rx;
  command.enable = enable;
  command.squelch = htons(sq);
  int bytes_sent = send_bytes(s, (char *)&command, sizeof(command));

  if (bytes_sent < 0) {
    t_perror("send_command");
  } else {
    //t_print("send_command: %d\n",bytes_sent);
  }
}

void send_noise(int s, int rx, int nb, int nr, int anf, int snb) {
  NOISE_COMMAND command;
  t_print("send_noise rx=%d nb=%d nr=%d anf=%d snb=%d\n", rx, nb, nr, anf, snb);
  command.header.sync = REMOTE_SYNC;
  command.header.data_type = htons(CMD_RESP_RX_NOISE);
  command.header.version = htonl(CLIENT_SERVER_VERSION);
  command.id = rx;
  command.nb = nb;
  command.nr = nr;
  command.anf = anf;
  command.snb = snb;
  int bytes_sent = send_bytes(s, (char *)&command, sizeof(command));

  if (bytes_sent < 0) {
    t_perror("send_command");
  } else {
    //t_print("send_command: %d\n",bytes_sent);
  }
}

void send_band(int s, int rx, int band) {
  BAND_COMMAND command;
  t_print("send_band rx=%d band=%d\n", rx, band);
  command.header.sync = REMOTE_SYNC;
  command.header.data_type = htons(CMD_RESP_RX_BAND);
  command.header.version = htonl(CLIENT_SERVER_VERSION);
  command.id = rx;
  command.band = htons(band);
  int bytes_sent = send_bytes(s, (char *)&command, sizeof(command));

  if (bytes_sent < 0) {
    t_perror("send_command");
  } else {
    //t_print("send_command: %d\n",bytes_sent);
  }
}

void send_mode(int s, int rx, int mode) {
  MODE_COMMAND command;
  t_print("send_mode rx=%d mode=%d\n", rx, mode);
  command.header.sync = REMOTE_SYNC;
  command.header.data_type = htons(CMD_RESP_RX_MODE);
  command.header.version = htonl(CLIENT_SERVER_VERSION);
  command.id = rx;
  command.mode = htons(mode);
  int bytes_sent = send_bytes(s, (char *)&command, sizeof(command));

  if (bytes_sent < 0) {
    t_perror("send_command");
  } else {
    //t_print("send_command: %d\n",bytes_sent);
  }
}

void send_filter(int s, int rx, int filter) {
  FILTER_COMMAND command;
  t_print("send_filter rx=%d filter=%d\n", rx, filter);
  command.header.sync = REMOTE_SYNC;
  command.header.data_type = htons(CMD_RESP_RX_FILTER);
  command.header.version = htonl(CLIENT_SERVER_VERSION);
  command.id = rx;
  command.filter = htons(filter);
  command.filter_low = htons(receiver[rx]->filter_low);
  command.filter_high = htons(receiver[rx]->filter_high);
  int bytes_sent = send_bytes(s, (char *)&command, sizeof(command));

  if (bytes_sent < 0) {
    t_perror("send_command");
  } else {
    //t_print("send_command: %d\n",bytes_sent);
  }
}

void send_split(int s, int split) {
  SPLIT_COMMAND command;
  t_print("send_split split=%d\n", split);
  command.header.sync = REMOTE_SYNC;
  command.header.data_type = htons(CMD_RESP_SPLIT);
  command.header.version = htonl(CLIENT_SERVER_VERSION);
  command.split = split;
  int bytes_sent = send_bytes(s, (char *)&command, sizeof(command));

  if (bytes_sent < 0) {
    t_perror("send_command");
  } else {
    //t_print("send_command: %d\n",bytes_sent);
  }
}

void send_sat(int s, int sat) {
  SAT_COMMAND command;
  t_print("send_sat sat=%d\n", sat);
  command.header.sync = REMOTE_SYNC;
  command.header.data_type = htons(CMD_RESP_SAT);
  command.header.version = htonl(CLIENT_SERVER_VERSION);
  command.sat = sat;
  int bytes_sent = send_bytes(s, (char *)&command, sizeof(command));

  if (bytes_sent < 0) {
    t_perror("send_command");
  } else {
    //t_print("send_command: %d\n",bytes_sent);
  }
}

void send_dup(int s, int dup) {
  DUP_COMMAND command;
  t_print("send_dup dup=%d\n", dup);
  command.header.sync = REMOTE_SYNC;
  command.header.data_type = htons(CMD_RESP_DUP);
  command.header.version = htonl(CLIENT_SERVER_VERSION);
  command.dup = dup;
  int bytes_sent = send_bytes(s, (char *)&command, sizeof(command));

  if (bytes_sent < 0) {
    t_perror("send_command");
  } else {
    //t_print("send_command: %d\n",bytes_sent);
  }
}

void send_fps(int s, int rx, int fps) {
  FPS_COMMAND command;
  t_print("send_fps rx=%d fps=%d\n", rx, fps);
  command.header.sync = REMOTE_SYNC;
  command.header.data_type = htons(CMD_RESP_RX_FPS);
  command.header.version = htonl(CLIENT_SERVER_VERSION);
  command.id = rx;
  command.fps = htons(fps);
  int bytes_sent = send_bytes(s, (char *)&command, sizeof(command));

  if (bytes_sent < 0) {
    t_perror("send_command");
  } else {
    //t_print("send_command: %d\n",bytes_sent);
  }
}

void send_lock(int s, int lock) {
  LOCK_COMMAND command;
  t_print("send_lock lock=%d\n", lock);
  command.header.sync = REMOTE_SYNC;
  command.header.data_type = htons(CMD_RESP_LOCK);
  command.header.version = htonl(CLIENT_SERVER_VERSION);
  command.lock = lock;
  int bytes_sent = send_bytes(s, (char *)&command, sizeof(command));

  if (bytes_sent < 0) {
    t_perror("send_command");
  } else {
    //t_print("send_command: %d\n",bytes_sent);
  }
}

void send_ctun(int s, int vfo, int ctun) {
  CTUN_COMMAND command;
  t_print("send_ctun vfo=%d ctun=%d\n", vfo, ctun);
  command.header.sync = REMOTE_SYNC;
  command.header.data_type = htons(CMD_RESP_CTUN);
  command.header.version = htonl(CLIENT_SERVER_VERSION);
  command.id = vfo;
  command.ctun = ctun;
  int bytes_sent = send_bytes(s, (char *)&command, sizeof(command));

  if (bytes_sent < 0) {
    t_perror("send_command");
  } else {
    //t_print("send_command: %d\n",bytes_sent);
  }
}

void send_rx_select(int s, int rx) {
  RX_SELECT_COMMAND command;
  t_print("send_rx_select rx=%d\n", rx);
  command.header.sync = REMOTE_SYNC;
  command.header.data_type = htons(CMD_RESP_RX_SELECT);
  command.header.version = htonl(CLIENT_SERVER_VERSION);
  command.id = rx;
  int bytes_sent = send_bytes(s, (char *)&command, sizeof(command));

  if (bytes_sent < 0) {
    t_perror("send_command");
  } else {
    //t_print("send_command: %d\n",bytes_sent);
  }
}

void send_vfo(int s, int action) {
  VFO_COMMAND command;
  t_print("send_vfo action=%d\n", action);
  command.header.sync = REMOTE_SYNC;
  command.header.data_type = htons(CMD_RESP_VFO);
  command.header.version = htonl(CLIENT_SERVER_VERSION);
  command.id = action;
  int bytes_sent = send_bytes(s, (char *)&command, sizeof(command));

  if (bytes_sent < 0) {
    t_perror("send_command");
  } else {
    //t_print("send_command: %d\n",bytes_sent);
  }
}

void send_rit_toggle(int s, int rx) {
  RIT_TOGGLE_COMMAND command;
  t_print("send_rit_enable rx=%d\n", rx);
  command.header.sync = REMOTE_SYNC;
  command.header.data_type = htons(CMD_RESP_RIT_TOGGLE);
  command.header.version = htonl(CLIENT_SERVER_VERSION);
  command.id = rx;
  int bytes_sent = send_bytes(s, (char *)&command, sizeof(command));

  if (bytes_sent < 0) {
    t_perror("send_command");
  } else {
    //t_print("send_command: %d\n",bytes_sent);
  }
}

void send_rit_clear(int s, int rx) {
  RIT_CLEAR_COMMAND command;
  t_print("send_rit_clear rx=%d\n", rx);
  command.header.sync = REMOTE_SYNC;
  command.header.data_type = htons(CMD_RESP_RIT_CLEAR);
  command.header.version = htonl(CLIENT_SERVER_VERSION);
  command.id = rx;
  int bytes_sent = send_bytes(s, (char *)&command, sizeof(command));

  if (bytes_sent < 0) {
    t_perror("send_command");
  } else {
    //t_print("send_command: %d\n",bytes_sent);
  }
}

void send_rit(int s, int rx, int rit) {
  RIT_COMMAND command;
  t_print("send_rit rx=%d rit=%d\n", rx, rit);
  command.header.sync = REMOTE_SYNC;
  command.header.data_type = htons(CMD_RESP_RIT);
  command.header.version = htonl(CLIENT_SERVER_VERSION);
  command.id = rx;
  command.rit = htons(rit);
  int bytes_sent = send_bytes(s, (char *)&command, sizeof(command));

  if (bytes_sent < 0) {
    t_perror("send_command");
  } else {
    //t_print("send_command: %d\n",bytes_sent);
  }
}

// NOTYETUSED
void send_xit_toggle(int s) {
  XIT_TOGGLE_COMMAND command;
  t_print("send_xit_toggle\n");
  command.header.sync = REMOTE_SYNC;
  command.header.data_type = htons(CMD_RESP_XIT_TOGGLE);
  command.header.version = htonl(CLIENT_SERVER_VERSION);
  int bytes_sent = send_bytes(s, (char *)&command, sizeof(command));

  if (bytes_sent < 0) {
    t_perror("send_command");
  } else {
    //t_print("send_command: %d\n",bytes_sent);
  }
}

//NOTYETUSED
void send_xit_clear(int s) {
  XIT_CLEAR_COMMAND command;
  t_print("send_xit_clear\n");
  command.header.sync = REMOTE_SYNC;
  command.header.data_type = htons(CMD_RESP_XIT_CLEAR);
  command.header.version = htonl(CLIENT_SERVER_VERSION);
  int bytes_sent = send_bytes(s, (char *)&command, sizeof(command));

  if (bytes_sent < 0) {
    t_perror("send_command");
  } else {
    //t_print("send_command: %d\n",bytes_sent);
  }
}

//NOTYETUSED
void send_xit(int s, int xit) {
  XIT_COMMAND command;
  t_print("send_xit xit=%d\n", xit);
  command.header.sync = REMOTE_SYNC;
  command.header.data_type = htons(CMD_RESP_XIT);
  command.header.version = htonl(CLIENT_SERVER_VERSION);
  command.xit = htons(xit);
  int bytes_sent = send_bytes(s, (char *)&command, sizeof(command));

  if (bytes_sent < 0) {
    t_perror("send_command");
  } else {
    //t_print("send_command: %d\n",bytes_sent);
  }
}

void send_sample_rate(int s, int rx, int sample_rate) {
  SAMPLE_RATE_COMMAND command;
  long long rate = (long long)sample_rate;
  t_print("send_sample_rate rx=%d rate=%lld\n", rx, rate);
  command.header.sync = REMOTE_SYNC;
  command.header.data_type = htons(CMD_RESP_SAMPLE_RATE);
  command.header.version = htonl(CLIENT_SERVER_VERSION);
  command.id = rx;
  command.sample_rate = htonll(rate);
  int bytes_sent = send_bytes(s, (char *)&command, sizeof(command));

  if (bytes_sent < 0) {
    t_perror("send_command");
  } else {
    //t_print("send_command: %d\n",bytes_sent);
  }
}

void send_receivers(int s, int receivers) {
  RECEIVERS_COMMAND command;
  t_print("send_receivers receivers=%d\n", receivers);
  command.header.sync = REMOTE_SYNC;
  command.header.data_type = htons(CMD_RESP_RECEIVERS);
  command.header.version = htonl(CLIENT_SERVER_VERSION);
  command.receivers = receivers;
  int bytes_sent = send_bytes(s, (char *)&command, sizeof(command));

  if (bytes_sent < 0) {
    t_perror("send_command");
  } else {
    //t_print("send_command: %d\n",bytes_sent);
  }
}

void send_rit_increment(int s, int increment) {
  RIT_INCREMENT_COMMAND command;
  t_print("send_rit_increment increment=%d\n", increment);
  command.header.sync = REMOTE_SYNC;
  command.header.data_type = htons(CMD_RESP_RIT_INCREMENT);
  command.header.version = htonl(CLIENT_SERVER_VERSION);
  command.increment = htons(increment);
  int bytes_sent = send_bytes(s, (char *)&command, sizeof(command));

  if (bytes_sent < 0) {
    t_perror("send_command");
  } else {
    //t_print("send_command: %d\n",bytes_sent);
  }
}

void send_filter_board(int s, int filter_board) {
  FILTER_BOARD_COMMAND command;
  t_print("send_filter_board filter_board=%d\n", filter_board);
  command.header.sync = REMOTE_SYNC;
  command.header.data_type = htons(CMD_RESP_FILTER_BOARD);
  command.header.version = htonl(CLIENT_SERVER_VERSION);
  command.filter_board = filter_board;
  int bytes_sent = send_bytes(s, (char *)&command, sizeof(command));

  if (bytes_sent < 0) {
    t_perror("send_command");
  } else {
    //t_print("send_command: %d\n",bytes_sent);
  }
}

void send_swap_iq(int s, int iqswap) {
  SWAP_IQ_COMMAND command;
  t_print("send_swap_iq iqswap=%d\n", iqswap);
  command.header.sync = REMOTE_SYNC;
  command.header.data_type = htons(CMD_RESP_SWAP_IQ);
  command.header.version = htonl(CLIENT_SERVER_VERSION);
  command.iqswap = iqswap;
  int bytes_sent = send_bytes(s, (char *)&command, sizeof(command));

  if (bytes_sent < 0) {
    t_perror("send_command");
  } else {
    //t_print("send_command: %d\n",bytes_sent);
  }
}

void send_region(int s, int region) {
  REGION_COMMAND command;
  t_print("send_region region=%d\n", region);
  command.header.sync = REMOTE_SYNC;
  command.header.data_type = htons(CMD_RESP_REGION);
  command.header.version = htonl(CLIENT_SERVER_VERSION);
  command.region = region;
  int bytes_sent = send_bytes(s, (char *)&command, sizeof(command));

  if (bytes_sent < 0) {
    t_perror("send_command");
  } else {
    //t_print("send_command: %d\n",bytes_sent);
  }
}

void send_mute_rx(int s, int mute) {
  MUTE_RX_COMMAND command;
  t_print("send_mute_rx mute=%d\n", mute);
  command.header.sync = REMOTE_SYNC;
  command.header.data_type = htons(CMD_RESP_MUTE_RX);
  command.header.version = htonl(CLIENT_SERVER_VERSION);
  command.mute = mute;
  int bytes_sent = send_bytes(s, (char *)&command, sizeof(command));

  if (bytes_sent < 0) {
    t_perror("send_command");
  } else {
    //t_print("send_command: %d\n",bytes_sent);
  }
}

static void *listen_thread(void *arg) {
  struct sockaddr_in address;
  int on = 1;
  t_print("hpsdr_server: listening on port %d\n", listen_port);

  while (running) {
    // create TCP socket to listen on
    listen_socket = socket(AF_INET, SOCK_STREAM, 0);

    if (listen_socket < 0) {
      t_print("listen_thread: socket failed\n");
      return NULL;
    }

    setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    setsockopt(listen_socket, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));
    // bind to listening port
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(listen_port);

    if (bind(listen_socket, (struct sockaddr * )&address, sizeof(address)) < 0) {
      t_print("listen_thread: bind failed\n");
      return NULL;
    }

    // listen for connections
    if (listen(listen_socket, 5) < 0) {
      t_print("listen_thread: listen failed\n");
      break;
    }

    REMOTE_CLIENT* client = g_new(REMOTE_CLIENT, 1);
    client->spectrum_update_timer_id = 0;
    client->address_length = sizeof(client->address);
    client->running = TRUE;
    t_print("hpsdr_server: accept\n");

    if ((client->socket = accept(listen_socket, (struct sockaddr * )&client->address, &client->address_length)) < 0) {
      t_print("listen_thread: accept failed\n");
      g_free(client);
      continue;
    }

    char s[128];
    inet_ntop(AF_INET, &(((struct sockaddr_in *)&client->address)->sin_addr), s, 128);
    t_print("Client_connected from %s\n", s);
    client->thread_id = g_thread_new("SSDR_client", server_client_thread, client);
    add_client(client);
    close(listen_socket);
    (void) g_thread_join(client->thread_id);
  }

  return NULL;
}

int create_hpsdr_server() {
  t_print("create_hpsdr_server\n");
  g_mutex_init(&client_mutex);
  clients = NULL;
  running = TRUE;
  listen_thread_id = g_thread_new( "HPSDR_listen", listen_thread, NULL);
  return 0;
}

int destroy_hpsdr_server() {
  t_print("destroy_hpsdr_server\n");
  running = FALSE;
  return 0;
}

// CLIENT Code

static int check_vfo(void *arg) {
  if (!running) { return FALSE; }

  g_mutex_lock(&accumulated_mutex);

  if (accumulated_steps != 0) {
    send_vfo_step(client_socket, active_receiver->id, accumulated_steps);
    accumulated_steps = 0;
  }

  if (accumulated_hz != 0LL || accumulated_round) {
    send_vfo_move(client_socket, active_receiver->id, accumulated_hz, accumulated_round);
    accumulated_hz = 0LL;
    accumulated_round = FALSE;
  }

  g_mutex_unlock(&accumulated_mutex);
  return TRUE;
}

static char server_host[128];
static int delay = 0;

int start_spectrum(void *data) {
  const RECEIVER *rx = (RECEIVER *)data;

  if (delay != 3) {
    delay++;
    t_print("start_spectrum: delay %d\n", delay);
    return TRUE;
  }

  send_start_spectrum(client_socket, rx->id);
  return FALSE;
}

void start_vfo_timer() {
  g_mutex_init(&accumulated_mutex);
  check_vfo_timer_id = gdk_threads_add_timeout_full(G_PRIORITY_HIGH_IDLE, 100, check_vfo, NULL, NULL);
  t_print("check_vfo_timer_id %d\n", check_vfo_timer_id);
}

static void *client_thread(void* arg) {
  int bytes_read;
  HEADER header;
  char *server = (char *)arg;
  running = TRUE;

  while (running) {
    bytes_read = recv_bytes(client_socket, (char *)&header, sizeof(header));

    if (bytes_read <= 0) {
      t_print("client_thread: short read for HEADER\n");
      t_perror("client_thread");
      // dialog box?
      return NULL;
    }

    switch (ntohs(header.data_type)) {
    case INFO_RADIO: {
      RADIO_DATA radio_data;
      bytes_read = recv_bytes(client_socket, (char *)&radio_data.name, sizeof(radio_data) - sizeof(header));

      if (bytes_read <= 0) {
        t_print("client_thread: short read for RADIO_DATA\n");
        t_perror("client_thread");
        // dialog box?
        return NULL;
      }

      t_print("INFO_RADIO: %d\n", bytes_read);
      // build a radio (discovered) structure
      radio = g_new(DISCOVERED, 1);
      STRLCPY(radio->name, radio_data.name, sizeof(radio->name));
      // Note we use "protocol" and "device" througout the program
      protocol = radio->protocol = ntohs(radio_data.protocol);
      device = radio->device = ntohs(radio_data.device);
      uint64_t temp = ntohll(radio_data.frequency_min);
      radio->frequency_min = (double)temp;
      temp = ntohll(radio_data.frequency_max);
      radio->frequency_max = (double)temp;
      radio->supported_receivers = ntohs(radio_data.supported_receivers);
      temp = ntohll(radio_data.sample_rate);
      radio_sample_rate = (int)temp;
#ifdef SOAPYSDR

      if (protocol == SOAPYSDR_PROTOCOL) {
        radio->info.soapy.sample_rate = (int)temp;
      }

#endif
      // cppcheck-suppress uninitvar
      locked = radio_data.locked;
      receivers = ntohs(radio_data.receivers);
      //can_transmit=radio_data.can_transmit;
      can_transmit = 0; // forced temporarily until Client/Server supports transmitters
      split = radio_data.split;
      sat_mode = radio_data.sat_mode;
      duplex = radio_data.duplex;
      have_rx_gain = radio_data.have_rx_gain;
      short s = ntohs(radio_data.rx_gain_calibration);
      rx_gain_calibration = (int)s;
      filter_board = ntohs(radio_data.filter_board);
      t_print("have_rx_gain=%d rx_gain_calibration=%d filter_board=%d\n", have_rx_gain, rx_gain_calibration, filter_board);
      //
      // A semaphore for safely writing to the props file
      //
      g_mutex_init(&property_mutex);
      snprintf(title, 128, "piHPSDR: %s remote at %s", radio->name, server);
      g_idle_add(ext_set_title, (void *)title);
    }
    break;

    case INFO_ADC: {
      ADC_DATA adc_data;
      bytes_read = recv_bytes(client_socket, (char *)&adc_data.adc, sizeof(adc_data) - sizeof(header));

      if (bytes_read <= 0) {
        t_print("client_thread: short read for ADC_DATA\n");
        t_perror("client_thread");
        // dialog box?
        return NULL;
      }

      t_print("INFO_ADC: %d\n", bytes_read);
      // cppcheck-suppress uninitStructMember
      int i = adc_data.adc;
      adc[i].filters = ntohs(adc_data.filters);
      adc[i].hpf = ntohs(adc_data.hpf);
      adc[i].lpf = ntohs(adc_data.lpf);
      adc[i].antenna = ntohs(adc_data.antenna);
      // cppcheck-suppress uninitvar
      adc[i].dither = adc_data.dither;
      adc[i].random = adc_data.random;
      adc[i].preamp = adc_data.preamp;
      adc[i].attenuation = ntohs(adc_data.attenuation);
      adc[i].gain = ntohd(adc_data.gain);
      adc[i].min_gain = ntohd(adc_data.min_gain);
      adc[i].max_gain = ntohd(adc_data.max_gain);
    }
    break;

    case INFO_RECEIVER: {
      RECEIVER_DATA receiver_data;
      bytes_read = recv_bytes(client_socket, (char *)&receiver_data.rx, sizeof(receiver_data) - sizeof(header));

      if (bytes_read <= 0) {
        t_print("client_thread: short read for RECEIVER_DATA\n");
        t_perror("client_thread");
        // dialog box?
        return NULL;
      }

      t_print("INFO_RECEIVER: %d\n", bytes_read);
      // cppcheck-suppress uninitStructMember
      int rx = receiver_data.rx;
      receiver[rx] = g_new(RECEIVER, 1);
      receiver[rx]->id = rx;
      receiver[rx]->adc = ntohs(receiver_data.adc);;
      long long rate = ntohll(receiver_data.sample_rate);
      receiver[rx]->sample_rate = (int)rate;
      // cppcheck-suppress uninitvar
      receiver[rx]->displaying = receiver_data.displaying;
      receiver[rx]->display_panadapter = receiver_data.display_panadapter;
      receiver[rx]->display_waterfall = receiver_data.display_waterfall;
      receiver[rx]->fps = ntohs(receiver_data.fps);
      receiver[rx]->agc = receiver_data.agc;
      receiver[rx]->agc_hang = ntohd(receiver_data.agc_hang);
      receiver[rx]->agc_thresh = ntohd(receiver_data.agc_thresh);
      receiver[rx]->agc_hang_threshold = ntohd(receiver_data.agc_hang_thresh);
      receiver[rx]->nb = receiver_data.nb;
      receiver[rx]->nr = receiver_data.nr;
      receiver[rx]->anf = receiver_data.anf;
      receiver[rx]->snb = receiver_data.snb;
      short s = ntohs(receiver_data.filter_low);
      receiver[rx]->filter_low = (int)s;
      s = ntohs(receiver_data.filter_high);
      receiver[rx]->filter_high = (int)s;
      s = ntohs(receiver_data.panadapter_low);
      receiver[rx]->panadapter_low = (int)s;
      s = ntohs(receiver_data.panadapter_high);
      receiver[rx]->panadapter_high = (int)s;
      s = ntohs(receiver_data.panadapter_step);
      receiver[rx]->panadapter_step = s;
      s = ntohs(receiver_data.waterfall_low);
      receiver[rx]->waterfall_low = (int)s;
      s = ntohs(receiver_data.waterfall_high);
      receiver[rx]->waterfall_high = s;
      receiver[rx]->waterfall_automatic = receiver_data.waterfall_automatic;
      receiver[rx]->pixels = ntohs(receiver_data.pixels);
      receiver[rx]->zoom = ntohs(receiver_data.zoom);
      receiver[rx]->pan = ntohs(receiver_data.pan);
      receiver[rx]->width = ntohs(receiver_data.width);
      receiver[rx]->height = ntohs(receiver_data.height);
      receiver[rx]->x = ntohs(receiver_data.x);
      receiver[rx]->y = ntohs(receiver_data.y);
      receiver[rx]->volume = ntohd(receiver_data.volume);
      receiver[rx]->agc_gain = ntohd(receiver_data.agc_gain);
      //
      receiver[rx]->pixel_samples = NULL;
      g_mutex_init(&receiver[rx]->display_mutex);
      receiver[rx]->hz_per_pixel = (double)receiver[rx]->sample_rate / (double)receiver[rx]->pixels;
      //receiver[rx]->playback_handle=NULL;
      receiver[rx]->local_audio_buffer = NULL;
      receiver[rx]->local_audio = 0;
      g_mutex_init(&receiver[rx]->local_audio_mutex);
      receiver[rx]->mute_when_not_active = 0;
      receiver[rx]->audio_channel = STEREO;
      receiver[rx]->audio_device = -1;
      receiver[rx]->mute_radio = 0;
      receiver[rx]->display_gradient = receiver_data.display_gradient;
      receiver[rx]->display_filled = receiver_data.display_filled;
      receiver[rx]->display_detector_mode = receiver_data.display_detector_mode;
      receiver[rx]->display_average_mode = receiver_data.display_average_mode;
      receiver[rx]->display_average_time = (double) ntohs(receiver_data.display_average_time);
      t_print("rx=%d width=%d sample_rate=%d hz_per_pixel=%f pan=%d zoom=%d\n", rx, receiver[rx]->width,
              receiver[rx]->sample_rate, receiver[rx]->hz_per_pixel, receiver[rx]->pan, receiver[rx]->zoom);
    }
    break;

    case INFO_TRANSMITTER: {
      t_print("INFO_TRANSMITTER\n");
    }
    break;

    case INFO_VFO: {
      VFO_DATA vfo_data;
      bytes_read = recv_bytes(client_socket, (char *)&vfo_data.vfo, sizeof(vfo_data) - sizeof(header));

      if (bytes_read <= 0) {
        t_print("client_thread: short read for VFO_DATA\n");
        t_perror("client_thread");
        // dialog box?
        return NULL;
      }

      t_print("INFO_VFO: %d\n", bytes_read);
      // cppcheck-suppress uninitStructMember
      int v = vfo_data.vfo;
      vfo[v].band = ntohs(vfo_data.band);
      vfo[v].bandstack = ntohs(vfo_data.bandstack);
      vfo[v].frequency = ntohll(vfo_data.frequency);
      vfo[v].mode = ntohs(vfo_data.mode);
      vfo[v].filter = ntohs(vfo_data.filter);
      // cppcheck-suppress uninitvar
      vfo[v].ctun = vfo_data.ctun;
      vfo[v].ctun_frequency = ntohll(vfo_data.ctun_frequency);
      vfo[v].rit_enabled = vfo_data.rit_enabled;
      vfo[v].rit = ntohll(vfo_data.rit);
      vfo[v].lo = ntohll(vfo_data.lo);
      vfo[v].offset = ntohll(vfo_data.offset);
      vfo[v].step   = ntohll(vfo_data.step);

      // when VFO-B is initialized we can create the visual. start the MIDI interface and start the data flowing
      if (v == VFO_B && !remote_started) {
        t_print("g_idle_add: remote_start\n");
        g_idle_add(remote_start, (gpointer)server);
      } else if (remote_started) {
        t_print("g_idle_add: ext_vfo_update\n");
        g_idle_add(ext_vfo_update, NULL);
      }
    }
    break;

    case INFO_SPECTRUM: {
      SPECTRUM_DATA spectrum_data;
      bytes_read = recv_bytes(client_socket, (char *)&spectrum_data.rx, sizeof(spectrum_data) - sizeof(header));

      if (bytes_read <= 0) {
        t_print("client_thread: short read for SPECTRUM_DATA\n");
        t_perror("client_thread");
        // dialog box?
        return NULL;
      }

      // cppcheck-suppress uninitStructMember
      int r = spectrum_data.rx;
      long long frequency_a = ntohll(spectrum_data.vfo_a_freq);
      long long frequency_b = ntohll(spectrum_data.vfo_b_freq);
      long long ctun_frequency_a = ntohll(spectrum_data.vfo_a_ctun_freq);
      long long ctun_frequency_b = ntohll(spectrum_data.vfo_b_ctun_freq);
      long long offset_a = ntohll(spectrum_data.vfo_a_offset);
      long long offset_b = ntohll(spectrum_data.vfo_b_offset);
      receiver[r]->meter = ntohd(spectrum_data.meter);
      short samples = ntohs(spectrum_data.samples);

      if (receiver[r]->pixel_samples == NULL) {
        receiver[r]->pixel_samples = g_new(float, (int)samples);
      }

      for (int i = 0; i < samples; i++) {
        short sample = ntohs(spectrum_data.sample[i]);
        receiver[r]->pixel_samples[i] = (float)sample;
      }

      if (vfo[VFO_A].frequency != frequency_a || vfo[VFO_B].frequency != frequency_b
          || vfo[VFO_A].ctun_frequency != ctun_frequency_a || vfo[VFO_B].ctun_frequency != ctun_frequency_b
          || vfo[VFO_A].offset != offset_a || vfo[VFO_B].offset != offset_b) {
        vfo[VFO_A].frequency = frequency_a;
        vfo[VFO_B].frequency = frequency_b;
        vfo[VFO_A].ctun_frequency = ctun_frequency_a;
        vfo[VFO_B].ctun_frequency = ctun_frequency_b;
        vfo[VFO_A].offset = offset_a;
        vfo[VFO_B].offset = offset_b;
        g_idle_add(ext_vfo_update, NULL);
      }

      g_idle_add(ext_receiver_remote_update_display, receiver[r]);
    }
    break;

    case INFO_AUDIO: {
      AUDIO_DATA adata;
      bytes_read = recv_bytes(client_socket, (char *)&adata.rx, sizeof(adata) - sizeof(header));

      if (bytes_read <= 0) {
        t_print("client_thread: short read for AUDIO_DATA\n");
        t_perror("client_thread");
        // dialog box?
        return NULL;
      }

      // cppcheck-suppress uninitStructMember
      RECEIVER *rx = receiver[adata.rx];
      int samples = ntohs(adata.samples);

      if (rx->local_audio) {
        for (int i = 0; i < samples; i++) {
          short left_sample = ntohs(adata.sample[(i * 2)]);
          short right_sample = ntohs(adata.sample[(i * 2) + 1]);
          audio_write(rx, (float)left_sample / 32767.0, (float)right_sample / 32767.0);
        }
      }
    }
    break;

    case CMD_RESP_RX_ZOOM: {
      ZOOM_COMMAND zoom_cmd;
      bytes_read = recv_bytes(client_socket, (char *)&zoom_cmd.id, sizeof(zoom_cmd) - sizeof(header));

      if (bytes_read <= 0) {
        t_print("client_thread: short read for ZOOM_CMD\n");
        t_perror("client_thread");
        // dialog box?
        return NULL;
      }

      // cppcheck-suppress uninitStructMember
      int rx = zoom_cmd.id;
      short zoom = ntohs(zoom_cmd.zoom);
      t_print("CMD_RESP_RX_ZOOM: zoom=%d rx[%d]->zoom=%d\n", zoom, rx, receiver[rx]->zoom);

      if (receiver[rx]->zoom != zoom) {
        g_idle_add(ext_remote_set_zoom, GINT_TO_POINTER(zoom));
      } else {
        receiver[rx]->zoom = (int)(zoom + 0.5);
        receiver_update_zoom(receiver[rx]);
      }
    }
    break;

    case CMD_RESP_RX_PAN: {
      PAN_COMMAND pan_cmd;
      bytes_read = recv_bytes(client_socket, (char *)&pan_cmd.id, sizeof(pan_cmd) - sizeof(header));

      if (bytes_read <= 0) {
        t_print("client_thread: short read for PAN_CMD\n");
        t_perror("client_thread");
        // dialog box?
        return NULL;
      }

      // cppcheck-suppress uninitStructMember
      int rx = pan_cmd.id;
      short pan = ntohs(pan_cmd.pan);
      t_print("CMD_RESP_RX_PAN: pan=%d rx[%d]->pan=%d\n", pan, rx, receiver[rx]->pan);
      g_idle_add(ext_remote_set_pan, GINT_TO_POINTER(pan));
    }
    break;

    case CMD_RESP_RX_VOLUME: {
      VOLUME_COMMAND volume_cmd;
      bytes_read = recv_bytes(client_socket, (char *)&volume_cmd.id, sizeof(volume_cmd) - sizeof(header));

      if (bytes_read <= 0) {
        t_print("client_thread: short read for VOLUME_CMD\n");
        t_perror("client_thread");
        // dialog box?
        return NULL;
      }

      // cppcheck-suppress uninitStructMember
      int rx = volume_cmd.id;
      double volume = ntohd(volume_cmd.volume);
      t_print("CMD_RESP_RX_VOLUME: volume=%f rx[%d]->volume=%f\n", volume, rx, receiver[rx]->volume);
      receiver[rx]->volume = volume;
    }
    break;

    case CMD_RESP_RX_AGC: {
      AGC_COMMAND agc_cmd;
      bytes_read = recv_bytes(client_socket, (char *)&agc_cmd.id, sizeof(agc_cmd) - sizeof(header));

      if (bytes_read <= 0) {
        t_print("client_thread: short read for AGC_CMD\n");
        t_perror("client_thread");
        // dialog box?
        return NULL;
      }

      // cppcheck-suppress uninitStructMember
      int rx = agc_cmd.id;
      short a = ntohs(agc_cmd.agc);
      t_print("AGC_COMMAND: rx=%d agc=%d\n", rx, a);
      receiver[rx]->agc = (int)a;
      g_idle_add(ext_vfo_update, NULL);
    }
    break;

    case CMD_RESP_RX_AGC_GAIN: {
      AGC_GAIN_COMMAND agc_gain_cmd;
      bytes_read = recv_bytes(client_socket, (char *)&agc_gain_cmd.id, sizeof(agc_gain_cmd) - sizeof(header));

      if (bytes_read <= 0) {
        t_print("client_thread: short read for AGC_GAIN_CMD\n");
        t_perror("client_thread");
        // dialog box?
        return NULL;
      }

      // cppcheck-suppress uninitStructMember
      int rx = agc_gain_cmd.id;
      receiver[rx]->agc_gain = ntohd(agc_gain_cmd.gain);
      receiver[rx]->agc_hang = ntohd(agc_gain_cmd.hang);
      receiver[rx]->agc_thresh = ntohd(agc_gain_cmd.thresh);
      receiver[rx]->agc_hang_threshold = ntohd(agc_gain_cmd.hang_thresh);
    }
    break;

    case CMD_RESP_RX_GAIN: {
      RFGAIN_COMMAND command;
      bytes_read = recv_bytes(client_socket, (char *)&command.id, sizeof(command) - sizeof(header));

      if (bytes_read <= 0) {
        t_print("client_thread: short read for RFGAIN_CMD\n");
        t_perror("client_thread");
        // dialog box?
        return NULL;
      }

      // cppcheck-suppress uninitStructMember
      int rx = command.id;
      double gain = ntohd(command.gain);
      t_print("CMD_RESP_RX_GAIN: new=%f rx=%d old=%f\n", gain, rx, adc[receiver[rx]->adc].gain);
      adc[receiver[rx]->adc].gain = gain;
    }
    break;

    case CMD_RESP_RX_ATTENUATION: {
      ATTENUATION_COMMAND attenuation_cmd;
      bytes_read = recv_bytes(client_socket, (char *)&attenuation_cmd.id, sizeof(attenuation_cmd) - sizeof(header));

      if (bytes_read <= 0) {
        t_print("client_thread: short read for ATTENUATION_CMD\n");
        t_perror("client_thread");
        // dialog box?
        return NULL;
      }

      // cppcheck-suppress uninitStructMember
      int rx = attenuation_cmd.id;
      short attenuation = ntohs(attenuation_cmd.attenuation);
      t_print("CMD_RESP_RX_ATTENUATION: attenuation=%d attenuation[rx[%d]->adc]=%d\n", attenuation, rx,
              adc[receiver[rx]->adc].attenuation);
      adc[receiver[rx]->adc].attenuation = attenuation;
    }
    break;

    case CMD_RESP_RX_NOISE: {
      NOISE_COMMAND noise_command;
      bytes_read = recv_bytes(client_socket, (char *)&noise_command.id, sizeof(noise_command) - sizeof(header));

      if (bytes_read <= 0) {
        t_print("client_thread: short read for NOISE_CMD\n");
        t_perror("client_thread");
        // dialog box?
        return NULL;
      }

      // cppcheck-suppress uninitStructMember
      int id = noise_command.id;
      RECEIVER *rx = receiver[id];
      // cppcheck-suppress uninitvar
      rx->nb = noise_command.nb;
      rx->nr = noise_command.nr;
      rx->snb = noise_command.snb;
      rx->anf = noise_command.anf;

      if (id == 0) {
        mode_settings[vfo[id].mode].nb = rx->nb;
        mode_settings[vfo[id].mode].nr = rx->nr;
        mode_settings[vfo[id].mode].snb = rx->snb;
        mode_settings[vfo[id].mode].anf = rx->anf;
      }
      g_idle_add(ext_vfo_update, NULL);
    }
    break;

    case CMD_RESP_RX_MODE: {
      MODE_COMMAND mode_cmd;
      bytes_read = recv_bytes(client_socket, (char *)&mode_cmd.id, sizeof(mode_cmd) - sizeof(header));

      if (bytes_read <= 0) {
        t_print("client_thread: short read for MODE_CMD\n");
        t_perror("client_thread");
        // dialog box?
        return NULL;
      }

      // cppcheck-suppress uninitStructMember
      int rx = mode_cmd.id;
      short m = ntohs(mode_cmd.mode);
      vfo[rx].mode = m;
      g_idle_add(ext_vfo_update, NULL);
    }
    break;

    case CMD_RESP_RX_FILTER: {
      FILTER_COMMAND filter_cmd;
      bytes_read = recv_bytes(client_socket, (char *)&filter_cmd.id, sizeof(filter_cmd) - sizeof(header));

      if (bytes_read <= 0) {
        t_print("client_thread: short read for FILTER_CMD\n");
        t_perror("client_thread");
        // dialog box?
        return NULL;
      }

      // cppcheck-suppress uninitStructMember
      int rx = filter_cmd.id;
      short low = ntohs(filter_cmd.filter_low);
      short high = ntohs(filter_cmd.filter_high);
      receiver[rx]->filter_low = (int)low;
      receiver[rx]->filter_high = (int)high;
      g_idle_add(ext_vfo_update, NULL);
    }
    break;

    case CMD_RESP_SPLIT: {
      SPLIT_COMMAND split_cmd;
      bytes_read = recv_bytes(client_socket, (char *)&split_cmd.split, sizeof(split_cmd) - sizeof(header));

      if (bytes_read <= 0) {
        t_print("client_thread: short read for SPLIT_CMD\n");
        t_perror("client_thread");
        // dialog box?
        return NULL;
      }

      // cppcheck-suppress uninitStructMember
      split = split_cmd.split;
    }

    g_idle_add(ext_vfo_update, NULL);
    break;

    case CMD_RESP_SAT: {
      SAT_COMMAND sat_cmd;
      bytes_read = recv_bytes(client_socket, (char *)&sat_cmd.sat, sizeof(sat_cmd) - sizeof(header));

      if (bytes_read <= 0) {
        t_print("client_thread: short read for SAT_CMD\n");
        t_perror("client_thread");
        // dialog box?
        return NULL;
      }

      // cppcheck-suppress uninitStructMember
      sat_mode = sat_cmd.sat;
    }

    g_idle_add(ext_vfo_update, NULL);
    break;

    case CMD_RESP_DUP: {
      DUP_COMMAND dup_cmd;
      bytes_read = recv_bytes(client_socket, (char *)&dup_cmd.dup, sizeof(dup_cmd) - sizeof(header));

      if (bytes_read <= 0) {
        t_print("client_thread: short read for DUP_CMD\n");
        t_perror("client_thread");
        // dialog box?
        return NULL;
      }

      // cppcheck-suppress uninitStructMember
      duplex = dup_cmd.dup;
    }

    g_idle_add(ext_vfo_update, NULL);
    break;

    case CMD_RESP_LOCK: {
      LOCK_COMMAND lock_cmd;
      bytes_read = recv_bytes(client_socket, (char *)&lock_cmd.lock, sizeof(lock_cmd) - sizeof(header));

      if (bytes_read <= 0) {
        t_print("client_thread: short read for LOCK_CMD\n");
        t_perror("client_thread");
        // dialog box?
        return NULL;
      }

      // cppcheck-suppress uninitStructMember
      locked = lock_cmd.lock;
    }

    g_idle_add(ext_vfo_update, NULL);
    break;

    case CMD_RESP_RX_FPS: {
      FPS_COMMAND fps_cmd;
      bytes_read = recv_bytes(client_socket, (char *)&fps_cmd.id, sizeof(fps_cmd) - sizeof(header));

      if (bytes_read <= 0) {
        t_print("client_thread: short read for FPS_CMD\n");
        t_perror("client_thread");
        // dialog box?
        return NULL;
      }

      // cppcheck-suppress uninitStructMember
      int rx = fps_cmd.id;
      // cppcheck-suppress uninitvar
      receiver[rx]->fps = (int)fps_cmd.fps;
    }

    g_idle_add(ext_vfo_update, NULL);
    break;

    case CMD_RESP_RX_SELECT: {
      RX_SELECT_COMMAND rx_select_cmd;
      bytes_read = recv_bytes(client_socket, (char *)&rx_select_cmd.id, sizeof(rx_select_cmd) - sizeof(header));

      if (bytes_read <= 0) {
        t_print("client_thread: short read for RX_SELECT_CMD\n");
        t_perror("client_thread");
        // dialog box?
        return NULL;
      }

      // cppcheck-suppress uninitStructMember
      int rx = rx_select_cmd.id;
      receiver_set_active(receiver[rx]);
    }

    g_idle_add(ext_vfo_update, NULL);
    break;

    case CMD_RESP_SAMPLE_RATE: {
      SAMPLE_RATE_COMMAND sample_rate_cmd;
      bytes_read = recv_bytes(client_socket, (char *)&sample_rate_cmd.id, sizeof(sample_rate_cmd) - sizeof(header));

      if (bytes_read <= 0) {
        t_print("client_thread: short read for SAMPLE_RATE_CMD\n");
        t_perror("client_thread");
        // dialog box?
        return NULL;
      }

      int rx = (int)sample_rate_cmd.id;
      long long rate = ntohll(sample_rate_cmd.sample_rate);
      t_print("CMD_RESP_SAMPLE_RATE: rx=%d rate=%lld\n", rx, rate);

      if (rx == -1) {
        radio_sample_rate = (int)rate;

        for (rx = 0; rx < receivers; rx++) {
          receiver[rx]->sample_rate = (int)rate;
          receiver[rx]->hz_per_pixel = (double)receiver[rx]->sample_rate / (double)receiver[rx]->pixels;
        }
      } else {
        receiver[rx]->sample_rate = (int)rate;
        receiver[rx]->hz_per_pixel = (double)receiver[rx]->sample_rate / (double)receiver[rx]->pixels;
      }
    }
    break;

    case CMD_RESP_RECEIVERS: {
      RECEIVERS_COMMAND receivers_cmd;
      bytes_read = recv_bytes(client_socket, (char *)&receivers_cmd.receivers, sizeof(receivers_cmd) - sizeof(header));

      if (bytes_read <= 0) {
        t_print("client_thread: short read for RECEIVERS_CMD\n");
        t_perror("client_thread");
        // dialog box?
        return NULL;
      }

      int r = (int)receivers_cmd.receivers;
      t_print("CMD_RESP_RECEIVERS: receivers=%d\n", r);
      radio_change_receivers(r);
    }
    break;

    case CMD_RESP_RIT_INCREMENT: {
      RIT_INCREMENT_COMMAND rit_increment_cmd;
      bytes_read = recv_bytes(client_socket, (char *)&rit_increment_cmd.increment,
                              sizeof(rit_increment_cmd) - sizeof(header));

      if (bytes_read <= 0) {
        t_print("client_thread: short read for RIT_INCREMENT_CMD\n");
        t_perror("client_thread");
        // dialog box?
        return NULL;
      }

      int increment = ntohs(rit_increment_cmd.increment);
      t_print("CMD_RESP_RIT_INCREMENT: increment=%d\n", increment);
      rit_increment = increment;
    }
    break;

    case CMD_RESP_FILTER_BOARD: {
      FILTER_BOARD_COMMAND filter_board_cmd;
      bytes_read = recv_bytes(client_socket, (char *)&filter_board_cmd.filter_board,
                              sizeof(filter_board_cmd) - sizeof(header));

      if (bytes_read <= 0) {
        t_print("client_thread: short read for FILTER_BOARD_CMD\n");
        t_perror("client_thread");
        // dialog box?
        return NULL;
      }

      filter_board = (int)filter_board_cmd.filter_board;
      t_print("CMD_RESP_FILTER_BOARD: board=%d\n", filter_board);
    }
    break;

    case CMD_RESP_SWAP_IQ: {
      SWAP_IQ_COMMAND swap_iq_cmd;
      bytes_read = recv_bytes(client_socket, (char *)&swap_iq_cmd.iqswap, sizeof(swap_iq_cmd) - sizeof(header));

      if (bytes_read <= 0) {
        t_print("client_thread: short read for SWAP_IQ_COMMAND\n");
        t_perror("client_thread");
        // dialog box?
        return NULL;
      }

      iqswap = (int)swap_iq_cmd.iqswap;
      t_print("CMD_RESP_IQ_SWAP: iqswap=%d\n", iqswap);
    }
    break;

    case CMD_RESP_REGION: {
      REGION_COMMAND region_cmd;
      bytes_read = recv_bytes(client_socket, (char *)&region_cmd.region, sizeof(region_cmd) - sizeof(header));

      if (bytes_read <= 0) {
        t_print("client_thread: short read for REGION_COMMAND\n");
        t_perror("client_thread");
        // dialog box?
        return NULL;
      }

      region = (int)region_cmd.region;
      t_print("CMD_RESP_REGION: region=%d\n", region);
    }
    break;

    default:
      t_print("client_thread: Unknown type=%d\n", ntohs(header.data_type));
      break;
    }
  }

  return NULL;
}

int radio_connect_remote(char *host, int port) {
  struct sockaddr_in server_address;
  int on = 1;
  t_print("radio_connect_remote: %s:%d\n", host, port);
  client_socket = socket(AF_INET, SOCK_STREAM, 0);

  if (client_socket == -1) {
    t_print("radio_connect_remote: socket creation failed...\n");
    return -1;
  }

  setsockopt(client_socket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  setsockopt(client_socket, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));
  struct hostent *server = gethostbyname(host);

  if (server == NULL) {
    t_print("radio_connect_remote: no such host: %s\n", host);
    return -1;
  }

  // assign IP, PORT and bind to address
  memset(&server_address, 0, sizeof(server_address));
  server_address.sin_family = AF_INET;
  bcopy((char *)server->h_addr, (char *)&server_address.sin_addr.s_addr, server->h_length);
  server_address.sin_port = htons((short)port);

  if (connect(client_socket, (struct sockaddr *)&server_address, sizeof(server_address)) != 0) {
    t_print("client_thread: connect failed\n");
    t_perror("client_thread");
    return -1;
  }

  t_print("radio_connect_remote: socket %d bound to %s:%d\n", client_socket, host, port);
  snprintf(server_host, 128, "%s:%d", host, port);
  client_thread_id = g_thread_new("remote_client", client_thread, &server_host);
  return 0;
}

//
// Execute a remote command through the GTK idle queue
// and send a response.
// Because of the response required, we cannot just
// delegate to actions.c
//

//
// A proper handling may be required if the "remote command" refers to
// the second receiver while only 1 RX is present (this should probably
// not happen, but who knows?
// Therefore the CHECK_RX macro defined here logs such events
//

#define CHECK_RX(rx) if (rx > receivers) t_print("CHECK_RX %s:%d RX=%d > receivers=%d\n", \
                        __FUNCTION__, __LINE__, rx, receivers);

static int remote_command(void *data) {
  HEADER *header = (HEADER *)data;
  const REMOTE_CLIENT *client = header->context.client;
  int temp;

  switch (ntohs(header->data_type)) {
  case CMD_RESP_RX_FREQ: {
    FREQ_COMMAND *freq_command = (FREQ_COMMAND *)data;
    temp = active_receiver->pan;
    int v = freq_command->id;
    long long f = ntohll(freq_command->hz);
    vfo_set_frequency(v, f);
    vfo_update();
    send_vfo_data(client, VFO_A);
    send_vfo_data(client, VFO_B);

    if (temp != active_receiver->pan) {
      send_pan(client->socket, active_receiver->id, active_receiver->pan);
    }
  }
  break;

  case CMD_RESP_RX_STEP: {
    STEP_COMMAND *step_command = (STEP_COMMAND *)data;
    temp = active_receiver->pan;
    short steps = ntohs(step_command->steps);
    vfo_step(steps);

    //send_vfo_data(client,VFO_A);
    //send_vfo_data(client,VFO_B);
    if (temp != active_receiver->pan) {
      send_pan(client->socket, active_receiver->id, active_receiver->pan);
    }
  }
  break;

  case CMD_RESP_RX_MOVE: {
    MOVE_COMMAND *move_command = (MOVE_COMMAND *)data;
    temp = active_receiver->pan;
    long long hz = ntohll(move_command->hz);
    vfo_move(hz, move_command->round);

    //send_vfo_data(client,VFO_A);
    //send_vfo_data(client,VFO_B);
    if (temp != active_receiver->pan) {
      send_pan(client->socket, active_receiver->id, active_receiver->pan);
    }
  }
  break;

  case CMD_RESP_RX_MOVETO: {
    MOVE_TO_COMMAND *move_to_command = (MOVE_TO_COMMAND *)data;
    temp = active_receiver->pan;
    long long hz = ntohll(move_to_command->hz);
    vfo_move_to(hz);
    send_vfo_data(client, VFO_A);
    send_vfo_data(client, VFO_B);

    if (temp != active_receiver->pan) {
      send_pan(client->socket, active_receiver->id, active_receiver->pan);
    }
  }
  break;

  case CMD_RESP_RX_ZOOM: {
    ZOOM_COMMAND *zoom_command = (ZOOM_COMMAND *)data;
    temp = ntohs(zoom_command->zoom);
    set_zoom(zoom_command->id, (double)temp);
    send_zoom(client->socket, active_receiver->id, active_receiver->zoom);
    send_pan(client->socket, active_receiver->id, active_receiver->pan);
  }
  break;

  case CMD_RESP_RX_PAN: {
    PAN_COMMAND *pan_command = (PAN_COMMAND *)data;
    temp = ntohs(pan_command->pan);
    set_pan(pan_command->id, (double)temp);
    send_pan(client->socket, active_receiver->id, active_receiver->pan);
  }
  break;

  case CMD_RESP_RX_VOLUME: {
    VOLUME_COMMAND *volume_command = (VOLUME_COMMAND *)data;
    double dtmp = ntohd(volume_command->volume);
    set_af_gain(volume_command->id, dtmp);
  }
  break;

  case CMD_RESP_RX_AGC: {
    AGC_COMMAND *agc_command = (AGC_COMMAND *)data;
    int r = agc_command->id;
    CHECK_RX(r);
    RECEIVER *rx = receiver[r];
    rx->agc = ntohs(agc_command->agc);
    set_agc(rx, rx->agc);
    send_agc(client->socket, rx->id, rx->agc);
    g_idle_add(ext_vfo_update, NULL);
  }
  break;

  case CMD_RESP_RX_AGC_GAIN: {
    AGC_GAIN_COMMAND *agc_gain_command = (AGC_GAIN_COMMAND *)data;
    int r = agc_gain_command->id;
    CHECK_RX(r);
    RECEIVER *rx = receiver[r];
    rx->agc_hang = ntohd(agc_gain_command->hang);
    rx->agc_thresh = ntohd(agc_gain_command->thresh);
    rx->agc_hang_threshold = ntohd(agc_gain_command->hang_thresh);
    set_agc_gain(r, ntohd(agc_gain_command->gain));
    set_agc(rx, rx->agc);
    send_agc_gain(client->socket, rx->id, rx->agc_gain, rx->agc_hang, rx->agc_thresh, rx->agc_hang_threshold);
  }
  break;

  case CMD_RESP_RX_GAIN: {
    RFGAIN_COMMAND *command = (RFGAIN_COMMAND *) data;
    double td = ntohd(command->gain);
    set_rf_gain(command->id, td);
  }
  break;

  case CMD_RESP_RX_ATTENUATION: {
    ATTENUATION_COMMAND *attenuation_command = (ATTENUATION_COMMAND *)data;
    temp = ntohs(attenuation_command->attenuation);
    set_attenuation(temp);
  }
  break;

  case CMD_RESP_RX_SQUELCH: {
    SQUELCH_COMMAND *squelch_command = (SQUELCH_COMMAND *)data;
    int r = squelch_command->id;
    CHECK_RX(r);
    receiver[r]->squelch_enable = squelch_command->enable;
    temp = ntohs(squelch_command->squelch);
    receiver[r]->squelch = (double)temp;
    set_squelch(receiver[r]);
  }
  break;

  case CMD_RESP_RX_NOISE: {
    const NOISE_COMMAND *noise_command = (NOISE_COMMAND *)data;
    int id = noise_command->id;
    CHECK_RX(id);
    RECEIVER *rx = receiver[id];

    rx->nb = noise_command->nb;
    rx->nr = noise_command->nr;
    rx->anf = noise_command->anf;
    rx->snb = noise_command->snb;

    if (id == 0) {
      mode_settings[vfo[id].mode].nb = rx->nb;
      mode_settings[vfo[id].mode].nr = rx->nr;
      mode_settings[vfo[id].mode].anf = rx->anf;
      mode_settings[vfo[id].mode].snb = rx->snb;
    }

    set_noise();
    send_noise(client->socket, rx->id, rx->nb, rx->nr, rx->anf, rx->snb);
  }
  break;

  case CMD_RESP_RX_BAND: {
    BAND_COMMAND *band_command = (BAND_COMMAND *)data;
    int r = band_command->id;
    CHECK_RX(r);
    short b = htons(band_command->band);
    vfo_band_changed(r, b);
    send_vfo_data(client, VFO_A);
    send_vfo_data(client, VFO_B);
  }
  break;

  case CMD_RESP_RX_MODE: {
    MODE_COMMAND *mode_command = (MODE_COMMAND *)data;
    int r = mode_command->id;
    short m = htons(mode_command->mode);
    vfo_mode_changed(m);
    send_vfo_data(client, VFO_A);
    send_vfo_data(client, VFO_B);
    send_filter(client->socket, r, m);
  }
  break;

  case CMD_RESP_RX_FILTER: {
    FILTER_COMMAND *filter_command = (FILTER_COMMAND *)data;
    int r = filter_command->id;
    short f = htons(filter_command->filter);
    vfo_filter_changed(f);
    send_vfo_data(client, VFO_A);
    send_vfo_data(client, VFO_B);
    send_filter(client->socket, r, f);
  }
  break;

  case CMD_RESP_SPLIT: {
    const SPLIT_COMMAND *split_command = (SPLIT_COMMAND *)data;

    if (can_transmit) {
      split = split_command->split;
      tx_set_mode(transmitter, get_tx_mode());
      g_idle_add(ext_vfo_update, NULL);
    }

    send_split(client->socket, split);
  }
  break;

  case CMD_RESP_SAT: {
    const SAT_COMMAND *sat_command = (SAT_COMMAND *)data;
    sat_mode = sat_command->sat;
    g_idle_add(ext_vfo_update, NULL);
    send_sat(client->socket, sat_mode);
  }
  break;

  case CMD_RESP_DUP: {
    const DUP_COMMAND *dup_command = (DUP_COMMAND *)data;
    duplex = dup_command->dup;
    g_idle_add(ext_vfo_update, NULL);
    send_dup(client->socket, duplex);
  }
  break;

  case CMD_RESP_LOCK: {
    const LOCK_COMMAND *lock_command = (LOCK_COMMAND *)data;
    locked = lock_command->lock;
    g_idle_add(ext_vfo_update, NULL);
    send_lock(client->socket, locked);
  }
  break;

  case CMD_RESP_CTUN: {
    const CTUN_COMMAND *ctun_command = (CTUN_COMMAND *)data;
    int v = ctun_command->id;
    vfo[v].ctun = ctun_command->ctun;

    if (!vfo[v].ctun) {
      vfo[v].offset = 0;
    }

    vfo[v].ctun_frequency = vfo[v].frequency;
    set_offset(active_receiver, vfo[v].offset);
    g_idle_add(ext_vfo_update, NULL);
    send_ctun(client->socket, v, vfo[v].ctun);
    send_vfo_data(client, v);
  }
  break;

  case CMD_RESP_RX_FPS: {
    const FPS_COMMAND *fps_command = (FPS_COMMAND *)data;
    int rx = fps_command->id;
    CHECK_RX(rx);
    receiver[rx]->fps = fps_command->fps;
    calculate_display_average(receiver[rx]);
    set_displaying(receiver[rx], 1);
    send_fps(client->socket, rx, receiver[rx]->fps);
  }
  break;

  case CMD_RESP_RX_SELECT: {
    const RX_SELECT_COMMAND *rx_select_command = (RX_SELECT_COMMAND *)data;
    int rx = rx_select_command->id;
    CHECK_RX(rx);
    receiver_set_active(receiver[rx]);
    send_rx_select(client->socket, rx);
  }
  break;

  case CMD_RESP_VFO: {
    const VFO_COMMAND *vfo_command = (VFO_COMMAND *)data;
    int action = vfo_command->id;

    switch (action) {
    case VFO_A_TO_B:
      vfo_a_to_b();
      break;

    case VFO_B_TO_A:
      vfo_b_to_a();
      break;

    case VFO_A_SWAP_B:
      vfo_a_swap_b();
      break;
    }

    send_vfo_data(client, VFO_A);
    send_vfo_data(client, VFO_B);
  }
  break;

  case CMD_RESP_RIT_TOGGLE: {
    const RIT_TOGGLE_COMMAND *rit_toggle_command = (RIT_TOGGLE_COMMAND *)data;
    int rx = rit_toggle_command->id;
    vfo_rit_toggle(rx);
    send_vfo_data(client, rx);
  }
  break;

  case CMD_RESP_RIT_CLEAR: {
    const RIT_CLEAR_COMMAND *rit_clear_command = (RIT_CLEAR_COMMAND *)data;
    int rx = rit_clear_command->id;
    vfo_rit_value(rx, 0);
    send_vfo_data(client, rx);
  }
  break;

  case CMD_RESP_RIT: {
    RIT_COMMAND *rit_command = (RIT_COMMAND *)data;
    int rx = rit_command->id;
    short rit = ntohs(rit_command->rit);
    vfo_rit_incr(rx, (int)rit * rit_increment);
    send_vfo_data(client, rx);
  }
  break;

  case CMD_RESP_XIT_TOGGLE: {
    send_vfo_data(client, VFO_A);
    send_vfo_data(client, VFO_B);
  }
  break;

  case CMD_RESP_XIT_CLEAR: {
    send_vfo_data(client, VFO_A);
    send_vfo_data(client, VFO_B);
  }
  break;

  case CMD_RESP_XIT: {
    send_vfo_data(client, VFO_A);
    send_vfo_data(client, VFO_B);
  }
  break;

  case CMD_RESP_SAMPLE_RATE: {
    SAMPLE_RATE_COMMAND *sample_rate_command = (SAMPLE_RATE_COMMAND *)data;
    int rx = (int)sample_rate_command->id;
    CHECK_RX(rx);
    long long rate = ntohll(sample_rate_command->sample_rate);

    if (rx == -1) {
      radio_change_sample_rate((int)rate);
      send_sample_rate(client->socket, -1, radio_sample_rate);
    } else {
      receiver_change_sample_rate(receiver[rx], (int)rate);
      send_sample_rate(client->socket, rx, receiver[rx]->sample_rate);
    }
  }
  break;

  case CMD_RESP_RECEIVERS: {
    const RECEIVERS_COMMAND *receivers_command = (RECEIVERS_COMMAND *)data;
    int r = receivers_command->receivers;
    radio_change_receivers(r);
    send_receivers(client->socket, receivers);
  }
  break;

  case CMD_RESP_RIT_INCREMENT: {
    const RIT_INCREMENT_COMMAND *rit_increment_command = (RIT_INCREMENT_COMMAND *)data;
    short increment = ntohs(rit_increment_command->increment);
    rit_increment = (int)increment;
    send_rit_increment(client->socket, rit_increment);
  }
  break;

  case CMD_RESP_FILTER_BOARD: {
    const FILTER_BOARD_COMMAND *filter_board_command = (FILTER_BOARD_COMMAND *)data;
    filter_board = (int)filter_board_command->filter_board;
    load_filters();
    send_filter_board(client->socket, filter_board);
  }
  break;

  case CMD_RESP_SWAP_IQ: {
    const SWAP_IQ_COMMAND *swap_iq_command = (SWAP_IQ_COMMAND *)data;
    iqswap = (int)swap_iq_command->iqswap;
    send_swap_iq(client->socket, iqswap);
  }
  break;

  case CMD_RESP_REGION: {
    const REGION_COMMAND *region_command = (REGION_COMMAND *)data;
    iqswap = (int)region_command->region;
    send_region(client->socket, region);
  }
  break;
  }

  g_free(data);
  return 0;
}
