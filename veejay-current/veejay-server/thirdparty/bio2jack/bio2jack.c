/*
 * Copyright 2003-2004 Chris Morgan <cmorgan@alum.wpi.edu>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

 /*
   2026: Refactored for veejay
   
 */

#include <config.h>
#ifdef HAVE_JACK
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <math.h>
#include <unistd.h>
#include <inttypes.h>
#include <time.h>
#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <pthread.h>
#include <sys/time.h>
#include <veejaycore/vjmem.h>
#include <veejaycore/atomic.h>
#include <veejaycore/vj-msg.h>
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libavutil/version.h>
#include "bio2jack.h"

#define RESERVE_PERIODS 1

#ifndef VJ_JACK_PLAYBACK_RING_VIDEO_FRAMES
#define VJ_JACK_PLAYBACK_RING_VIDEO_FRAMES 1
#endif

#ifndef VJ_JACK_PLAYBACK_RING_MIN_PERIODS
#define VJ_JACK_PLAYBACK_RING_MIN_PERIODS 3
#endif

#ifndef VJ_JACK_CAPTURE_RING_VIDEO_FRAMES
#define VJ_JACK_CAPTURE_RING_VIDEO_FRAMES 2
#endif

#ifndef VJ_JACK_CAPTURE_RING_MIN_PERIODS
#define VJ_JACK_CAPTURE_RING_MIN_PERIODS 4
#endif

#ifndef VJ_JACK_RESAMPLE_SLACK_FRAMES
#define VJ_JACK_RESAMPLE_SLACK_FRAMES 64
#endif

#define WARN(format, args...) veejay_msg(1, "[AUDIO] " format, ##args);

#define ERR(format, args...) veejay_msg(0, "[AUDIO] " format, ##args);

#define min(a, b) ({      \
  __typeof__(a) _a = (a); \
  __typeof__(b) _b = (b); \
  _a < _b ? _a : _b;      \
})

#define max(a, b) ({      \
  __typeof__(a) _a = (a); \
  __typeof__(b) _b = (b); \
  _a > _b ? _a : _b;      \
})

#define MAX_OUTPUT_PORTS 2
#define MAX_INPUT_PORTS 2

#define DEFAULT_VOLUME 100

#define JACK_RECONNECT_INITIAL_DELAY_NS 250000000L
#define JACK_RECONNECT_MAX_DELAY_NS     8000000000L
#define JACK_RECONNECT_MAX_ATTEMPTS     6
#define JACK_MAX_RETIRED_RINGBUFFERS     32
typedef struct jack_driver_s
{
  int allocated;
  int deviceID;
  int clientCtr;
  int in_use;
  int cb_active;

  volatile int input_passthrough;

  jack_client_t *volatile client;
  jack_port_t *output_port[MAX_OUTPUT_PORTS];
  jack_port_t *input_port[MAX_INPUT_PORTS];

  char **jack_port_name;
  unsigned int jack_port_name_count;
  unsigned long jack_output_port_flags;
  unsigned long jack_input_port_flags;

  long jack_sample_rate;
  long client_sample_rate;

  double SPVF;

  volatile double output_sample_rate_ratio;
  volatile double input_sample_rate_ratio;

  unsigned long bits_per_channel;
  unsigned long num_input_channels;
  unsigned long num_output_channels;

  unsigned long bytes_per_output_frame;
  unsigned long bytes_per_input_frame;
  unsigned long bytes_per_jack_output_frame;
  unsigned long bytes_per_jack_input_frame;

  volatile int xrun_pending;
  volatile int xrun_flag;

  volatile unsigned long last_hw_frame_count;
  volatile long underrun_count;
  volatile long input_overrun_count;
  volatile unsigned long captured_client_bytes;

  jack_ringbuffer_t *volatile pPlayPtr;
  size_t volatile pPlayPtr_size;

  jack_ringbuffer_t *volatile pRecPtr;
  size_t volatile pRecPtr_size;

  jack_ringbuffer_t *retired_ringbuffers[JACK_MAX_RETIRED_RINGBUFFERS];
  unsigned int retired_ringbuffer_count;

  volatile unsigned long client_bytes;
  volatile unsigned long written_client_bytes;
  volatile unsigned long played_client_bytes;
  volatile long position_byte_offset;

  long jack_buffer_size;
  volatile jack_nframes_t last_callback_frame;

  struct timespec previousTime;
  double prefill_duration;

  volatile int state;
  volatile int jackd_died;
  struct timespec last_reconnect_attempt;
  unsigned int reconnect_attempts;
  long reconnect_delay_ns;
  volatile int reconnect_failed;
  volatile int reconnect_final_warned;
  volatile int pwjack_hint_emitted;

  volatile unsigned int volume[MAX_OUTPUT_PORTS];
  volatile int volumeEffectType;

  float leftover_frames;
  pthread_mutex_t mutex;

  struct SwrContext *swr_ctx;
  struct SwrContext *swr_in_ctx;

  float *resample_buf;
  long resample_buf_frames;

  float *input_resample_buf;
  long input_resample_buf_frames;

} jack_driver_t;

static char *client_name = NULL;

static int init_done = 0;

static enum JACK_PORT_CONNECTION_MODE port_connection_mode = CONNECT_ALL;

#define JACK_CLOSE_HACK 1

typedef jack_default_audio_sample_t sample_t;
typedef jack_nframes_t nframes_t;

#define MAX_OUTDEVICES 4
static jack_driver_t outDev[MAX_OUTDEVICES];

static pthread_mutex_t device_mutex = PTHREAD_MUTEX_INITIALIZER;

#if JACK_CLOSE_HACK
static void JACK_CloseDevice(jack_driver_t *drv, bool close_client);
#else
static void JACK_CloseDevice(jack_driver_t *drv);
#endif

static int JACK_OpenDevice(jack_driver_t *drv);
static int JACK_OpenDeviceRaw(jack_driver_t *drv);
static unsigned long JACK_GetBytesFreeSpaceFromDriver(jack_driver_t *drv);
static void JACK_ResetFromDriver(jack_driver_t *drv);
static unsigned long JACK_GetPositionFromDriver(jack_driver_t *drv, int position, int type);
static long JACK_rescale_client_to_jack_frames_ceil(jack_driver_t *drv, long client_frames);
static unsigned long JACK_RescaleJackFramesToClientFramesFloor(jack_driver_t *drv, unsigned long jack_frames);
static unsigned long JACK_RescaleJackFramesToClientFramesRound(jack_driver_t *drv, unsigned long jack_frames);
static long JACK_get_required_free_frames(jack_driver_t *drv, int client_frames);
static void JACK_CleanupDriver(jack_driver_t *drv);

static int JACK_PortIsOwnClient(jack_driver_t *drv, const char *port_name)
{
  const char *own;
  size_t n;

  if (!drv || !drv->client || !port_name)
    return 0;

  own = jack_get_client_name(drv->client);
  if (!own || !own[0])
    return 0;

  n = strlen(own);
  return strncmp(port_name, own, n) == 0 && port_name[n] == ':';
}

static int JACK_PortHasFlags(jack_driver_t *drv, const char *port_name, unsigned long required_flags)
{
  jack_port_t *p;
  unsigned long flags;

  if (!drv || !drv->client || !port_name)
    return 0;

  p = jack_port_by_name(drv->client, port_name);
  if (!p)
    return 0;

  flags = jack_port_flags(p);
  return ((flags & required_flags) == required_flags);
}

static int JACK_PortNameStartsWith(const char *s, const char *prefix)
{
  if (!s || !prefix)
    return 0;

  return strncmp(s, prefix, strlen(prefix)) == 0;
}

static int JACK_PortNameContains(const char *s, const char *needle)
{
  if (!s || !needle)
    return 0;

  return strstr(s, needle) != NULL;
}

static int JACK_PortLooksLikePlaybackTarget(const char *port_name)
{
  return (JACK_PortNameStartsWith(port_name, "system:playback_") ||
          JACK_PortNameContains(port_name, ":playback_") ||
          JACK_PortNameContains(port_name, "playback") ||
          JACK_PortNameContains(port_name, "Playback"));
}

static int JACK_PortLooksLikeNonPlaybackTarget(const char *port_name)
{
  return (!port_name ||
          JACK_PortNameContains(port_name, "monitor") ||
          JACK_PortNameContains(port_name, "Monitor") ||
          JACK_PortNameContains(port_name, "capture") ||
          JACK_PortNameContains(port_name, "Capture") ||
          JACK_PortNameContains(port_name, ":input") ||
          JACK_PortNameContains(port_name, ":Input") ||
          JACK_PortNameContains(port_name, ":in_"));
}

static const char **JACK_GetPortsAudioOrAny(jack_driver_t *drv,
                                             const char *pattern,
                                             unsigned long flags)
{
  const char **ports;

  if (!drv || !drv->client)
    return NULL;

  ports = jack_get_ports(drv->client, pattern, JACK_DEFAULT_AUDIO_TYPE, flags);
  return ports ? ports : jack_get_ports(drv->client, pattern, NULL, flags);
}

static const char **JACK_GetPreferredPlaybackPorts(jack_driver_t *drv)
{
  const char **ports = NULL;

  if (!drv || !drv->client)
    return NULL;

  ports = JACK_GetPortsAudioOrAny(drv,
                                  "^system:playback_[0-9]+$",
                                  JackPortIsInput);
  if (ports)
    return ports;

  ports = JACK_GetPortsAudioOrAny(drv,
                                  "^system:playback_.*$",
                                  JackPortIsInput);
  if (ports)
    return ports;

  ports = JACK_GetPortsAudioOrAny(drv,
                                  ".*:playback_[Ff][Ll]$",
                                  JackPortIsInput);
  if (ports)
    return ports;

  ports = JACK_GetPortsAudioOrAny(drv,
                                  ".*playback.*",
                                  JackPortIsInput);
  if (ports)
    return ports;

  ports = JACK_GetPortsAudioOrAny(drv,
                                  ".*Playback.*",
                                  JackPortIsInput);
  if (ports)
    return ports;

  ports = JACK_GetPortsAudioOrAny(drv,
                                  NULL,
                                  JackPortIsPhysical | JackPortIsInput);
  if (ports)
    return ports;

  return JACK_GetPortsAudioOrAny(drv, NULL, JackPortIsInput);
}

static int JACK_PortIsUsable(jack_driver_t *drv, const char *port_name, unsigned long required_flags)
{
  if (!port_name)
    return 0;

  if (JACK_PortIsOwnClient(drv, port_name))
    return 0;

  return JACK_PortHasFlags(drv, port_name, required_flags);
}

static const char *JACK_StereoSuffix(unsigned int channel)
{
  return (channel == 0) ? "FL" : ((channel == 1) ? "FR" : NULL);
}

static void JACK_FormatPortName(char *dst, size_t dst_size,
                                const char *prefix, unsigned int channel)
{
  const char *suffix = JACK_StereoSuffix(channel);

  if (suffix)
    snprintf(dst, dst_size, "%s_%s", prefix, suffix);
  else
    snprintf(dst, dst_size, "%s_%u", prefix, channel);
}

static void
JACK_RecalculateRatios(jack_driver_t *drv)
{
  if (!drv)
    return;

  long jack_rate = drv->jack_sample_rate;
  long client_rate = drv->client_sample_rate;

  const int resample = (client_rate != jack_rate && client_rate > 0 && jack_rate > 0);
  const double new_output_ratio = resample ? ((double)jack_rate / (double)client_rate) : 1.0;
  const double new_input_ratio = resample ? ((double)client_rate / (double)jack_rate) : 1.0;

  atomic_exchange_double(&drv->output_sample_rate_ratio, new_output_ratio);
  atomic_exchange_double(&drv->input_sample_rate_ratio, new_input_ratio);

  atomic_synchronize();
}

