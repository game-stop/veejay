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
#ifndef _H_JACK_OUT_H
#define _H_JACK_OUT_H
#ifdef HAVE_JACK

#include <jack/jack.h>

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#define ERR_SUCCESS 0
#define ERR_OPENING_JACK 1
#define ERR_RATE_MISMATCH 2
#define ERR_BYTES_PER_OUTPUT_FRAME_INVALID 3
#define ERR_BYTES_PER_INPUT_FRAME_INVALID 4
#define ERR_TOO_MANY_OUTPUT_CHANNELS 5
#define ERR_PORT_NAME_OUTPUT_CHANNEL_MISMATCH 6
#define ERR_PORT_NOT_FOUND 7
#define ERR_TOO_MANY_INPUT_CHANNELS 8
#define ERR_PORT_NAME_INPUT_CHANNEL_MISMATCH 9
#define ERR_TOO_MANY_CHANNELS 10

#define BYTES 0
#define MILLISECONDS 1

#define PLAYING 0
#define PAUSED 1
#define STOPPED 2
#define CLOSED 3
#define RESET 4

#define PLAYED 1
#define WRITTEN_TO_JACK 2
#define WRITTEN 3
#define FRAMES_WRITTEN_TO_JACK 4

#define LINEAR 0
#define DBATTENUATION 1

void JACK_Init(void);
int JACK_Open(int *deviceID, unsigned int bits_per_sample, unsigned long *rate, int channels, double SPVF);
int JACK_OpenEx(int *deviceID, unsigned int bits_per_channel, unsigned long *rate, unsigned int input_channels, unsigned int output_channels, const char **jack_port_name, unsigned int jack_port_name_count, unsigned long jack_port_flags, double SPVF);
int JACK_Close(int deviceID);
void JACK_Reset(int deviceID);
long JACK_Write(int deviceID, unsigned char *data, unsigned long bytes);
long JACK_Read(int deviceID, unsigned char *data, unsigned long bytes);
long JACK_GetPosition(int deviceID, int position, int type);
long JACK_GetUnderruns(int deviceID);
long JACK_GetInputOverruns(int deviceID);
int JACK_SetState(int deviceID, int state);
int JACK_GetState(int deviceID);
int JACK_SetVolumeEffectType(int deviceID, int type);
int JACK_SetAllVolume(int deviceID, unsigned int volume);
int JACK_SetVolumeForChannel(int deviceID, unsigned int channel, unsigned int volume);
void JACK_GetVolumeForChannel(int deviceID, unsigned int channel, unsigned int *volume);
void JACK_FreeClientName(void);
unsigned long JACK_GetOutputBytesPerSecond(int deviceID);
unsigned long JACK_GetInputBytesPerSecond(int deviceID);
unsigned long JACK_GetBytesStored(int deviceID);
unsigned long JACK_GetInputBytesStored(int deviceID);
unsigned long JACK_GetBytesFreeSpace(int deviceID);
unsigned long JACK_GetBytesPerOutputFrame(int deviceID);
unsigned long JACK_GetBytesPerInputFrame(int deviceID);
unsigned long JACK_GetPlayedFramesFromDriver(int deviceID);
void JACK_ResetBuffer(int deviceID);
void JACK_ResetInputBuffer(int deviceID);
long JACK_GetSampleRateJack(int deviceID);
long JACK_GetSampleRate(int deviceID);
long JACK_GetPeriodSize(int deviceID);
int JACK_GetRingBufferFreeFrames(int deviceID);
int JACK_GetClientToJackFrames(int deviceID, int client_frames);
int JACK_GetRingBufferSize(int deviceID);
long JACK_GetRingBufferUsed(int deviceID);
void JACK_SetClientName(char *name);

enum JACK_PORT_CONNECTION_MODE
{
    CONNECT_ALL,
    CONNECT_OUTPUT,
    CONNECT_NONE
};

void JACK_SetPortConnectionMode(enum JACK_PORT_CONNECTION_MODE mode);
long JACK_OutputStatus(int deviceID, long *sec, long *usec);
int JACK_BufferIsStarving(int deviceID);
long JACK_GetRequiredFreeFrames(int deviceID, int client_frames);
void JACK_Flush(int deviceID);
double JACK_GetTotalLatency(int deviceID);
int JACK_GetCallbackActive(int deviceID);
int JACK_XRUNHandled(int deviceID);

#ifdef __cplusplus
}
#endif

#endif
#endif
