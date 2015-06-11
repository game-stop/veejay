#ifndef OSC_CLIENT_H
#define OSC_CLIENT_H
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
void	*veejay_new_osc_sender_uri( const char *uri );
void	*veejay_new_osc_sender( const char *addr, const char *port );
void	veejay_free_osc_sender( void *dosc );

void	veejay_osc_set_window( void *osc, char *window );
int	veejay_send_osc( void *osc ,const char *msg, const char *format, ... );
int	veejay_vevo_send_osc( void *osc, const char *msg, void *vevo_port );
int	veejay_send_osc_strargs( void *osc, const char *msg, int n_str, char **strs );
void	veejay_message_add_argument( void *osc, void *msg, const char *format, ... );

void    *veejay_message_new_pulldown( void *osc, const char *str0,const char *str1,const char *id, const char *str2,
                                      const char *str3, double dv , const char *str   );
void	*veejay_message_new_widget( void *osc, const char *str1,const char *str2, int n_names );
void	veejay_message_linked_pulldown_done( void *osc, void *msg );
void	veejay_message_widget_done( void *osc, void *msg );
void    *veejay_message_new_linked_pulldown( void *osc, const char *str0,const char *str1, const char *str2,
                                      const char *str3, const char *str4 , const char *str5   );
void	veejay_message_pulldown_done( void *osc, void *msg );
void	veejay_message_pulldown_done_update( void *osc, void *msg );
void	veejay_bundle_sample_add( void *osc, int id, const char *msg, const char *format, ... );
void	veejay_bundle_add( void *osc, const char *msg, const char *format, ... );
void	veejay_bundle_send( void *osc );
void 	veejay_bundle_destroy(void *osc );
void	veejay_bundle_add_blob( void *osc, const char *msg, void *blub );
void	veejay_bundle_add_blobs( void *osc, const char *msg, void *blub, void *blab, void *blib );
void	veejay_xbundle_add( void *osc, const char *window, const char *widget, const char *format, ... );
void	veejay_bundle_plugin_add( void *osc, const char *window, const char *path, const char *format, void *value );
void	veejay_bundle_sample_fx_add( void *osc, int id, int entry, const char *word, const char *format, ... );

void	veejay_bundle_sample_add_fx_atom( void *osc, int id,int entry, const char *word, const char *format, int type, void *value );
void	veejay_bundle_add_atom( void *osc, const char *osc_path, const char *format, int type, void *value );
void	veejay_bundle_sample_add_atom( void *osc, int id, const char *word, const char *format, int type, void *value );
void	veejay_ui_bundle_add( void *osc, const char *msg, const char *format, ... );

#endif