static size_t
JACK_CalcRingbufferBytes(jack_driver_t *drv,
                         double SPVF,
                         unsigned long jack_frame_bytes,
                         uint32_t video_frames,
                         uint32_t min_periods)
{
  if (!drv || drv->jack_sample_rate == 0 || jack_frame_bytes == 0)
    return 0;

  const uint32_t sr = drv->jack_sample_rate;
  const uint32_t jack_period = drv->jack_buffer_size > 0 ? drv->jack_buffer_size : 1024;
  const uint32_t frames_per_vframe = (uint32_t)(SPVF * sr + 0.5);

  uint32_t min_frames = jack_period * min_periods;
  uint32_t required_frames = (video_frames * frames_per_vframe) + jack_period;

  if (min_frames < jack_period * 2)
    min_frames = jack_period * 2;

  required_frames = (required_frames < min_frames) ? min_frames : required_frames;

  return (size_t)required_frames * jack_frame_bytes;
}

static void JACK_RetireRingbuffer(jack_driver_t *drv, jack_ringbuffer_t *rb)
{
  if (!drv || !rb)
    return;

  if (drv->retired_ringbuffer_count < JACK_MAX_RETIRED_RINGBUFFERS)
  {
    drv->retired_ringbuffers[drv->retired_ringbuffer_count++] = rb;
    return;
  }
}

static void
JACK_ResizeSingleRingbuffer(jack_driver_t *drv,
                            jack_ringbuffer_t *volatile *target,
                            size_t volatile *target_size,
                            size_t new_size_bytes,
                            const char *label,
                            long jack_sample_rate,
                            unsigned long jack_frame_bytes)
{
  if (new_size_bytes == 0 || jack_frame_bytes == 0)
    return;

  jack_ringbuffer_t *current = (jack_ringbuffer_t *)*target;
  if (current && *target_size >= new_size_bytes)
    return;

  jack_ringbuffer_t *rb = jack_ringbuffer_create(new_size_bytes);
  if (!rb)
    return;

  jack_ringbuffer_mlock(rb);

  jack_ringbuffer_t *old = (jack_ringbuffer_t *)atomic_exchange_ptr((uintptr_t *)target, (uintptr_t)rb);
  *target_size = new_size_bytes;

  if (old)
    JACK_RetireRingbuffer(drv, old);

  const unsigned long requested_frames = (unsigned long)(new_size_bytes / jack_frame_bytes);
  const unsigned long capacity_frames = (unsigned long)(rb->size / jack_frame_bytes);

  veejay_msg(
      VEEJAY_MSG_INFO,
      "[AUDIO]: Jack %s ringbuffer requested %lu frames, capacity %lu frames (%.2f ms) [%ld Hz]",
      label,
      requested_frames,
      capacity_frames,
      (double)capacity_frames * 1000.0 / (double)jack_sample_rate,
      jack_sample_rate);
}

static void JACK_ResizeRingBuffers(jack_driver_t *drv, double SPVF)
{
  if (!drv || drv->jack_sample_rate == 0)
    return;

  if (drv->num_output_channels > 0 && drv->bytes_per_jack_output_frame > 0)
  {
    const size_t play_bytes =
        JACK_CalcRingbufferBytes(drv,
                                 SPVF,
                                 drv->bytes_per_jack_output_frame,
                                 VJ_JACK_PLAYBACK_RING_VIDEO_FRAMES,
                                 VJ_JACK_PLAYBACK_RING_MIN_PERIODS);

    JACK_ResizeSingleRingbuffer(
        drv,
        &drv->pPlayPtr,
        &drv->pPlayPtr_size,
        play_bytes,
        "playback",
        drv->jack_sample_rate,
        drv->bytes_per_jack_output_frame);
  }

  if (drv->num_input_channels > 0 && drv->bytes_per_jack_input_frame > 0)
  {
    const size_t rec_bytes =
        JACK_CalcRingbufferBytes(drv,
                                 SPVF,
                                 drv->bytes_per_jack_input_frame,
                                 VJ_JACK_CAPTURE_RING_VIDEO_FRAMES,
                                 VJ_JACK_CAPTURE_RING_MIN_PERIODS);

    JACK_ResizeSingleRingbuffer(
        drv,
        &drv->pRecPtr,
        &drv->pRecPtr_size,
        rec_bytes,
        "capture",
        drv->jack_sample_rate,
        drv->bytes_per_jack_input_frame);
  }
}

long TimeValDifference(struct timespec *start, struct timespec *end)
{
  return ((end->tv_sec * 1000000000) + end->tv_nsec) -
         ((start->tv_sec * 1000000000) + start->tv_nsec);
}

static long JACK_ReconnectDelayNs(unsigned int failures)
{
  long delay = JACK_RECONNECT_INITIAL_DELAY_NS;

  if (failures == 0)
    return 0;

  for (unsigned int i = 1; i < failures; i++)
  {
    if (delay >= (JACK_RECONNECT_MAX_DELAY_NS / 2))
      return JACK_RECONNECT_MAX_DELAY_NS;

    delay *= 2;
  }

  return (delay > JACK_RECONNECT_MAX_DELAY_NS)
             ? JACK_RECONNECT_MAX_DELAY_NS
             : delay;
}

static void JACK_ResetReconnectPolicy(jack_driver_t *drv)
{
  if (!drv)
    return;

  drv->reconnect_attempts = 0;
  drv->reconnect_delay_ns = JACK_RECONNECT_INITIAL_DELAY_NS;
  drv->reconnect_failed = FALSE;
  drv->reconnect_final_warned = FALSE;
  drv->pwjack_hint_emitted = FALSE;

  clock_gettime(CLOCK_MONOTONIC, &drv->last_reconnect_attempt);
}

static int JACK_ReconnectAllowed(jack_driver_t *drv)
{
  if (!drv)
    return 0;

  if (atomic_load_int(&drv->reconnect_failed))
  {
    if (!atomic_exchange_int(&drv->reconnect_final_warned, TRUE))
    {
      WARN("Jack reconnect disabled after %u failed attempts. Audio will stay offline until JACK is restarted or veejay is restarted.",
           drv->reconnect_attempts);
    }
    return 0;
  }

  if (drv->reconnect_attempts == 0)
  {
    clock_gettime(CLOCK_MONOTONIC, &drv->last_reconnect_attempt);
    return 1;
  }

  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);

  if (TimeValDifference(&drv->last_reconnect_attempt, &now) < drv->reconnect_delay_ns)
    return 0;

  drv->last_reconnect_attempt = now;
  return 1;
}

static void JACK_MaybeWarnPwJack(jack_driver_t *drv)
{
  if (!drv)
    return;

  if (atomic_exchange_int(&drv->pwjack_hint_emitted, TRUE))
    return;

  char jack_socket[128];
  snprintf(jack_socket, sizeof(jack_socket),
           "/dev/shm/jack-%ld/default/jack_0", (long)getuid());

  if (access(jack_socket, F_OK) != 0)
  {
    WARN("No JACK server socket found at %s. If this is a PipeWire JACK system, start veejay through pw-jack, e.g. `pw-jack veejay ...`; otherwise start jackd.",
         jack_socket);
  }
}

static void JACK_RecordOpenFailure(jack_driver_t *drv, int retval)
{
  if (!drv)
    return;

  drv->client = NULL;
  drv->jackd_died = TRUE;

  drv->reconnect_attempts++;

  JACK_MaybeWarnPwJack(drv);

  if (drv->reconnect_attempts >= JACK_RECONNECT_MAX_ATTEMPTS)
  {
    drv->reconnect_failed = TRUE;
    drv->reconnect_delay_ns = JACK_RECONNECT_MAX_DELAY_NS;
    atomic_exchange_int(&drv->state, CLOSED);

    if (!atomic_exchange_int(&drv->reconnect_final_warned, TRUE))
    {
      WARN("Giving up on JACK after %u failed connection attempts (last error %d). Audio reconnect is now disabled for this device.",
           drv->reconnect_attempts, retval);
    }
    return;
  }

  drv->reconnect_delay_ns = JACK_ReconnectDelayNs(drv->reconnect_attempts);

  WARN("JACK unavailable; reconnect attempt %u/%u failed (error %d). Next retry in %.2f seconds.",
       drv->reconnect_attempts,
       JACK_RECONNECT_MAX_ATTEMPTS,
       retval,
       (double)drv->reconnect_delay_ns / 1000000000.0);
}

static inline jack_driver_t *JACK_GetDriverRaw(int deviceID)
{
  if (deviceID < 0 || deviceID >= MAX_OUTDEVICES)
    return NULL;

  return &outDev[deviceID];
}

static inline jack_driver_t *JACK_GetDriverReconnect(int deviceID)
{
  jack_driver_t *drv = JACK_GetDriverRaw(deviceID);

  if (!drv)
    return NULL;

  if (drv->allocated && drv->jackd_died && drv->client == 0)
  {
    int retval = JACK_OpenDevice(drv);
    if (retval == ERR_SUCCESS)
      veejay_msg(VEEJAY_MSG_INFO, "[AUDIO] Reconnected to JACK");
  }

  return drv;
}

#define SAMPLE_MAX_16BIT 32767.0f
#define SAMPLE_MAX_8BIT 255.0f

static const float BIT16_MULT = 1.0f / SAMPLE_MAX_16BIT;
static const float BIT8_MULT = 1.0f / SAMPLE_MAX_8BIT;

static inline void
mux(sample_t *dst, sample_t *src, unsigned long nsamples,
    unsigned long dst_skip)
{
  for (int i = 0; i < nsamples; i++)
  {
    *dst = *src;
    dst += dst_skip;
    src++;
  }
}
static inline void
sample_move_short_float(sample_t *dst, short *src, unsigned long nsamples)
{
  unsigned long i;
  for (i = 0; i < nsamples; i++)
    dst[i] = (sample_t)(src[i] * BIT16_MULT);
}

static inline void
sample_move_float_short(short *dst, sample_t *src, unsigned long nsamples)
{
  unsigned long i;
  for (i = 0; i < nsamples; i++)
    dst[i] = (short)((src[i]) * SAMPLE_MAX_16BIT);
}

static inline void
sample_move_char_float(sample_t *dst, unsigned char *src, unsigned long nsamples)
{
  unsigned long i;
  for (i = 0; i < nsamples; i++)
    dst[i] = (sample_t)(src[i] * BIT8_MULT);
}

static inline void
sample_move_float_char(unsigned char *dst, sample_t *src, unsigned long nsamples)
{
  unsigned long i;
  for (i = 0; i < nsamples; i++)
    dst[i] = (char)((src[i]) * SAMPLE_MAX_8BIT);
}

static inline void
sample_silence_float(sample_t *dst, unsigned long nsamples)
{
  while (nsamples--)
  {
    *dst = 0;
    dst++;
  }
}

static int
JACK_xrun_callback(void *arg)
{
  jack_driver_t *drv = (jack_driver_t *)arg;

  atomic_store_int(&drv->xrun_pending, 1);

  atomic_store_uint(&drv->last_callback_frame, 0);

  return 0;
}

#define STARVATION_PERIOD_THRESHOLD 1.5f
static int JACK_isStarving(jack_driver_t *drv)
{

  if (drv->pPlayPtr == NULL || drv->bytes_per_jack_output_frame == 0 || drv->jack_buffer_size == 0)
  {
    return 0;
  }
  size_t inputBytesAvailable = jack_ringbuffer_read_space(drv->pPlayPtr);

  unsigned long inputFramesAvailable;
  inputFramesAvailable = inputBytesAvailable / drv->bytes_per_jack_output_frame;

  unsigned long requiredFrames = drv->jack_buffer_size;

  const unsigned long starvationThresholdFrames =
      (unsigned long)((float)requiredFrames * STARVATION_PERIOD_THRESHOLD);

  return (inputFramesAvailable < starvationThresholdFrames) ? 1 : 0;
}

