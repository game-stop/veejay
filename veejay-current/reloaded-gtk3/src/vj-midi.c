/* Gveejay Reloaded - graphical interface for VeeJay
 * 	     (C) 2002-2007 Niels Elburg <nwelburg@gmail.com> 
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
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <veejay/vjmem.h>
#include <veejay/vj-msg.h>
#include <alsa/asoundlib.h>
#include <gtk/gtk.h>
#include <veejay/libvevo.h>
#include <veejay/vevo.h>
#include <src/vj-api.h>

extern GtkWidget      *glade_xml_get_widget_( GtkBuilder *m, const char *name );
extern void    msg_vims(char *message);
extern void    vj_msg(int type, const char format[], ...);
extern int prompt_dialog(const char *title, char *msg);
static	int	vj_midi_events(void *vv );

typedef struct
{
	snd_seq_t	*sequencer;
	void		*vims;
	struct pollfd	*pfd;
	int		npfd;
	int		learn;
	int		learn_event[4];
	int		active;
	void		*mw;
} vmidi_t;

typedef struct
{
	int		extra;
	char		*widget;
	char		*msg;
} dvims_t;


void	vj_midi_learn( void *vv )
{
	vmidi_t *v = (vmidi_t*) vv;
	if(!v->active) return;
	v->learn = 1;
	vj_msg(VEEJAY_MSG_INFO, "Learning MIDI commands. Touch a midi key and then click a widget");
}

void	vj_midi_play(void *vv )
{
	vmidi_t *v = (vmidi_t*) vv;
        if(!v->active) return;

	v->learn = 0;
	int a = vj_midi_events(vv);
	char msg[100];
	snprintf(msg,sizeof(msg), "MIDI listener active, %d events registered", a );
	vj_msg(VEEJAY_MSG_INFO, "%s",msg);
}
static	int	vj_midi_events(void *vv )
{
	vmidi_t *v = (vmidi_t*)vv;
	char **items = vevo_list_properties(v->vims);
	if(!items) return 0;

	int i;
	int len = 0;
	for( i = 0; items[i] != NULL ; i ++ )
	{
		len ++;
		free(items[i]);
	}
	free(items);
	return len;	
}
void	vj_midi_reset( void *vv )
{
	vmidi_t *v = (vmidi_t*)vv;

	int a = vj_midi_events(vv);
	if( a > 0 )
	{
		char warn[200];
		snprintf(warn,sizeof(warn), "This will clear %d MIDI events, Continue ?",a );
		if( prompt_dialog( "MIDI", warn ) == GTK_RESPONSE_REJECT )
			return;

	}

	char **items = vevo_list_properties(v->vims);
	if(!items) {
		vj_msg(VEEJAY_MSG_INFO,"No MIDI events to clear");
		return;
	}
	int i;
	for( i = 0; items[i] != NULL ; i ++ )
	{
		dvims_t *d = NULL;
		if( vevo_property_get( v->vims, items[i],0,&d ) == VEVO_NO_ERROR )
		{
			if(d->msg) free(d->msg);
			if(d->widget) free(d->widget);
			free(d);
		}
		free(items[i]);
	}
	free(items);
	
	vpf(v->vims);

	v->vims = vpn(VEVO_ANONYMOUS_PORT);

	vj_msg(VEEJAY_MSG_INFO, "Cleared %d MIDI events.",a);
}

void	vj_midi_load(void *vv, const char *filename)
{
	vmidi_t *v = (vmidi_t*) vv;
        if(!v->active) return;

	int a = vj_midi_events(vv);
	if( a > 0 )
	{
		char warn[200];
		snprintf(warn,sizeof(warn), "There are %d MIDI event known, loading from file will overwrite any existing events. Continue ?", a );
		if( prompt_dialog( "MIDI", warn ) == GTK_RESPONSE_REJECT )
			return;

	}

	int fd = open( filename, O_RDONLY );
	if(!fd)
	{
		vj_msg(VEEJAY_MSG_ERROR, "Unable to open file '%s': %s", filename, strerror(errno));
		return;
	}
	struct stat t;
	if( fstat( fd, &t) != 0 )
	{
		veejay_msg(0, "Error loading MIDI config '%s':%s",filename,strerror(errno));
		vj_msg(VEEJAY_MSG_ERROR,"Unable to load %s: %s", filename, strerror(errno));
		return;
	}
	else
	{
		if( !S_ISREG( t.st_mode ) && !S_ISLNK(t.st_mode) )
		{
			vj_msg(VEEJAY_MSG_ERROR, "File '%s' is not a regular file or symbolic link. Refusing to load it.",
				filename );
			return;
		}
	}

	char *buf = (char*) vj_calloc( 128000 );
	uint32_t count = 0;
	if (read( fd, buf, 128000 ) > 0 )
	{
		int len = strlen( buf );
		int j,k=0;

		char value[1024];
		veejay_memset(value,0,sizeof(value));
	
		for( j = 0; j < len; j ++ )
		{
			if( buf[j] == '\0' ) break;
			if( buf[j] == '\n' )
			{
				char key[32];
				char widget[100];
				char message[512];
				int extra = 0;
				
				veejay_memset( key,0,sizeof(key));
				veejay_memset( widget,0,sizeof(widget));
				veejay_memset( message,0,sizeof(message));

				if(sscanf( value, "%s %d %s \"%[^\"]", key, &extra, widget, message ) == 4 )
				{
					veejay_memset( value,0,sizeof(value));
					k = 0;
					dvims_t *d = (dvims_t*) vj_calloc(sizeof(dvims_t));
					dvims_t *cur = NULL;
					d->extra = extra;
					d->widget = NULL;
					if( strncasecmp( widget, "none", 4 ) !=0 )
						d->widget = strdup(widget);
					d->msg = strdup(message);
					if( vevo_property_get( v->vims, key, 0, &cur ) == VEVO_NO_ERROR )
					{
						if(cur->widget) free(cur->widget);
						if(cur->msg) free(cur->msg);
						free(cur);
					}
					int error = vevo_property_set( v->vims, key, 1, VEVO_ATOM_TYPE_VOIDPTR, &d);
					if( error == VEVO_NO_ERROR )
						count ++;
#ifdef STRICT_CHECKING
					else {
						veejay_msg(VEEJAY_MSG_ERROR, "Error loading MIDI event '%s': %d",key, error );

					}
#endif
				}

			}
			if( buf[j] != '\n' && buf[j] != '\0' )
				value[k++] = buf[j];
		
		}
	
	}
	free(buf);
	veejay_msg(VEEJAY_MSG_INFO, "loaded %d midi events from %s",count,filename);
	vj_msg(VEEJAY_MSG_INFO, "Loaded %d MIDI events from %s", count ,filename);
}

void	vj_midi_save(void *vv, const char *filename)
{
	vmidi_t *v = (vmidi_t*) vv;
        if(!v->active) return;

	int fd = open( filename, O_TRUNC|O_CREAT|O_WRONLY,S_IRWXU );
	if(!fd)
	{
		vj_msg(VEEJAY_MSG_ERROR, "Unable to save MIDI settings to %s",filename);
		return;
	}

	char **items = vevo_list_properties( v->vims );
	int i;
	if( items == NULL )
	{
		vj_msg(VEEJAY_MSG_ERROR, "No MIDI events learned yet");
		return;
	}
	uint32_t count = 0;
	for( i =0 ; items[i] != NULL ;i ++ )
	{
		char tmp[512];
		dvims_t *d  = NULL;
		if( vevo_property_get( v->vims, items[i], 0, &d ) == VEVO_NO_ERROR )
		{
			snprintf(tmp, 512, "%s %d %s \"%s\"\n",
				items[i],
				d->extra,
				(d->widget == NULL ? "none" : d->widget ),
				d->msg );
			if( write( fd, tmp, strlen( tmp )) >= 0 )
				count ++;
		}
		free(items[i]);
	}
	free(items);
	close(fd);

	vj_msg(VEEJAY_MSG_INFO, "Wrote %d MIDI events to %s", count, filename );
}

void	vj_midi_learning_vims( void *vv, char *widget, char *msg, int extra )
{
	vmidi_t *v = (vmidi_t*) vv;
        if(!v->active) return;

	if( !v->learn )
		return;

	if( v->learn_event[0] == -1 || v->learn_event[1] == -1 || v->learn_event[2] == -1 ) {
		veejay_msg(0, "Cannot learn '%s' (%s) - unknown midi event.",
			widget, msg );
		return;
	}

	dvims_t *d = (dvims_t*) vj_malloc(sizeof(dvims_t));
	d->extra = extra;	
	d->msg = (msg == NULL ? NULL : strdup(msg));
	d->widget = (widget == NULL ? NULL : strdup(widget));
	char key[32];
	snprintf(key,sizeof(key), "%03d%03d", v->learn_event[0],v->learn_event[1] );

	dvims_t *cur = NULL;
	if( vevo_property_get( v->vims, key, 0, &cur ) == VEVO_NO_ERROR )
	{
		if( cur->widget ) free(cur->widget );
		if( cur->msg ) free(cur->msg);
		free(cur);
	}
	int error = vevo_property_set( v->vims, key, 1, VEVO_ATOM_TYPE_VOIDPTR, &d );
	if( error != VEVO_NO_ERROR )
		return;
	vj_msg( VEEJAY_MSG_INFO, 
		"Midi %x: %x ,%x learned", v->learn_event[0],v->learn_event[1],v->learn_event[2]);
}

void	vj_midi_learning_vims_simple( void *vv, char *widget, int id )
{
	char message[100];
	if( widget == NULL )
		snprintf(message,sizeof(message), "%03d:;", id );
	else
		snprintf(message,sizeof(message), "%03d:", id );
	vj_midi_learning_vims( vv, widget, message, (widget == NULL ? 0 : 1 ) );
}

void	vj_midi_learning_vims_spin( void *vv, char *widget, int id )
{
	char message[100];
	if( widget == NULL )
		snprintf(message,sizeof(message), "%03d:;", id );
	else
		snprintf(message,sizeof(message), "%03d:", id );
	vj_midi_learning_vims( vv, widget, message, (widget == NULL ? 0 : 2) );
}


void	vj_midi_learning_vims_complex( void *vv, char *widget, int id, int first , int extra)
{
	char message[100];
	snprintf( message, sizeof(message), "%03d:%d",id, first );

	vj_midi_learning_vims( vv, widget, message, extra );
}

void	vj_midi_learning_vims_msg( void *vv, char *widget, int id, int arg )
{
	char message[100];
	snprintf(message, sizeof(message), "%03d:%d;",id, arg );

	vj_midi_learning_vims( vv, widget, message, 0 );
}

void	vj_midi_learning_vims_msg2(void *vv, char *widget, int id, int arg, int b )
{
	char message[100];
	snprintf(message,sizeof(message), "%03d:%d %d;", id, arg,b );
	vj_midi_learning_vims( vv, widget, message, 0 );
}

void	vj_midi_learning_vims_fx( void *vv, int widget, int id, int a, int b, int c, int extra )
{
	char message[100];
	char wid[100];
	snprintf(message,sizeof(message), "%03d:%d %d %d", id, a,b,c );
	snprintf(wid, sizeof(wid),"slider_p%d", widget );
	vj_midi_learning_vims( vv, wid, message, extra );
}

static	void	vj_midi_send_vims_now( vmidi_t *v, int *data )
{
	// format vims message and send it now
	// it would be nice to filter out unique events per frame step
	
	// this can be done by keeping a temporary vevo port
	// and store (instead of send) the VIMS message
	// including the sample_id and chain_entry_id but
	// cutting off all other arguments.
	// then, last SET_SPEED will overwrite any previous ones for this frame step.
	//
	// last, send all messages in temporary port out and cleanup

	char key[32];

	if( v->learn )
	{
		veejay_memcpy( v->learn_event, data, sizeof(v->learn_event ));
		vj_msg(VEEJAY_MSG_INFO, "MIDI %x:%x,%x -> ?", v->learn_event[0],v->learn_event[1],
			v->learn_event[2]);
		return;
	}

	snprintf(key,sizeof(key), "%03d%03d", data[0],data[1] ); //@ event key is midi event type + midi control/param id

	dvims_t *d = NULL;
	int error = vevo_property_get( v->vims, key, 0, &d);
	if( error == VEVO_NO_ERROR )
	{
		if( d->extra )
		{	//@ argument is dynamic
			double min = 0.0;
			double max = 0.0;
			double val = 0.0;
			switch(d->extra)
			{
				case 1: //slider
				{
					GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE(
							glade_xml_get_widget_( v->mw, d->widget ) ) );

					min = gtk_adjustment_get_lower (a);
					max = gtk_adjustment_get_upper (a);
				}
				break;
				case 2: //spinbox
					gtk_spin_button_get_range( GTK_SPIN_BUTTON(
							glade_xml_get_widget_( v->mw, d->widget)), &min, &max);
				
				break;
			}
			
			if( data[0] == SND_SEQ_EVENT_PITCHBEND )
			{
				val =  ( (data[2]/16384.0f) * (max-min) );
			}
			else if( data[0] == SND_SEQ_EVENT_CONTROLLER || data[0] == SND_SEQ_EVENT_KEYPRESS )
			{
				val = ((max-min)/127.0) * data[2] + min;
			}
		   	else {
				vj_msg(VEEJAY_MSG_INFO, "MIDI: what's this %x,%x,%x ?",data[0],data[1],data[2]);
				return;
			}

			char vims_msg[255];
			snprintf(vims_msg,sizeof(vims_msg), "%s %d;", d->msg, (int) val );

			/* use control/param as sample_id */
			int tmpv[3];
			if ( sscanf(vims_msg, "%03d:%d %d;",&tmpv[0],&tmpv[1],&tmpv[2]) == 3 )
			{
			    if(tmpv[1] == 0 && tmpv[0] >= 100 && tmpv[0] < 200) //@ VIMS: sample events, replace 0 (current_id) for control/param number
			    {
				snprintf(vims_msg,sizeof(vims_msg),"%03d:%d %d;", tmpv[0], data[1], (int)val);
			    	veejay_msg(VEEJAY_MSG_DEBUG, "(midi) using control/param %d as sample identifer",data[1]);
			    }	    
			}

			msg_vims( vims_msg );
			vj_msg(VEEJAY_MSG_INFO, "MIDI %x:%x, %x ->  vims %s", data[0], data[1],data[2], vims_msg);
		}
		else
		{
			msg_vims( d->msg );
			vj_msg(VEEJAY_MSG_INFO, "MIDI %x: %x,%x -> vims %s", data[0],data[1],data[2], d->msg);
		}
	}
	else
	{
		vj_msg(VEEJAY_MSG_ERROR, "No vims event for MIDI %x:%x,%x found",data[0],data[1],data[2]);
	}
}

