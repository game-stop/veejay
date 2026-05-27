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

#define DEBUG_OUTPUT 0
#define RESERVE_PERIODS 1

#define OUTFILE stderr

#define DEBUG(format, args...) veejay_msg(4, "[AUDIO] " format, ##args);
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
typedef struct jack_driver_s
{
  int allocated;
  int deviceID;
  int clientCtr;
  int in_use;
  int cb_active;

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
static unsigned long JACK_GetBytesFreeSpaceFromDriver(jack_driver_t *drv);
static void JACK_ResetFromDriver(jack_driver_t *drv);
static unsigned long JACK_GetPositionFromDriver(jack_driver_t *drv, int position, int type);
static void JACK_CleanupDriver(jack_driver_t *drv);

static void
JACK_RecalculateRatios(jack_driver_t *drv)
{
  if (!drv)
    return;

  long jack_rate = drv->jack_sample_rate;
  long client_rate = drv->client_sample_rate;

  double new_output_ratio;
  double new_input_ratio;

  if (client_rate != jack_rate && client_rate > 0 && jack_rate > 0)
  {
    new_output_ratio = (double)jack_rate / (double)client_rate;
    new_input_ratio = (double)client_rate / (double)jack_rate;
  }
  else
  {
    new_output_ratio = 1.0;
    new_input_ratio = 1.0;
  }

  atomic_exchange_double(&drv->output_sample_rate_ratio, new_output_ratio);
  atomic_exchange_double(&drv->input_sample_rate_ratio, new_input_ratio);

  atomic_synchronize();
}

static size_t
JACK_CalcRingbufferBytes(jack_driver_t *drv, double SPVF, unsigned long jack_frame_bytes)
{
  if (!drv || drv->jack_sample_rate == 0 || jack_frame_bytes == 0)
    return 0;

  const int num_video_frames = 2;
  const uint32_t sr = drv->jack_sample_rate;
  const uint32_t jack_period = drv->jack_buffer_size > 0 ? drv->jack_buffer_size : 1024;
  const uint32_t frames_per_vframe = (uint32_t)(SPVF * sr + 0.5);

  uint32_t required_frames = (num_video_frames * frames_per_vframe) + jack_period;

  if (required_frames < jack_period * 4)
    required_frames = jack_period * 4;

  return (size_t)required_frames * jack_frame_bytes;
}

static void
JACK_ResizeSingleRingbuffer(jack_ringbuffer_t *volatile *target,
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
    jack_ringbuffer_free(old);

  const unsigned long frames = (unsigned long)(new_size_bytes / jack_frame_bytes);

  veejay_msg(
      VEEJAY_MSG_INFO,
      "[AUDIO]: Jack %s ringbuffer %lu frames (%.2f ms) [%ld Hz]",
      label,
      frames,
      (double)frames * 1000.0 / (double)jack_sample_rate,
      jack_sample_rate);
}

static void JACK_ResizeRingBuffers(jack_driver_t *drv, double SPVF)
{
  if (!drv || drv->jack_sample_rate == 0)
    return;

  if (drv->num_output_channels > 0 && drv->bytes_per_jack_output_frame > 0)
  {
    const size_t play_bytes =
        JACK_CalcRingbufferBytes(drv, SPVF, drv->bytes_per_jack_output_frame);

    JACK_ResizeSingleRingbuffer(
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
        JACK_CalcRingbufferBytes(drv, SPVF, drv->bytes_per_jack_input_frame);

    JACK_ResizeSingleRingbuffer(
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

static inline jack_driver_t *getDriver(int deviceID)
{
  jack_driver_t *drv = &outDev[deviceID];

  if (drv->jackd_died && drv->client == 0)
  {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    if (TimeValDifference(&drv->last_reconnect_attempt, &now) >= 250000000L)
    {
      JACK_OpenDevice(drv);
      drv->last_reconnect_attempt = now;
      veejay_msg(VEEJAY_MSG_WARNING, "[AUDIO] Last connection attempt to Jack!");
    }
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

  unsigned long starvationThresholdFrames;
  starvationThresholdFrames = (unsigned long)(requiredFrames * STARVATION_PERIOD_THRESHOLD);

  if (inputFramesAvailable < starvationThresholdFrames)
  {
    DEBUG("STARVATION CHECK: Available frames (%lu) is below threshold (%lu).",
          inputFramesAvailable, starvationThresholdFrames);
    return 1; 
  }

  return 0;
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

  veejay_msg(VEEJAY_MSG_DEBUG, "[AUDIO] Flush complete, resamplers and ringbuffers reset");
}

static void
JACK_callback_playback(jack_driver_t *drv, jack_nframes_t nframes)
{
  if (!drv || drv->num_output_channels == 0)
    return;

  const int chs = drv->num_output_channels;
  const size_t frame_bytes = drv->bytes_per_jack_output_frame;

  float *jack_out[MAX_OUTPUT_PORTS];

  for (int ch = 0; ch < chs; ch++)
  {
    jack_out[ch] = (float *)jack_port_get_buffer(drv->output_port[ch], nframes);
    if (!jack_out[ch])
      return;
  }

  jack_ringbuffer_t *play = (jack_ringbuffer_t *)drv->pPlayPtr;

  if (!play || frame_bytes == 0)
  {
    for (int ch = 0; ch < chs; ch++)
      memset(jack_out[ch], 0, nframes * sizeof(float));
    return;
  }

  jack_ringbuffer_data_t vec[2];
  jack_ringbuffer_get_read_vector(play, vec);

  long frames_done = 0;

  for (int v = 0; v < 2 && frames_done < (long)nframes; v++)
  {
    const float *src = (const float *)vec[v].buf;
    long vec_frames = vec[v].len / frame_bytes;
    long remaining = (long)nframes - frames_done;
    long to_copy = (vec_frames < remaining) ? vec_frames : remaining;

    if (to_copy <= 0)
      continue;

    if (chs == 2)
    {
      float *dst0 = jack_out[0] + frames_done;
      float *dst1 = jack_out[1] + frames_done;

      for (long f = 0; f < to_copy; f++)
      {
        dst0[f] = src[(f << 1)];
        dst1[f] = src[(f << 1) + 1];
      }
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

  if (frames_done < (long)nframes)
  {
    atomic_exchange_long(&drv->underrun_count, 1);

    long zero_frames = (long)nframes - frames_done;
    for (int ch = 0; ch < chs; ch++)
      memset(jack_out[ch] + frames_done, 0, zero_frames * sizeof(float));
  }

  jack_ringbuffer_read_advance(play, frames_done * frame_bytes);
}

static void
JACK_callback_capture(jack_driver_t *drv, jack_nframes_t nframes)
{
  if (!drv || drv->num_input_channels == 0)
    return;

  const int chs = drv->num_input_channels;
  const size_t frame_bytes = drv->bytes_per_jack_input_frame;

  if (frame_bytes == 0)
    return;

  jack_ringbuffer_t *rec = (jack_ringbuffer_t *)drv->pRecPtr;
  if (!rec)
    return;

  float *jack_in[MAX_INPUT_PORTS];

  for (int ch = 0; ch < chs; ch++)
  {
    jack_in[ch] = (float *)jack_port_get_buffer(drv->input_port[ch], nframes);
    if (!jack_in[ch])
      return;
  }

  long writable_frames = (long)(jack_ringbuffer_write_space(rec) / frame_bytes);
  long frames_to_write = ((long)nframes < writable_frames) ? (long)nframes : writable_frames;

  if (frames_to_write <= 0)
  {
    __sync_add_and_fetch(&drv->input_overrun_count, 1);
    return;
  }

  if (frames_to_write < (long)nframes)
    __sync_add_and_fetch(&drv->input_overrun_count, 1);

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
      const float *src0 = jack_in[0] + frames_done;
      const float *src1 = jack_in[1] + frames_done;

      for (long f = 0; f < to_copy; f++)
      {
        dst[(f << 1)] = src0[f];
        dst[(f << 1) + 1] = src1[f];
      }
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

  atomic_exchange_int(&drv->cb_active, TRUE);

  if (atomic_exchange_int(&drv->xrun_pending, 0))
    atomic_store_int(&drv->xrun_flag, 1);

  atomic_add_fetch_ulong(&drv->last_hw_frame_count, (unsigned long)nframes);

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

  getDriver(drv->deviceID);

  drv->client = 0;
  drv->jackd_died = TRUE;

#if JACK_CLOSE_HACK
  JACK_CloseDevice(drv, TRUE);
#else
  JACK_CloseDevice(drv);
#endif

  veejay_msg(VEEJAY_MSG_ERROR, "[AUDIO] Jack has shutdown. You will probably need to restart for Audio playback");
}

static void
JACK_Error(const char *desc)
{
  ERR("%s", desc);
}

static int
JACK_OpenDevice(jack_driver_t *drv)
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
  DEBUG("client name '%s'", our_client_name);

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
    snprintf(portname, sizeof(portname), "out_%d", i);
    veejay_msg(VEEJAY_MSG_DEBUG, "[AUDIO] Jack output port %d is named '%s'", i, portname);

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
    snprintf(portname, sizeof(portname), "in_%d", i);
    veejay_msg(VEEJAY_MSG_DEBUG, "[AUDIO] Jack input port %d is named '%s'", i, portname);

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

    sprintf(portname_suffix, "out_%d", i);

    snprintf(full_portname, sizeof(full_portname), "%s:%s", client_name_stable, portname_suffix);

    jack_port_t *active_port = jack_port_by_name(drv->client, full_portname);

    DEBUG("Port Re-acquisition: full_portname '%s' , portname_suffix '%s' client_name '%s' active_port '%p'",
          full_portname, portname_suffix, client_name_stable, active_port);

    if (active_port != NULL)
    {
      drv->output_port[i] = active_port;
    }
    else
    {

      ERR("Failed to retrieve active port pointer for %s. Original pointer remains.", full_portname);
    }
  }

  if ((drv->num_output_channels > 0) && (port_connection_mode != CONNECT_NONE))
  {
    if ((drv->jack_port_name_count == 0) || (drv->jack_port_name_count == 1))
    {
      if (drv->jack_port_name_count == 0)
      {
        ports = jack_get_ports(drv->client, NULL, NULL,
                               drv->jack_output_port_flags);
      }
      else
      {
        ports = jack_get_ports(drv->client, drv->jack_port_name[0], NULL,
                               drv->jack_output_port_flags);
      }

      unsigned int num_ports = 0;
      if (ports)
      {
        for (i = 0; ports[i]; i++)
        {
          veejay_msg(VEEJAY_MSG_DEBUG, "[AUDIO] Found jack output port '%s'", ports[i]);
          num_ports++;
        }
      }

      if (!ports || (i < drv->num_output_channels))
      {
        DEBUG("ERR: jack_get_ports() failed to find ports with jack port flags of 0x%lX'",
              drv->jack_output_port_flags);
#if JACK_CLOSE_HACK
        JACK_CloseDevice(drv, TRUE);
#else
        JACK_CloseDevice(drv);
#endif
        return ERR_PORT_NOT_FOUND;
      }

      for (i = 0; i < drv->num_output_channels; i++)
      {
        DEBUG("jack_connect() connecting client port '%s' to JACK port '%s'",
              jack_port_name(drv->output_port[i]), ports[i]);

        if (jack_connect(drv->client, jack_port_name(drv->output_port[i]), ports[i]))
        {
          ERR("cannot connect to output port %d('%s')", i, ports[i]);
          failed = 1;
        }
      }

      if (port_connection_mode == CONNECT_ALL)
      {
        if (drv->num_output_channels < num_ports)
        {
          for (i = drv->num_output_channels; ports[i]; i++)
          {
            int n = i % drv->num_output_channels;
            DEBUG("jack_connect() connecting client port '%s' to additional JACK port '%s'",
                  jack_port_name(drv->output_port[n]), ports[i]);
            if (jack_connect(drv->client, jack_port_name(drv->output_port[n]), ports[i]))
            {
              ERR("cannot connect to output port %d('%s')", n, ports[i]);
            }
          }
        }
        else if (drv->num_output_channels > num_ports)
        {
          for (i = num_ports; i < drv->num_output_channels; i++)
          {
            int n = i % num_ports;
            DEBUG("jack_connect() connecting client port '%s' to additional JACK port '%s'",
                  jack_port_name(drv->output_port[i]), ports[n]);
            if (jack_connect(drv->client, jack_port_name(drv->output_port[i]), ports[n]))
            {
              ERR("cannot connect to output port %d('%s')", i, ports[n]);
            }
          }
        }
      }

      free(ports);
    }
    else
    {
      for (i = 0; i < drv->jack_port_name_count; i++)
      {
        ports = jack_get_ports(drv->client, drv->jack_port_name[i], NULL,
                               drv->jack_output_port_flags);

        if (!ports)
        {
          ERR("jack_get_ports() failed to find ports with jack port flags of 0x%lX'",
              drv->jack_output_port_flags);
          return ERR_PORT_NOT_FOUND;
        }

        if (jack_connect(drv->client, jack_port_name(drv->output_port[i]), ports[0]))
        {
          ERR("cannot connect to output port %d('%s')", 0, ports[0]);
          failed = 1;
        }
        free(ports);
      }
    }
  }

  if ((drv->num_input_channels > 0) && (port_connection_mode != CONNECT_NONE))
  {
    if ((drv->jack_port_name_count == 0) || (drv->jack_port_name_count == 1))
    {
      if (drv->jack_port_name_count == 0)
      {
        ports = jack_get_ports(drv->client, NULL, NULL, drv->jack_input_port_flags);
      }
      else
      {
        ports = jack_get_ports(drv->client, drv->jack_port_name[0], NULL,
                               drv->jack_input_port_flags);
      }

      unsigned int num_ports = 0;
      if (ports)
      {
        for (i = 0; ports[i]; i++)
          num_ports++;
      }

      if (!ports || num_ports == 0)
      {
        veejay_msg(VEEJAY_MSG_WARNING,
                   "[AUDIO] No JACK capture source ports found; input ports remain available for manual wiring");
        if (ports)
          free(ports);
      }
      else
      {
        unsigned int first_pass = (drv->num_input_channels < num_ports)
                                      ? drv->num_input_channels
                                      : num_ports;

        for (i = 0; i < first_pass; i++)
        {
          DEBUG("jack_connect() connecting capture source '%s' to client input '%s'",
                ports[i], jack_port_name(drv->input_port[i]));

          if (jack_connect(drv->client, ports[i], jack_port_name(drv->input_port[i])))
          {
            WARN("cannot connect JACK capture source %d('%s')", i, ports[i]);
          }
        }

        if (port_connection_mode == CONNECT_ALL && drv->num_input_channels > 0)
        {
          for (i = first_pass; i < num_ports; i++)
          {
            int n = i % drv->num_input_channels;
            DEBUG("jack_connect() connecting additional capture source '%s' to client input '%s'",
                  ports[i], jack_port_name(drv->input_port[n]));

            if (jack_connect(drv->client, ports[i], jack_port_name(drv->input_port[n])))
            {
              WARN("cannot connect additional JACK capture source %d('%s')", n, ports[i]);
            }
          }
        }

        free(ports);
      }
    }
    else
    {
      for (i = 0; i < drv->jack_port_name_count; i++)
      {
        ports = jack_get_ports(drv->client, drv->jack_port_name[i], NULL,
                               drv->jack_input_port_flags);

        if (!ports)
        {
          WARN("jack_get_ports() found no capture source matching '%s'",
               drv->jack_port_name[i]);
          continue;
        }

        int n = i % drv->num_input_channels;

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

    JACK_ResetFromDriver(drv);

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
  atomic_exchange_int(&drv->state, RESET);
}

void JACK_Reset(int deviceID)
{
  jack_driver_t *drv = getDriver(deviceID);
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
  jack_driver_t *drv = getDriver(deviceID);
  if (!drv || !drv->allocated)
    return -1;

  atomic_exchange_int(&drv->state, CLOSED);

#if JACK_CLOSE_HACK
  JACK_CloseDevice(drv, TRUE);
#else
  JACK_CloseDevice(drv);
#endif

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
  jack_driver_t *drv = getDriver(deviceID);
  JACK_flush(drv);
}

static long JACK_RingbufferWriteFrames( jack_driver_t *drv, const float *src, long frames)
{
  jack_ringbuffer_data_t vec[2];
  jack_ringbuffer_get_write_vector(drv->pPlayPtr, vec);

  long frames_written = 0;
  const int chs = drv->num_output_channels;

  for (int v = 0; v < 2 && frames_written < frames; v++)
  {
    const long vec_frames = vec[v].len / drv->bytes_per_jack_output_frame;

    const long to_copy =(frames - frames_written < vec_frames) ? (frames - frames_written) : vec_frames;

    if (to_copy <= 0)
      continue;

    memcpy(
        vec[v].buf, src + frames_written * chs, to_copy * drv->bytes_per_jack_output_frame);

    frames_written += to_copy;
  }

  jack_ringbuffer_write_advance(
      drv->pPlayPtr,
      frames_written * drv->bytes_per_jack_output_frame);

  return frames_written;
}

long JACK_Write(int deviceID, unsigned char *data, unsigned long bytes)
{
  jack_driver_t *drv = getDriver(deviceID);

  if (!drv || !data || bytes == 0)
    return 0;

  if (drv->num_output_channels == 0 || !drv->pPlayPtr || !drv->swr_ctx)
    return 0;

  int bytes_per_sample = (drv->bits_per_channel == 16) ? 2 : 1;
  int bytes_per_frame = bytes_per_sample * drv->num_output_channels;

  if (bytes_per_frame <= 0)
    return 0;

  const long in_frames = bytes / bytes_per_frame;

  if (in_frames <= 0)
    return 0;

  if (atomic_exchange_int(&drv->xrun_pending, 0))
    JACK_flush(drv);

  int out_frames_needed = swr_get_out_samples(drv->swr_ctx, in_frames);

  const size_t rb_write_space = jack_ringbuffer_write_space(drv->pPlayPtr);
  const long rb_frames_capacity = rb_write_space / drv->bytes_per_jack_output_frame;

  long out_frames_cap = out_frames_needed;
  if (out_frames_cap > rb_frames_capacity)
    out_frames_cap = rb_frames_capacity;
  if (out_frames_cap > drv->resample_buf_frames)
    out_frames_cap = drv->resample_buf_frames;

  if (out_frames_cap <= 0)
    return 0;

  const uint8_t *in_ptr = data;
  float *out_ptr = drv->resample_buf;

  const int out_frames = swr_convert(
      drv->swr_ctx,
      (uint8_t **)&out_ptr,
      out_frames_cap,
      (const uint8_t **)&in_ptr,
      in_frames);

  if (out_frames <= 0)
    return 0;

  return JACK_RingbufferWriteFrames(drv, out_ptr, out_frames);
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
        to_copy * frame_bytes);

    frames_read += to_copy;
  }

  jack_ringbuffer_read_advance(drv->pRecPtr, frames_read * frame_bytes);

  return frames_read;
}

long JACK_Read(int deviceID, unsigned char *data, unsigned long bytes)
{
  jack_driver_t *drv = getDriver(deviceID);

  if (!drv || !data || bytes == 0)
    return 0;

  if (drv->num_input_channels == 0 || !drv->pRecPtr || !drv->swr_in_ctx || !drv->input_resample_buf)
    return 0;

  if (drv->bytes_per_jack_input_frame == 0)
    return 0;

  const int bytes_per_sample = (drv->bits_per_channel == 16) ? 2 : 1;
  const int bytes_per_frame = bytes_per_sample * drv->num_input_channels;

  if (bytes_per_frame <= 0)
    return 0;

  const long target_client_frames = bytes / bytes_per_frame;

  if (target_client_frames <= 0)
    return 0;

  long client_frames_done = 0;

  while (client_frames_done < target_client_frames)
  {
    const size_t available_bytes = jack_ringbuffer_read_space(drv->pRecPtr);
    long available_jack_frames = available_bytes / drv->bytes_per_jack_input_frame;

    if (available_jack_frames <= 0)
      break;

    long remaining_client_frames = target_client_frames - client_frames_done;

    long desired_jack_frames =
        (long)ceil(
            (double)remaining_client_frames *
            (double)drv->jack_sample_rate /
            (double)drv->client_sample_rate) +
        8;

    if (desired_jack_frames < 1)
      desired_jack_frames = 1;

    long chunk_jack_frames = available_jack_frames;

    if (chunk_jack_frames > desired_jack_frames)
      chunk_jack_frames = desired_jack_frames;

    if (chunk_jack_frames > drv->input_resample_buf_frames)
      chunk_jack_frames = drv->input_resample_buf_frames;

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

  if (volume > 100)
    volume = 100;

  atomic_exchange_uint(&drv->volume[channel], volume);

  return ERR_SUCCESS;
}

int JACK_SetVolumeForChannel(int deviceID, unsigned int channel,
                             unsigned int volume)
{
  jack_driver_t *drv = getDriver(deviceID);
  int retval = JACK_SetVolumeForChannelFromDriver(drv, channel, volume);
  return retval;
}

int JACK_SetAllVolume(int deviceID, unsigned int volume)
{
  jack_driver_t *drv = getDriver(deviceID);
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
  jack_driver_t *drv = getDriver(deviceID);

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
  jack_driver_t *drv = getDriver(deviceID);
  if (!drv)
    return LINEAR;

  DEBUG("setting type of '%s'",
        (type == DBATTENUATION ? "dbAttenuation" : "linear"));

  int retval = atomic_exchange_int(&drv->volumeEffectType, type);

  return retval;
}

int JACK_SetState(int deviceID, int state)
{
  jack_driver_t *drv = getDriver(deviceID);
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
  jack_driver_t *drv = getDriver(deviceID);
  if (!drv)
    return -1;
  return atomic_exchange_int(&drv->xrun_flag,0);
}

int JACK_GetCallbackActive(int deviceID)
{
  jack_driver_t *drv = getDriver(deviceID);

  if (!drv || !drv->allocated)
  {
    return CLOSED;
  }

  return atomic_load_int(&drv->cb_active);
}

int JACK_GetState(int deviceID)
{
  jack_driver_t *drv = getDriver(deviceID);

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
  jack_driver_t *drv = getDriver(deviceID);
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
  jack_driver_t *drv = getDriver(deviceID);
  long return_val;
  ;
  return_val = JACK_GetInputBytesPerSecondFromDriver(drv);
  ;

  return return_val;
}

unsigned long
JACK_GetBytesPerInputFrame(int deviceID)
{
  jack_driver_t *drv = getDriver(deviceID);

  if (!drv)
    return 0;

  return (unsigned long)drv->bytes_per_input_frame;
}

unsigned long
JACK_GetInputBytesStored(int deviceID)
{
  jack_driver_t *drv = getDriver(deviceID);

  if (!drv || !drv->pRecPtr || drv->bytes_per_jack_input_frame == 0)
    return 0;

  size_t bytes_in_rb = jack_ringbuffer_read_space(drv->pRecPtr);
  unsigned long jack_frames =
      (unsigned long)(bytes_in_rb / drv->bytes_per_jack_input_frame);

  if (drv->jack_sample_rate <= 0 || drv->client_sample_rate <= 0)
    return jack_frames * drv->bytes_per_input_frame;

  unsigned long client_frames =
      (unsigned long)(
          ((double)jack_frames *
           (double)drv->client_sample_rate /
           (double)drv->jack_sample_rate) +
          0.5);

  return client_frames * drv->bytes_per_input_frame;
}

long
JACK_GetInputOverruns(int deviceID)
{
  jack_driver_t *drv = getDriver(deviceID);

  if (!drv)
    return 0;

  return atomic_load_long(&drv->input_overrun_count);
}

static long
JACK_GetBytesStoredFromDriver(jack_driver_t *drv)
{
  if (drv->pPlayPtr == 0)
    return 0;

  unsigned long jack_frame_size = drv->bytes_per_jack_output_frame;
  unsigned long client_frame_size = drv->bytes_per_output_frame;

  if (jack_frame_size == 0)
    return 0;

  size_t bytes_in_rb = jack_ringbuffer_read_space(drv->pPlayPtr);

  size_t reserve_bytes = (size_t)drv->jack_buffer_size;

  long effective_jack_bytes = (long)bytes_in_rb - (long)reserve_bytes;

  if (effective_jack_bytes <= 0)
  {
    return 0;
  }

  long return_val = (effective_jack_bytes / jack_frame_size) * client_frame_size;

  return return_val;
}

unsigned long
JACK_GetBytesStored(int deviceID)
{
  jack_driver_t *drv = getDriver(deviceID);
  return (unsigned long)JACK_GetBytesStoredFromDriver(drv);
}

static unsigned long
JACK_GetBytesFreeSpaceFromDriver(jack_driver_t *drv)
{
  if (drv->pPlayPtr == 0)
    return 0;

  unsigned long jack_frame_size = drv->bytes_per_jack_output_frame;
  unsigned long client_frame_size = drv->bytes_per_output_frame;

  if (jack_frame_size == 0)
    return 0;

  long safety_margin_bytes = (long)drv->jack_buffer_size * jack_frame_size;

  long available_write_space_bytes = (long)jack_ringbuffer_write_space(drv->pPlayPtr);

  long return_val = available_write_space_bytes - safety_margin_bytes;

  if (return_val <= 0)
  {
    return_val = 0;
  }
  else
  {
    return_val = (return_val / jack_frame_size) * client_frame_size;
  }

  return (return_val > 0) ? (unsigned long)return_val : 0;
}

unsigned long
JACK_GetBytesFreeSpace(int deviceID)
{
  jack_driver_t *drv = getDriver(deviceID);
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

    jack_nframes_t current_jack_frame = jack_frame_time(drv->client);

    jack_nframes_t last_cb_frame = atomic_load_uint(&drv->last_callback_frame);

    if (last_cb_frame != 0 && current_jack_frame > last_cb_frame)
    {
      jack_nframes_t frames_since_cb = current_jack_frame - last_cb_frame;

      double seconds_since = (double)frames_since_cb / (double)drv->jack_sample_rate;
      long bytes_since = (long)(seconds_since * (double)output_bytes_per_sec);

      return_val += bytes_since;

      DEBUG("Sync: CB_Base=%ld, Hardware_Delta_Bytes=%ld",
            drv->played_client_bytes, bytes_since);
    }
  }

  return_val += atomic_load_long(&drv->position_byte_offset);

  if (position == MILLISECONDS)
  {
    if (output_bytes_per_sec != 0)
    {
      return_val = (long)(((double)return_val / (double)output_bytes_per_sec) * 1000.0);
    }
    else
    {
      return_val = 0;
    }
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
  double hardware_latency_s = (double)range.max / (double)drv->jack_sample_rate;

  size_t bytes_buffered = jack_ringbuffer_read_space(drv->pPlayPtr);

  double ringbuffer_samples = (double)bytes_buffered / (double)drv->bytes_per_jack_output_frame;
  double ringbuffer_latency_s = ringbuffer_samples / (double)drv->jack_sample_rate;

  jack_nframes_t frames_since_cycle = jack_frames_since_cycle_start(drv->client);
  double cycle_offset_s = (double)frames_since_cycle / (double)drv->jack_sample_rate;

  return hardware_latency_s + ringbuffer_latency_s + cycle_offset_s;
}

static unsigned long JACK_get_played_frames_from_driver(jack_driver_t *drv)
{
  if (!drv)
    return 0;
  return atomic_load_ulong(&drv->last_hw_frame_count);
}

long JACK_GetPosition(int deviceID, int position, int type)
{
  jack_driver_t *drv = getDriver(deviceID);
  long return_val;

  return_val = JACK_GetPositionFromDriver(drv, position, type);

  return return_val;
}

unsigned long
JACK_GetPlayedFramesFromDriver(int deviceID)
{
  jack_driver_t *drv = getDriver(deviceID);
  return JACK_get_played_frames_from_driver(drv);
}

long JACK_GetUnderruns(int deviceID)
{
  jack_driver_t *drv = getDriver(deviceID);
  return JACK_get_underruns(drv);
}

double
JACK_GetTotalLatency(int deviceID)
{
  jack_driver_t *drv = getDriver(deviceID);
  return JACK_get_total_latency(drv);
}

static long JACK_get_required_free_frames(jack_driver_t *drv, int client_frames)
{
  if (!drv || drv->client_sample_rate <= 0)
    return 0;

  const double ratio =
      (double)drv->jack_sample_rate /
      (double)drv->client_sample_rate;

  return (long)(client_frames * ratio + 0.5);
}

int JACK_get_ringbuffer_free_frames(jack_driver_t *drv)
{
  if (!drv || drv->pPlayPtr == NULL)
    return 0;

  unsigned long jack_frame_size = drv->bytes_per_jack_output_frame;
  if (jack_frame_size == 0)
    return 0;

  size_t write_bytes_available = jack_ringbuffer_write_space(drv->pPlayPtr);

  unsigned long frames_available = write_bytes_available / jack_frame_size;

  return (int)frames_available;
}

long JACK_get_ringbuffer_used(jack_driver_t *drv)
{
  if (!drv || drv->pPlayPtr == NULL)
    return 0;

  unsigned long jack_frame_size = drv->bytes_per_jack_output_frame;
  unsigned long client_frame_size = drv->bytes_per_output_frame;

  if (jack_frame_size == 0)
    return 0;

  size_t filled_jack_bytes = jack_ringbuffer_read_space(drv->pPlayPtr);
  return (long)((filled_jack_bytes / jack_frame_size) * client_frame_size);
}

int JACK_get_ringbuffer_size(jack_driver_t *drv)
{
  if (!drv || drv->pPlayPtr == NULL || drv->bytes_per_jack_output_frame == 0)
    return 0;

  int size = drv->pPlayPtr->size / drv->bytes_per_jack_output_frame;
  return size;
}

int JACK_get_client_to_jack_frames(jack_driver_t *drv, int client_frames)
{
  if (!drv)
    return 0;

  long client_rate = drv->client_sample_rate;
  long jack_rate = drv->jack_sample_rate;

  if (client_rate <= 0 || jack_rate <= 0)
    return client_frames;

  if (client_rate == jack_rate)
    return client_frames;

  return (int)((double)client_frames * (double)jack_rate / (double)client_rate + 0.5);
}

int JACK_BufferIsStarving(int deviceID)
{
  jack_driver_t *drv = getDriver(deviceID);
  int return_val;

  return_val = JACK_isStarving(drv);

  return return_val;
}

unsigned long
JACK_GetBytesPerOutputFrame(int deviceID)
{
  jack_driver_t *drv = getDriver(deviceID);

  if (!drv)
    return 0;

  unsigned long return_val = (unsigned long)drv->bytes_per_output_frame;

  return return_val;
}

void JACK_ResetBuffer(int deviceID)
{
  jack_driver_t *drv = getDriver(deviceID);

  if (drv && drv->pPlayPtr)
    jack_ringbuffer_reset(drv->pPlayPtr);
}

void JACK_ResetInputBuffer(int deviceID)
{
  jack_driver_t *drv = getDriver(deviceID);

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
  jack_driver_t *drv = getDriver(deviceID);
  if (!drv)
    return 0;

  return drv->client_sample_rate;
}

int JACK_GetRingBufferFreeFrames(int deviceID)
{
  jack_driver_t *drv = getDriver(deviceID);
  int return_val;
  return_val = JACK_get_ringbuffer_free_frames(drv);
  return return_val;
}

long JACK_GetSampleRateJack(int deviceID)
{
  jack_driver_t *drv = getDriver(deviceID);
  if (!drv)
    return 0;

  return drv->jack_sample_rate;
}

long JACK_GetPeriodSize(int deviceID)
{
  jack_driver_t *drv = getDriver(deviceID);
  if (!drv)
    return 0;

  return drv->jack_buffer_size;
}

int JACK_GetClientToJackFrames(int deviceID, int client_frames)
{
  jack_driver_t *drv = getDriver(deviceID);
  int return_val;
  return_val = JACK_get_client_to_jack_frames(drv, client_frames);
  return return_val;
}

int JACK_GetRingBufferSize(int deviceID)
{
  jack_driver_t *drv = getDriver(deviceID);
  int return_val = 0;
  return_val = JACK_get_ringbuffer_size(drv);
  return return_val;
}

long JACK_GetRequiredFreeFrames(int deviceID, int client_frames)
{
  jack_driver_t *drv = getDriver(deviceID);
  long return_val = 0;
  return_val = JACK_get_required_free_frames(drv, client_frames);
  return return_val;
}

long JACK_GetRingBufferUsed(int deviceID)
{
  jack_driver_t *drv = getDriver(deviceID);
  int return_val = 0;
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

  drv->jack_sample_rate = 0;
  drv->jack_buffer_size = 0;
  drv->jackd_died = FALSE;

  drv->output_sample_rate_ratio = 1.0;
  drv->input_sample_rate_ratio = 1.0;

  atomic_exchange_long(&drv->input_overrun_count, 0);
  atomic_exchange_ulong(&drv->captured_client_bytes, 0);

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
    DEBUG("not initing twice");
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

    JACK_ResetFromDriver(drv);
  }

  client_name = NULL;
  JACK_SetClientName((char*)"bio2jack");

  DEBUG("JACK System Initialized (Lock-Free Mode)");
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
      veejay_msg(VEEJAY_MSG_DEBUG, "[AUDIO] Jack client name set to: %s", client_name);
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
  jack_driver_t *drv = getDriver(deviceID);
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