static int
JACK_bufsize(nframes_t nframes, void *arg)
{
  jack_driver_t *drv = (jack_driver_t *)arg;

  drv->jack_buffer_size = nframes;

  JACK_ResizeRingBuffers(drv, drv->SPVF);

  return 0;
}

static void JACK_ResetSwrContext(struct SwrContext *ctx)
{
  if (!ctx)
    return;

  swr_close(ctx);

  if (swr_init(ctx) < 0)
  {
    veejay_msg(VEEJAY_MSG_WARNING, "[AUDIO] Failed to reset resampler context");
  }
}

static void JACK_flush(jack_driver_t *drv)
{
  if (!drv)
    return;

  JACK_ResetSwrContext(drv->swr_ctx);
  JACK_ResetSwrContext(drv->swr_in_ctx);

  if (drv->pPlayPtr)
    jack_ringbuffer_reset(drv->pPlayPtr);

  if (drv->pRecPtr)
    jack_ringbuffer_reset(drv->pRecPtr);

  drv->leftover_frames = 0;
  atomic_exchange_ulong(&drv->captured_client_bytes, 0);

}

static void JACK_silence_outputs(float **jack_out, int chs, jack_nframes_t nframes)
{
  const size_t bytes = (size_t)nframes * sizeof(float);

  for (int ch = 0; ch < chs; ch++)
    memset(jack_out[ch], 0, bytes);
}

static void JACK_apply_output_volume(jack_driver_t *drv, float **jack_out, int chs, jack_nframes_t nframes)
{
  if (!drv || !jack_out || chs <= 0 || nframes == 0)
    return;

  for (int ch = 0; ch < chs; ch++)
  {
    unsigned int volume = atomic_load_uint(&drv->volume[ch]);

    if (volume >= 100)
      continue;

    float gain = (volume == 0) ? 0.0f : ((float)volume * 0.01f);
    float *dst = jack_out[ch];

    for (jack_nframes_t f = 0; f < nframes; f++)
      dst[f] *= gain;
  }
}


static inline void JACK_deinterleave_stereo(float *dst0,
                                            float *dst1,
                                            const float *src,
                                            long frames)
{
  long f = 0;
  const long limit = frames & ~3L;

  for (; f < limit; f += 4)
  {
    const long s = f << 1;
    dst0[f]     = src[s];
    dst1[f]     = src[s + 1];
    dst0[f + 1] = src[s + 2];
    dst1[f + 1] = src[s + 3];
    dst0[f + 2] = src[s + 4];
    dst1[f + 2] = src[s + 5];
    dst0[f + 3] = src[s + 6];
    dst1[f + 3] = src[s + 7];
  }

  for (; f < frames; f++)
  {
    const long s = f << 1;
    dst0[f] = src[s];
    dst1[f] = src[s + 1];
  }
}

static inline void JACK_interleave_stereo(float *dst,
                                          const float *src0,
                                          const float *src1,
                                          long frames)
{
  long f = 0;
  const long limit = frames & ~3L;

  for (; f < limit; f += 4)
  {
    const long d = f << 1;
    dst[d]     = src0[f];
    dst[d + 1] = src1[f];
    dst[d + 2] = src0[f + 1];
    dst[d + 3] = src1[f + 1];
    dst[d + 4] = src0[f + 2];
    dst[d + 5] = src1[f + 2];
    dst[d + 6] = src0[f + 3];
    dst[d + 7] = src1[f + 3];
  }

  for (; f < frames; f++)
  {
    const long d = f << 1;
    dst[d] = src0[f];
    dst[d + 1] = src1[f];
  }
}

static inline void JACK_mix_stereo_to_mono(float *dst,
                                           const float *src0,
                                           const float *src1,
                                           long frames)
{
  long f = 0;
  const long limit = frames & ~3L;

  for (; f < limit; f += 4)
  {
    dst[f]     = (src0[f]     + src1[f])     * 0.5f;
    dst[f + 1] = (src0[f + 1] + src1[f + 1]) * 0.5f;
    dst[f + 2] = (src0[f + 2] + src1[f + 2]) * 0.5f;
    dst[f + 3] = (src0[f + 3] + src1[f + 3]) * 0.5f;
  }

  for (; f < frames; f++)
    dst[f] = (src0[f] + src1[f]) * 0.5f;
}

static void JACK_drain_playback_ring(jack_driver_t *drv, jack_ringbuffer_t *play)
{
  if (!drv || !play || drv->bytes_per_jack_output_frame == 0)
    return;

  const size_t frame_bytes = drv->bytes_per_jack_output_frame;
  size_t avail = jack_ringbuffer_read_space(play);
  avail -= avail % frame_bytes;

  if (avail > 0)
    jack_ringbuffer_read_advance(play, avail);
}

static void JACK_update_played_position(jack_driver_t *drv)
{
  if (!drv || !drv->client || drv->num_output_channels == 0 ||
      drv->bytes_per_output_frame == 0 ||
      drv->jack_sample_rate <= 0 ||
      drv->client_sample_rate <= 0)
    return;

  jack_nframes_t current = jack_last_frame_time(drv->client);
  jack_nframes_t previous =
      atomic_exchange_uint(&drv->last_callback_frame, current);

  if (previous == 0 || current <= previous)
    return;

  unsigned long client_frames =
      JACK_RescaleJackFramesToClientFramesRound(
          drv,
          (unsigned long)(current - previous));

  if (client_frames > 0)
    atomic_add_fetch_ulong(
        &drv->played_client_bytes,
        client_frames * drv->bytes_per_output_frame);
}

static void
JACK_callback_playback(jack_driver_t *drv, jack_nframes_t nframes)
{
  if (!drv || drv->num_output_channels == 0)
    return;

  if (drv->num_output_channels > MAX_OUTPUT_PORTS)
    return;

  const int chs = (int)drv->num_output_channels;
  const size_t frame_bytes = drv->bytes_per_jack_output_frame;
  const long nframes_long = (long)nframes;
  const size_t sample_bytes = (size_t)nframes * sizeof(float);

  float *jack_out[MAX_OUTPUT_PORTS];

  for (int ch = 0; ch < chs; ch++)
  {
    if (!drv->output_port[ch])
      return;

    jack_out[ch] = (float *)jack_port_get_buffer(drv->output_port[ch], nframes);
    if (!jack_out[ch])
      return;
  }

  jack_ringbuffer_t *play = (jack_ringbuffer_t *)drv->pPlayPtr;

  if (atomic_load_int(&drv->input_passthrough))
  {
    int ok = 0;

    if (drv->num_input_channels > 0 &&
        drv->num_input_channels <= MAX_INPUT_PORTS &&
        drv->bytes_per_jack_input_frame > 0)
    {
      float *jack_in[MAX_INPUT_PORTS];
      ok = 1;

      for (int ch = 0; ch < (int)drv->num_input_channels; ch++)
      {
        if (!drv->input_port[ch])
        {
          ok = 0;
          break;
        }

        jack_in[ch] = (float *)jack_port_get_buffer(drv->input_port[ch], nframes);
        if (!jack_in[ch])
        {
          ok = 0;
          break;
        }
      }

      if (ok)
      {
        if (drv->num_input_channels == 1)
        {
          for (int ch = 0; ch < chs; ch++)
            memcpy(jack_out[ch], jack_in[0], sample_bytes);
        }
        else if (chs == 1)
        {
          JACK_mix_stereo_to_mono(jack_out[0],
                                  jack_in[0],
                                  jack_in[1],
                                  (long)nframes);
        }
        else
        {
          memcpy(jack_out[0], jack_in[0], sample_bytes);
          memcpy(jack_out[1], jack_in[1], sample_bytes);
          for (int ch = 2; ch < chs; ch++)
            memcpy(jack_out[ch], jack_in[1], sample_bytes);
        }

        JACK_apply_output_volume(drv, jack_out, chs, nframes);
        JACK_drain_playback_ring(drv, play);
        return;
      }
    }

    JACK_silence_outputs(jack_out, chs, nframes);
    JACK_drain_playback_ring(drv, play);
    return;
  }

  if (!play || frame_bytes == 0)
  {
    JACK_silence_outputs(jack_out, chs, nframes);
    return;
  }

  const long available_frames =
      (long)(jack_ringbuffer_read_space(play) / frame_bytes);
  const long target_frames =
      (available_frames < nframes_long) ? available_frames : nframes_long;

  if (target_frames <= 0)
  {
    atomic_exchange_long(&drv->underrun_count, 1);
    JACK_silence_outputs(jack_out, chs, nframes);
    return;
  }

  jack_ringbuffer_data_t vec[2];
  jack_ringbuffer_get_read_vector(play, vec);

  long frames_done = 0;

  for (int v = 0; v < 2 && frames_done < target_frames; v++)
  {
    const float *src = (const float *)vec[v].buf;
    long vec_frames = vec[v].len / frame_bytes;
    long remaining = target_frames - frames_done;
    long to_copy = (vec_frames < remaining) ? vec_frames : remaining;

    if (to_copy <= 0)
      continue;

    if (chs == 2)
    {
      JACK_deinterleave_stereo(jack_out[0] + frames_done,
                               jack_out[1] + frames_done,
                               src,
                               to_copy);
    }
    else if (chs == 1)
    {
      memcpy(jack_out[0] + frames_done,
             src,
             (size_t)to_copy * sizeof(float));
    }
    else
    {
      for (int ch = 0; ch < chs; ch++)
      {
        float *dst = jack_out[ch] + frames_done;
        const float *s = src + ch;

        for (long f = 0; f < to_copy; f++)
          dst[f] = s[f * chs];
      }
    }

    frames_done += to_copy;
  }

  if (frames_done < nframes_long)
  {
    atomic_exchange_long(&drv->underrun_count, 1);

    long zero_frames = nframes_long - frames_done;
    for (int ch = 0; ch < chs; ch++)
      memset(jack_out[ch] + frames_done, 0, zero_frames * sizeof(float));
  }

  JACK_apply_output_volume(drv, jack_out, chs, nframes);
  jack_ringbuffer_read_advance(play, frames_done * frame_bytes);
}


static void
JACK_callback_capture(jack_driver_t *drv, jack_nframes_t nframes)
{
  if (!drv || drv->num_input_channels == 0)
    return;

  if (drv->num_input_channels > MAX_INPUT_PORTS)
    return;

  const int chs = (int)drv->num_input_channels;
  const long nframes_long = (long)nframes;
  const size_t frame_bytes = drv->bytes_per_jack_input_frame;

  if (frame_bytes == 0)
    return;

  jack_ringbuffer_t *rec = (jack_ringbuffer_t *)drv->pRecPtr;
  if (!rec)
    return;

  const long writable_frames =
      (long)(jack_ringbuffer_write_space(rec) / frame_bytes);
  const long frames_to_write =
      (nframes_long < writable_frames) ? nframes_long : writable_frames;

  if (frames_to_write <= 0)
  {
    __sync_add_and_fetch(&drv->input_overrun_count, 1);
    return;
  }

  if (frames_to_write < nframes_long)
    __sync_add_and_fetch(&drv->input_overrun_count, 1);

  float *jack_in[MAX_INPUT_PORTS];

  for (int ch = 0; ch < chs; ch++)
  {
    if (!drv->input_port[ch])
      return;

    jack_in[ch] = (float *)jack_port_get_buffer(drv->input_port[ch], nframes);
    if (!jack_in[ch])
      return;
  }

  jack_ringbuffer_data_t vec[2];
  jack_ringbuffer_get_write_vector(rec, vec);

  long frames_done = 0;

  for (int v = 0; v < 2 && frames_done < frames_to_write; v++)
  {
    float *dst = (float *)vec[v].buf;
    long vec_frames = vec[v].len / frame_bytes;
    long remaining = frames_to_write - frames_done;
    long to_copy = (vec_frames < remaining) ? vec_frames : remaining;

    if (to_copy <= 0)
      continue;

    if (chs == 2)
    {
      JACK_interleave_stereo(dst,
                             jack_in[0] + frames_done,
                             jack_in[1] + frames_done,
                             to_copy);
    }
    else if (chs == 1)
    {
      memcpy(dst,
             jack_in[0] + frames_done,
             (size_t)to_copy * sizeof(float));
    }
    else
    {
      for (long f = 0; f < to_copy; f++)
      {
        for (int ch = 0; ch < chs; ch++)
          dst[(f * chs) + ch] = jack_in[ch][frames_done + f];
      }
    }

    frames_done += to_copy;
  }

  jack_ringbuffer_write_advance(rec, frames_done * frame_bytes);
}

