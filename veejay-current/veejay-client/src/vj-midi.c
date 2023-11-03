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
#ifdef HAVE_ALSA
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <veejaycore/vjmem.h>
#include <veejaycore/vj-msg.h>
#include <alsa/asoundlib.h>
#include <gtk/gtk.h>
#include <veejaycore/libvevo.h>
#include <veejaycore/vevo.h>
#include <src/vj-api.h>
#include <src/gtktimeselection.h>

extern GtkWidget      *glade_xml_get_widget_( GtkBuilder *m, const char *name );
extern void    msg_vims(char *message);
extern void    vj_msg(int type, const char format[], ...);
extern int prompt_dialog(const char *title, char *msg);
static	int	vj_midi_events(void *vv );

#define MAX_14BIT_CONTROLLERS 64
typedef struct
{
	snd_seq_t	*sequencer;
	void		*vims;
	void        *events;
	struct pollfd	*pfd;
	int		npfd;
	int		learn;
	int		learn_event[4];
	int		active;
	void		*mw;
	void		*timeline;
	int		controllers[MAX_14BIT_CONTROLLERS];
} vmidi_t;

typedef struct
{
	int		extra;
	char		*widget;
	char		*msg;
} dvims_t;


void	vj_midi_learn( void *vv , int start)
{
    vmidi_t *v = (vmidi_t*) vv;
    if(!v->active) return;
    if (!start) {
        v->learn = 0;
        int a = vj_midi_events(vv);
        vj_msg(VEEJAY_MSG_INFO, "End learning MIDI, %d events registered.", a);
        return;
    }
    v->learn = 1;
    vj_msg(VEEJAY_MSG_INFO, "Learning MIDI commands. Touch a midi key and then click a widget");
}

void	vj_midi_play(void *vv , int play)
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
	vpf(v->events);

	v->vims   = vpn(VEVO_ANONYMOUS_PORT);
	v->events = vpn(VEVO_ANONYMOUS_PORT);

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
	if(fd < 0)
	{
		vj_msg(VEEJAY_MSG_ERROR, "Unable to open file '%s': %s", filename, strerror(errno));
		return;
	}
	struct stat t;
	if( fstat( fd, &t) != 0 )
	{
		veejay_msg(0, "MIDI: Loading MIDI config '%s':%s",filename,strerror(errno));
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
					int error = vevo_property_set( v->vims, key, VEVO_ATOM_TYPE_VOIDPTR,1, &d);
					if( error == VEVO_NO_ERROR ) {
						veejay_msg(VEEJAY_MSG_DEBUG, "MIDI: %s [%d] VIMS: '%s' origin: %s",
							key,
							extra,
							message,
						        widget);
						count ++;
					}
				}

			}
			if( buf[j] != '\n' && buf[j] != '\0' )
				value[k++] = buf[j];
		
		}
	
	}
	free(buf);
	veejay_msg(VEEJAY_MSG_INFO, "MIDI: loaded %d midi events from %s",count,filename);
	vj_msg(VEEJAY_MSG_INFO, "Loaded %d MIDI events from %s", count ,filename);
}