static	int		vj_dequeue_midi_event( vmidi_t *v )
{
	int ret = 0;
	int err = 0;
	while( snd_seq_event_input_pending( v->sequencer, 1 ) > 0 ) {
		int data[4] = { 0,0,0,0};
		int isvalid = 1;
		snd_seq_event_t *ev = NULL;

		err = snd_seq_event_input( v->sequencer, &ev );
		if( err == -ENOSPC || err == -EAGAIN )
			return ret;

		data[0] = ev->type;
		switch( ev->type )
		{
			/* controller: channel <0-N>, <modwheel 0-127> */
			case SND_SEQ_EVENT_CONTROLLER:
				data[1] = ev->data.control.channel*256+ev->data.control.param; // OB: added chan+param as identifier
				data[2] = ev->data.control.value;
				break;
			case SND_SEQ_EVENT_PITCHBEND:
				data[1] = ev->data.control.channel;
				data[2] = ev->data.control.value;
				break;
			case SND_SEQ_EVENT_NOTE:
				data[1] = ev->data.control.channel;
				data[2] = ev->data.note.note;
				break;
			case SND_SEQ_EVENT_NOTEON:
				data[2] = ev->data.control.channel;
				data[1] = ev->data.note.note;
				break;
			case SND_SEQ_EVENT_NOTEOFF:
				data[2] = ev->data.control.channel;
				data[1] = ev->data.note.note;
				break;
			case SND_SEQ_EVENT_KEYPRESS:
				data[1] = ev->data.control.channel;
				data[2] = ev->data.note.velocity;
				break;
			case SND_SEQ_EVENT_PGMCHANGE:
				data[1] = ev->data.control.param;
				data[2] = ev->data.control.value;
				break;
			default:
				data[1] = -1;
				data[2] = -1;
				isvalid = 0;
				veejay_msg(VEEJAY_MSG_WARNING, "unknown midi event received: %d %x %x",ev->type,data[1],data[2],data[2]);
				break;
		}

		if( isvalid == 1 ) {
			vj_midi_send_vims_now( v, data );
		}

		if( ev ) {
			snd_seq_free_event( ev );
			ret ++;
		}
	}

	return ret; 
}