static int JACK_callback(jack_nframes_t nframes, void *arg)
{
  jack_driver_t *drv = (jack_driver_t *)arg;

  if (!drv)
    return 0;

  atomic_store_int(&drv->cb_active, TRUE);

  if (atomic_exchange_int(&drv->xrun_pending, 0))
    atomic_store_int(&drv->xrun_flag, 1);

  atomic_add_fetch_ulong(&drv->last_hw_frame_count, (unsigned long)nframes);
  JACK_update_played_position(drv);

  JACK_callback_capture(drv, nframes);
  JACK_callback_playback(drv, nframes);

  return 0;
}

int JACK_srate(nframes_t nframes, void *arg)
{
  jack_driver_t *drv = (jack_driver_t *)arg;

  drv->jack_sample_rate = (long)nframes;

  JACK_RecalculateRatios(drv);

  JACK_ResizeRingBuffers(drv, drv->SPVF);

  return 0;
}

void JACK_shutdown(void *arg)
{
  jack_driver_t *drv = (jack_driver_t *)arg;

  if (!drv)
    return;

  drv->client = 0;

#if JACK_CLOSE_HACK
  JACK_CloseDevice(drv, TRUE);
#else
  JACK_CloseDevice(drv);
#endif

  drv->jackd_died = TRUE;
  drv->client = 0;

  veejay_msg(VEEJAY_MSG_ERROR, "[AUDIO] Jack has shutdown. Audio reconnect will use the JACK backoff policy.");
}

static void
JACK_Error(const char *desc)
{
  ERR("%s", desc);
}

static int
JACK_OpenDeviceRaw(jack_driver_t *drv)
{
  const char **ports;
  char *our_client_name = 0;
  unsigned int i;
  int failed = 0;

#if JACK_CLOSE_HACK

  if (drv->client)
  {
    if (drv->in_use)
      return ERR_OPENING_JACK;

    drv->in_use = TRUE;
    return ERR_SUCCESS;
  }
#endif

  jack_set_error_function(JACK_Error);

  int name_len = snprintf(NULL, 0, "%s_%d_%d%02d", client_name, getpid(),
                          drv->deviceID, drv->clientCtr + 1);

  our_client_name = (char *)vj_calloc(name_len + 1);

  if (our_client_name == NULL)
  {
    ERR("Failed to allocate memory for client name.");
    return ERR_OPENING_JACK;
  }

  snprintf(our_client_name, name_len + 1, "%s_%d_%d%02d", client_name, getpid(),
           drv->deviceID, drv->clientCtr++);

  jack_status_t status;
#ifndef HAVE_JACK2
  if ((drv->client = jack_client_new(our_client_name)) == 0)
#else
  if ((drv->client = jack_client_open(our_client_name, JackNullOption | JackNoStartServer, &status)) == 0)
#endif
  {

    free(our_client_name);
    return ERR_OPENING_JACK;
  }

  free(our_client_name);

  jack_set_process_callback(drv->client, JACK_callback, drv);

  jack_set_buffer_size_callback(drv->client, JACK_bufsize, drv);

  jack_set_sample_rate_callback(drv->client, JACK_srate, drv);

  jack_set_xrun_callback(drv->client, JACK_xrun_callback, drv);

  jack_on_shutdown(drv->client, JACK_shutdown, drv);

  drv->jack_sample_rate = jack_get_sample_rate(drv->client);
  drv->jack_buffer_size = jack_get_buffer_size(drv->client);

  if (drv->client_sample_rate <= 0)
    drv->client_sample_rate = drv->jack_sample_rate;

  JACK_RecalculateRatios(drv);

  const enum AVSampleFormat client_fmt =
      (drv->bits_per_channel == 16)
          ? AV_SAMPLE_FMT_S16
          : AV_SAMPLE_FMT_U8;

  const long max_client_frames = 8192;
  const size_t alignment = 32;

  if (drv->num_output_channels > 0)
  {
#if LIBAVUTIL_VERSION_MAJOR >= 57
    AVChannelLayout out_layout;
    AVChannelLayout in_layout;

    av_channel_layout_default(&out_layout, drv->num_output_channels);
    av_channel_layout_default(&in_layout, drv->num_output_channels);

    drv->swr_ctx = NULL;

    swr_alloc_set_opts2(
        &drv->swr_ctx,
        &out_layout,
        AV_SAMPLE_FMT_FLT,
        drv->jack_sample_rate,
        &in_layout,
        client_fmt,
        drv->client_sample_rate,
        0,
        NULL);

    av_channel_layout_uninit(&out_layout);
    av_channel_layout_uninit(&in_layout);
#else
    drv->swr_ctx = swr_alloc_set_opts(
        NULL,
        av_get_default_channel_layout(drv->num_output_channels),
        AV_SAMPLE_FMT_FLT,
        drv->jack_sample_rate,
        av_get_default_channel_layout(drv->num_output_channels),
        client_fmt,
        drv->client_sample_rate,
        0,
        NULL);
#endif

    if (!drv->swr_ctx || swr_init(drv->swr_ctx) < 0)
    {
      veejay_msg(VEEJAY_MSG_ERROR,
                 "[AUDIO] Failed to initialize output resampler context");
      return -1;
    }

    const long max_out_frames =
        (long)ceil(
            (double)max_client_frames *
            (double)drv->jack_sample_rate /
            (double)drv->client_sample_rate) +
        32;

    drv->resample_buf_frames = max_out_frames;

    size_t size =
        drv->resample_buf_frames *
        drv->num_output_channels *
        sizeof(float);

    int err = posix_memalign((void **)&drv->resample_buf, alignment, size);
    if (err != 0 || !drv->resample_buf)
    {
      veejay_msg(VEEJAY_MSG_ERROR, "[AUDIO] Failed to allocate output resample buffer");
      return -1;
    }
  }

  if (drv->num_input_channels > 0)
  {
#if LIBAVUTIL_VERSION_MAJOR >= 57
    AVChannelLayout out_layout;
    AVChannelLayout in_layout;

    av_channel_layout_default(&out_layout, drv->num_input_channels);
    av_channel_layout_default(&in_layout, drv->num_input_channels);

    drv->swr_in_ctx = NULL;

    swr_alloc_set_opts2(
        &drv->swr_in_ctx,
        &out_layout,
        client_fmt,
        drv->client_sample_rate,
        &in_layout,
        AV_SAMPLE_FMT_FLT,
        drv->jack_sample_rate,
        0,
        NULL);

    av_channel_layout_uninit(&out_layout);
    av_channel_layout_uninit(&in_layout);
#else
    drv->swr_in_ctx = swr_alloc_set_opts(
        NULL,
        av_get_default_channel_layout(drv->num_input_channels),
        client_fmt,
        drv->client_sample_rate,
        av_get_default_channel_layout(drv->num_input_channels),
        AV_SAMPLE_FMT_FLT,
        drv->jack_sample_rate,
        0,
        NULL);
#endif

    if (!drv->swr_in_ctx || swr_init(drv->swr_in_ctx) < 0)
    {
      veejay_msg(VEEJAY_MSG_ERROR,
                 "[AUDIO] Failed to initialize input resampler context");
      return -1;
    }

    const long max_in_jack_frames =
        (long)ceil(
            (double)max_client_frames *
            (double)drv->jack_sample_rate /
            (double)drv->client_sample_rate) +
        drv->jack_buffer_size +
        64;

    drv->input_resample_buf_frames = max_in_jack_frames;

    size_t size =
        drv->input_resample_buf_frames *
        drv->num_input_channels *
        sizeof(float);

    int err = posix_memalign((void **)&drv->input_resample_buf, alignment, size);
    if (err != 0 || !drv->input_resample_buf)
    {
      veejay_msg(VEEJAY_MSG_ERROR, "[AUDIO] Failed to allocate input resample buffer");
      return -1;
    }
  }

  for (i = 0; i < drv->num_output_channels; i++)
  {
    char portname[32];
    JACK_FormatPortName(portname, sizeof(portname), "output", i);

    drv->output_port[i] = jack_port_register(drv->client, portname,
                                             JACK_DEFAULT_AUDIO_TYPE,
                                             JackPortIsOutput, 0);
    if (drv->output_port[i] == NULL)
    {
      ERR("Failed to register output port '%s'. Cannot continue connection.", portname);
      failed = 1;
      break;
    }
  }

  for (i = 0; i < drv->num_input_channels; i++)
  {
    char portname[32];
    JACK_FormatPortName(portname, sizeof(portname), "input", i);

    drv->input_port[i] = jack_port_register(drv->client, portname,
                                            JACK_DEFAULT_AUDIO_TYPE,
                                            JackPortIsInput, 0);
    if (drv->input_port[i] == NULL)
    {
      ERR("Failed to register input port '%s'.", portname);
      failed = 1;
      break;
    }
  }

  if (failed)
  {
#if JACK_CLOSE_HACK
    JACK_CloseDevice(drv, TRUE);
#else
    JACK_CloseDevice(drv);
#endif
    return ERR_OPENING_JACK;
  }

#if JACK_CLOSE_HACK
  drv->in_use = TRUE;
#endif

  if (jack_activate(drv->client))
  {
    ERR("cannot activate client");
    return ERR_OPENING_JACK;
  }

  const char *client_name_stable = jack_get_client_name(drv->client);
  if (client_name_stable == NULL)
  {
    ERR("FATAL: jack_get_client_name returned NULL after activation.");
    return ERR_OPENING_JACK;
  }

  for (i = 0; i < drv->num_output_channels; i++)
  {
    char full_portname[64];
    char portname_suffix[32];

    JACK_FormatPortName(portname_suffix, sizeof(portname_suffix), "output", i);

    snprintf(full_portname, sizeof(full_portname), "%s:%s", client_name_stable, portname_suffix);

    jack_port_t *active_port = jack_port_by_name(drv->client, full_portname);

    if (active_port != NULL)
    {
      drv->output_port[i] = active_port;
    }
    else
    {

      ERR("Failed to retrieve active port pointer for %s. Original pointer remains.", full_portname);
    }
  }

  if ((drv->num_output_channels > 0) &&
      (port_connection_mode == CONNECT_ALL ||
       port_connection_mode == CONNECT_OUTPUT))
  {
    if ((drv->jack_port_name_count == 0) || (drv->jack_port_name_count == 1))
    {
      if (drv->jack_port_name_count == 0)
      {

        ports = JACK_GetPreferredPlaybackPorts(drv);
      }
      else
      {
        ports = JACK_GetPortsAudioOrAny(drv,
                                      drv->jack_port_name[0],
                                      drv->jack_output_port_flags);
      }

      if (!ports)
      {
        WARN("No JACK playback target ports found; output ports remain available for manual wiring");
      }
      else
      {
        unsigned int connected = 0;
        unsigned int seen_usable = 0;
        const int default_autoconnect = (drv->jack_port_name_count == 0);

        for (i = 0; ports[i]; i++)
        {
          const int playback_target = JACK_PortLooksLikePlaybackTarget(ports[i]);

          if (!JACK_PortIsUsable(drv, ports[i], JackPortIsInput))
            continue;

          if (default_autoconnect)
          {
            if (!playback_target && JACK_PortLooksLikeNonPlaybackTarget(ports[i]))
              continue;

            if (connected >= drv->num_output_channels && !playback_target)
              break;
          }
          else if (connected >= drv->num_output_channels)
          {
            break;
          }

          seen_usable++;

          const int n = connected % drv->num_output_channels;

          if (jack_connect(drv->client,
                           jack_port_name(drv->output_port[n]),
                           ports[i]))
          {
            WARN("cannot connect output channel %d to JACK playback target '%s'",
                 n, ports[i]);
          }
          else
          {
            connected++;
          }
        }

        if (connected == 0)
        {
          WARN("No usable JACK playback target ports were connected; output ports remain available for manual wiring");
        }
        else if (!default_autoconnect && connected < drv->num_output_channels)
        {
          WARN("Only connected %u/%lu JACK playback output channels",
               connected,
               drv->num_output_channels);
        }

        if (seen_usable == 0)
        {
          WARN("No usable JACK playback targets found after filtering own/capture/monitor ports");
        }

        free(ports);
      }
    }
    else
    {
      for (i = 0; i < drv->jack_port_name_count && i < drv->num_output_channels; i++)
      {
        ports = JACK_GetPortsAudioOrAny(drv,
                               drv->jack_port_name[i],
                               drv->jack_output_port_flags);

        if (!ports)
        {
          WARN("jack_get_ports() found no playback target matching '%s'",
               drv->jack_port_name[i]);
          continue;
        }

        if (!JACK_PortIsUsable(drv, ports[0], JackPortIsInput))
        {
          WARN("skipping unusable JACK playback target '%s'", ports[0]);
          free(ports);
          continue;
        }

        if (jack_connect(drv->client,
                         jack_port_name(drv->output_port[i]),
                         ports[0]))
        {
          WARN("cannot connect output channel %u to JACK playback target '%s'",
               i, ports[0]);
          failed = 1;
        }

        free(ports);
      }
    }
  }

  if ((drv->num_input_channels > 0) &&
      (port_connection_mode == CONNECT_ALL))
  {
    if ((drv->jack_port_name_count == 0) || (drv->jack_port_name_count == 1))
    {
      if (drv->jack_port_name_count == 0)
      {
        ports = jack_get_ports(drv->client, NULL, JACK_DEFAULT_AUDIO_TYPE, drv->jack_input_port_flags);
      }
      else
      {
        ports = jack_get_ports(drv->client, drv->jack_port_name[0], JACK_DEFAULT_AUDIO_TYPE,
                               drv->jack_input_port_flags);
      }

      if (!ports)
      {
        veejay_msg(VEEJAY_MSG_WARNING,
                   "[AUDIO] No JACK capture source ports found; input ports remain available for manual wiring");
      }
      else
      {
        unsigned int connected = 0;
        unsigned int seen_usable = 0;

        for (i = 0; ports[i]; i++)
        {
          if (!JACK_PortIsUsable(drv, ports[i], JackPortIsOutput))
            continue;

          seen_usable++;
          int n = connected % drv->num_input_channels;

          if (jack_connect(drv->client, ports[i], jack_port_name(drv->input_port[n])))
          {
            WARN("cannot connect JACK capture source %d('%s')", n, ports[i]);
          }

          connected++;

          if (port_connection_mode != CONNECT_ALL && connected >= drv->num_input_channels)
            break;
        }

        if (seen_usable == 0)
        {
          veejay_msg(VEEJAY_MSG_WARNING,
                     "[AUDIO] No usable external JACK audio capture sources found; input ports remain available for manual wiring");
        }

        free(ports);
      }
    }
    else
    {
      for (i = 0; i < drv->jack_port_name_count; i++)
      {
        ports = jack_get_ports(drv->client, drv->jack_port_name[i], JACK_DEFAULT_AUDIO_TYPE,
                               drv->jack_input_port_flags);

        if (!ports)
        {
          WARN("jack_get_ports() found no capture source matching '%s'",
               drv->jack_port_name[i]);
          continue;
        }

        int n = i % drv->num_input_channels;

        if (!JACK_PortIsUsable(drv, ports[0], JackPortIsOutput))
        {
          WARN("skipping unusable JACK capture source '%s'", ports[0]);
          free(ports);
          continue;
        }

        if (jack_connect(drv->client, ports[0], jack_port_name(drv->input_port[n])))
        {
          WARN("cannot connect JACK capture source '%s' to input port %d",
               ports[0], n);
        }

        free(ports);
      }
    }
  }

  if (failed)
  {
#if JACK_CLOSE_HACK
    JACK_CloseDevice(drv, TRUE);
#else
    JACK_CloseDevice(drv);
#endif
    return ERR_OPENING_JACK;
  }

  drv->jackd_died = FALSE;
  drv->state = PAUSED;

  return ERR_SUCCESS;
}