void	vj_midi_save(void *vv, const char *filename)
{
	vmidi_t *v = (vmidi_t*) vv;
        if(!v->active) return;

	int fd = open( filename, O_TRUNC|O_CREAT|O_WRONLY,S_IRWXU );
	if(fd<0)
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
			if( write( fd, tmp, strlen( tmp )) >= 0 ) {
				count ++;
			}
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
		veejay_msg(0, "MIDI: Cannot learn '%s' (%s) - unknown midi event.",
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
	int error = vevo_property_set( v->vims, key, VEVO_ATOM_TYPE_VOIDPTR,1, &d );
	if( error != VEVO_NO_ERROR ) {
		veejay_msg(VEEJAY_MSG_ERROR, "MIDI: Failed to store %s", key );
		return;
	}
	veejay_msg( VEEJAY_MSG_INFO, 
		"MIDI %d: %d ,%d learned: VIMS '%s' %d, origin: %s", 
			v->learn_event[0],
			v->learn_event[1],
			v->learn_event[2], d->msg, d->extra, d->widget);

	vj_msg(VEEJAY_MSG_INFO, "MIDI event %x: %x,%x to VIMS %s / %d", v->learn_event[0],v->learn_event[1],v->learn_event[2],
			d->msg, d->extra );
}

void	vj_midi_learning_vims_simple( void *vv, char *widget, int id )
{
	char message[8];
	if( widget == NULL )
		snprintf(message,sizeof(message), "%03d:;", id );
	else
		snprintf(message,sizeof(message), "%03d:", id );
	vj_midi_learning_vims( vv, widget, message, (widget == NULL ? 0 : 1 ) );
}

void	vj_midi_learning_vims_spin( void *vv, char *widget, int id )
{
	char message[8];
	if( widget == NULL )
		snprintf(message,sizeof(message), "%03d:;", id );
	else
		snprintf(message,sizeof(message), "%03d:", id );
	vj_midi_learning_vims( vv, widget, message, (widget == NULL ? 0 : 2) );
}


void	vj_midi_learning_vims_complex( void *vv, char *widget, int id, int first , int extra)
{
	char message[16];
	snprintf( message, sizeof(message), "%03d:%d",id, first );

	vj_midi_learning_vims( vv, widget, message, extra );
}

void	vj_midi_learning_vims_msg( void *vv, char *widget, int id, int arg )
{
	char message[32];
	snprintf(message, sizeof(message), "%03d:%d;",id, arg );

	vj_midi_learning_vims( vv, widget, message, 0 );
}

void	vj_midi_learning_vims_msg2(void *vv, char *widget, int id, int arg, int b )
{
	char message[32];
	snprintf(message,sizeof(message), "%03d:%d %d;", id, arg,b );
	vj_midi_learning_vims( vv, widget, message, 0 );
}

void	vj_midi_learning_vims_msg2_extra(void *vv, int id,int a, int extra )
{
	char message[32];
	snprintf(message,sizeof(message), "%03d:%d", id, a );
	vj_midi_learning_vims( vv, NULL, message, extra );
}

void	vj_midi_learning_vims_fx( void *vv, int widget, int id, int a, int b, int c, int extra )
{
	char message[32];
	char wid[32];
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

	if( d != NULL && error == VEVO_NO_ERROR )
	{
		if( d->extra )
		{	//@ argument is dynamic
			double min = 0.0;
			double max = 0.0;
			double val = 0.0;
			double range = 127.0;
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
				case 3: // timeline selection range
					min = timeline_get_in_point( v->timeline );
					max = timeline_get_out_point( v->timeline );
				break;
				case 4: //timeline range
					min = 0.0;
					max = (double) timeline_get_length( v->timeline );
				break;
				case 5: //spinbutton value
					min = gtk_spin_button_get_value( GTK_SPIN_BUTTON( glade_xml_get_widget_( v->mw, d->widget )) );
					max = min;
					val = min;
				break;
			}
			
			if( data[0] == SND_SEQ_EVENT_PITCHBEND )
			{
				range = (double) data[3];
				val = (( data[2] + (range/2.0f) ) / range ) * (max-min);
				veejay_msg(VEEJAY_MSG_DEBUG, "MIDI: pitch bend %g (min=%g,max=%g) data [%d,%d,%d,%d]",
					val, min,max,data[0],data[1],data[2],data[3] );
			}
			else if( data[0] == SND_SEQ_EVENT_CONTROLLER || data[0] == SND_SEQ_EVENT_KEYPRESS )
			{
				range = (double) data[3];
				val = ((max-min)/range) * data[2] + min;
				veejay_msg(VEEJAY_MSG_DEBUG, "MIDI: controller value=%g (min=%g,max=%g) data [%d,%d,%d,%d]",
					val, min,max, data[0],data[1],data[2],data[3]);
			}

			char vims_msg[64];
			snprintf(vims_msg,sizeof(vims_msg), "%s %d;", d->msg, (int) val );

			/* use control/param as sample_id */
			int tmpv[3];
			if ( sscanf(vims_msg, "%03d:%d %d;",&tmpv[0],&tmpv[1],&tmpv[2]) == 3 )
			{
			    // 108 = frame
			    // 109 = frame
			    // 115 = frame
			    if( tmpv[0] == 108 || tmpv[0] == 109 || tmpv[0] == 115 ) {
					//@ VIMS: sample marker events, replace frame for control/param value
					if(d->extra == 3 && tmpv[0] == 108) {
						// invert in point
						val = range - ( ((max-min)/range) * data[2] + min);
					}
					snprintf(vims_msg, sizeof(vims_msg), "%03d:%d %d;", tmpv[0], 0, (int) val );
				}	
				else if(tmpv[1] == 0 && tmpv[0] >= 100 && tmpv[0] < 200) 
				//@ VIMS: sample events, replace 0 (current_id) for control/param value
			    {
				snprintf(vims_msg,sizeof(vims_msg),"%03d:%d %d;", tmpv[0], data[1], (int)val);
			    }	    
			}
			
			msg_vims( vims_msg );
			vj_msg(VEEJAY_MSG_INFO, "MIDI %x:%x, %x {%g, %g}->  vims %s", data[0], data[1],data[2],range,val,vims_msg);
		}
		else
		{
			msg_vims( d->msg );
			vj_msg(VEEJAY_MSG_INFO, "MIDI %x: %x,%x -> vims %s", data[0],data[1],data[2], d->msg);
		}
	}
	else
	{
		veejay_msg(VEEJAY_MSG_WARNING, "MIDI, no event for: [%d,%d,%d]", data[0],data[1], data[2]);
		vj_msg(VEEJAY_MSG_ERROR, "No vims event for MIDI %x:%x,%x found",data[0],data[1],data[2]);
	}
}

