/* veejay - Linux VeeJay
 *           (C) 2002-2004 Niels Elburg <elburg@hio.hen.nl> 
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
#include <bio2jack/bio2jack.h>
#include <libel/vj-el.h>
static int driver = 0;
static int bits_per_sample = 0;
static unsigned long audio_rate = 0;
static int audio_channels = 0;
extern void veejay_msg(int type, const char format[], ...);

int vj_jack_initialize()
{
	JACK_Init();
	JACK_SetClientName("veejay");
	return 0;
}
static int _vj_jack_start(int *dri)
{
	int err = JACK_Open(dri, bits_per_sample,&audio_rate,audio_channels);
	if(err == ERR_RATE_MISMATCH)
	{
		veejay_msg(1, "(Jackd) Sample rate mismatch (Retrying)");

		JACK_Close( *dri );

		err = JACK_Open(dri, bits_per_sample,&audio_rate, audio_channels);
	}

	if(err != ERR_SUCCESS)
	{
		switch(err)
		{
			case ERR_OPENING_JACK:
			  veejay_msg(0, "Unable to make a connection with Jack" );
			  break;
			case ERR_RATE_MISMATCH:
			  veejay_msg(0, "Jackd cannot handle samplerate of %d Hz", audio_rate);
			  break;
			case ERR_TOO_MANY_OUTPUT_CHANNELS:	
			  veejay_msg(0, "Cannot connect to jackd, Too many output channels: %d",
				audio_channels );
			  break;
			case ERR_PORT_NOT_FOUND:
			   veejay_msg(0, "Unable to find jack port");
			   break;
		}
		
		JACK_Close( *dri );
//		veejay_msg(0, "To run veejay without audio, use -a0");
		return 0;
	}

	return 1;
}

int vj_jack_init(editlist *el)
{
	int ret = 0;

//	JACK_Init();
//	JACK_SetClientName("veejay");

	bits_per_sample = 16;
	audio_channels = el->audio_chans;

	if( !_vj_jack_start(&driver) )
		return ret;

	long jack_rate = JACK_GetSampleRate(driver );

	ret = 1;

	if( jack_rate != el->audio_rate ) {
		veejay_msg(1,"Jack is running with a different sample rate (%ld)! Really should use a rate of %d", jack_rate,el->audio_rate );
		ret = 2;
	}

	JACK_SetState(driver, PAUSED );

	return ret;
}

int	vj_jack_rate()
{
	return JACK_GetSampleRate(driver);
}


int	vj_jack_continue(int speed)
{
	if(speed==0)
	{
		if(JACK_GetState(driver) == PAUSED) 
			return 1;

		JACK_SetState(driver, PAUSED );
		return 1;
	}

	if( JACK_GetState(driver) == PAUSED )
	{
		JACK_SetState(driver, PLAYING);
		return 1;
	}
	return 0;
}


int	vj_jack_stop()
{
	JACK_Reset( driver );

	if(JACK_Close(driver))
	{
		veejay_msg(2,
			"(Jack) Error closing device");
	}
	return 1;
}

void	vj_jack_enable()
{
	if( JACK_GetState(driver) == STOPPED )
		JACK_SetState(driver, PLAYING );
}

void	vj_jack_disable()
{
	JACK_SetState( driver, STOPPED );
}

int	vj_jack_c_play(void *data, int len, int entry)
{
	return 0;
}

int	vj_jack_play(void *data, int len)
{
	int res = JACK_Write( driver, data, len );
	if( res == 0 && JACK_GetState(driver) == PLAYING ) {
		vj_jack_disable();
	}
	return res;
}

int	vj_jack_set_volume(int volume)
{
	if(JACK_SetAllVolume( driver, volume)==0)
		return 1;
	return 0;
}

int	vj_jack_pause()
{
	JACK_SetState(driver,PAUSED);
	return 1;
}

int	vj_jack_resume()
{
	JACK_SetState(driver,PLAYING);
	return 1;
}

int	vj_jack_get_space()
{
	return JACK_GetBytesFreeSpace(driver);
}

long	vj_jack_get_status(long int *sec, long int *usec)
{
	return JACK_OutputStatus( driver, sec, usec);
}
#endif