static int
JACK_OpenDevice(jack_driver_t *drv)
{
  if (!JACK_ReconnectAllowed(drv))
    return ERR_OPENING_JACK;

  int retval = JACK_OpenDeviceRaw(drv);

  if (retval == ERR_SUCCESS)
  {
    JACK_ResetReconnectPolicy(drv);
    return ERR_SUCCESS;
  }

  if (drv && drv->client)
  {
#if JACK_CLOSE_HACK
    JACK_CloseDevice(drv, TRUE);
#else
    JACK_CloseDevice(drv);
#endif
  }

  JACK_RecordOpenFailure(drv, retval);
  return retval;
}

#if JACK_CLOSE_HACK
static void
JACK_CloseDevice(jack_driver_t *drv, bool close_client)
#else
static void
JACK_CloseDevice(jack_driver_t *drv)
#endif
{
  unsigned int i;

#if JACK_CLOSE_HACK
  if (close_client)
  {
#endif
    if (drv->client)
    {
      int errorCode = jack_client_close(drv->client);
      if (errorCode)
        ERR("jack_client_close() failed returning an error code of %d", errorCode);
    }
    drv->client = 0;
    if (drv->jack_port_name_count > 0 && drv->jack_port_name != NULL)
    {
      for (i = 0; i < drv->jack_port_name_count; i++)
        free(drv->jack_port_name[i]);
      free(drv->jack_port_name);
      drv->jack_port_name = NULL;
    }
    JACK_CleanupDriver(drv);

#if JACK_CLOSE_HACK
  }
  else
  {
    drv->in_use = FALSE;

    if (!drv->client)
    {

    }
  }
#endif
}

static void
JACK_ResetFromDriver(jack_driver_t *drv)
{
  if (!drv)
    return;

  atomic_exchange_int(&drv->state, RESET);
}

void JACK_Reset(int deviceID)
{
  jack_driver_t *drv = JACK_GetDriverRaw(deviceID);
  JACK_ResetFromDriver(drv);
}

int JACK_Open(int *deviceID, unsigned int bits_per_channel, unsigned long *rate,
              int channels, double SPVF)
{
  return JACK_OpenEx(deviceID, bits_per_channel,
                     rate,
                     0, channels,
                     NULL, 0, JackPortIsPhysical, SPVF);
}

int JACK_OpenEx(int *deviceID, unsigned int bits_per_channel,
                unsigned long *rate,
                unsigned int input_channels, unsigned int output_channels,
                const char **jack_port_name,
                unsigned int jack_port_name_count, unsigned long jack_port_flags, double SPVF)
{
  jack_driver_t *drv = 0;
  unsigned int i;
  int retval;

  if (input_channels < 1 && output_channels < 1)
  {
    ERR("no input OR output channels, nothing to do");
    return ERR_OPENING_JACK;
  }

  for (i = 0; i < MAX_OUTDEVICES; i++)
  {
    if (!outDev[i].allocated)
    {
      drv = &outDev[i];
      break;
    }
  }

  if (!drv)
  {
    ERR("no more devices available");
    return ERR_OPENING_JACK;
  }

  atomic_exchange_int(&drv->state, STOPPED);
  atomic_exchange_uint(&drv->last_callback_frame, 0);
  atomic_exchange_ulong(&drv->written_client_bytes, 0);
  atomic_exchange_ulong(&drv->client_bytes, 0);
  atomic_exchange_ulong(&drv->played_client_bytes, 0);
  atomic_exchange_ulong(&drv->captured_client_bytes, 0);
  atomic_exchange_long(&drv->input_overrun_count, 0);

  if (output_channels > MAX_OUTPUT_PORTS || input_channels > MAX_INPUT_PORTS)
  {
    return ERR_TOO_MANY_CHANNELS;
  }

  drv->jack_output_port_flags = jack_port_flags | JackPortIsInput;
  drv->jack_input_port_flags = jack_port_flags | JackPortIsOutput;

  drv->in_use = FALSE;
  drv->client_sample_rate = *rate;
  drv->bits_per_channel = bits_per_channel;
  drv->num_input_channels = input_channels;
  drv->num_output_channels = output_channels;

  drv->bytes_per_input_frame = (drv->bits_per_channel * drv->num_input_channels) / 8;
  drv->bytes_per_output_frame = (drv->bits_per_channel * drv->num_output_channels) / 8;

  drv->bytes_per_jack_output_frame = sizeof(jack_default_audio_sample_t) * drv->num_output_channels;
  drv->bytes_per_jack_input_frame = sizeof(jack_default_audio_sample_t) * drv->num_input_channels;

  drv->SPVF = SPVF;

  drv->jack_port_name = NULL;
  drv->jack_port_name_count = 0;

  if (jack_port_name && jack_port_name_count > 0)
  {
    drv->jack_port_name = (char **)vj_calloc(sizeof(char *) * jack_port_name_count);
    if (!drv->jack_port_name)
      return ERR_OPENING_JACK;

    for (i = 0; i < jack_port_name_count; i++)
    {
      if (!jack_port_name[i])
        continue;

      size_t len = strlen(jack_port_name[i]);
      drv->jack_port_name[i] = (char *)vj_calloc(len + 1);
      if (!drv->jack_port_name[i])
      {
        for (unsigned int j = 0; j < i; j++)
          free(drv->jack_port_name[j]);
        free(drv->jack_port_name);
        drv->jack_port_name = NULL;
        drv->jack_port_name_count = 0;
        return ERR_OPENING_JACK;
      }

      memcpy(drv->jack_port_name[i], jack_port_name[i], len);
    }

    drv->jack_port_name_count = jack_port_name_count;
  }

  retval = JACK_OpenDevice(drv);
  if (retval != ERR_SUCCESS)
    return retval;

  JACK_RecalculateRatios(drv);
  JACK_ResizeRingBuffers(drv, SPVF);

  jack_nframes_t buffer_size = jack_get_buffer_size(drv->client);
  drv->jack_buffer_size = buffer_size;
  drv->allocated = TRUE;
  *deviceID = drv->deviceID;

  return ERR_SUCCESS;
}

int JACK_Close(int deviceID)
{
  jack_driver_t *drv = JACK_GetDriverRaw(deviceID);
  if (!drv || !drv->allocated)
    return -1;

  atomic_exchange_int(&drv->state, CLOSED);

#if JACK_CLOSE_HACK
  JACK_CloseDevice(drv, TRUE);
#else
  JACK_CloseDevice(drv);
#endif

  JACK_ResetReconnectPolicy(drv);

  if (drv->pPlayPtr)
  {
    jack_ringbuffer_free(drv->pPlayPtr);
    drv->pPlayPtr = NULL;
  }

  if (drv->jack_port_name)
  {
    for (int i = 0; i < drv->jack_port_name_count; i++)
    {
      if (drv->jack_port_name[i])
        free(drv->jack_port_name[i]);
    }
    free(drv->jack_port_name);
    drv->jack_port_name = NULL;
  }

  pthread_mutex_lock(&device_mutex);
  drv->allocated = FALSE;
  atomic_exchange_int(&drv->in_use, FALSE);
  pthread_mutex_unlock(&device_mutex);

  return 0;
}