static  inline int is_14bit_controller_number(vmidi_t *v, int param) {
	for( int i = 0; i < MAX_14BIT_CONTROLLERS; i ++ ) {
	   if( v->controllers[i] == -1 ) // first occurence of -1 ends the search
		 return 0;
	   if( v->controllers[i] == param)
	     return 1; // its a 14 bit midi control number
	}
	return 0;
}

static  int     midi_lsb(vmidi_t *v, int type, int param, int value )
{
	char key[32];
	int lsb = -1;
	int val = value;
	// format key for control number
	snprintf(key, sizeof(key), "%d-%d", type, param);
	
	// if the control number has a lsb value
	int error = vevo_property_get( v->events, key, 0, &lsb );
	if( error == VEVO_NO_ERROR ) {
	    // delete the lsb 
		vevo_property_del( v->events, key );
		veejay_msg(VEEJAY_MSG_DEBUG, "control number %d -> lsb = %d", param, lsb );
		// and return its value
		return lsb;
	}
	else {
		// no lsb value, set it
		vevo_property_set( v->events, key, VEVO_ATOM_TYPE_INT, 1, &val );
		veejay_msg(VEEJAY_MSG_DEBUG, "control number %d <- lsb = %d", param, val);
	}
	
	return lsb;   
}

static	int		vj_dequeue_midi_event( vmidi_t *v )
{
	int ret = 0;
	int err = 0;

	while( snd_seq_event_input_pending( v->sequencer, 1 ) > 0 ) {
		int data[4] = { 0,0,0,127 };
		int isvalid = 1;
		snd_seq_event_t *ev = NULL;

		err = snd_seq_event_input( v->sequencer, &ev );
		if( err == -ENOSPC || err == -EAGAIN )
			return ret;

		data[0] = ev->type;
		switch( ev->type )
		{
			case SND_SEQ_EVENT_CONTROLLER:
				if( is_14bit_controller_number(v, ev->data.control.param) ) {
					int lsb = midi_lsb( v, data[0], ev->data.control.param, ev->data.control.value );
					if( lsb == -1 ) {
						isvalid = 0; // it's 2 midi events for 14 bit ?
					}
					else {
						int control_number = ev->data.control.param;
						int control_value = (ev->data.control.value << 7) + lsb;
						data[1] = control_number;
						data[2] = control_value;
						data[3] = 16384;
					}
				}
				else
				{
					data[1] = ev->data.control.channel*256+ev->data.control.param;
					data[2] = ev->data.control.value;
				}
				break;
			case SND_SEQ_EVENT_PITCHBEND:
				data[1] = ev->data.control.channel;
				data[2] = ev->data.control.value;
				data[3] = 16384;
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
				isvalid = 0;
				break;
		}

		veejay_msg(VEEJAY_MSG_DEBUG, "MIDI type %d param %d , data [ %d, %d, %d ] valid = %d",
			ev->type, ev->data.control.param, data[0], data[1], data[2], isvalid );

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

	return 0;
}

void	scan_14bit_midi_from_env(vmidi_t *v)
{
	// initialize
	for( int i = 0; i < MAX_14BIT_CONTROLLERS; i ++ ) {
		v->controllers[i] = -1;
	}

	// check if any
	char* env_var = getenv("VEEJAY_14BIT_MIDI_CONTROLLERS");
    if (env_var == NULL) {
        veejay_msg(VEEJAY_MSG_WARNING,"Environment variable VEEJAY_14BIT_MIDI_CONTROLLERS not set");
        return;
    }

    char* saveptr = NULL;
    char* token = NULL;
    int num_controllers = 0;

    token = strtok_r(env_var, ",", &saveptr);
    while (token != NULL && num_controllers < MAX_14BIT_CONTROLLERS) {
        v->controllers[num_controllers] = atoi(token);
        token = strtok_r(NULL, ",", &saveptr);
    	veejay_msg(VEEJAY_MSG_DEBUG, "Control number %d is 14 bit", v->controllers[num_controllers]);
		num_controllers++;
	}


}

void	*vj_midi_new(void *mw, void *timeline)
{
	vmidi_t *v = (vmidi_t*) vj_calloc(sizeof(vmidi_t));
	int portid = 0;

	if( snd_seq_open( &(v->sequencer), "hw", SND_SEQ_OPEN_DUPLEX | SND_SEQ_NONBLOCK, 0 ) < 0 )
	{
		veejay_msg(0, "MIDI: Error opening ALSA sequencer");
		return v;
	}

	snd_seq_set_client_name( v->sequencer, "Veejay" );

	if( (portid = snd_seq_create_simple_port( v->sequencer, "Reloaded",
			SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE ,
			SND_SEQ_PORT_TYPE_APPLICATION )) < 0 )
	{
		veejay_msg(0, "MIDI: Error creating sequencer port");
		return v;
	}

	v->npfd = snd_seq_poll_descriptors_count( v->sequencer, POLLIN );
	if( v->npfd <= 0 )
	{
		veejay_msg(0,"MIDI: Unable to poll in from sequencer");
		return v;
	}
	v->pfd     = (struct pollfd *) vj_calloc( v->npfd * sizeof( struct pollfd ));
	v->mw      = mw;
	v->timeline = timeline;
	v->learn   = 0;
	v->vims    = vpn(VEVO_ANONYMOUS_PORT);	
	v->active = 1;
	snd_seq_poll_descriptors( v->sequencer, v->pfd, v->npfd, POLLIN );

	veejay_msg(VEEJAY_MSG_INFO, "MIDI listener active! Type 'aconnect -o' to see where to connect to.");
	veejay_msg(VEEJAY_MSG_INFO, "For example: $ aconnect 128 129");

	veejay_msg(VEEJAY_MSG_INFO, "In case of 14 bit MIDI events, you can set VEEJAY_14BIT_MIDI_CONTROLLERS=param,param,param");
	veejay_msg(VEEJAY_MSG_INFO, "For example: $ export VEEJAY_14BIT_MIDI_CONTROLLERS=60,61,62");

	scan_14bit_midi_from_env(v);

	return (void*) v;
}

#endif
