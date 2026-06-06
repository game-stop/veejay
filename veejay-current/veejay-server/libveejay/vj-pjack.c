/* veejay - Linux VeeJay
 *           (C) 2002-2004 Niels Elburg <nwelburg@gmail.com> 
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <config.h>
#ifdef HAVE_JACK
#include <stdint.h>
#include <sys/time.h>
#include <jack/jack.h>
#include <veejaycore/defs.h>
#include <bio2jack/bio2jack.h>
#include <libel/vj-el.h>
#include "vj-jack.h"

static int driver = -1;
static int driver_open = 0;
static int driver_input_channels = 0;
static int driver_output_channels = 0;

extern void veejay_msg(int type, const char format[], ...);

static int _vj_jack_bits_per_channel(int bytes_per_frame, int audio_channels)
{
    if(bytes_per_frame <= 0 || audio_channels <= 0)
        return 0;
    return (bytes_per_frame * 8) / audio_channels;
}

static void _vj_jack_clear_open_state(void)
{
    driver = -1;
    driver_open = 0;
    driver_input_channels = 0;
    driver_output_channels = 0;
}

static void _vj_jack_set_open_state(int input_channels, int output_channels)
{
    driver_open = 1;
    driver_input_channels = input_channels;
    driver_output_channels = output_channels;
}

static void _vj_jack_report_open_error(int err, long audio_rate, int input_channels, int output_channels)
{
    switch(err)
    {
        case ERR_OPENING_JACK:
            veejay_msg(0, "[AUDIO] Unable to connect with jack");
            break;
        case ERR_RATE_MISMATCH:
            veejay_msg(0, "[AUDIO] Samplerate mismatch %ld Hz", audio_rate);
            break;
        case ERR_TOO_MANY_OUTPUT_CHANNELS:
            veejay_msg(0, "[AUDIO] Too many output channels: %d", output_channels);
            break;
        case ERR_TOO_MANY_INPUT_CHANNELS:
            veejay_msg(0, "[AUDIO] Too many input channels: %d", input_channels);
            break;
        case ERR_TOO_MANY_CHANNELS:
            veejay_msg(0, "[AUDIO] Too many channels: input=%d output=%d", input_channels, output_channels);
            break;
        case ERR_PORT_NOT_FOUND:
            veejay_msg(0, "[AUDIO] Unable to find jack port");
            break;
        default:
            veejay_msg(0, "[AUDIO] Jack error %d", err);
            break;
    }
}

int vj_jack_initialize(void)
{
    JACK_Init();
    JACK_SetClientName("veejay");
    _vj_jack_clear_open_state();
    return 0;
}
static int _vj_jack_start_ex(int *dri,
                             int bytes_per_frame,
                             long audio_rate,
                             int audio_channels,
                             int input_channels,
                             int output_channels,
                             unsigned long jack_port_flags,
                             double SPVF,
                             enum JACK_PORT_CONNECTION_MODE connect_mode)
{
    int bpc = _vj_jack_bits_per_channel(bytes_per_frame, audio_channels);
    unsigned long rate = (unsigned long)audio_rate;
    int err;

    if(bpc <= 0)
        return 0;

    JACK_SetPortConnectionMode(connect_mode);

    err = JACK_OpenEx(dri, bpc, &rate,
                      (unsigned int)input_channels,
                      (unsigned int)output_channels,
                      NULL,
                      0,
                      jack_port_flags,
                      SPVF);

    if(err == ERR_RATE_MISMATCH)
    {
        JACK_Close(*dri);
        rate = (unsigned long)audio_rate;

        JACK_SetPortConnectionMode(connect_mode);

        err = JACK_OpenEx(dri, bpc, &rate,
                          (unsigned int)input_channels,
                          (unsigned int)output_channels,
                          NULL,
                          0,
                          jack_port_flags,
                          SPVF);
    }

    if(err != ERR_SUCCESS)
    {
        _vj_jack_report_open_error(err, audio_rate, input_channels, output_channels);
        JACK_Close(*dri);
        _vj_jack_clear_open_state();
        return 0;
    }

    _vj_jack_set_open_state(input_channels, output_channels);
    return 1;
}

int vj_jack_stop(void)
{
    if(!driver_open || driver < 0)
    {
        _vj_jack_clear_open_state();
        return 1;
    }

    JACK_SetInputPassthrough(driver, 0);

    if(JACK_GetState(driver) == PLAYING)
        JACK_SetState(driver, STOPPED);

    JACK_Reset(driver);

    if(JACK_Close(driver))
        veejay_msg(2, "[AUDIO] Error closing jack device");

    _vj_jack_clear_open_state();
    return 1;
}

static int _vj_jack_start(int *dri, int bytes_per_frame, long audio_rate, int audio_channels, double SPVF)
{
    int input_channels = 2;

    if(audio_channels < 1)
        return 0;

    return _vj_jack_start_ex(
        dri,
        bytes_per_frame,
        audio_rate,
        audio_channels,
        input_channels,
        audio_channels,
        0,
        SPVF,
        CONNECT_OUTPUT
    );
}

int vj_jack_init(editlist *el)
{
    if(!el)
        return 0;

    if(driver_open)
        vj_jack_stop();

    return _vj_jack_start(
        &driver,
        el->audio_bps,
        el->audio_rate,
        el->audio_chans,
        1.0 / (double)el->video_fps
    );
}

int vj_jack_init_input(editlist *el)
{
    if(!el)
        return 0;

    if(driver_open)
        vj_jack_stop();

    return _vj_jack_start_ex(
        &driver,
        el->audio_bps,
        el->audio_rate,
        el->audio_chans,
        2,
        0,
        0,
        1.0 / (double)el->video_fps,
        CONNECT_NONE
    );
}

int vj_jack_init_duplex(editlist *el)
{
    if(!el)
        return 0;

    if(driver_open)
        vj_jack_stop();

    return _vj_jack_start_ex(
        &driver,
        el->audio_bps,
        el->audio_rate,
        el->audio_chans,
        2,
        el->audio_chans,
        0,
        1.0 / (double)el->video_fps,
        CONNECT_OUTPUT
    );
}

int vj_jack_init_capture(int input_channels, unsigned int bits_per_channel, unsigned long jack_port_flags)
{
    int bytes_per_frame;

    if(input_channels < 1)
        input_channels = 2;

    if(bits_per_channel != 8 && bits_per_channel != 16)
        bits_per_channel = 16;

    if(driver_open)
        return driver_input_channels > 0;

    bytes_per_frame = input_channels * ((int)bits_per_channel / 8);

    return _vj_jack_start_ex(
        &driver,
        bytes_per_frame,
        0,
        input_channels,
        input_channels,
        0,
        jack_port_flags,
        1.0 / 25.0,
        CONNECT_NONE
    );
}

int vj_jack_update_buffer(uint8_t *buff, int bps, int num_channels, int buf_len)
{
    (void)bps;
    (void)num_channels;
    return vj_jack_play(buff, buf_len);
}

int vj_jack_get_client_samplerate(void)
{
    if(!driver_open)
        return 0;
    return JACK_GetSampleRate(driver);
}

int vj_jack_start(void)
{
    if(!driver_open)
        return 0;
    JACK_SetState(driver, PLAYING);
    return 1;
}

long vj_jack_underruns(void)
{
    if(!driver_open)
        return 0;
    return JACK_GetUnderruns(driver);
}

long vj_jack_input_overruns(void)
{
    if(!driver_open || driver_input_channels <= 0)
        return 0;
    return JACK_GetInputOverruns(driver);
}

int vj_jack_xrun_flag(void)
{
    if(!driver_open)
        return 0;
    return JACK_XRUNHandled(driver);
}

void vj_jack_enable(void)
{
    if(driver_open)
        JACK_SetState(driver, PLAYING);
}

void vj_jack_disable(void)
{
    if(driver_open)
        JACK_SetState(driver, STOPPED);
}

int vj_jack_c_play(void *data, int len, int entry)
{
    (void)data;
    (void)len;
    (void)entry;
    return 0;
}

int vj_jack_play(void *data, int len)
{
    int frames;
    if(!driver_open || driver_output_channels <= 0 || !data || len <= 0)
        return 0;
    frames = JACK_Write(driver, data, (unsigned long)len);
    if(frames == 0 && JACK_GetState(driver) == PLAYING)
    {
        vj_jack_disable();
        veejay_msg(0, "[AUDIO] Error writing to JACK!");
    }
    return frames;
}

int vj_jack_play2(void *data, int len)
{
    int res;
    if(!driver_open || driver_output_channels <= 0 || !data || len <= 0)
        return 0;
    res = JACK_Write(driver, data, (unsigned long)len);
    if(res == 0 && JACK_GetState(driver) == PLAYING)
        vj_jack_disable();
    return res;
}

int vj_jack_read(void *data, int len)
{
    if(!driver_open || driver_input_channels <= 0 || !data || len <= 0)
        return 0;
    return (int)JACK_Read(driver, data, (unsigned long)len);
}

int vj_jack_capture_read(void *data, int len)
{
    return vj_jack_read(data, len);
}

int vj_jack_get_rate(void)
{
    if(!driver_open)
        return 0;
    return JACK_GetSampleRateJack(driver);
}

int vj_jack_set_volume(int volume)
{
    if(!driver_open || driver_output_channels <= 0)
        return 0;
    return JACK_SetAllVolume(driver, (unsigned int)volume) == 0 ? 1 : 0;
}

void vj_jack_flush(void)
{
    if(driver_open)
        JACK_Flush(driver);
}

int vj_jack_pause(void)
{
    if(!driver_open)
        return 0;
    JACK_SetState(driver, PAUSED);
    return 1;
}

int vj_jack_resume(void)
{
    if(!driver_open)
        return 0;
    JACK_SetState(driver, PLAYING);
    return 1;
}

int vj_jack_get_space(void)
{
    if(!driver_open || driver_output_channels <= 0)
        return 0;
    return JACK_GetBytesFreeSpace(driver);
}

long vj_jack_get_status(long int *sec, long int *nsec)
{
    if(!driver_open)
    {
        if(sec)
            *sec = 0;
        if(nsec)
            *nsec = 0;
        return 0;
    }
    return JACK_OutputStatus(driver, sec, nsec);
}

double vj_jack_get_played_position(void)
{
    if(!driver_open || driver_output_channels <= 0)
        return 0.0;
    return (double)JACK_GetPosition(driver, MILLISECONDS, PLAYED) / 1000.0;
}

unsigned long vj_jack_get_played_frames(void)
{
    if(!driver_open || driver_output_channels <= 0)
        return 0;
    return JACK_GetPlayedFramesFromDriver(driver);
}

long vj_jack_get_written_frames(void)
{
    if(!driver_open || driver_output_channels <= 0)
        return 0;
    return JACK_GetPosition(driver, BYTES, FRAMES_WRITTEN_TO_JACK);
}

void vj_jack_reset(void)
{
    if(driver_open && driver_output_channels > 0)
        JACK_ResetBuffer(driver);
}

void vj_jack_reset_input(void)
{
    if(driver_open && driver_input_channels > 0)
        JACK_ResetInputBuffer(driver);
}

int vj_jack_is_starving(void)
{
    if(!driver_open || driver_output_channels <= 0)
        return 0;
    return JACK_BufferIsStarving(driver);
}

int vj_jack_get_period_size(void)
{
    if(!driver_open)
        return 0;
    return JACK_GetPeriodSize(driver);
}

int vj_jack_get_ringbuffer_frames_free(void)
{
    if(!driver_open || driver_output_channels <= 0)
        return 0;
    return JACK_GetRingBufferFreeFrames(driver);
}

int vj_jack_get_ringbuffer_size(void)
{
    if(!driver_open || driver_output_channels <= 0)
        return 0;
    return JACK_GetRingBufferSize(driver);
}

long vj_jack_get_ringbuffer_used(void)
{
    if(!driver_open || driver_output_channels <= 0)
        return 0;
    return JACK_GetRingBufferUsed(driver);
}

int vj_jack_client_to_jack_frames(int client_frames)
{
    if(!driver_open)
        return client_frames;
    return JACK_GetClientToJackFrames(driver, client_frames);
}

long vj_jack_get_required_free_frames(int client_frames)
{
    if(!driver_open || driver_output_channels <= 0)
        return 0;
    return JACK_GetRequiredFreeFrames(driver, client_frames);
}

unsigned long vj_jack_get_bytes_per_frame(void)
{
    if(!driver_open || driver_output_channels <= 0)
        return 0;
    return JACK_GetBytesPerOutputFrame(driver);
}

unsigned long vj_jack_get_bytes_per_input_frame(void)
{
    if(!driver_open || driver_input_channels <= 0)
        return 0;
    return JACK_GetBytesPerInputFrame(driver);
}

unsigned long vj_jack_get_input_bytes_per_second(void)
{
    if(!driver_open || driver_input_channels <= 0)
        return 0;
    return JACK_GetInputBytesPerSecond(driver);
}

long vj_jack_get_bytes_stored_from_driver(void)
{
    if(!driver_open || driver_output_channels <= 0)
        return 0;
    return JACK_GetBytesStored(driver);
}

long vj_jack_get_input_bytes_stored(void)
{
    if(!driver_open || driver_input_channels <= 0)
        return 0;
    return JACK_GetInputBytesStored(driver);
}

int vj_jack_is_stopped(void)
{
    if(!driver_open)
        return 1;
    return JACK_GetState(driver) == STOPPED;
}

int vj_jack_is_playing(void)
{
    if(!driver_open)
        return 0;
    return JACK_GetState(driver) == PLAYING;
}

int vj_jack_is_callback_active(void)
{
    if(!driver_open)
        return CLOSED;
    return JACK_GetCallbackActive(driver);
}

double vj_jack_get_total_latency(void)
{
    if(!driver_open || driver_output_channels <= 0)
        return 0.0;
    return JACK_GetTotalLatency(driver);
}

void vj_jack_set_input_passthrough(int enabled)
{
    if(driver_open && driver >= 0) {
        int on = enabled ? 1 : 0;
        JACK_SetInputPassthrough(driver, on);
    }
}

int vj_jack_get_input_passthrough(void)
{
    if(!driver_open || driver < 0)
        return 0;
    return JACK_GetInputPassthrough(driver);
}

int vj_jack_is_running(void)
{
    return driver_open;
}

int vj_jack_has_input(void)
{
    return driver_open && driver_input_channels > 0;
}

int vj_jack_has_output(void)
{
    return driver_open && driver_output_channels > 0;
}

int vj_jack_get_input_channels(void)
{
    return driver_input_channels;
}

int vj_jack_get_output_channels(void)
{
    return driver_output_channels;
}

#endif