void JACK_Flush(int deviceID)
{
  jack_driver_t *drv = JACK_GetDriverRaw(deviceID);
  JACK_flush(drv);
}

static long JACK_RingbufferWriteFrames( jack_driver_t *drv, const float *src, long frames)
{
  jack_ringbuffer_data_t vec[2];
  jack_ringbuffer_get_write_vector(drv->pPlayPtr, vec);

  long frames_written = 0;
  const int chs = (int)drv->num_output_channels;
  const size_t frame_bytes = drv->bytes_per_jack_output_frame;

  for (int v = 0; v < 2 && frames_written < frames; v++)
  {
    const long vec_frames = vec[v].len / frame_bytes;
    const long remaining = frames - frames_written;
    const long to_copy = (remaining < vec_frames) ? remaining : vec_frames;

    if (to_copy <= 0)
      continue;

    memcpy(vec[v].buf,
           src + frames_written * chs,
           (size_t)to_copy * frame_bytes);

    frames_written += to_copy;
  }

  jack_ringbuffer_write_advance(
      drv->pPlayPtr,
      (size_t)frames_written * frame_bytes);

  return frames_written;
}

static void JACK_add_accepted_output_bytes(jack_driver_t *drv,
                                           unsigned long bytes)
{
  if (!drv || bytes == 0)
    return;

  atomic_add_fetch_ulong(&drv->client_bytes, bytes);
  atomic_add_fetch_ulong(&drv->written_client_bytes, bytes);
}

long JACK_Write(int deviceID, unsigned char *data, unsigned long bytes)
{
  jack_driver_t *drv = JACK_GetDriverReconnect(deviceID);

  if (!drv || !data || bytes == 0)
    return 0;

  if (drv->num_output_channels == 0 || !drv->pPlayPtr || !drv->swr_ctx)
    return 0;

  const int output_channels = (int)drv->num_output_channels;
  const int bytes_per_sample = (drv->bits_per_channel == 16) ? 2 : 1;
  const int bytes_per_frame = bytes_per_sample * output_channels;
  const size_t jack_frame_bytes = drv->bytes_per_jack_output_frame;

  if (bytes_per_frame <= 0 || jack_frame_bytes == 0)
    return 0;

  const long in_frames = bytes / bytes_per_frame;

  if (in_frames <= 0)
    return 0;

  if (atomic_load_int(&drv->input_passthrough))
  {
    JACK_add_accepted_output_bytes(
        drv,
        (unsigned long)in_frames * (unsigned long)bytes_per_frame);
    return in_frames;
  }

  if (atomic_exchange_int(&drv->xrun_pending, 0))
    JACK_flush(drv);

  long out_required = JACK_get_required_free_frames(drv, (int)in_frames);
  int out_frames_needed = (int)out_required;

  if (out_frames_needed <= 0)
    return 0;

  if (out_frames_needed > drv->resample_buf_frames)
  {
    int upper = swr_get_out_samples(drv->swr_ctx, in_frames);
    WARN("JACK_Write block too large for resample buffer: client=%ld jack_required=%d upper=%d cap=%ld delay=%" PRId64 " rates=%ld/%ld",
         in_frames,
         out_frames_needed,
         upper,
         drv->resample_buf_frames,
         swr_get_delay(drv->swr_ctx, drv->client_sample_rate),
         drv->client_sample_rate,
         drv->jack_sample_rate);
    return 0;
  }

  const size_t rb_write_space = jack_ringbuffer_write_space(drv->pPlayPtr);
  const long rb_frames_capacity = rb_write_space / jack_frame_bytes;

  if (rb_frames_capacity < out_frames_needed)
    return 0;

  const uint8_t *in_ptr = data;
  float *out_ptr = drv->resample_buf;

  const int out_frames = swr_convert(
      drv->swr_ctx,
      (uint8_t **)&out_ptr,
      out_frames_needed,
      (const uint8_t **)&in_ptr,
      in_frames);

  if (out_frames <= 0)
    return 0;

  long jack_frames_written = JACK_RingbufferWriteFrames(drv, out_ptr, out_frames);

  if (jack_frames_written != out_frames)
    return 0;

  JACK_add_accepted_output_bytes(
      drv,
      (unsigned long)in_frames * (unsigned long)bytes_per_frame);

  return in_frames;
}

static long
JACK_RingbufferReadFramesToFloat(jack_driver_t *drv, float *dst, long frames)
{
  if (!drv || !drv->pRecPtr || !dst || frames <= 0)
    return 0;

  const int chs = drv->num_input_channels;
  const size_t frame_bytes = drv->bytes_per_jack_input_frame;

  if (chs <= 0 || frame_bytes == 0)
    return 0;

  jack_ringbuffer_data_t vec[2];
  jack_ringbuffer_get_read_vector(drv->pRecPtr, vec);

  long frames_read = 0;

  for (int v = 0; v < 2 && frames_read < frames; v++)
  {
    const long vec_frames = vec[v].len / frame_bytes;
    const long remaining = frames - frames_read;
    const long to_copy = (remaining < vec_frames) ? remaining : vec_frames;

    if (to_copy <= 0)
      continue;

    memcpy(
        dst + frames_read * chs,
        vec[v].buf,
        (size_t)to_copy * frame_bytes);

    frames_read += to_copy;
  }

  jack_ringbuffer_read_advance(drv->pRecPtr, frames_read * frame_bytes);

  return frames_read;
}

long JACK_Read(int deviceID, unsigned char *data, unsigned long bytes)
{
  jack_driver_t *drv = JACK_GetDriverReconnect(deviceID);

  if (!drv || !data || bytes == 0)
    return 0;

  if (drv->num_input_channels == 0 || !drv->pRecPtr || !drv->swr_in_ctx || !drv->input_resample_buf)
    return 0;

  if (drv->bytes_per_jack_input_frame == 0)
    return 0;

  const int input_channels = (int)drv->num_input_channels;
  const int bytes_per_sample = (drv->bits_per_channel == 16) ? 2 : 1;
  const int bytes_per_frame = bytes_per_sample * input_channels;
  const size_t jack_frame_bytes = drv->bytes_per_jack_input_frame;
  const long max_chunk_jack_frames = drv->input_resample_buf_frames;

  if (bytes_per_frame <= 0 || max_chunk_jack_frames <= 0)
    return 0;

  const long target_client_frames = bytes / bytes_per_frame;

  if (target_client_frames <= 0)
    return 0;

  long client_frames_done = 0;

  while (client_frames_done < target_client_frames)
  {
    const size_t available_bytes = jack_ringbuffer_read_space(drv->pRecPtr);
    const long available_jack_frames = (long)(available_bytes / jack_frame_bytes);

    if (available_jack_frames <= 0)
      break;

    const long remaining_client_frames = target_client_frames - client_frames_done;
    const long desired_jack_frames =
        JACK_rescale_client_to_jack_frames_ceil(drv, remaining_client_frames) + 8;

    long chunk_jack_frames =
        (available_jack_frames < desired_jack_frames)
            ? available_jack_frames
            : desired_jack_frames;
    chunk_jack_frames =
        (chunk_jack_frames < max_chunk_jack_frames)
            ? chunk_jack_frames
            : max_chunk_jack_frames;

    if (chunk_jack_frames <= 0)
      break;

    long read_jack_frames =
        JACK_RingbufferReadFramesToFloat(
            drv,
            drv->input_resample_buf,
            chunk_jack_frames);

    if (read_jack_frames <= 0)
      break;

    const uint8_t *in_ptr = (const uint8_t *)drv->input_resample_buf;
    uint8_t *out_ptr =
        data + (client_frames_done * bytes_per_frame);

    const int out_capacity =
        (int)(target_client_frames - client_frames_done);

    const int out_frames = swr_convert(
        drv->swr_in_ctx,
        &out_ptr,
        out_capacity,
        (const uint8_t **)&in_ptr,
        (int)read_jack_frames);

    if (out_frames < 0)
      break;

    if (out_frames == 0 && read_jack_frames < chunk_jack_frames)
      break;

    client_frames_done += out_frames;

    if (read_jack_frames < chunk_jack_frames)
      break;
  }

  const unsigned long bytes_done =
      (unsigned long)(client_frames_done * bytes_per_frame);

  if (bytes_done > 0)
    atomic_add_fetch_ulong(&drv->captured_client_bytes, bytes_done);

  return (long)bytes_done;
}

static int
JACK_SetVolumeForChannelFromDriver(jack_driver_t *drv,
                                   unsigned int channel, unsigned int volume)
{
  if (!drv || channel >= drv->num_output_channels)
    return 1;

  volume = (volume > 100) ? 100 : volume;

  atomic_exchange_uint(&drv->volume[channel], volume);

  return ERR_SUCCESS;
}

int JACK_SetVolumeForChannel(int deviceID, unsigned int channel,
                             unsigned int volume)
{
  jack_driver_t *drv = JACK_GetDriverRaw(deviceID);
  int retval = JACK_SetVolumeForChannelFromDriver(drv, channel, volume);
  return retval;
}

int JACK_SetAllVolume(int deviceID, unsigned int volume)
{
  jack_driver_t *drv = JACK_GetDriverRaw(deviceID);
  unsigned int i;

  if (!drv)
    return 1;

  for (i = 0; i < drv->num_output_channels; i++)
  {
    if (JACK_SetVolumeForChannelFromDriver(drv, i, volume) != ERR_SUCCESS)
    {
      return 1;
    }
  }

  return ERR_SUCCESS;
}

void JACK_GetVolumeForChannel(int deviceID, unsigned int channel,
                              unsigned int *volume)
{
  jack_driver_t *drv = JACK_GetDriverRaw(deviceID);

  if (!drv)
    return;

  if (channel >= drv->num_output_channels)
  {
    ERR("asking for channel index %d but we only have %d channels",
        channel, drv->num_output_channels);
    return;
  }

  if (volume)
  {
    *volume = atomic_load_uint(&drv->volume[channel]);
  }
}

int JACK_SetVolumeEffectType(int deviceID, int type)
{
  jack_driver_t *drv = JACK_GetDriverRaw(deviceID);
  if (!drv)
    return LINEAR;

  int retval = atomic_exchange_int(&drv->volumeEffectType, type);

  return retval;
}

int JACK_SetState(int deviceID, int state)
{
  jack_driver_t *drv = JACK_GetDriverRaw(deviceID);
  if (!drv)
    return -1;

  atomic_exchange_int(&drv->state, state);

  if (__sync_add_and_fetch(&drv->state, 0) == RESET)
  {
    atomic_exchange_ulong(&drv->written_client_bytes, 0);
    atomic_exchange_ulong(&drv->played_client_bytes, 0);
    atomic_exchange_ulong(&drv->client_bytes, 0);
    atomic_exchange_long(&drv->position_byte_offset, 0);

    if (drv->pPlayPtr)
      jack_ringbuffer_reset(drv->pPlayPtr);

    if (drv->pRecPtr)
      jack_ringbuffer_reset(drv->pRecPtr);

    JACK_ResetSwrContext(drv->swr_ctx);
    JACK_ResetSwrContext(drv->swr_in_ctx);

    atomic_exchange_ulong(&drv->captured_client_bytes, 0);

    atomic_exchange_int(&drv->state, STOPPED);
    veejay_msg(0, "[AUDIO] Jack playback/capture stopped");
  }

  return 0;
}

