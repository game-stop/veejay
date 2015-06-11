#ifndef OSC_SERVER_H
#define OSC_SERVER_H
/* veejay - Linux VeeJay
 * 	     (C) 2002-2006 Niels Elburg <nwelburg@gmail.com> 
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
void	*veejay_new_osc_server( void *data, const char *port );
void	veejay_free_osc_server( void *dosc );
char	*veejay_osc_server_get_addr( void *data );
int	 veejay_osc_server_get_port( void *data );

void	veejay_osc_del_methods( void *user_data, void *osc_space,void *vevo_port, void *fx_instance );

int	plugin_new_event(
		void *userdata,
		void *osc_space,
		void *instance,
		const char *base,
		const char *key,
		const char *fmt,
		const char **args,
		const char *descr,
		void *func,
		int extra_token,
		void *ptempl);

int	veejay_new_event(
		void *userdata,
		void *osc_space,
		void *instance,
		const char *base,
		const char *key,
		const char *fmt,
		const char **args,
		const char *descr,
		void *func,
		int extra_token);


int	vevosample_new_event(
		void *userdata,
		void *osc_space,
		void *instance,
		const char *base,
		const char *key,
		const char *fmt,
		const char **args,
		const char *descr,
		void *func,
		int extra_token);
#endif
