/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2004 Niels Elburg <nelburg@looze.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 */

#ifndef VJ_EVENT_H
#define VJ_EVENT_H
#include <config.h>
#ifdef HAVE_XML2
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#endif

void 	vj_event_fmt_arg			(	int *args, 	char *str, 	const char format[], 	va_list ap);
void 	vj_event_init				();
void	vj_event_print_range			(	int n1,		int n2);
#ifdef HAVE_SDL
#ifdef HAVE_XML2
void    vj_event_xml_new_keyb_event		( 	void *v,	xmlDocPtr doc, 	xmlNodePtr cur );
#endif
#endif
void vj_event_update_remote(void *ptr);
void	vj_event_dump(void);
void	vj_event_chain_clear			(	void *ptr,	const char format[],	va_list ap	);
void 	vj_event_el_copy			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_el_crop			(	void *ptr, 	const char format[], 	va_list ap	); 
void 	vj_event_el_cut				(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_el_del				(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_el_paste_at			(	void *ptr, 	const char format[], 	va_list ap	);
void	vj_event_el_load_editlist		(	void *ptr,	const char format[],	va_list ap	);
void 	vj_event_el_save_editlist		(	void *ptr,	const char format[],	va_list ap	);
void 	vj_event_el_add_video			(	void *ptr,	const char format[],	va_list ap	);
void 	vj_event_goto_end			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_goto_start			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_inc_frame			(	void *ptr, 	const char format[], 	va_list ap	);
void	vj_event_dec_frame			(	void *ptr,	const char format[],	va_list ap	);
void 	vj_event_next_second			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_none				(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_play_forward			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_play_reverse			(	void *ptr,	const char format[], 	va_list ap	); 
void 	vj_event_play_speed			(	void *ptr, 	const char format[], 	va_list ap	);
void vj_event_play_repeat(void *ptr, const char format[], va_list ap);
void 	vj_event_play_stop			(	void *ptr, 	const char format[], 	va_list ap	); 
void 	vj_event_prev_second			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_sample_copy			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_sample_del			(	void *ptr, 	const char format[], 	va_list ap	);
void	vj_event_sample_select			(	void *ptr, 	const char format[],	va_list ap	);
void 	vj_event_set_property			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_get_property			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_set_frame			(	void *ptr, 	const char format[], 	va_list ap	);
#ifdef USE_DISPLAY
void 	vj_event_set_screen_size		(	void *ptr, 	const char format[], 	va_list ap	);
#endif
void 	vj_event_sample_new			(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_quit				(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_set_volume			(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_debug_level			( 	void *ptr,	const char format[],	va_list ap	);

void	vj_event_bezerk				(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_fullscreen			( void *ptr, const char format[],	va_list ap );

void	vj_event_chain_entry_set_active( void *ptr, const char format[], va_list ap );
void 	vj_event_chain_entry_set(void *ptr, const char format[], va_list ap);
void	vj_event_chain_entry_clear( void *ptr, const char format[], va_list ap );
void    vj_event_chain_entry_set_parameter_value( void *ptr, const char format[], va_list ap );
void    vj_event_chain_entry_set_input( void *ptr, const char format[], va_list ap );
void	vj_event_chain_entry_set_alpha( void *ptr, const char format[], va_list ap);

void	vj_event_sample_attach_out_parameter( void *ptr, const char format[], va_list ap );
void	vj_event_sample_detach_out_parameter( void *ptr, const char format[], va_list ap );

void	vj_event_sample_configure_recorder( void *ptr, const char format[], va_list ap );
void	vj_event_sample_start_recorder( void *ptr, const char format[], va_list ap );
void	vj_event_sample_stop_recorder( void *ptr, const char format[], va_list ap );


#endif