int JACK_XRUNHandled(int deviceID)
{
  jack_driver_t *drv = JACK_GetDriverRaw(deviceID);
  if (!drv)
    return -1;
  return atomic_exchange_int(&drv->xrun_flag,0);
}

int JACK_GetCallbackActive(int deviceID)
{
  jack_driver_t *drv = JACK_GetDriverRaw(deviceID);

  if (!drv || !drv->allocated)
  {
    return CLOSED;
  }

  return atomic_load_int(&drv->cb_active);
}

int JACK_GetState(int deviceID)
{
  jack_driver_t *drv = JACK_GetDriverRaw(deviceID);

  if (!drv || !drv->allocated)
  {
    return CLOSED;
  }

  int return_val = atomic_load_int(&drv->state);

  return return_val;
}

unsigned long
JACK_GetOutputBytesPerSecondFromDriver(jack_driver_t *drv)
{
  if (!drv)
    return 0;

  unsigned long bytes_per_frame = drv->bytes_per_output_frame;
  long sample_rate = drv->client_sample_rate;

  return bytes_per_frame * sample_rate;
}

unsigned long
JACK_GetOutputBytesPerSecond(int deviceID)
{
  jack_driver_t *drv = JACK_GetDriverRaw(deviceID);
  unsigned long return_val;

  return_val = JACK_GetOutputBytesPerSecondFromDriver(drv);

  return return_val;
}

static long
JACK_GetInputBytesPerSecondFromDriver(jack_driver_t *drv)
{
  if (!drv)
    return 0;

  unsigned long bytes_per_frame = drv->bytes_per_input_frame;
  long sample_rate = drv->client_sample_rate;

  return (long)(bytes_per_frame * sample_rate);
}

unsigned long
JACK_GetInputBytesPerSecond(int deviceID)
{
  jack_driver_t *drv = JACK_GetDriverRaw(deviceID);
  long return_val;
  ;
  return_val = JACK_GetInputBytesPerSecondFromDriver(drv);
  ;

  return return_val;
}

unsigned long
JACK_GetBytesPerInputFrame(int deviceID)
{
  jack_driver_t *drv = JACK_GetDriverRaw(deviceID);

  if (!drv)
    return 0;

  return (unsigned long)drv->bytes_per_input_frame;
}

unsigned long
JACK_GetInputBytesStored(int deviceID)
{
  jack_driver_t *drv = JACK_GetDriverRaw(deviceID);

  if (!drv || !drv->pRecPtr || drv->bytes_per_jack_input_frame == 0)
    return 0;

  size_t bytes_in_rb = jack_ringbuffer_read_space(drv->pRecPtr);
  unsigned long jack_frames =
      (unsigned long)(bytes_in_rb / drv->bytes_per_jack_input_frame);

  unsigned long client_frames =
      JACK_RescaleJackFramesToClientFramesRound(drv, jack_frames);

  return client_frames * drv->bytes_per_input_frame;
}

long
JACK_GetInputOverruns(int deviceID)
{
  jack_driver_t *drv = JACK_GetDriverRaw(deviceID);

  if (!drv)
    return 0;

  return atomic_load_long(&drv->input_overrun_count);
}

static unsigned long
JACK_RescaleJackFramesToClientFramesFloor(jack_driver_t *drv, unsigned long jack_frames)
{
  if (!drv || jack_frames == 0)
    return 0;

  if (drv->jack_sample_rate <= 0 || drv->client_sample_rate <= 0 ||
      drv->jack_sample_rate == drv->client_sample_rate)
    return jack_frames;

  return (unsigned long)(((uint64_t)jack_frames *
                          (uint64_t)drv->client_sample_rate) /
                         (uint64_t)drv->jack_sample_rate);
}

static unsigned long
JACK_RescaleJackFramesToClientFramesRound(jack_driver_t *drv, unsigned long jack_frames)
{
  if (!drv || jack_frames == 0)
    return 0;

  if (drv->jack_sample_rate <= 0 || drv->client_sample_rate <= 0 ||
      drv->jack_sample_rate == drv->client_sample_rate)
    return jack_frames;

  return (unsigned long)((((uint64_t)jack_frames *
                           (uint64_t)drv->client_sample_rate) +
                          ((uint64_t)drv->jack_sample_rate / 2ULL)) /
                         (uint64_t)drv->jack_sample_rate);
}

static unsigned long
JACK_JackFramesToClientBytesFloor(jack_driver_t *drv,
                                  unsigned long jack_frames,
                                  unsigned long client_frame_size)
{
  unsigned long client_frames =
      JACK_RescaleJackFramesToClientFramesFloor(drv, jack_frames);

  return client_frames * client_frame_size;
}

static long
JACK_GetBytesStoredFromDriver(jack_driver_t *drv)
{
  if (!drv || drv->pPlayPtr == 0)
    return 0;

  unsigned long jack_frame_size = drv->bytes_per_jack_output_frame;
  unsigned long client_frame_size = drv->bytes_per_output_frame;

  if (jack_frame_size == 0 || client_frame_size == 0)
    return 0;

  unsigned long jack_frames =
      (unsigned long)(jack_ringbuffer_read_space(drv->pPlayPtr) / jack_frame_size);
  unsigned long reserve_jack_frames =
      (drv->jack_buffer_size > 0) ? (unsigned long)drv->jack_buffer_size : 0UL;

  if (jack_frames <= reserve_jack_frames)
    return 0;

  unsigned long effective_jack_frames = jack_frames - reserve_jack_frames;
  unsigned long client_bytes =
      JACK_JackFramesToClientBytesFloor(drv,
                                        effective_jack_frames,
                                        client_frame_size);

  return (long)client_bytes;
}

unsigned long
JACK_GetBytesStored(int deviceID)
{
  jack_driver_t *drv = JACK_GetDriverRaw(deviceID);
  return (unsigned long)JACK_GetBytesStoredFromDriver(drv);
}

static unsigned long
JACK_GetBytesFreeSpaceFromDriver(jack_driver_t *drv)
{
  if (!drv || drv->pPlayPtr == 0)
    return 0;

  unsigned long jack_frame_size = drv->bytes_per_jack_output_frame;
  unsigned long client_frame_size = drv->bytes_per_output_frame;

  if (jack_frame_size == 0 || client_frame_size == 0)
    return 0;

  unsigned long free_jack_frames =
      (unsigned long)(jack_ringbuffer_write_space(drv->pPlayPtr) / jack_frame_size);
  unsigned long reserve_jack_frames =
      (drv->jack_buffer_size > 0) ? (unsigned long)drv->jack_buffer_size : 0UL;

  if (free_jack_frames <= reserve_jack_frames)
    return 0;

  free_jack_frames -= reserve_jack_frames;

  return JACK_JackFramesToClientBytesFloor(drv,
                                           free_jack_frames,
                                           client_frame_size);
}

unsigned long
JACK_GetBytesFreeSpace(int deviceID)
{
  jack_driver_t *drv = JACK_GetDriverRaw(deviceID);
  unsigned long return_val;

  return_val = JACK_GetBytesFreeSpaceFromDriver(drv);

  return return_val;
}

static unsigned long
JACK_GetPositionFromDriver(jack_driver_t *drv, int position, int type)
{
  if (!drv)
    return 0;

  long return_val = 0;
  long output_bytes_per_sec = JACK_GetOutputBytesPerSecondFromDriver(drv);

  if (atomic_load_int(&drv->state) == RESET)
  {
    return 0L;
  }

  if (type == WRITTEN)
  {
    return_val = atomic_load_ulong(&drv->client_bytes);
  }
  else if (type == WRITTEN_TO_JACK)
  {
    return_val = atomic_load_ulong(&drv->written_client_bytes);
  }
  else if (type == FRAMES_WRITTEN_TO_JACK)
  {
    return_val = atomic_load_ulong(&drv->written_client_bytes) / drv->bytes_per_output_frame;
  }
  else if (type == PLAYED)
  {
    return_val = atomic_load_ulong(&drv->played_client_bytes);

    if (drv->client && drv->jack_sample_rate > 0 && output_bytes_per_sec > 0)
    {
      jack_nframes_t current_jack_frame = jack_frame_time(drv->client);
      jack_nframes_t last_cb_frame = atomic_load_uint(&drv->last_callback_frame);

      if (last_cb_frame != 0 && current_jack_frame > last_cb_frame)
      {
        const uint64_t frames_since_cb =
            (uint64_t)(current_jack_frame - last_cb_frame);
        const uint64_t bytes_since =
            (frames_since_cb *
             (uint64_t)drv->client_sample_rate *
             (uint64_t)drv->bytes_per_output_frame) /
            (uint64_t)drv->jack_sample_rate;

        return_val += (long)bytes_since;
      }
    }
  }

  return_val += atomic_load_long(&drv->position_byte_offset);

  if (position == MILLISECONDS)
  {
    return_val = (output_bytes_per_sec != 0)
                     ? (long)(((double)return_val / (double)output_bytes_per_sec) * 1000.0)
                     : 0;
  }

  return return_val;
}

static long JACK_get_underruns(jack_driver_t *drv)
{

  return atomic_load_long(&drv->underrun_count);
}

static double JACK_get_total_latency(jack_driver_t *drv)
{
  if (!drv || !drv->client || !drv->pPlayPtr || !drv->output_port[0] ||
      drv->bytes_per_jack_output_frame == 0 || drv->jack_sample_rate <= 0)
    return 0.0;

  jack_latency_range_t range;
  jack_port_get_latency_range(drv->output_port[0], JackPlaybackLatency, &range);

  const double inv_jack_rate = 1.0 / (double)drv->jack_sample_rate;
  const size_t bytes_buffered = jack_ringbuffer_read_space(drv->pPlayPtr);
  const double ringbuffer_frames =
      (double)bytes_buffered / (double)drv->bytes_per_jack_output_frame;
  const jack_nframes_t frames_since_cycle =
      jack_frames_since_cycle_start(drv->client);

  return ((double)range.max +
          ringbuffer_frames +
          (double)frames_since_cycle) *
         inv_jack_rate;
}

static unsigned long JACK_get_played_frames_from_driver(jack_driver_t *drv)
{
  if (!drv)
    return 0;
  return atomic_load_ulong(&drv->last_hw_frame_count);
}

long JACK_GetPosition(int deviceID, int position, int type)
{
  jack_driver_t *drv = JACK_GetDriverRaw(deviceID);
  long return_val;

  return_val = JACK_GetPositionFromDriver(drv, position, type);

  return return_val;
}

unsigned long
JACK_GetPlayedFramesFromDriver(int deviceID)
{
  jack_driver_t *drv = JACK_GetDriverRaw(deviceID);
  return JACK_get_played_frames_from_driver(drv);
}

long JACK_GetUnderruns(int deviceID)
{
  jack_driver_t *drv = JACK_GetDriverRaw(deviceID);
  return JACK_get_underruns(drv);
}

double
JACK_GetTotalLatency(int deviceID)
{
  jack_driver_t *drv = JACK_GetDriverRaw(deviceID);
  return JACK_get_total_latency(drv);
}

void JACK_SetInputPassthrough(int deviceID, int enabled)
{
  jack_driver_t *drv = JACK_GetDriverRaw(deviceID);
  if (!drv)
    return;

  const int on = enabled ? TRUE : FALSE;
  atomic_exchange_int(&drv->input_passthrough, on);
}

int JACK_GetInputPassthrough(int deviceID)
{
  jack_driver_t *drv = JACK_GetDriverRaw(deviceID);
  if (!drv)
    return 0;
  return atomic_load_int(&drv->input_passthrough) ? 1 : 0;
}