int	vj_midi_handle_events(void *vv)
{
	vmidi_t *v = (vmidi_t*) vv;
    	
	if(!v->active) return 0;

	int status = poll( v->pfd, v->npfd, 0 );

	if( status == -1 ) {
		veejay_msg(VEEJAY_MSG_ERROR, "MIDI: unable to poll file descriptor: %s", strerror(errno));
		return 0;
	}
	else if( status > 0 ) {
		return vj_dequeue_midi_event(v);
	}

	/*

	while( poll( v->pfd, v->npfd, 0 ) > 0 )
	{
		n_msg ++;
		if(vj_midi_action( v ))
			continue;
		break;
	}
	if(n_msg>0)
		veejay_msg(VEEJAY_MSG_INFO, "MIDI: %d events received", n_msg);
	*/

	return 0;
}


void	*vj_midi_new(void *mw)
{
	vmidi_t *v = (vmidi_t*) vj_calloc(sizeof(vmidi_t));
	int portid = 0;

	if( snd_seq_open( &(v->sequencer), "hw", SND_SEQ_OPEN_DUPLEX | SND_SEQ_NONBLOCK, 0 ) < 0 )
	{
		veejay_msg(0, "Error opening ALSA sequencer");
		return v;
	}

	snd_seq_set_client_name( v->sequencer, "Veejay" );

	if( (portid = snd_seq_create_simple_port( v->sequencer, "Reloaded",
			SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE ,
			SND_SEQ_PORT_TYPE_APPLICATION )) < 0 )
	{
		veejay_msg(0, "Error creating sequencer port");
		return v;
	}

	v->npfd = snd_seq_poll_descriptors_count( v->sequencer, POLLIN );
	if( v->npfd <= 0 )
	{
		veejay_msg(0,"Unable to poll in from sequencer");
		return v;
	}
	v->pfd     = (struct pollfd *) vj_calloc( v->npfd * sizeof( struct pollfd ));
	v->mw      = mw;
	v->learn   = 0;
	v->vims    = vpn(VEVO_ANONYMOUS_PORT);	
	v->active = 1;
	snd_seq_poll_descriptors( v->sequencer, v->pfd, v->npfd, POLLIN );

	veejay_msg(VEEJAY_MSG_INFO, "MIDI listener active! Type 'aconnect -o' to see where to connect to.");
	veejay_msg(VEEJAY_MSG_INFO, "For example: $ aconnect 128 129");

	return (void*) v;
}


