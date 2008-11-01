/* global definition file (C) Niels Elburg <elburg@hio.hen.nl>
 * 
 * This program is free software you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#ifndef VJ_GLOBAL_H
#define VJ_GLOBAL_H

/*
 
   This header file defines the actions for the GUI.
   It looks like a transaction system on a 1 on 1 basis (gui <-> veejay)
   The named pipes are defined in libveejayvj.c 
 
   status:
   	current_frame, total_frames, samples, sample_id,
	speed, looptype, chain_on, speedlock
	
 
   request: (REQ)
	responds with a message (data)
 
 
   command: (CMD)
	do action, no response	
 
*/

enum {
    VJ_PLAYBACK_MODE_PLAIN = 2,
    VJ_PLAYBACK_MODE_SAMPLE = 0,
    VJ_PLAYBACK_MODE_PATTERN = 3,
    VJ_PLAYBACK_MODE_TAG = 1,
    VJ_PLAYBACK_MODE_MEM = 4,
};

enum {
    VJ_MAX_V4L_DEVICES = 2,
    VJ_MAX_IN_STREAMS = 4,
    VJ_MAX_OUT_STREAMS = 1,
    VJ_MAX_VLOOPBACK_PIPES = 8,
};
#include <libvje/vje.h>

/* request messages, global */
#define MAX_SEQUENCES		100
#define MAX_SPEED		12
#define SAMPLE_MAX_EFFECTS 	20
#define PATTERN_MAX_TRACKS	3
#define CELL_LENGTH		30
#define ROW_SIZE		(PATTERN_MAX_TRACKS * CELL_LENGTH)
#define PATTERN_DEFAULT_LENGTH  (25*60)	/* have aprox. 1 minute */
#define EDIT_CUT		101
#define EDIT_PASTE_AT		102
#define EDIT_COPY		103
#define EDIT_CROP		104
#define EDIT_DEL		105
#define MESSAGE_SIZE		1024	/* enough for my needs */
#define EL_MIN_BUF		(65535 * 4)
#define XMLTAG_BUNDLE_FILE  "ACTIONFILE"
#define XMLTAG_EVENT_AS_KEY "BUNDLE"
#define SOCKETFRAMELEN		(1024*4096)
#define FMT_420	0
#define FMT_422 1
#define FMT_420F 2
#define FMT_422F 3

#define VEEJAY_PORT_CMD 0
#define VEEJAY_PORT_STA 1
#define VEEJAY_PORT_DAT 3
#define VEEJAY_PORT_MAT 2

#define FMT_RGB24	2
#define FMT_RGB32	1

#endif
#define MAX_EDIT_LIST_FILES 4096
