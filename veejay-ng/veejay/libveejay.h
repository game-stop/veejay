 /*
 * This program is free software; you can redistribute it and/or
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
 */

/* todo: cleanup. lots bad things. */
#ifndef VJ_LIBLAVPLAY_H
#define VJ_LIBLAVPLAY_H

veejay_t *veejay_malloc();

void veejay_change_playback_mode(veejay_t *info, int pm, int sample);

void veejay_free(veejay_t *info);

void veejay_signal_loop(void *);

void	veejay_deinit(veejay_t *info);

int veejay_init(veejay_t *info);

int veejay_open(veejay_t *info);

int veejay_main(veejay_t * info);

int veejay_stop(veejay_t *info);

void veejay_quit(veejay_t *info);

int veejay_close(veejay_t *info);

void veejay_change_state(veejay_t *info, int new_state);

void veejay_busy(veejay_t *info);

int veejay_get_state(veejay_t *info); 

int veejay_toggle_audio(veejay_t * info, int audio);

int vj_server_setup(veejay_t *info);

int veejay_init_project_from_args( veejay_t *info, int w, int h, float fps, int inter, int norm, int fmt,
		int audio, int rate, int n_chan, int bits, int display );
#endif