static long JACK_rescale_client_to_jack_frames_ceil(jack_driver_t *drv,
                                                        long client_frames)
{
  if (!drv || client_frames <= 0)
    return 0;

  if (drv->client_sample_rate <= 0 || drv->jack_sample_rate <= 0)
    return client_frames;

  if (drv->client_sample_rate == drv->jack_sample_rate)
    return client_frames;

  return (long)((((uint64_t)client_frames * (uint64_t)drv->jack_sample_rate) +
                 (uint64_t)drv->client_sample_rate - 1ULL) /
                (uint64_t)drv->client_sample_rate);
}

static long JACK_get_required_free_frames(jack_driver_t *drv, int client_frames)
{
  long ratio_frames;
  long required;

  if (!drv || client_frames <= 0)
    return 0;

  ratio_frames = JACK_rescale_client_to_jack_frames_ceil(drv, client_frames);
  required = ratio_frames;

  required += (drv->client_sample_rate > 0 && drv->jack_sample_rate > 0 &&
               drv->client_sample_rate != drv->jack_sample_rate)
                  ? VJ_JACK_RESAMPLE_SLACK_FRAMES
                  : 0;

  return (required > 0) ? required : client_frames;
}

int JACK_get_ringbuffer_free_frames(jack_driver_t *drv)
{
  if (!drv || drv->pPlayPtr == NULL)
    return 0;

  unsigned long jack_frame_size = drv->bytes_per_jack_output_frame;
  if (jack_frame_size == 0)
    return 0;

  unsigned long jack_frames =
      (unsigned long)(jack_ringbuffer_write_space(drv->pPlayPtr) / jack_frame_size);
  unsigned long client_frames =
      JACK_RescaleJackFramesToClientFramesFloor(drv, jack_frames);

  return (client_frames > (unsigned long)INT32_MAX)
             ? INT32_MAX
             : (int)client_frames;
}

long JACK_get_ringbuffer_used(jack_driver_t *drv)
{
  if (!drv || drv->pPlayPtr == NULL)
    return 0;

  unsigned long jack_frame_size = drv->bytes_per_jack_output_frame;

  if (jack_frame_size == 0)
    return 0;

  unsigned long jack_frames =
      (unsigned long)(jack_ringbuffer_read_space(drv->pPlayPtr) / jack_frame_size);
  unsigned long client_frames =
      JACK_RescaleJackFramesToClientFramesFloor(drv, jack_frames);

  return (client_frames > (unsigned long)INT32_MAX)
             ? INT32_MAX
             : (long)client_frames;
}

int JACK_get_ringbuffer_size(jack_driver_t *drv)
{
  if (!drv || drv->pPlayPtr == NULL || drv->bytes_per_jack_output_frame == 0)
    return 0;

  unsigned long jack_frames =
      (unsigned long)(drv->pPlayPtr->size / drv->bytes_per_jack_output_frame);
  unsigned long client_frames =
      JACK_RescaleJackFramesToClientFramesFloor(drv, jack_frames);

  return (client_frames > (unsigned long)INT32_MAX)
             ? INT32_MAX
             : (int)client_frames;
}

int JACK_get_client_to_jack_frames(jack_driver_t *drv, int client_frames)
{
  long frames;

  if (!drv)
    return 0;

  frames = JACK_rescale_client_to_jack_frames_ceil(drv, client_frames);
  if (frames <= 0)
    return client_frames;

  return (int)frames;
}

int JACK_BufferIsStarving(int deviceID)
{
  jack_driver_t *drv = JACK_GetDriverRaw(deviceID);
  int return_val;

  return_val = JACK_isStarving(drv);

  return return_val;
}

unsigned long
JACK_GetBytesPerOutputFrame(int deviceID)
{
  jack_driver_t *drv = JACK_GetDriverRaw(deviceID);

  if (!drv)
    return 0;

  unsigned long return_val = (unsigned long)drv->bytes_per_output_frame;

  return return_val;
}

void JACK_ResetBuffer(int deviceID)
{
  jack_driver_t *drv = JACK_GetDriverRaw(deviceID);

  if (drv && drv->pPlayPtr)
    jack_ringbuffer_reset(drv->pPlayPtr);
}

void JACK_ResetInputBuffer(int deviceID)
{
  jack_driver_t *drv = JACK_GetDriverRaw(deviceID);

  if (drv && drv->pRecPtr)
    jack_ringbuffer_reset(drv->pRecPtr);

  if (drv)
  {
    JACK_ResetSwrContext(drv->swr_in_ctx);
    atomic_exchange_ulong(&drv->captured_client_bytes, 0);
  }
}

long JACK_GetSampleRate(int deviceID)
{
  jack_driver_t *drv = JACK_GetDriverRaw(deviceID);
  if (!drv)
    return 0;

  return drv->client_sample_rate;
}

int JACK_GetRingBufferFreeFrames(int deviceID)
{
  jack_driver_t *drv = JACK_GetDriverRaw(deviceID);
  int return_val;
  return_val = JACK_get_ringbuffer_free_frames(drv);
  return return_val;
}

long JACK_GetSampleRateJack(int deviceID)
{
  jack_driver_t *drv = JACK_GetDriverRaw(deviceID);
  if (!drv)
    return 0;

  return drv->jack_sample_rate;
}

long JACK_GetPeriodSize(int deviceID)
{
  jack_driver_t *drv = JACK_GetDriverRaw(deviceID);
  if (!drv)
    return 0;

  return drv->jack_buffer_size;
}

int JACK_GetClientToJackFrames(int deviceID, int client_frames)
{
  jack_driver_t *drv = JACK_GetDriverRaw(deviceID);
  int return_val;
  return_val = JACK_get_client_to_jack_frames(drv, client_frames);
  return return_val;
}

int JACK_GetRingBufferSize(int deviceID)
{
  jack_driver_t *drv = JACK_GetDriverRaw(deviceID);
  int return_val = 0;
  return_val = JACK_get_ringbuffer_size(drv);
  return return_val;
}

long JACK_GetRequiredFreeFrames(int deviceID, int client_frames)
{
  jack_driver_t *drv = JACK_GetDriverRaw(deviceID);
  long return_val = 0;
  return_val = JACK_get_required_free_frames(drv, client_frames);
  return return_val;
}

long JACK_GetRingBufferUsed(int deviceID)
{
  jack_driver_t *drv = JACK_GetDriverRaw(deviceID);
  long return_val = 0;
  return_val = JACK_get_ringbuffer_used(drv);
  return return_val;
}

static void JACK_CleanupDriver(jack_driver_t *drv)
{
  if (!drv)
    return;

  atomic_exchange_int(&drv->state, CLOSED);
  atomic_exchange_int(&drv->in_use, FALSE);
  atomic_exchange_int(&drv->cb_active, FALSE);
  atomic_exchange_int(&drv->input_passthrough, FALSE);

  drv->client = NULL;

  if (drv->swr_ctx)
  {
    swr_free(&drv->swr_ctx);
    drv->swr_ctx = NULL;
  }

  if (drv->swr_in_ctx)
  {
    swr_free(&drv->swr_in_ctx);
    drv->swr_in_ctx = NULL;
  }

  if (drv->resample_buf)
  {
    free(drv->resample_buf);
    drv->resample_buf = NULL;
  }
  drv->resample_buf_frames = 0;

  if (drv->input_resample_buf)
  {
    free(drv->input_resample_buf);
    drv->input_resample_buf = NULL;
  }
  drv->input_resample_buf_frames = 0;

  jack_ringbuffer_t *old_play_ptr =
      (jack_ringbuffer_t *)atomic_exchange_ptr((uintptr_t *)&drv->pPlayPtr, (uintptr_t)NULL);

  if (old_play_ptr)
    jack_ringbuffer_free(old_play_ptr);

  drv->pPlayPtr_size = 0;

  jack_ringbuffer_t *old_rec_ptr =
      (jack_ringbuffer_t *)atomic_exchange_ptr((uintptr_t *)&drv->pRecPtr, (uintptr_t)NULL);

  if (old_rec_ptr)
    jack_ringbuffer_free(old_rec_ptr);

  drv->pRecPtr_size = 0;

  for (unsigned int i = 0; i < drv->retired_ringbuffer_count; i++)
  {
    if (drv->retired_ringbuffers[i])
      jack_ringbuffer_free(drv->retired_ringbuffers[i]);
    drv->retired_ringbuffers[i] = NULL;
  }
  drv->retired_ringbuffer_count = 0;

  drv->jack_sample_rate = 0;
  drv->jack_buffer_size = 0;
  drv->jackd_died = FALSE;

  drv->output_sample_rate_ratio = 1.0;
  drv->input_sample_rate_ratio = 1.0;

  atomic_exchange_long(&drv->input_overrun_count, 0);
  atomic_exchange_ulong(&drv->captured_client_bytes, 0);
  atomic_exchange_ulong(&drv->played_client_bytes, 0);
  atomic_exchange_uint(&drv->last_callback_frame, 0);

  for (int i = 0; i < MAX_OUTPUT_PORTS; i++)
    drv->output_port[i] = NULL;

  for (int i = 0; i < MAX_INPUT_PORTS; i++)
    drv->input_port[i] = NULL;

  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  drv->previousTime = now;
  drv->last_reconnect_attempt = now;
}

void JACK_Init(void)
{
  jack_driver_t *drv;
  int x, y;

  if (init_done)
  {
    return;
  }
  init_done = 1;

  for (x = 0; x < MAX_OUTDEVICES; x++)
  {
    drv = &outDev[x];
    veejay_memset(drv, 0, sizeof(jack_driver_t));
    if (pthread_mutex_init(&drv->mutex, NULL) != 0)
    {
      ERR("Failed to initialize device mutex for device %d", x);
    }

    drv->deviceID = x;
    drv->volumeEffectType = LINEAR;

    for (y = 0; y < MAX_OUTPUT_PORTS; y++)
    {
      drv->volume[y] = DEFAULT_VOLUME;
    }

    JACK_CleanupDriver(drv);
    JACK_ResetReconnectPolicy(drv);

    JACK_ResetFromDriver(drv);
  }

  client_name = NULL;
  JACK_SetClientName((char*)"bio2jack");

}

void JACK_SetClientName(char *name)
{
  if (client_name)
  {
    free(client_name);
    client_name = NULL;
  }

  if (name && strlen(name) > 0)
  {
    int max_jack_size = jack_client_name_size();
    int name_len = strlen(name);

    int alloc_size = (name_len >= max_jack_size) ? max_jack_size : (name_len + 1);

    client_name = (char *)vj_calloc(alloc_size * sizeof(char));

    if (client_name)
    {
      snprintf(client_name, alloc_size, "%s", name);
    }
    else
    {
      veejay_msg(VEEJAY_MSG_ERROR, "[AUDIO] Unable to allocate %d bytes for JACK client_name", alloc_size);
    }
  }
}

void JACK_FreeClientName(void)
{
  if (client_name)
  {
    free(client_name);
    client_name = NULL;
  }
}

long JACK_OutputStatus(int deviceID, long *sec, long *nsec)
{
  jack_driver_t *drv = JACK_GetDriverRaw(deviceID);
  if (!drv)
    return 0;

  atomic_synchronize();
  *sec = drv->previousTime.tv_sec;
  *nsec = drv->previousTime.tv_nsec;

  unsigned long current_bytes = atomic_load_ulong(&drv->written_client_bytes);
  unsigned long client_frame_size = drv->bytes_per_output_frame;

  if (client_frame_size != 0)
  {
    return (long)(current_bytes / client_frame_size);
  }

  return 0;
}

void JACK_SetPortConnectionMode(enum JACK_PORT_CONNECTION_MODE mode)
{
  port_connection_mode = mode;
}
#endif
